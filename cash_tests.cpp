// Copyright (c) 2018 The SalemCash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cash.h>
#include <script/standard.h>
#include <uint256.h>
#include <undo.h>
#include <utilstrencodings.h>
#include <test/test_Salemcash.h>
#include <validation.h>
#include <consensus/validation.h>

#include <vector>
#include <map>

#include <boost/test/unit_test.hpp>

int ApplyTxInUndo(Cash&& undo, CCashViewCache& view, const COutPoint& out);
void UpdateCash(const CTransaction& tx, CCashViewCache& inputs, CTxUndo &txundo, int nHeight);

namespace
{
//! equality test
bool operator==(const Cash &a, const Cash &b) {
    // Empty Cash objects are always equal.
    if (a.IsSpent() && b.IsSpent()) return true;
    return a.fCashBase == b.fCashBase &&
           a.nHeight == b.nHeight &&
           a.out == b.out;
}

class CCashViewTest : public CCashView
{
    uint256 hashBestBlock_;
    std::map<COutPoint, Cash> map_;

public:
    bool GetCash(const COutPoint& outpoint, Cash& cash) const override
    {
        std::map<COutPoint, Cash>::const_iterator it = map_.find(outpoint);
        if (it == map_.end()) {
            return false;
        }
        cash = it->second;
        if (cash.IsSpent() && InsecureRandBool() == 0) {
            // Randomly return false in case of an empty entry.
            return false;
        }
        return true;
    }

    uint256 GetBestBlock() const override { return hashBestBlock_; }

    bool BatchWrite(CCashMap& mapCash, const uint256& hashBlock) override
    {
        for (CCashMap::iterator it = mapCash.begin(); it != mapCash.end(); ) {
            if (it->second.flags & CCashCacheEntry::DIRTY) {
                // Same optimization used in CCashViewDB is to only write dirty entries.
                map_[it->first] = it->second.cash;
                if (it->second.cash.IsSpent() && InsecureRandRange(3) == 0) {
                    // Randomly delete empty entries on write.
                    map_.erase(it->first);
                }
            }
            mapCash.erase(it++);
        }
        if (!hashBlock.IsNull())
            hashBestBlock_ = hashBlock;
        return true;
    }
};

class CCashViewCacheTest : public CCashViewCache
{
public:
    explicit CCashViewCacheTest(CCashView* _base) : CCashViewCache(_base) {}

    void SelfTest() const
    {
        // Manually recompute the dynamic usage of the whole data, and compare it.
        size_t ret = memusage::DynamicUsage(cacheCash);
        size_t count = 0;
        for (const auto& entry : cacheCash) {
            ret += entry.second.cash.DynamicMemoryUsage();
            ++count;
        }
        BOOST_CHECK_EQUAL(GetCacheSize(), count);
        BOOST_CHECK_EQUAL(DynamicMemoryUsage(), ret);
    }

    CCashMap& map() const { return cacheCash; }
    size_t& usage() const { return cachedCashUsage; }
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(cash_tests, BasicTestingSetup)

static const unsigned int NUM_SIMULATION_ITERATIONS = 40000;

// This is a large randomized insert/remove simulation test on a variable-size
// stack of caches on top of CCashViewTest.
//
// It will randomly create/update/delete Cash entries to a tip of caches, with
// txids picked from a limited list of random 256-bit hashes. Occasionally, a
// new tip is added to the stack of caches, or the tip is flushed and removed.
//
// During the process, booleans are kept to make sure that the randomized
// operation hits all branches.
BOOST_AUTO_TEST_CASE(cash_cache_simulation_test)
{
    // Various coverage trackers.
    bool removed_all_caches = false;
    bool reached_4_caches = false;
    bool added_an_entry = false;
    bool added_an_unspendable_entry = false;
    bool removed_an_entry = false;
    bool updated_an_entry = false;
    bool found_an_entry = false;
    bool missed_an_entry = false;
    bool uncached_an_entry = false;

    // A simple map to track what we expect the cache stack to represent.
    std::map<COutPoint, Cash> result;

    // The cache stack.
    CCashViewTest base; // A CCashViewTest at the bottom.
    std::vector<CCashViewCacheTest*> stack; // A stack of CCashViewCaches on top.
    stack.push_back(new CCashViewCacheTest(&base)); // Start with one cache.

    // Use a limited set of random transaction ids, so we do test overwriting entries.
    std::vector<uint256> txids;
    txids.resize(NUM_SIMULATION_ITERATIONS / 8);
    for (unsigned int i = 0; i < txids.size(); i++) {
        txids[i] = InsecureRand256();
    }

    for (unsigned int i = 0; i < NUM_SIMULATION_ITERATIONS; i++) {
        // Do a random modification.
        {
            uint256 txid = txids[InsecureRandRange(txids.size())]; // txid we're going to modify in this iteration.
            Cash& cash = result[COutPoint(txid, 0)];

            // Determine whether to test HaveCash before or after Access* (or both). As these functions
            // can influence each other's behaviour by pulling things into the cache, all combinations
            // are tested.
            bool test_havecash_before = InsecureRandBits(2) == 0;
            bool test_havecash_after = InsecureRandBits(2) == 0;

            bool result_havecash = test_havecash_before ? stack.back()->HaveCash(COutPoint(txid, 0)) : false;
            const Cash& entry = (InsecureRandRange(500) == 0) ? AccessByTxid(*stack.back(), txid) : stack.back()->AccessCash(COutPoint(txid, 0));
            BOOST_CHECK(cash == entry);
            BOOST_CHECK(!test_havecash_before || result_havecash == !entry.IsSpent());

            if (test_havecash_after) {
                bool ret = stack.back()->HaveCash(COutPoint(txid, 0));
                BOOST_CHECK(ret == !entry.IsSpent());
            }

            if (InsecureRandRange(5) == 0 || cash.IsSpent()) {
                Cash newcash;
                newcash.out.nValue = InsecureRand32();
                newcash.nHeight = 1;
                if (InsecureRandRange(16) == 0 && cash.IsSpent()) {
                    newcash.out.scriptPubKey.assign(1 + InsecureRandBits(6), OP_RETURN);
                    BOOST_CHECK(newcash.out.scriptPubKey.IsUnspendable());
                    added_an_unspendable_entry = true;
                } else {
                    newcash.out.scriptPubKey.assign(InsecureRandBits(6), 0); // Random sizes so we can test memory usage accounting
                    (cash.IsSpent() ? added_an_entry : updated_an_entry) = true;
                    cash = newcash;
                }
                stack.back()->AddCash(COutPoint(txid, 0), std::move(newcash), !cash.IsSpent() || InsecureRand32() & 1);
            } else {
                removed_an_entry = true;
                cash.Clear();
                stack.back()->SpendCash(COutPoint(txid, 0));
            }
        }

        // One every 10 iterations, remove a random entry from the cache
        if (InsecureRandRange(10) == 0) {
            COutPoint out(txids[InsecureRand32() % txids.size()], 0);
            int cacheid = InsecureRand32() % stack.size();
            stack[cacheid]->Uncache(out);
            uncached_an_entry |= !stack[cacheid]->HaveCashInCache(out);
        }

        // Once every 1000 iterations and at the end, verify the full cache.
        if (InsecureRandRange(1000) == 1 || i == NUM_SIMULATION_ITERATIONS - 1) {
            for (const auto& entry : result) {
                bool have = stack.back()->HaveCash(entry.first);
                const Cash& cash = stack.back()->AccessCash(entry.first);
                BOOST_CHECK(have == !cash.IsSpent());
                BOOST_CHECK(cash == entry.second);
                if (cash.IsSpent()) {
                    missed_an_entry = true;
                } else {
                    BOOST_CHECK(stack.back()->HaveCashInCache(entry.first));
                    found_an_entry = true;
                }
            }
            for (const CCashViewCacheTest *test : stack) {
                test->SelfTest();
            }
        }

        if (InsecureRandRange(100) == 0) {
            // Every 100 iterations, flush an intermediate cache
            if (stack.size() > 1 && InsecureRandBool() == 0) {
                unsigned int flushIndex = InsecureRandRange(stack.size() - 1);
                stack[flushIndex]->Flush();
            }
        }
        if (InsecureRandRange(100) == 0) {
            // Every 100 iterations, change the cache stack.
            if (stack.size() > 0 && InsecureRandBool() == 0) {
                //Remove the top cache
                stack.back()->Flush();
                delete stack.back();
                stack.pop_back();
            }
            if (stack.size() == 0 || (stack.size() < 4 && InsecureRandBool())) {
                //Add a new cache
                CCashView* tip = &base;
                if (stack.size() > 0) {
                    tip = stack.back();
                } else {
                    removed_all_caches = true;
                }
                stack.push_back(new CCashViewCacheTest(tip));
                if (stack.size() == 4) {
                    reached_4_caches = true;
                }
            }
        }
    }

    // Clean up the stack.
    while (stack.size() > 0) {
        delete stack.back();
        stack.pop_back();
    }

    // Verify coverage.
    BOOST_CHECK(removed_all_caches);
    BOOST_CHECK(reached_4_caches);
    BOOST_CHECK(added_an_entry);
    BOOST_CHECK(added_an_unspendable_entry);
    BOOST_CHECK(removed_an_entry);
    BOOST_CHECK(updated_an_entry);
    BOOST_CHECK(found_an_entry);
    BOOST_CHECK(missed_an_entry);
    BOOST_CHECK(uncached_an_entry);
}

// Store of all necessary tx and undo data for next test
typedef std::map<COutPoint, std::tuple<CTransaction,CTxUndo,Cash>> UtxoData;
UtxoData utxoData;

UtxoData::iterator FindRandomFrom(const std::set<COutPoint> &utxoSet) {
    assert(utxoSet.size());
    auto utxoSetIt = utxoSet.lower_bound(COutPoint(InsecureRand256(), 0));
    if (utxoSetIt == utxoSet.end()) {
        utxoSetIt = utxoSet.begin();
    }
    auto utxoDataIt = utxoData.find(*utxoSetIt);
    assert(utxoDataIt != utxoData.end());
    return utxoDataIt;
}


// This test is similar to the previous test
// except the emphasis is on testing the functionality of UpdateCash
// random txs are created and UpdateCash is used to update the cache stack
// In particular it is tested that spending a duplicate cashbase tx
// has the expected effect (the other duplicate is overwritten at all cache levels)
BOOST_AUTO_TEST_CASE(updatecash_simulation_test)
{
    bool spent_a_duplicate_cashbase = false;
    // A simple map to track what we expect the cache stack to represent.
    std::map<COutPoint, Cash> result;

    // The cache stack.
    CCashViewTest base; // A CCashViewTest at the bottom.
    std::vector<CCashViewCacheTest*> stack; // A stack of CCashViewCaches on top.
    stack.push_back(new CCashViewCacheTest(&base)); // Start with one cache.

    // Track the txids we've used in various sets
    std::set<COutPoint> cashbase_cash;
    std::set<COutPoint> disconnected_cash;
    std::set<COutPoint> duplicate_cash;
    std::set<COutPoint> utxoset;

    for (unsigned int i = 0; i < NUM_SIMULATION_ITERATIONS; i++) {
        uint32_t randiter = InsecureRand32();

        // 19/20 txs add a new transaction
        if (randiter % 20 < 19) {
            CMutableTransaction tx;
            tx.vin.resize(1);
            tx.vout.resize(1);
            tx.vout[0].nValue = i; //Keep txs unique unless intended to duplicate
            tx.vout[0].scriptPubKey.assign(InsecureRand32() & 0x3F, 0); // Random sizes so we can test memory usage accounting
            unsigned int height = InsecureRand32();
            Cash old_cash;

            // 2/20 times create a new cashbase
            if (randiter % 20 < 2 || cashbase_cash.size() < 10) {
                // 1/10 of those times create a duplicate cashbase
                if (InsecureRandRange(10) == 0 && cashbase_cash.size()) {
                    auto utxod = FindRandomFrom(cashbase_cash);
                    // Reuse the exact same cashbase
                    tx = std::get<0>(utxod->second);
                    // shouldn't be available for reconnection if it's been duplicated
                    disconnected_cash.erase(utxod->first);

                    duplicate_cash.insert(utxod->first);
                }
                else {
                    cashbase_cash.insert(COutPoint(tx.GetHash(), 0));
                }
                assert(CTransaction(tx).IsCashBase());
            }

            // 17/20 times reconnect previous or add a regular tx
            else {

                COutPoint prevout;
                // 1/20 times reconnect a previously disconnected tx
                if (randiter % 20 == 2 && disconnected_cash.size()) {
                    auto utxod = FindRandomFrom(disconnected_cash);
                    tx = std::get<0>(utxod->second);
                    prevout = tx.vin[0].prevout;
                    if (!CTransaction(tx).IsCashBase() && !utxoset.count(prevout)) {
                        disconnected_cash.erase(utxod->first);
                        continue;
                    }

                    // If this tx is already IN the UTXO, then it must be a cashbase, and it must be a duplicate
                    if (utxoset.count(utxod->first)) {
                        assert(CTransaction(tx).IsCashBase());
                        assert(duplicate_cash.count(utxod->first));
                    }
                    disconnected_cash.erase(utxod->first);
                }

                // 16/20 times create a regular tx
                else {
                    auto utxod = FindRandomFrom(utxoset);
                    prevout = utxod->first;

                    // Construct the tx to spend the cash of prevouthash
                    tx.vin[0].prevout = prevout;
                    assert(!CTransaction(tx).IsCashBase());
                }
                // In this simple test cash only have two states, spent or unspent, save the unspent state to restore
                old_cash = result[prevout];
                // Update the expected result of prevouthash to know these coins are spent
                result[prevout].Clear();

                utxoset.erase(prevout);

                // The test is designed to ensure spending a duplicate cashbase will work properly
                // if that ever happens and not resurrect the previously overwritten cashbase
                if (duplicate_cash.count(prevout)) {
                    spent_a_duplicate_cashbase = true;
                }

            }
            // Update the expected result to know about the new output cash
            assert(tx.vout.size() == 1);
            const COutPoint outpoint(tx.GetHash(), 0);
            result[outpoint] = Cash(tx.vout[0], height, CTransaction(tx).IsCashBase());

            // Call UpdateCash on the top cache
            CTxUndo undo;
            UpdateCash(tx, *(stack.back()), undo, height);

            // Update the utxo set for future spends
            utxoset.insert(outpoint);

            // Track this tx and undo info to use later
            utxoData.emplace(outpoint, std::make_tuple(tx,undo,old_cash));
        } else if (utxoset.size()) {
            //1/20 times undo a previous transaction
            auto utxod = FindRandomFrom(utxoset);

            CTransaction &tx = std::get<0>(utxod->second);
            CTxUndo &undo = std::get<1>(utxod->second);
            Cash &orig_cash = std::get<2>(utxod->second);

            // Update the expected result
            // Remove new outputs
            result[utxod->first].Clear();
            // If not cashbase restore prevout
            if (!tx.IsCashBase()) {
                result[tx.vin[0].prevout] = orig_cash;
            }

            // Disconnect the tx from the current UTXO
            // See code in DisconnectBlock
            // remove outputs
            stack.back()->SpendCash(utxod->first);
            // restore inputs
            if (!tx.IsCashBase()) {
                const COutPoint &out = tx.vin[0].prevout;
                Cash cash = undo.vprevout[0];
                ApplyTxInUndo(std::move(cash), *(stack.back()), out);
            }
            // Store as a candidate for reconnection
            disconnected_cash.insert(utxod->first);

            // Update the utxoset
            utxoset.erase(utxod->first);
            if (!tx.IsCashBase())
                utxoset.insert(tx.vin[0].prevout);
        }

        // Once every 1000 iterations and at the end, verify the full cache.
        if (InsecureRandRange(1000) == 1 || i == NUM_SIMULATION_ITERATIONS - 1) {
            for (const auto& entry : result) {
                bool have = stack.back()->HaveCash(entry.first);
                const Cash& cash = stack.back()->AccessCash(entry.first);
                BOOST_CHECK(have == !cash.IsSpent());
                BOOST_CHECK(cash == entry.second);
            }
        }

        // One every 10 iterations, remove a random entry from the cache
        if (utxoset.size() > 1 && InsecureRandRange(30) == 0) {
            stack[InsecureRand32() % stack.size()]->Uncache(FindRandomFrom(utxoset)->first);
        }
        if (disconnected_cash.size() > 1 && InsecureRandRange(30) == 0) {
            stack[InsecureRand32() % stack.size()]->Uncache(FindRandomFrom(disconnected_cash)->first);
        }
        if (duplicate_cash.size() > 1 && InsecureRandRange(30) == 0) {
            stack[InsecureRand32() % stack.size()]->Uncache(FindRandomFrom(duplicate_cash)->first);
        }

        if (InsecureRandRange(100) == 0) {
            // Every 100 iterations, flush an intermediate cache
            if (stack.size() > 1 && InsecureRandBool() == 0) {
                unsigned int flushIndex = InsecureRandRange(stack.size() - 1);
                stack[flushIndex]->Flush();
            }
        }
        if (InsecureRandRange(100) == 0) {
            // Every 100 iterations, change the cache stack.
            if (stack.size() > 0 && InsecureRandBool() == 0) {
                stack.back()->Flush();
                delete stack.back();
                stack.pop_back();
            }
            if (stack.size() == 0 || (stack.size() < 4 && InsecureRandBool())) {
                CCashView* tip = &base;
                if (stack.size() > 0) {
                    tip = stack.back();
                }
                stack.push_back(new CCashViewCacheTest(tip));
            }
        }
    }

    // Clean up the stack.
    while (stack.size() > 0) {
        delete stack.back();
        stack.pop_back();
    }

    // Verify coverage.
    BOOST_CHECK(spent_a_duplicate_cashbase);
}

BOOST_AUTO_TEST_CASE(ccash_serialization)
{
    // Good example
    CDataStream ss1(ParseHex("97f23c835800816115944e077fe7c803cfa57f29b36bf87c1d35"), SER_DISK, CLIENT_VERSION);
    Cash cc1;
    ss1 >> cc1;
    BOOST_CHECK_EQUAL(cc1.fCashBase, false);
    BOOST_CHECK_EQUAL(cc1.nHeight, 203998);
    BOOST_CHECK_EQUAL(cc1.out.nValue, 60000000000ULL);
    BOOST_CHECK_EQUAL(HexStr(cc1.out.scriptPubKey), HexStr(GetScriptForDestination(CKeyID(uint160(ParseHex("816115944e077fe7c803cfa57f29b36bf87c1d35"))))));

    // Good example
    CDataStream ss2(ParseHex("8ddf77bbd123008c988f1a4a4de2161e0f50aac7f17e7f9555caa4"), SER_DISK, CLIENT_VERSION);
    Cash cc2;
    ss2 >> cc2;
    BOOST_CHECK_EQUAL(cc2.fCashBase, true);
    BOOST_CHECK_EQUAL(cc2.nHeight, 120891);
    BOOST_CHECK_EQUAL(cc2.out.nValue, 110397);
    BOOST_CHECK_EQUAL(HexStr(cc2.out.scriptPubKey), HexStr(GetScriptForDestination(CKeyID(uint160(ParseHex("8c988f1a4a4de2161e0f50aac7f17e7f9555caa4"))))));

    // Smallest possible example
    CDataStream ss3(ParseHex("000006"), SER_DISK, CLIENT_VERSION);
    Cash cc3;
    ss3 >> cc3;
    BOOST_CHECK_EQUAL(cc3.fCashBase, false);
    BOOST_CHECK_EQUAL(cc3.nHeight, 0);
    BOOST_CHECK_EQUAL(cc3.out.nValue, 0);
    BOOST_CHECK_EQUAL(cc3.out.scriptPubKey.size(), 0);

    // scriptPubKey that ends beyond the end of the stream
    CDataStream ss4(ParseHex("000007"), SER_DISK, CLIENT_VERSION);
    try {
        Cash cc4;
        ss4 >> cc4;
        BOOST_CHECK_MESSAGE(false, "We should have thrown");
    } catch (const std::ios_base::failure& e) {
    }

    // Very large scriptPubKey (3*10^9 bytes) past the end of the stream
    CDataStream tmp(SER_DISK, CLIENT_VERSION);
    uint64_t x = 3000000000ULL;
    tmp << VARINT(x);
    BOOST_CHECK_EQUAL(HexStr(tmp.begin(), tmp.end()), "8a95c0bb00");
    CDataStream ss5(ParseHex("00008a95c0bb00"), SER_DISK, CLIENT_VERSION);
    try {
        Cash cc5;
        ss5 >> cc5;
        BOOST_CHECK_MESSAGE(false, "We should have thrown");
    } catch (const std::ios_base::failure& e) {
    }
}

const static COutPoint OUTPOINT;
const static CAmount PRUNED = -1;
const static CAmount ABSENT = -2;
const static CAmount FAIL = -3;
const static CAmount VALUE1 = 100;
const static CAmount VALUE2 = 200;
const static CAmount VALUE3 = 300;
const static char DIRTY = CCashCacheEntry::DIRTY;
const static char FRESH = CCashCacheEntry::FRESH;
const static char NO_ENTRY = -1;

const static auto FLAGS = {char(0), FRESH, DIRTY, char(DIRTY | FRESH)};
const static auto CLEAN_FLAGS = {char(0), FRESH};
const static auto ABSENT_FLAGS = {NO_ENTRY};

void SetCashValue(CAmount value, Cash& cash)
{
    assert(value != ABSENT);
    cash.Clear();
    assert(cash.IsSpent());
    if (value != PRUNED) {
        cash.out.nValue = value;
        cash.nHeight = 1;
        assert(!cash.IsSpent());
    }
}

size_t InsertCashMapEntry(CCashMap& map, CAmount value, char flags)
{
    if (value == ABSENT) {
        assert(flags == NO_ENTRY);
        return 0;
    }
    assert(flags != NO_ENTRY);
    CCashCacheEntry entry;
    entry.flags = flags;
    SetCashValue(value, entry.cash);
    auto inserted = map.emplace(OUTPOINT, std::move(entry));
    assert(inserted.second);
    return inserted.first->second.cash.DynamicMemoryUsage();
}

void GetCashMapEntry(const CCashMap& map, CAmount& value, char& flags)
{
    auto it = map.find(OUTPOINT);
    if (it == map.end()) {
        value = ABSENT;
        flags = NO_ENTRY;
    } else {
        if (it->second.cash.IsSpent()) {
            value = PRUNED;
        } else {
            value = it->second.cash.out.nValue;
        }
        flags = it->second.flags;
        assert(flags != NO_ENTRY);
    }
}

void WriteCashViewEntry(CCashView& view, CAmount value, char flags)
{
    CCashMap map;
    InsertCashMapEntry(map, value, flags);
    view.BatchWrite(map, {});
}

class SingleEntryCacheTest
{
public:
    SingleEntryCacheTest(CAmount base_value, CAmount cache_value, char cache_flags)
    {
        WriteCashViewEntry(base, base_value, base_value == ABSENT ? NO_ENTRY : DIRTY);
        cache.usage() += InsertCashsMapEntry(cache.map(), cache_value, cache_flags);
    }

    CCashView root;
    CCashViewCacheTest base{&root};
    CCashViewCacheTest cache{&base};
};

void CheckAccessCash(CAmount base_value, CAmount cache_value, CAmount expected_value, char cache_flags, char expected_flags)
{
    SingleEntryCacheTest test(base_value, cache_value, cache_flags);
    test.cache.AccessCash(OUTPOINT);
    test.cache.SelfTest();

    CAmount result_value;
    char result_flags;
    GetCoinsMapEntry(test.cache.map(), result_value, result_flags);
    BOOST_CHECK_EQUAL(result_value, expected_value);
    BOOST_CHECK_EQUAL(result_flags, expected_flags);
}

BOOST_AUTO_TEST_CASE(ccash_access)
{
    /* Check AccessCash behavior, requesting the cash from a cache view layered on
     * top of a base view, and checking the resulting entry in the cache after
     * the access.
     *
     *               Base    Cache   Result  Cache        Result
     *               Value   Value   Value   Flags        Flags
     */
    CheckAccessCash(ABSENT, ABSENT, ABSENT, NO_ENTRY   , NO_ENTRY   );
    CheckAccessCash(ABSENT, PRUNED, PRUNED, 0          , 0          );
    CheckAccessCash(ABSENT, PRUNED, PRUNED, FRESH      , FRESH      );
    CheckAccessCash(ABSENT, PRUNED, PRUNED, DIRTY      , DIRTY      );
    CheckAccessCash(ABSENT, PRUNED, PRUNED, DIRTY|FRESH, DIRTY|FRESH);
    CheckAccessCash(ABSENT, VALUE2, VALUE2, 0          , 0          );
    CheckAccessCash(ABSENT, VALUE2, VALUE2, FRESH      , FRESH      );
    CheckAccessCash(ABSENT, VALUE2, VALUE2, DIRTY      , DIRTY      );
    CheckAccessCash(ABSENT, VALUE2, VALUE2, DIRTY|FRESH, DIRTY|FRESH);
    CheckAccessCash(PRUNED, ABSENT, ABSENT, NO_ENTRY   , NO_ENTRY   );
    CheckAccessCash(PRUNED, PRUNED, PRUNED, 0          , 0          );
    CheckAccessCash(PRUNED, PRUNED, PRUNED, FRESH      , FRESH      );
    CheckAccessCash(PRUNED, PRUNED, PRUNED, DIRTY      , DIRTY      );
    CheckAccessCash(PRUNED, PRUNED, PRUNED, DIRTY|FRESH, DIRTY|FRESH);
    CheckAccessCash(PRUNED, VALUE2, VALUE2, 0          , 0          );
    CheckAccessCash(PRUNED, VALUE2, VALUE2, FRESH      , FRESH      );
    CheckAccessCash(PRUNED, VALUE2, VALUE2, DIRTY      , DIRTY      );
    CheckAccessCash(PRUNED, VALUE2, VALUE2, DIRTY|FRESH, DIRTY|FRESH);
    CheckAccessCash(VALUE1, ABSENT, VALUE1, NO_ENTRY   , 0          );
    CheckAccessCash(VALUE1, PRUNED, PRUNED, 0          , 0          );
    CheckAccessCash(VALUE1, PRUNED, PRUNED, FRESH      , FRESH      );
    CheckAccessCash(VALUE1, PRUNED, PRUNED, DIRTY      , DIRTY      );
    CheckAccessCash(VALUE1, PRUNED, PRUNED, DIRTY|FRESH, DIRTY|FRESH);
    CheckAccessCash(VALUE1, VALUE2, VALUE2, 0          , 0          );
    CheckAccessCash(VALUE1, VALUE2, VALUE2, FRESH      , FRESH      );
    CheckAccessCash(VALUE1, VALUE2, VALUE2, DIRTY      , DIRTY      );
    CheckAccessCash(VALUE1, VALUE2, VALUE2, DIRTY|FRESH, DIRTY|FRESH);
}

void CheckSpendCash(CAmount base_value, CAmount cache_value, CAmount expected_value, char cache_flags, char expected_flags)
{
    SingleEntryCacheTest test(base_value, cache_value, cache_flags);
    test.cache.SpendCash(OUTPOINT);
    test.cache.SelfTest();

    CAmount result_value;
    char result_flags;
    GetCashMapEntry(test.cache.map(), result_value, result_flags);
    BOOST_CHECK_EQUAL(result_value, expected_value);
    BOOST_CHECK_EQUAL(result_flags, expected_flags);
};

BOOST_AUTO_TEST_CASE(ccash_spend)
{
    /* Check SpendCash behavior, requesting the Cash from a cache view layered on
     * top of a base view, spending, and then checking
     * the resulting entry in the cache after the modification.
     *
     *              Base    Cache   Result  Cache        Result
     *              Value   Value   Value   Flags        Flags
     */
    CheckSpendCash(ABSENT, ABSENT, ABSENT, NO_ENTRY   , NO_ENTRY   );
    CheckSpendCash(ABSENT, PRUNED, PRUNED, 0          , DIRTY      );
    CheckSpendCash(ABSENT, PRUNED, ABSENT, FRESH      , NO_ENTRY   );
    CheckSpendCash(ABSENT, PRUNED, PRUNED, DIRTY      , DIRTY      );
    CheckSpendCash(ABSENT, PRUNED, ABSENT, DIRTY|FRESH, NO_ENTRY   );
    CheckSpendCash(ABSENT, VALUE2, PRUNED, 0          , DIRTY      );
    CheckSpendCash(ABSENT, VALUE2, ABSENT, FRESH      , NO_ENTRY   );
    CheckSpendCash(ABSENT, VALUE2, PRUNED, DIRTY      , DIRTY      );
    CheckSpendCash(ABSENT, VALUE2, ABSENT, DIRTY|FRESH, NO_ENTRY   );
    CheckSpendCash(PRUNED, ABSENT, ABSENT, NO_ENTRY   , NO_ENTRY   );
    CheckSpendCash(PRUNED, PRUNED, PRUNED, 0          , DIRTY      );
    CheckSpendCash(PRUNED, PRUNED, ABSENT, FRESH      , NO_ENTRY   );
    CheckSpendCash(PRUNED, PRUNED, PRUNED, DIRTY      , DIRTY      );
    CheckSpendCash(PRUNED, PRUNED, ABSENT, DIRTY|FRESH, NO_ENTRY   );
    CheckSpendCash(PRUNED, VALUE2, PRUNED, 0          , DIRTY      );
    CheckSpendCash(PRUNED, VALUE2, ABSENT, FRESH      , NO_ENTRY   );
    CheckSpendCash(PRUNED, VALUE2, PRUNED, DIRTY      , DIRTY      );
    CheckSpendCash(PRUNED, VALUE2, ABSENT, DIRTY|FRESH, NO_ENTRY   );
    CheckSpendCash(VALUE1, ABSENT, PRUNED, NO_ENTRY   , DIRTY      );
    CheckSpendCash(VALUE1, PRUNED, PRUNED, 0          , DIRTY      );
    CheckSpendCash(VALUE1, PRUNED, ABSENT, FRESH      , NO_ENTRY   );
    CheckSpendCash(VALUE1, PRUNED, PRUNED, DIRTY      , DIRTY      );
    CheckSpendCash(VALUE1, PRUNED, ABSENT, DIRTY|FRESH, NO_ENTRY   );
    CheckSpendCash(VALUE1, VALUE2, PRUNED, 0          , DIRTY      );
    CheckSpendCash(VALUE1, VALUE2, ABSENT, FRESH      , NO_ENTRY   );
    CheckSpendCash(VALUE1, VALUE2, PRUNED, DIRTY      , DIRTY      );
    CheckSpendCash(VALUE1, VALUE2, ABSENT, DIRTY|FRESH, NO_ENTRY   );
}

void CheckAddCashBase(CAmount base_value, CAmount cache_value, CAmount modify_value, CAmount expected_value, char cache_flags, char expected_flags, bool cashbase)
{
    SingleEntryCacheTest test(base_value, cache_value, cache_flags);

    CAmount result_value;
    char result_flags;
    try {
        CTxOut output;
        output.nValue = modify_value;
        test.cache.AddCash(OUTPOINT, Cash(std::move(output), 1, cashbase), cashbase);
        test.cache.SelfTest();
        GetCashMapEntry(test.cache.map(), result_value, result_flags);
    } catch (std::logic_error& e) {
        result_value = FAIL;
        result_flags = NO_ENTRY;
    }

    BOOST_CHECK_EQUAL(result_value, expected_value);
    BOOST_CHECK_EQUAL(result_flags, expected_flags);
}

// Simple wrapper for CheckAddCashBase function above that loops through
// different possible base_values, making sure each one gives the same results.
// This wrapper lets the cash_add test below be shorter and less repetitive,
// while still verifying that the CashViewCache::AddCash implementation
// ignores base values.
template <typename... Args>
void CheckAddCash(Args&&... args)
{
    for (CAmount base_value : {ABSENT, PRUNED, VALUE1})
        CheckAddCashBase(base_value, std::forward<Args>(args)...);
}

BOOST_AUTO_TEST_CASE(ccash_add)
{
    /* Check AddCash behavior, requesting a new cash from a cache view,
     * writing a modification to the cash, and then checking the resulting
     * entry in the cache after the modification. Verify behavior with the
     * with the AddCash potential_overwrite argument set to false, and to true.
     *
     *           Cache   Write   Result  Cache        Result       potential_overwrite
     *           Value   Value   Value   Flags        Flags
     */
    CheckAddCash(ABSENT, VALUE3, VALUE3, NO_ENTRY   , DIRTY|FRESH, false);
    CheckAddCash(ABSENT, VALUE3, VALUE3, NO_ENTRY   , DIRTY      , true );
    CheckAddCash(PRUNED, VALUE3, VALUE3, 0          , DIRTY|FRESH, false);
    CheckAddCash(PRUNED, VALUE3, VALUE3, 0          , DIRTY      , true );
    CheckAddCash(PRUNED, VALUE3, VALUE3, FRESH      , DIRTY|FRESH, false);
    CheckAddCash(PRUNED, VALUE3, VALUE3, FRESH      , DIRTY|FRESH, true );
    CheckAddCash(PRUNED, VALUE3, VALUE3, DIRTY      , DIRTY      , false);
    CheckAddCash(PRUNED, VALUE3, VALUE3, DIRTY      , DIRTY      , true );
    CheckAddCash(PRUNED, VALUE3, VALUE3, DIRTY|FRESH, DIRTY|FRESH, false);
    CheckAddCash(PRUNED, VALUE3, VALUE3, DIRTY|FRESH, DIRTY|FRESH, true );
    CheckAddCash(VALUE2, VALUE3, FAIL  , 0          , NO_ENTRY   , false);
    CheckAddCash(VALUE2, VALUE3, VALUE3, 0          , DIRTY      , true );
    CheckAddCash(VALUE2, VALUE3, FAIL  , FRESH      , NO_ENTRY   , false);
    CheckAddCash(VALUE2, VALUE3, VALUE3, FRESH      , DIRTY|FRESH, true );
    CheckAddCash(VALUE2, VALUE3, FAIL  , DIRTY      , NO_ENTRY   , false);
    CheckAddCash(VALUE2, VALUE3, VALUE3, DIRTY      , DIRTY      , true );
    CheckAddCash(VALUE2, VALUE3, FAIL  , DIRTY|FRESH, NO_ENTRY   , false);
    CheckAddCash(VALUE2, VALUE3, VALUE3, DIRTY|FRESH, DIRTY|FRESH, true );
}

void CheckWriteCash(CAmount parent_value, CAmount child_value, CAmount expected_value, char parent_flags, char child_flags, char expected_flags)
{
    SingleEntryCacheTest test(ABSENT, parent_value, parent_flags);

    CAmount result_value;
    char result_flags;
    try {
        WriteCashViewEntry(test.cache, child_value, child_flags);
        test.cache.SelfTest();
        GetCashMapEntry(test.cache.map(), result_value, result_flags);
    } catch (std::logic_error& e) {
        result_value = FAIL;
        result_flags = NO_ENTRY;
    }

    BOOST_CHECK_EQUAL(result_value, expected_value);
    BOOST_CHECK_EQUAL(result_flags, expected_flags);
}

BOOST_AUTO_TEST_CASE(ccash_write)
{
    /* Check BatchWrite behavior, flushing one entry from a child cache to a
     * parent cache, and checking the resulting entry in the parent cache
     * after the write.
     *
     *              Parent  Child   Result  Parent       Child        Result
     *              Value   Value   Value   Flags        Flags        Flags
     */
    CheckWriteCash(ABSENT, ABSENT, ABSENT, NO_ENTRY   , NO_ENTRY   , NO_ENTRY   );
    CheckWriteCash(ABSENT, PRUNED, PRUNED, NO_ENTRY   , DIRTY      , DIRTY      );
    CheckWriteCash(ABSENT, PRUNED, ABSENT, NO_ENTRY   , DIRTY|FRESH, NO_ENTRY   );
    CheckWriteCash(ABSENT, VALUE2, VALUE2, NO_ENTRY   , DIRTY      , DIRTY      );
    CheckWriteCash(ABSENT, VALUE2, VALUE2, NO_ENTRY   , DIRTY|FRESH, DIRTY|FRESH);
    CheckWriteCash(PRUNED, ABSENT, PRUNED, 0          , NO_ENTRY   , 0          );
    CheckWriteCash(PRUNED, ABSENT, PRUNED, FRESH      , NO_ENTRY   , FRESH      );
    CheckWriteCash(PRUNED, ABSENT, PRUNED, DIRTY      , NO_ENTRY   , DIRTY      );
    CheckWriteCash(PRUNED, ABSENT, PRUNED, DIRTY|FRESH, NO_ENTRY   , DIRTY|FRESH);
    CheckWriteCash(PRUNED, PRUNED, PRUNED, 0          , DIRTY      , DIRTY      );
    CheckWriteCash(PRUNED, PRUNED, PRUNED, 0          , DIRTY|FRESH, DIRTY      );
    CheckWriteCash(PRUNED, PRUNED, ABSENT, FRESH      , DIRTY      , NO_ENTRY   );
    CheckWriteCash(PRUNED, PRUNED, ABSENT, FRESH      , DIRTY|FRESH, NO_ENTRY   );
    CheckWriteCash(PRUNED, PRUNED, PRUNED, DIRTY      , DIRTY      , DIRTY      );
    CheckWriteCash(PRUNED, PRUNED, PRUNED, DIRTY      , DIRTY|FRESH, DIRTY      );
    CheckWriteCash(PRUNED, PRUNED, ABSENT, DIRTY|FRESH, DIRTY      , NO_ENTRY   );
    CheckWriteCash(PRUNED, PRUNED, ABSENT, DIRTY|FRESH, DIRTY|FRESH, NO_ENTRY   );
    CheckWriteCash(PRUNED, VALUE2, VALUE2, 0          , DIRTY      , DIRTY      );
    CheckWriteCash(PRUNED, VALUE2, VALUE2, 0          , DIRTY|FRESH, DIRTY      );
    CheckWriteCash(PRUNED, VALUE2, VALUE2, FRESH      , DIRTY      , DIRTY|FRESH);
    CheckWriteCash(PRUNED, VALUE2, VALUE2, FRESH      , DIRTY|FRESH, DIRTY|FRESH);
    CheckWriteCash(PRUNED, VALUE2, VALUE2, DIRTY      , DIRTY      , DIRTY      );
    CheckWriteCash(PRUNED, VALUE2, VALUE2, DIRTY      , DIRTY|FRESH, DIRTY      );
    CheckWriteCash(PRUNED, VALUE2, VALUE2, DIRTY|FRESH, DIRTY      , DIRTY|FRESH);
    CheckWriteCash(PRUNED, VALUE2, VALUE2, DIRTY|FRESH, DIRTY|FRESH, DIRTY|FRESH);
    CheckWriteCash(VALUE1, ABSENT, VALUE1, 0          , NO_ENTRY   , 0          );
    CheckWriteCash(VALUE1, ABSENT, VALUE1, FRESH      , NO_ENTRY   , FRESH      );
    CheckWriteCash(VALUE1, ABSENT, VALUE1, DIRTY      , NO_ENTRY   , DIRTY      );
    CheckWriteCash(VALUE1, ABSENT, VALUE1, DIRTY|FRESH, NO_ENTRY   , DIRTY|FRESH);
    CheckWriteCash(VALUE1, PRUNED, PRUNED, 0          , DIRTY      , DIRTY      );
    CheckWriteCash(VALUE1, PRUNED, FAIL  , 0          , DIRTY|FRESH, NO_ENTRY   );
    CheckWriteCash(VALUE1, PRUNED, ABSENT, FRESH      , DIRTY      , NO_ENTRY   );
    CheckWriteCash(VALUE1, PRUNED, FAIL  , FRESH      , DIRTY|FRESH, NO_ENTRY   );
    CheckWriteCash(VALUE1, PRUNED, PRUNED, DIRTY      , DIRTY      , DIRTY      );
    CheckWriteCash(VALUE1, PRUNED, FAIL  , DIRTY      , DIRTY|FRESH, NO_ENTRY   );
    CheckWriteCash(VALUE1, PRUNED, ABSENT, DIRTY|FRESH, DIRTY      , NO_ENTRY   );
    CheckWriteCash(VALUE1, PRUNED, FAIL  , DIRTY|FRESH, DIRTY|FRESH, NO_ENTRY   );
    CheckWriteCash(VALUE1, VALUE2, VALUE2, 0          , DIRTY      , DIRTY      );
    CheckWriteCash(VALUE1, VALUE2, FAIL  , 0          , DIRTY|FRESH, NO_ENTRY   );
    CheckWriteCash(VALUE1, VALUE2, VALUE2, FRESH      , DIRTY      , DIRTY|FRESH);
    CheckWriteCash(VALUE1, VALUE2, FAIL  , FRESH      , DIRTY|FRESH, NO_ENTRY   );
    CheckWriteCash(VALUE1, VALUE2, VALUE2, DIRTY      , DIRTY      , DIRTY      );
    CheckWriteCash(VALUE1, VALUE2, FAIL  , DIRTY      , DIRTY|FRESH, NO_ENTRY   );
    CheckWriteCash(VALUE1, VALUE2, VALUE2, DIRTY|FRESH, DIRTY      , DIRTY|FRESH);
    CheckWriteCash(VALUE1, VALUE2, FAIL  , DIRTY|FRESH, DIRTY|FRESH, NO_ENTRY   );

    // The checks above omit cases where the child flags are not DIRTY, since
    // they would be too repetitive (the parent cache is never updated in these
    // cases). The loop below covers these cases and makes sure the parent cache
    // is always left unchanged.
    for (CAmount parent_value : {ABSENT, PRUNED, VALUE1})
        for (CAmount child_value : {ABSENT, PRUNED, VALUE2})
            for (char parent_flags : parent_value == ABSENT ? ABSENT_FLAGS : FLAGS)
                for (char child_flags : child_value == ABSENT ? ABSENT_FLAGS : CLEAN_FLAGS)
                    CheckWriteCash(parent_value, child_value, parent_value, parent_flags, child_flags, parent_flags);
}

BOOST_AUTO_TEST_SUITE_END()
