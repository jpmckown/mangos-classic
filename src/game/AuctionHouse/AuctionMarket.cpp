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

#include "AuctionHouse/AuctionMarket.h"
#include "AuctionHouse/AuctionHouseMgr.h"
#include "AuctionHouse/AuctionService.h"
#include "Config/Config.h"
#include "Database/DatabaseEnv.h"
#include "Entities/ItemPrototype.h"
#include "Globals/ObjectMgr.h"
#include "Globals/SharedDefines.h"
#include "Log/Log.h"
#include "Server/DBCStores.h"
#include "Util/Util.h"
#include "World/World.h"

#ifdef ENABLE_PLAYERBOTS
#include "playerbot/PlayerbotAIConfig.h"
#endif

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <sstream>

INSTANTIATE_SINGLETON_1(AuctionMarket);

static uint32 const MARKET_HOUSES[] = { 1, 6, 7 };          // AuctionHouse.dbc: Stormwind, Orgrimmar, Booty Bay
static time_t const SELLER_RELOAD_INTERVAL = HOUR;
static time_t const NEVER = std::numeric_limits<time_t>::max();

static uint32 RoundPrice(double price)
{
    double const silver = double(SILVER), gold = double(GOLD);
    if (price < 1.0)
        return 1;
    if (price < gold)
        return uint32(price);
    if (price < 100 * gold)
        return uint32(price / silver) * SILVER;
    return uint32(price / gold) * GOLD;
}

static uint64 FactorKey(uint32 item, uint32 houseIndex)
{
    return (uint64(item) << 8) | houseIndex;
}

void AuctionMarket::LoadConfig()
{
    m_config.enabled = sConfig.GetBoolDefault("AhMarket.Enable", false);
    m_config.passInterval = std::max(1, sConfig.GetIntDefault("AhMarket.PassInterval", 20));
    m_config.maxPostsPerPass = std::max(0, sConfig.GetIntDefault("AhMarket.MaxPostsPerPass", 60));
    m_config.restockChance = sConfig.GetFloatDefault("AhMarket.RestockChance", 0.25f);
    m_config.soldStep = sConfig.GetFloatDefault("AhMarket.Price.SoldStep", 0.05f);
    m_config.expiredStep = sConfig.GetFloatDefault("AhMarket.Price.ExpiredStep", 0.03f);
    m_config.minFactor = sConfig.GetFloatDefault("AhMarket.Price.MinFactor", 0.5f);
    m_config.maxFactor = sConfig.GetFloatDefault("AhMarket.Price.MaxFactor", 2.0f);
    m_config.buyerEnabled = sConfig.GetBoolDefault("AhMarket.Buyer.Enable", false);
    m_config.buyerDelayMin = std::max(0, sConfig.GetIntDefault("AhMarket.Buyer.DelayMin", 600));
    m_config.buyerDelayMax = std::max<uint32>(m_config.buyerDelayMin, sConfig.GetIntDefault("AhMarket.Buyer.DelayMax", 3600));
    m_config.buyerCatalogRatio = sConfig.GetFloatDefault("AhMarket.Buyer.CatalogRatio", 0.75f);
    m_config.buyerVendorMultiplier[ITEM_QUALITY_NORMAL] = sConfig.GetFloatDefault("AhMarket.Buyer.VendorMultiplier.Common", 2.0f);
    m_config.buyerVendorMultiplier[ITEM_QUALITY_UNCOMMON] = sConfig.GetFloatDefault("AhMarket.Buyer.VendorMultiplier.Uncommon", 3.0f);
    m_config.buyerVendorMultiplier[ITEM_QUALITY_RARE] = sConfig.GetFloatDefault("AhMarket.Buyer.VendorMultiplier.Rare", 5.0f);
    m_config.buyerVendorMultiplier[ITEM_QUALITY_EPIC] = sConfig.GetFloatDefault("AhMarket.Buyer.VendorMultiplier.Epic", 8.0f);
    m_config.buyerDailyBudget = std::max(0, sConfig.GetIntDefault("AhMarket.Buyer.DailyBudget", 500));
}

void AuctionMarket::Load()
{
    LoadConfig();

    if (m_houses.empty())
    {
        for (uint32 houseId : MARKET_HOUSES)
        {
            AuctionHouseEntry const* entry = sAuctionHouseStore.LookupEntry(houseId);
            if (!entry)
                continue;

            // with cross-faction auctions every house is the neutral one
            if (sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_AUCTION) && houseId != 7)
                continue;

            House house;
            house.houseId = houseId;
            house.team = AuctionHouseMgr::GetAuctionHouseTeam(entry);
            m_houses.push_back(house);
        }
    }

    if (!m_config.enabled)
    {
        sLog.outString("AH market: disabled (AhMarket.Enable = 0)");
        return;
    }

    m_tiers.clear();
    if (auto result = WorldDatabase.Query("SELECT tier, name, max_listings, min_hours, max_hours, price_spread FROM ahmarket_tier"))
    {
        do
        {
            Field* fields = result->Fetch();
            Tier& tier = m_tiers[fields[0].GetUInt32()];
            tier.name = fields[1].GetCppString();
            tier.maxListings = fields[2].GetUInt32();
            tier.minHours = fields[3].GetUInt32();
            tier.maxHours = std::max(tier.minHours, fields[4].GetUInt32());
            tier.spread = fields[5].GetFloat();
        }
        while (result->NextRow());
    }

    m_catalog.clear();
    m_catalogOrder.clear();
    if (auto result = WorldDatabase.Query("SELECT item, tier, min_listings, max_listings, stack_min, stack_max, price, weight FROM ahmarket_catalog"))
    {
        do
        {
            Field* fields = result->Fetch();
            CatalogItem entry;
            entry.item = fields[0].GetUInt32();
            entry.tier = fields[1].GetUInt32();
            entry.minListings = fields[2].GetUInt32();
            entry.maxListings = std::max(entry.minListings, fields[3].GetUInt32());
            entry.stackMin = std::max(1u, fields[4].GetUInt32());
            entry.stackMax = std::max(entry.stackMin, fields[5].GetUInt32());
            entry.price = fields[6].GetUInt32();
            entry.weight = std::max(1u, fields[7].GetUInt32());

            ItemPrototype const* proto = ObjectMgr::GetItemPrototype(entry.item);
            if (!proto)
            {
                sLog.outErrorDb("ahmarket_catalog: item %u does not exist, skipped", entry.item);
                continue;
            }
            if (m_tiers.find(entry.tier) == m_tiers.end())
            {
                sLog.outErrorDb("ahmarket_catalog: item %u has unknown tier %u, skipped", entry.item, entry.tier);
                continue;
            }
            entry.stackMax = std::min(entry.stackMax, proto->GetMaxStackSize());
            entry.stackMin = std::min(entry.stackMin, entry.stackMax);

            m_catalog[entry.item] = entry;
            m_catalogOrder.push_back(entry.item);
        }
        while (result->NextRow());
    }

    m_factors.clear();
    if (auto result = CharacterDatabase.Query("SELECT item, house, factor FROM ahmarket_state"))
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 houseId = fields[1].GetUInt32();
            for (House const& house : m_houses)
                if (house.houseId == houseId)
                    m_factors[FactorKey(fields[0].GetUInt32(), HouseIndex(house))] = fields[2].GetFloat();
        }
        while (result->NextRow());
    }

    if (m_catalog.empty())
        sLog.outError("AH market: ahmarket_catalog is empty or missing (apply custom-content/AH_MARKET_CATALOG.sql); nothing will be listed");

    sLog.outString("AH market: %u catalog items in %u tiers, %u houses, %u price factors%s",
                   uint32(m_catalog.size()), uint32(m_tiers.size()), uint32(m_houses.size()), uint32(m_factors.size()),
                   m_config.buyerEnabled ? ", buyer on" : "");
}

void AuctionMarket::LoadSellers()
{
    for (auto& sellers : m_sellers)
        sellers.clear();
    m_sellerSet.clear();
    m_sellerSet.insert(0);                                  // listings without a seller are ours too
    m_sellersLoadedAt = time(nullptr);

#ifdef ENABLE_PLAYERBOTS
    if (sPlayerbotAIConfig.randomBotAccounts.empty())
        return;

    std::ostringstream accounts;
    bool first = true;
    for (uint32 account : sPlayerbotAIConfig.randomBotAccounts)
    {
        accounts << (first ? "" : ",") << account;
        first = false;
    }

    if (auto result = CharacterDatabase.PQuery("SELECT guid, race FROM characters WHERE account IN (%s)", accounts.str().c_str()))
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 guid = fields[0].GetUInt32();
            uint32 raceMask = 1 << (fields[1].GetUInt8() - 1);
            m_sellers[(raceMask & RACEMASK_ALLIANCE) ? 0 : 1].push_back(guid);
            m_sellers[2].push_back(guid);
            m_sellerSet.insert(guid);
        }
        while (result->NextRow());
    }
#endif
}

void AuctionMarket::Update(uint32 diff)
{
    if (!m_config.enabled || m_houses.empty() || m_catalog.empty())
        return;

    m_timer += diff;
    if (m_timer < m_config.passInterval * IN_MILLISECONDS)
        return;
    m_timer = 0;

    RunHouse(m_houses[m_nextHouse++ % m_houses.size()]);
}

void AuctionMarket::RunAllHouses()
{
    if (m_catalog.empty())
        return;

    for (House& house : m_houses)
        RunHouse(house);
}

bool AuctionMarket::IsMarketListing(AuctionEntry const* auction) const
{
    return auction->deposit == 0 && m_sellerSet.count(auction->owner) && m_catalog.count(auction->itemTemplate);
}

float AuctionMarket::GetFactor(uint32 item, uint32 houseIndex) const
{
    auto itr = m_factors.find(FactorKey(item, houseIndex));
    return itr != m_factors.end() ? itr->second : 1.0f;
}

void AuctionMarket::Adjust(House& house, uint32 item, bool sold)
{
    uint32 houseIndex = HouseIndex(house);
    float factor = GetFactor(item, houseIndex);
    factor = sold ? factor * (1.0f + m_config.soldStep) : factor * (1.0f - m_config.expiredStep);
    factor = std::max(m_config.minFactor, std::min(m_config.maxFactor, factor));
    m_factors[FactorKey(item, houseIndex)] = factor;

    if (sold)
        ++house.sold;
    else
        ++house.expired;

    CharacterDatabase.PExecute("REPLACE INTO ahmarket_state (item, house, factor) VALUES ('%u', '%u', '%f')", item, house.houseId, factor);
}

void AuctionMarket::RunHouse(House& house)
{
    AuctionHouseEntry const* houseEntry = sAuctionHouseStore.LookupEntry(house.houseId);
    if (!houseEntry)
        return;

    time_t now = time(nullptr);
    if (m_sellersLoadedAt + SELLER_RELOAD_INTERVAL <= now || m_sellerSet.size() <= 1)
        LoadSellers();

    uint32 today = uint32(sWorld.GetGameTime() / DAY);
    if (today != m_buyerDay)
    {
        m_buyerDay = today;
        for (House& h : m_houses)
            h.spentToday = 0;
    }

    // a house is visited every houses * interval seconds; take listings down before they can expire in between
    time_t takeDownBefore = now + time_t(m_houses.size() * m_config.passInterval * 2 + MINUTE);

    std::unordered_map<uint32, uint32> counts;
    std::map<uint32, uint32> tierCounts;
    std::unordered_set<uint32> present;
    std::vector<uint32> takeDown;
    std::vector<AuctionEntry const*> playerAuctions;

    AuctionHouseObject* auctionHouse = sAuctionMgr.GetAuctionsMap(houseEntry);
    for (auto const& itr : auctionHouse->GetAuctions())
    {
        AuctionEntry const* auction = itr.second;
        if (IsMarketListing(auction))
        {
            present.insert(auction->Id);
            Listing& listing = house.listings[auction->Id];
            listing.item = auction->itemTemplate;
            listing.expireTime = auction->expireTime;
            listing.bid = auction->bid;
            if (!auction->bid && auction->expireTime <= takeDownBefore)
                takeDown.push_back(auction->Id);
            else
            {
                ++counts[auction->itemTemplate];
                ++tierCounts[m_catalog[auction->itemTemplate].tier];
            }
        }
        else if (!m_sellerSet.count(auction->owner))
            playerAuctions.push_back(auction);
    }

    // listings gone since the last pass: bought out or won at expiry, unless it expired with no bid
    for (auto itr = house.listings.begin(); itr != house.listings.end();)
    {
        if (present.count(itr->first))
        {
            ++itr;
            continue;
        }
        if (itr->second.ours)
            Adjust(house, itr->second.item, itr->second.bid || now < itr->second.expireTime);
        itr = house.listings.erase(itr);
    }

    for (uint32 auctionId : takeDown)
    {
        Listing listing = house.listings[auctionId];
        house.listings.erase(auctionId);
        if (!sAuctionService.RemoveServerAuction(house.houseId, auctionId))
            continue;
        if (listing.ours)
            Adjust(house, listing.item, false);
        else
            ++house.cleaned;
    }

    house.tierCounts = tierCounts;
    Restock(house, counts);

    if (m_config.buyerEnabled)
        Buy(house, playerAuctions);
    else
        house.buyerWait.clear();
}

void AuctionMarket::Restock(House& house, std::unordered_map<uint32, uint32>& counts)
{
    uint32 posts = 0;
    auto tierFull = [&](uint32 tier) { return house.tierCounts[tier] >= m_tiers[tier].maxListings; };
    auto post = [&](CatalogItem const& entry)
    {
        if (!Post(house, entry))
            return false;
        ++counts[entry.item];
        ++house.tierCounts[entry.tier];
        return ++posts < m_config.maxPostsPerPass;
    };

    if (!m_config.maxPostsPerPass)
        return;

    // everything below its minimum first, in random order so a capped pass doesn't always favour the same items
    std::vector<uint32> order = m_catalogOrder;
    std::shuffle(order.begin(), order.end(), std::mt19937(urand()));
    for (uint32 item : order)
    {
        CatalogItem const& entry = m_catalog[item];
        while (counts[item] < entry.minListings && !tierFull(entry.tier))
            if (!post(entry))
                return;
    }

    // then items below their maximum, by chance, in weighted random order (Efraimidis-Spirakis keys)
    std::vector<std::pair<double, uint32>> candidates;
    for (uint32 item : m_catalogOrder)
    {
        CatalogItem const& entry = m_catalog[item];
        if (counts[item] < entry.maxListings)
            candidates.emplace_back(std::log(std::max(rand_norm(), 1e-12)) / entry.weight, item);
    }
    std::sort(candidates.begin(), candidates.end(), std::greater<std::pair<double, uint32>>());

    for (auto const& candidate : candidates)
    {
        CatalogItem const& entry = m_catalog[candidate.second];
        if (tierFull(entry.tier) || rand_norm() >= m_config.restockChance)
            continue;
        if (!post(entry))
            return;
    }
}

bool AuctionMarket::Post(House& house, CatalogItem const& entry)
{
    Tier const& tier = m_tiers[entry.tier];

    std::vector<uint32> const& sellers = m_sellers[house.team == ALLIANCE ? 0 : house.team == HORDE ? 1 : 2];
    uint32 seller = sellers.empty() ? 0 : sellers[urand(0, sellers.size() - 1)];

    // stacks of 5, 10, 15... when the range allows it, like players post them
    uint32 stack = urand(entry.stackMin, entry.stackMax);
    if (entry.stackMax >= 10)
    {
        uint32 lo = (entry.stackMin + 4) / 5, hi = entry.stackMax / 5;
        if (lo && lo <= hi)
            stack = urand(lo, hi) * 5;
    }

    double unit = entry.price * GetFactor(entry.item, HouseIndex(house)) * frand(1.0f - tier.spread, 1.0f + tier.spread);

    AuctionService::PostRequest request;
    request.houseId = house.houseId;
    request.owner = seller;
    request.itemEntry = entry.item;
    request.count = stack;
    request.buyout = RoundPrice(unit * stack);
    request.startBid = std::max(1u, RoundPrice(request.buyout * frand(0.7f, 0.9f)));
    request.duration = urand(tier.minHours, tier.maxHours) * HOUR;

    AuctionEntry* auction = sAuctionService.PostServerAuction(request);
    if (!auction)
        return false;

    house.listings[auction->Id] = { auction->itemTemplate, auction->expireTime, 0, true };
    ++house.posted;
    return true;
}

uint32 AuctionMarket::BuyerLimit(uint32 item, uint32 count, uint32 houseIndex) const
{
    auto catalogItr = m_catalog.find(item);
    if (catalogItr != m_catalog.end())
        return uint32(catalogItr->second.price * GetFactor(item, houseIndex) * m_config.buyerCatalogRatio * count);

    ItemPrototype const* proto = ObjectMgr::GetItemPrototype(item);
    if (!proto || !proto->SellPrice || proto->Quality == ITEM_QUALITY_POOR || proto->Quality > ITEM_QUALITY_EPIC)
        return 0;

    switch (proto->Class)
    {
        case ITEM_CLASS_WEAPON:
        case ITEM_CLASS_ARMOR:
            if (proto->Quality < ITEM_QUALITY_UNCOMMON)     // white gear is vendor trash
                return 0;
            break;
        case ITEM_CLASS_QUEST:
        case ITEM_CLASS_KEY:
        case ITEM_CLASS_MONEY:
        case ITEM_CLASS_PERMANENT:
            return 0;
        default:
            break;
    }

    return uint32(proto->SellPrice * m_config.buyerVendorMultiplier[proto->Quality] * count);
}

void AuctionMarket::Buy(House& house, std::vector<AuctionEntry const*> const& candidates)
{
    time_t now = time(nullptr);
    uint32 budget = m_config.buyerDailyBudget * GOLD;

    // decide on each auction once, a while after it shows up, like a player browsing now and then
    std::vector<std::pair<uint32, uint32>> purchases;       // auction id, amount
    std::unordered_set<uint32> seen;
    for (AuctionEntry const* auction : candidates)
    {
        seen.insert(auction->Id);
        auto wait = house.buyerWait.find(auction->Id);
        if (wait == house.buyerWait.end())
        {
            house.buyerWait[auction->Id] = now + urand(m_config.buyerDelayMin, m_config.buyerDelayMax);
            continue;
        }
        if (now < wait->second)
            continue;

        if (auction->bid && !auction->bidder)               // we already hold the top bid
            continue;

        uint32 amount = auction->buyout ? auction->buyout : auction->bid ? auction->bid + auction->GetAuctionOutBid() : auction->startbid;
        uint32 limit = BuyerLimit(auction->itemTemplate, std::max(1u, auction->itemCount), HouseIndex(house));
        if (!amount || amount > limit)
        {
            wait->second = NEVER;                           // a new price means a new auction
            continue;
        }
        if (house.spentToday + amount > budget)
            continue;                                       // maybe tomorrow

        purchases.emplace_back(auction->Id, amount);
        house.spentToday += amount;
        wait->second = NEVER;
    }

    for (auto itr = house.buyerWait.begin(); itr != house.buyerWait.end();)
        itr = seen.count(itr->first) ? std::next(itr) : house.buyerWait.erase(itr);

    for (auto const& purchase : purchases)
    {
        if (sAuctionService.PlaceServerBid(house.houseId, purchase.first, purchase.second))
            ++house.bought;
        else
            house.spentToday -= purchase.second;
    }
}

std::string AuctionMarket::GetStatus() const
{
    std::ostringstream out;
    out << "AH market: " << (m_config.enabled ? "on" : "off") << ", " << m_catalog.size() << " catalog items, buyer "
        << (m_config.buyerEnabled ? "on" : "off") << ", " << m_sellerSet.size() - 1 << " sellers\n";
    for (House const& house : m_houses)
    {
        out << "House " << house.houseId << ": " << house.listings.size() << " listings (";
        bool first = true;
        for (auto const& tier : m_tiers)
        {
            auto count = house.tierCounts.find(tier.first);
            out << (first ? "" : ", ") << tier.second.name << " " << (count != house.tierCounts.end() ? count->second : 0)
                << "/" << tier.second.maxListings;
            first = false;
        }
        out << "); since start: " << house.posted << " posted, " << house.sold << " sold, " << house.expired
            << " unsold, " << house.cleaned << " old bot listings removed, " << house.bought << " bought from players; buyer spent " << house.spentToday / GOLD << "g today\n";
    }
    return out.str();
}
