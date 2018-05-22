// Copyright (c) 2018 The SalemCash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cash.h>

#include <consensus/consensus.h>
#include <random.h>

bool CCashView::GetCash(const COutPoint &outpoint, Cash &cash) const { return false; }
uint256 CCashView::GetBestBlock() const { return uint256(); }
std::vector<uint256> CCashView::GetHeadBlocks() const { return std::vector<uint256>(); }
bool CCashView::BatchWrite(CCashMap &mapCash, const uint256 &hashBlock) { return false; }
CCashViewCursor *CCashView::Cursor() const { return nullptr; }

bool CCashView::HaveCash(const COutPoint &outpoint) const
{
    Cash cash;
    return GetCash(outpoint, cash);
}

CCashViewBacked::CCashViewBacked(CCashView *viewIn) : base(viewIn) { }
bool CCashViewBacked::GetCash(const COutPoint &outpoint, Cash &cash) const { return base->GetCash(outpoint, cash); }
bool CCashViewBacked::HaveCash(const COutPoint &outpoint) const { return base->HaveCash(outpoint); }
uint256 CCashViewBacked::GetBestBlock() const { return base->GetBestBlock(); }
std::vector<uint256> CCashViewBacked::GetHeadBlocks() const { return base->GetHeadBlocks(); }
void CCashViewBacked::SetBackend(CCashView &viewIn) { base = &viewIn; }
bool CCashViewBacked::BatchWrite(CCashMap &mapCash, const uint256 &hashBlock) { return base->BatchWrite(mapCash, hashBlock); }
CCashViewCursor *CCashViewBacked::Cursor() const { return base->Cursor(); }
size_t CCashViewBacked::EstimateSize() const { return base->EstimateSize(); }

SaltedOutpointHasher::SaltedOutpointHasher() : k0(GetRand(std::numeric_limits<uint64_t>::max())), k1(GetRand(std::numeric_limits<uint64_t>::max())) {}

CCashViewCache::CCashViewCache(CCashView *baseIn) : CCashViewBacked(baseIn), cachedCashUsage(0) {}

size_t CCashViewCache::DynamicMemoryUsage() const {
    return memusage::DynamicUsage(cacheCash) + cachedCashUsage;
}

CCashMap::iterator CCashViewCache::FetchCash(const COutPoint &outpoint) const {
    CCashMap::iterator it = cacheCash.find(outpoint);
    if (it != cacheCash.end())
        return it;
    Cash tmp;
    if (!base->GetCash(outpoint, tmp))
        return cacheCash.end();
    CCashMap::iterator ret = cacheCash.emplace(std::piecewise_construct, std::forward_as_tuple(outpoint), std::forward_as_tuple(std::move(tmp))).first;
    if (ret->second.cash.IsSpent()) {
        // The parent only has an empty entry for this outpoint; we can consider our
        // version as fresh.
        ret->second.flags = CCashCacheEntry::FRESH;
    }
    cachedCashUsage += ret->second.cash.DynamicMemoryUsage();
    return ret;
}

bool CCashViewCache::GetCash(const COutPoint &outpoint, Cash &cash) const {
    CCashMap::const_iterator it = FetchCash(outpoint);
    if (it != cacheCash.end()) {
        cash = it->second.cash;
        return !cash.IsSpent();
    }
    return false;
}

void CCashViewCache::AddCash(const COutPoint &outpoint, Cash&& cash, bool possible_overwrite) {
    assert(!cash.IsSpent());
    if (cash.out.scriptPubKey.IsUnspendable()) return;
    CCashMap::iterator it;
    bool inserted;
    std::tie(it, inserted) = cacheCash.emplace(std::piecewise_construct, std::forward_as_tuple(outpoint), std::tuple<>());
    bool fresh = false;
    if (!inserted) {
        cachedCashUsage -= it->second.cash.DynamicMemoryUsage();
    }
    if (!possible_overwrite) {
        if (!it->second.cash.IsSpent()) {
            throw std::logic_error("Adding new cash that replaces non-pruned entry");
        }
        fresh = !(it->second.flags & CCashCacheEntry::DIRTY);
    }
    it->second.cash = std::move(cash);
    it->second.flags |= CCashCacheEntry::DIRTY | (fresh ? CCashCacheEntry::FRESH : 0);
    cachedCashUsage += it->second.cash.DynamicMemoryUsage();
}

void AddCash(CCashViewCache& cache, const CTransaction &tx, int nHeight, bool check) {
    bool fCashBase = tx.IsCashBase();
    const uint256& txid = tx.GetHash();
    for (size_t i = 0; i < tx.vout.size(); ++i) {
        bool overwrite = check ? cache.HaveCash(COutPoint(txid, i)) : fCashBase;
        // Always set the possible_overwrite flag to AddCash for cashbase txn, in order to correctly
        // deal with the pre-BIP30 occurrences of duplicate cashbase transactions.
        cache.AddCash(COutPoint(txid, i), Cash(tx.vout[i], nHeight, fCashBase), overwrite);
    }
}

bool CCashViewCache::SpendCash(const COutPoint &outpoint, Cash* moveout) {
    CCashMap::iterator it = FetchCash(outpoint);
    if (it == cacheCash.end()) return false;
    cachedCashUsage -= it->second.cash.DynamicMemoryUsage();
    if (moveout) {
        *moveout = std::move(it->second.cash);
    }
    if (it->second.flags & CCashCacheEntry::FRESH) {
        cacheCash.erase(it);
    } else {
        it->second.flags |= CCashCacheEntry::DIRTY;
        it->second.cash.Clear();
    }
    return true;
}

static const Cash cashEmpty;

const Cash& CCashViewCache::AccessCash(const COutPoint &outpoint) const {
    CCashMap::const_iterator it = FetchCash(outpoint);
    if (it == cacheCash.end()) {
        return cashEmpty;
    } else {
        return it->second.cash;
    }
}

bool CCashViewCache::HaveCash(const COutPoint &outpoint) const {
    CCashMap::const_iterator it = FetchCash(outpoint);
    return (it != cacheCash.end() && !it->second.cash.IsSpent());
}

bool CCashViewCache::HaveCashInCache(const COutPoint &outpoint) const {
    CCashMap::const_iterator it = cacheCash.find(outpoint);
    return (it != cacheCash.end() && !it->second.cash.IsSpent());
}

uint256 CCashViewCache::GetBestBlock() const {
    if (hashBlock.IsNull())
        hashBlock = base->GetBestBlock();
    return hashBlock;
}

void CCashViewCache::SetBestBlock(const uint256 &hashBlockIn) {
    hashBlock = hashBlockIn;
}

bool CCashViewCache::BatchWrite(CCashMap &mapCash, const uint256 &hashBlockIn) {
    for (CCashMap::iterator it = mapCash.begin(); it != mapCash.end(); it = mapCash.erase(it)) {
        // Ignore non-dirty entries (optimization).
        if (!(it->second.flags & CCashCacheEntry::DIRTY)) {
            continue;
        }
        CCashMap::iterator itUs = cacheCash.find(it->first);
        if (itUs == cacheCash.end()) {
            // The parent cache does not have an entry, while the child does
            // We can ignore it if it's both FRESH and pruned in the child
            if (!(it->second.flags & CCashCacheEntry::FRESH && it->second.cash.IsSpent())) {
                // Otherwise we will need to create it in the parent
                // and move the data up and mark it as dirty
                CCashCacheEntry& entry = cacheCash[it->first];
                entry.cash = std::move(it->second.cash);
                cachedCashUsage += entry.cash.DynamicMemoryUsage();
                entry.flags = CCashCacheEntry::DIRTY;
                // We can mark it FRESH in the parent if it was FRESH in the child
                // Otherwise it might have just been flushed from the parent's cache
                // and already exist in the grandparent
                if (it->second.flags & CCashCacheEntry::FRESH) {
                    entry.flags |= CCashCacheEntry::FRESH;
                }
            }
        } else {
            // Assert that the child cache entry was not marked FRESH if the
            // parent cache entry has unspent outputs. If this ever happens,
            // it means the FRESH flag was misapplied and there is a logic
            // error in the calling code.
            if ((it->second.flags & CCashCacheEntry::FRESH) && !itUs->second.cash.IsSpent()) {
                throw std::logic_error("FRESH flag misapplied to cache entry for base transaction with spendable outputs");
            }

            // Found the entry in the parent cache
            if ((itUs->second.flags & CCashCacheEntry::FRESH) && it->second.cash.IsSpent()) {
                // The grandparent does not have an entry, and the child is
                // modified and being pruned. This means we can just delete
                // it from the parent.
                cachedCashUsage -= itUs->second.cash.DynamicMemoryUsage();
                cacheCash.erase(itUs);
            } else {
                // A normal modification.
                cachedCashUsage -= itUs->second.cash.DynamicMemoryUsage();
                itUs->second.cash = std::move(it->second.cash);
                cachedCashUsage += itUs->second.cash.DynamicMemoryUsage();
                itUs->second.flags |= CCashCacheEntry::DIRTY;
                // NOTE: It is possible the child has a FRESH flag here in
                // the event the entry we found in the parent is pruned. But
                // we must not copy that FRESH flag to the parent as that
                // pruned state likely still needs to be communicated to the
                // grandparent.
            }
        }
    }
    hashBlock = hashBlockIn;
    return true;
}

bool CCashViewCache::Flush() {
    bool fOk = base->BatchWrite(cacheCash, hashBlock);
    cacheCash.clear();
    cachedCashUsage = 0;
    return fOk;
}

void CCashViewCache::Uncache(const COutPoint& hash)
{
    CCashMap::iterator it = cacheCash.find(hash);
    if (it != cacheCash.end() && it->second.flags == 0) {
        cachedCashUsage -= it->second.cash.DynamicMemoryUsage();
        cacheCash.erase(it);
    }
}

unsigned int CCashViewCache::GetCacheSize() const {
    return cacheCash.size();
}

CAmount CCashViewCache::GetValueIn(const CTransaction& tx) const
{
    if (tx.IsCashBase())
        return 0;

    CAmount nResult = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
        nResult += AccessCash(tx.vin[i].prevout).out.nValue;

    return nResult;
}

bool CCashViewCache::HaveInputs(const CTransaction& tx) const
{
    if (!tx.IsCashBase()) {
        for (unsigned int i = 0; i < tx.vin.size(); i++) {
            if (!HaveCash(tx.vin[i].prevout)) {
                return false;
            }
        }
    }
    return true;
}

static const size_t MIN_TRANSACTION_OUTPUT_WEIGHT = WITNESS_SCALE_FACTOR * ::GetSerializeSize(CTxOut(), SER_NETWORK, PROTOCOL_VERSION);
static const size_t MAX_OUTPUTS_PER_BLOCK = MAX_BLOCK_WEIGHT / MIN_TRANSACTION_OUTPUT_WEIGHT;

const Cash& AccessByTxid(const CCashViewCache& view, const uint256& txid)
{
    COutPoint iter(txid, 0);
    while (iter.n < MAX_OUTPUTS_PER_BLOCK) {
        const Cash& alternate = view.AccessCash(iter);
        if (!alternate.IsSpent()) return alternate;
        ++iter.n;
    }
    return cashEmpty;
}
