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

#ifndef _AUCTION_SERVICE_H
#define _AUCTION_SERVICE_H

#include "Common.h"
#include "Multithreading/Messager.h"
#include "Policies/Singleton.h"

#include <atomic>
#include <thread>

struct AuctionEntry;
struct AuctionHouseEntry;

// Single owner of auction house state.
//
// AuctionHouseObject and AuctionHouseMgr have no locks: every change to auctions or auction items must happen on
// the world thread. Reading them during the map update phase is fine because maps never run alongside the rest of
// World::Update, but nothing may change them there or on any other thread.
// Code that runs elsewhere hands its work over with Submit(); it runs on the world thread at the next World::Update.
// Auction mutators call CheckWorldThread(), which logs any change made from another thread.
class AuctionService
{
    public:
        struct PostRequest
        {
            uint32 houseId = 0;                             // AuctionHouse.dbc id (1, 6, 7...)
            uint32 owner = 0;                               // seller character low guid, 0 for none
            uint32 itemEntry = 0;
            uint32 count = 1;
            uint32 randomPropertyId = 0;                    // 0 = roll one as a drop would
            uint32 startBid = 0;
            uint32 buyout = 0;
            uint32 duration = 0;                            // seconds, before Rate.Auction.Time
        };

        void Update();

        // Thread-safe. The work runs on the world thread at the next World::Update.
        void Submit(std::function<void(AuctionService*)> const& work) { m_messager.AddMessage(work); }

        bool IsWorldThread() const;
        void CheckWorldThread(char const* operation) const;

        // World thread only.
        // Lists a new item that comes from nowhere (no deposit, no inventory). The seller gets the money mail as usual.
        AuctionEntry* PostServerAuction(PostRequest const& request);
        // Bids or buys out as the server (bidder 0): the outbid player is refunded, the item is deleted when won.
        bool PlaceServerBid(uint32 houseId, uint32 auctionId, uint32 amount);
        // Removes an auction without bids and deletes its item instead of mailing it back to the seller.
        bool RemoveServerAuction(uint32 houseId, uint32 auctionId);

    private:
        std::atomic<std::thread::id> m_worldThread{std::thread::id()};
        mutable std::atomic<uint32> m_offThreadReports{0};
        Messager<AuctionService> m_messager;
};

#define sAuctionService MaNGOS::Singleton<AuctionService>::Instance()

#endif
