// Copyright (c) 2018 The SalemCash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SALEMCASH_CASH_H
#define SALEMCASH_CASH_H

#include <primitives/transaction.h>
#include <compressor.h>
#include <core_memusage.h>
#include <hash.h>
#include <memusage.h>
#include <serialize.h>
#include <uint256.h>

#include <assert.h>
#include <stdint.h>

#include <unordered_map>

/**
 * A UTXO entry.
 *
 * Serialized format:
 * - VARINT((cashbase ? 1 : 0) | (height << 1))
 * - the non-spent CTxOut (via CTxOutCompressor)
 */
class Cash
{
public:
    //! unspent transaction output
    CTxOut out;

    //! whether containing transaction of a cashbase
    unsigned int fCashBase : 1;

    //! at which height this containing transaction was included in the active block chain
    uint32_t nHeight : 31;

    //! construct Cash from a CTxOut and height/cashbase information.
    Cash(CTxOut&& outIn, int nHeightIn, bool fCashBaseIn) : out(std::move(outIn)), fCashBase(fCashBaseIn), nHeight(nHeightIn) {}
    Cash(const CTxOut& outIn, int nHeightIn, bool fCashBaseIn) : out(outIn), fCashBase(fCashBaseIn),nHeight(nHeightIn) {}

    void Clear() {
        out.SetNull();
        fCashBase = false;
        nHeight = 0;
    }

    //! empty constructor
    Cash() : fCashBase(false), nHeight(0) { }

    bool IsCashBase() const {
        return fCashBase;
    }

    template<typename Stream>
    void Serialize(Stream &s) const {
        assert(!IsSpent());
        uint32_t code = nHeight * 2 + fCashBase;
        ::Serialize(s, VARINT(code));
        ::Serialize(s, CTxOutCompressor(REF(out)));
    }

    template<typename Stream>
    void Unserialize(Stream &s) {
        uint32_t code = 0;
        ::Unserialize(s, VARINT(code));
        nHeight = code >> 1;
        fCashBase = code & 1;
        ::Unserialize(s, CTxOutCompressor(out));
    }

    bool IsSpent() const {
        return out.IsNull();
    }

    size_t DynamicMemoryUsage() const {
        return memusage::DynamicUsage(out.scriptPubKey);
    }
};

class SaltedOutpointHasher
{
private:
    /** Salt */
    const uint64_t k0, k1;

public:
    SaltedOutpointHasher();

    /**
     * This *must* return size_t. With Boost 1.46 on 32-bit systems the
     * unordered_map will behave unpredictably if the custom hasher returns a
     * uint64_t, resulting in failures when syncing the chain (#4634).
     */
    size_t operator()(const COutPoint& id) const {
        return SipHashUint256Extra(k0, k1, id.hash, id.n);
    }
};

struct CCashCacheEntry
{
    Cash cash; // The actual cached data.
    unsigned char flags;

    enum Flags {
        DIRTY = (1 << 0), // This cache entry is potentially different from the version in the parent view.
        FRESH = (1 << 1), // The parent view does not have this entry (or it is pruned).
        /* Note that FRESH is a performance optimization with which we can
         * erase the cash that are fully spent if we know we do not need to
         * flush the changes to the parent cache.  It is always safe to
         * not mark FRESH if that condition is not guaranteed.
         */
    };

    CCashCacheEntry() : flags(0) {}
    explicit CCashCacheEntry(Cash&& cash_) : cash(std::move(cash_)), flags(0) {}
};

typedef std::unordered_map<COutPoint, CCashCacheEntry, SaltedOutpointHasher> CCashMap;

/** Cursor for iterating over CashView state */
class CCashViewCursor
{
public:
    CCashViewCursor(const uint256 &hashBlockIn): hashBlock(hashBlockIn) {}
    virtual ~CCashViewCursor() {}

    virtual bool GetKey(COutPoint &key) const = 0;
    virtual bool GetValue(Cash &cash) const = 0;
    virtual unsigned int GetValueSize() const = 0;

    virtual bool Valid() const = 0;
    virtual void Next() = 0;

    //! Get best block at the time this cursor was created
    const uint256 &GetBestBlock() const { return hashBlock; }
private:
    uint256 hashBlock;
};

/** Abstract view on the open txout dataset. */
class CCashView
{
public:
    /** Retrieve the Cash (unspent transaction output) for a given outpoint.
     *  Returns true only when an unspent cash was found, which is returned in cash.
     *  When false is returned, cash's value is unspecified.
     */
    virtual bool GetCash(const COutPoint &outpoint, Cash &cash) const;

    //! Just check whether a given outpoint is unspent.
    virtual bool HaveCash(const COutPoint &outpoint) const;

    //! Retrieve the block hash whose state this CCashView currently represents
    virtual uint256 GetBestBlock() const;

    //! Retrieve the range of blocks that may have been only partially written.
    //! If the database is in a consistent state, the result is the empty vector.
    //! Otherwise, a two-element vector is returned consisting of the new and
    //! the old block hash, in that order.
    virtual std::vector<uint256> GetHeadBlocks() const;

    //! Do a bulk modification (multiple Cash changes + BestBlock change).
    //! The passed mapCash can be modified.
    virtual bool BatchWrite(CCashMap &mapCash, const uint256 &hashBlock);

    //! Get a cursor to iterate over the whole state
    virtual CCashViewCursor *Cursor() const;

    //! As we use CCashViews polymorphically, have a virtual destructor
    virtual ~CCashView() {}

    //! Estimate database size (0 if not implemented)
    virtual size_t EstimateSize() const { return 0; }
};

/** CCashView backed by another CCashView */
class CCashViewBacked : public CCashView
{
protected:
    CCashView *base;

public:
    CCashViewBacked(CCashView *viewIn);
    bool GetCash(const COutPoint &outpoint, Cash &cash) const override;
    bool HaveCash(const COutPoint &outpoint) const override;
    uint256 GetBestBlock() const override;
    std::vector<uint256> GetHeadBlocks() const override;
    void SetBackend(CashView &viewIn);
    bool BatchWrite(CCashMap &mapCash, const uint256 &hashBlock) override;
    CCashViewCursor *Cursor() const override;
    size_t EstimateSize() const override;
};

/** CCashView that adds a memory cache for transactions to another CCashView */
class CCashViewCache : public CCashViewBacked
{
protected:
    /**
     * Make mutable so that we can "fill the cache" even from Get-methods
     * declared as "const".  
     */
    mutable uint256 hashBlock;
    mutable CCashMap cacheCash;

    /* Cached dynamic memory usage for the inner Cash objects. */
    mutable size_t cachedCashUsage;

public:
    CCashViewCache(CCashView *baseIn);

    /**
     * By deleting the copy constructor, we prevent accidentally using it when one intends to create a cache on top of a base cache.
     */
    CCashViewCache(const CCashViewCache &) = delete;

    // Standard CCashView methods
    bool GetCash(const COutPoint &outpoint, Cash &cash) const override;
    bool HaveCash(const COutPoint &outpoint) const override;
    uint256 GetBestBlock() const override;
    void SetBestBlock(const uint256 &hashBlock);
    bool BatchWrite(CCashMap &mapCash, const uint256 &hashBlock) override;
    CCashViewCursor* Cursor() const override {
        throw std::logic_error("CCashViewCache cursor iteration not supported.");
    }

    /**
     * Check if we have the given utxo already loaded in this cache.
     * The semantics are the same as HaveCash(), but no calls to
     * the backing CCashView are made.
     */
    bool HaveCashInCache(const COutPoint &outpoint) const;

    /**
     * Return a reference to Cash in the cache, or a pruned one if not found. This is
     * more efficient than GetCash.
     *
     * Generally, do not hold the reference returned for more than a short scope.
     * While the current implementation allows for modifications to the contents
     * of the cache while holding the reference, this behavior should not be relied
     * on! To be safe, best to not hold the returned reference through any other
     * calls to this cache.
     */
    const Cash& AccessCash(const COutPoint &output) const;

    /**
     * Add Cash. Set potential_overwrite to true if a non-pruned version may
     * already exist.
     */
    void AddCash(const COutPoint& outpoint, Cash&& cash, bool potential_overwrite);

    /**
     * Spend Cash. Pass moveto in order to get the deleted data.
     * If no unspent output exists for the passed outpoint, this call
     * has no effect.
     */
    bool SpendCash(const COutPoint &outpoint, Cash* moveto = nullptr);

    /**
     * Push the modifications applied to this cache to its base.
     * Failure to call this method before destruction will cause the changes to be forgotten.
     * If false is returned, the state of this cache (and its backing view) will be undefined.
     */
    bool Flush();

    /**
     * Removes the UTXO with the given outpoint from the cache, if it is
     * not modified.
     */
    void Uncache(const COutPoint &outpoint);

    //! Calculate the size of the cache (in number of transaction outputs)
    unsigned int GetCacheSize() const;

    //! Calculate the size of the cache (in bytes)
    size_t DynamicMemoryUsage() const;

    /** 
     * Amount of SalemCash coming in to a transaction
     * Note that lightweight clients may not know anything besides the hash of previous transactions,
     * so may not be able to calculate this.
     *
     * @param[in] tx	transaction for which we are checking input total
     * @return	Sum of value of all inputs (scriptSigs)
     */
    CAmount GetValueIn(const CTransaction& tx) const;

    //! Check whether all prevouts of the transaction are present in the UTXO set represented by this view
    bool HaveInputs(const CTransaction& tx) const;

private:
    CCashMap::iterator FetchCash(const COutPoint &outpoint) const;
};

//! Utility function to add all of a transaction's outputs to a cache.
// When check is false, this assumes that overwrites are only possible for cashbase transactions.
// When check is true, the underlying view may be queried to determine whether an addition is
// an overwrite.
// TODO: pass in a boolean to limit these possible overwrites to known
// (pre-BIP34) cases.
void AddCash(CCashViewCache& cache, const CTransaction& tx, int nHeight, bool check = false);

//! Utility function to find any unspent output with a given txid.
// This function can be quite expensive because in the event of a transaction
// which is not found in the cache, it can cause up to MAX_OUTPUTS_PER_BLOCK
// lookups to database, so it should be used with care.
const Cash& AccessByTxid(const CCashViewCache& cache, const uint256& txid);

#endif // SALEMCASH_CASH_H
