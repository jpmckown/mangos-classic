/*
 * This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "AuctionHouse/AuctionService.h"
#include "AuctionHouse/AuctionHouseMgr.h"
#include "Database/DatabaseEnv.h"
#include "Entities/Item.h"
#include "Globals/ObjectMgr.h"
#include "Log/Log.h"
#include "Server/DBCStores.h"
#include "World/World.h"
#include "Policies/Singleton.h"

#include <sstream>

INSTANTIATE_SINGLETON_1(AuctionService);

// Off-thread changes are logged, but only this many times, so a hot path can't flood the log
static uint32 const MAX_OFF_THREAD_REPORTS = 50;

void AuctionService::Update()
{
    // World::Update calls this first thing every tick, so the first caller is the world thread.
    // Auctions loaded at startup are added on the main thread before this is set, which the check allows.
    if (m_worldThread.load() == std::thread::id())
        m_worldThread.store(std::this_thread::get_id());

    m_messager.Execute(this);
}

bool AuctionService::IsWorldThread() const
{
    std::thread::id world = m_worldThread.load();
    return world == std::thread::id() || world == std::this_thread::get_id();
}

void AuctionService::CheckWorldThread(char const* operation) const
{
    if (IsWorldThread())
        return;

    uint32 reports = ++m_offThreadReports;
    if (reports > MAX_OFF_THREAD_REPORTS)
        return;

    std::ostringstream thread;
    thread << std::this_thread::get_id();
    sLog.outError("AuctionService: %s called off the world thread (thread %s)%s", operation, thread.str().c_str(),
                  reports == MAX_OFF_THREAD_REPORTS ? "; further reports suppressed" : "");
}

AuctionEntry* AuctionService::PostServerAuction(PostRequest const& request)
{
    CheckWorldThread("PostServerAuction");

    AuctionHouseEntry const* houseEntry = sAuctionHouseStore.LookupEntry(request.houseId);
    if (!houseEntry || !request.count || !request.duration)
        return nullptr;

    Item* item = Item::CreateItem(request.itemEntry, request.count, nullptr, request.randomPropertyId);
    if (!item)
        return nullptr;

    AuctionEntry* auction = new AuctionEntry;
    auction->Id = sObjectMgr.GenerateAuctionID();
    auction->itemGuidLow = item->GetGUIDLow();
    auction->itemTemplate = item->GetEntry();
    auction->itemCount = item->GetCount();
    auction->itemRandomPropertyId = item->GetItemRandomPropertyId();
    auction->owner = request.owner;
    auction->startbid = request.startBid;
    auction->bidder = 0;
    auction->bid = 0;
    auction->buyout = request.buyout;
    auction->expireTime = time(nullptr) + time_t(request.duration * sWorld.getConfig(CONFIG_FLOAT_RATE_AUCTION_TIME));
    auction->deposit = 0;
    auction->auctionHouseEntry = houseEntry;

    sAuctionMgr.GetAuctionsMap(houseEntry)->AddAuction(auction);
    sAuctionMgr.AddAItem(item);

    CharacterDatabase.BeginTransaction();
    item->SaveToDB();
    auction->SaveToDB();
    CharacterDatabase.CommitTransaction();

    return auction;
}

bool AuctionService::PlaceServerBid(uint32 houseId, uint32 auctionId, uint32 amount)
{
    CheckWorldThread("PlaceServerBid");

    AuctionHouseEntry const* houseEntry = sAuctionHouseStore.LookupEntry(houseId);
    if (!houseEntry)
        return false;

    AuctionEntry* auction = sAuctionMgr.GetAuctionsMap(houseEntry)->GetAuction(auctionId);
    if (!auction)
        return false;

    // the server already holds the top bid
    if (auction->bid && !auction->bidder)
        return false;

    bool buyout = auction->buyout && amount >= auction->buyout;
    uint32 minimum = auction->bid ? auction->bid + auction->GetAuctionOutBid() : auction->startbid;
    if (!buyout && amount < minimum)
        return false;

    auction->UpdateBid(amount);                             // may delete the auction (buyout)
    return true;
}

bool AuctionService::RemoveServerAuction(uint32 houseId, uint32 auctionId)
{
    CheckWorldThread("RemoveServerAuction");

    AuctionHouseEntry const* houseEntry = sAuctionHouseStore.LookupEntry(houseId);
    if (!houseEntry)
        return false;

    AuctionHouseObject* auctionHouse = sAuctionMgr.GetAuctionsMap(houseEntry);
    AuctionEntry* auction = auctionHouse->GetAuction(auctionId);
    if (!auction || auction->bid)
        return false;

    if (Item* item = sAuctionMgr.GetAItem(auction->itemGuidLow))
    {
        sAuctionMgr.RemoveAItem(auction->itemGuidLow);
        item->DeleteFromDB();
        delete item;
    }

    auction->DeleteFromDB();
    auctionHouse->RemoveAuction(auction->Id);
    delete auction;
    return true;
}
