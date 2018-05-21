// Copyright (c) 2018 The SalemCash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <wallet/wallet.h>
#include <wallet/cashselection.h>

#include <set>

static void addCash(const CAmount& nValue, const CWallet& wallet, std::vector<COutput>& vCash)
{
    int nInput = 0;

    static int nextLockTime = 0;
    CMutableTransaction tx;
    tx.nLockTime = nextLockTime++; // so all transactions get different hashes
    tx.vout.resize(nInput + 1);
    tx.vout[nInput].nValue = nValue;
    CWalletTx* wtx = new CWalletTx(&wallet, MakeTransactionRef(std::move(tx)));

    int nAge = 6 * 24;
    COutput output(wtx, nInput, nAge, true /* spendable */, true /* solvable */, true /* safe */);
    vCash.push_back(output);
}

// Simple benchmark for cash wallet selection. Note that it maybe be necessary
// to build up more complicated scenarios in order to get meaningful
// measurements of performance. From laanwj, "Cash wallet selection is probably
// the hardest, as you need a wider selection of scenarios, just testing the
// same one over and over isn't too useful. Generating random isn't useful
// either for measurements."
// see(https://github.com/PastorOmbura/SalemCash/issues/7883#issuecomment-224807484)
static void CashSelection(benchmark::State& state)
{
    const CWallet wallet("dummy", CWalletDBWrapper::CreateDummy());
    std::vector<COutput> vCash;
    LOCK(wallet.cs_wallet);

    while (state.KeepRunning()) {
        // Add cash.
        for (int i = 0; i < 1000; i++)
            addCash(1000 * CASH, wallet, vCash);
        addCash(3 * CASH, wallet, vCash);

        std::set<CInputCash> setCashRet;
        CAmount nValueRet;
        bool bnb_used;
        CashEligibilityFilter filter_standard(1, 6, 0);
        CashSelectionParams cash_selection_params(false, 34, 148, CFeeRate(0), 0);
        bool success = wallet.SelectCashMinConf(1003 * CASH, filter_standard, vCash, setCashRet, nValueRet, cash_selection_params, bnb_used)
                       || wallet.SelectCashMinConf(1003 * CASH, filter_standard, vCash, setCashRet, nValueRet, cash_selection_params, bnb_used);
        assert(success);
        assert(nValueRet == 1003 * CASH);
        assert(setCashRet.size() == 2);

        // Empty wallet.
        for (COutput& output : vCash) {
            delete output.tx;
        }
        vCash.clear();
    }
}

typedef std::set<CInputCash> CashSet;

// Copied from src/wallet/test/cashselector_tests.cpp
static void add_cash(const CAmount& nValue, int nInput, std::vector<CInputCash>& set)
{
    CMutableTransaction tx;
    tx.vout.resize(nInput + 1);
    tx.vout[nInput].nValue = nValue;
    set.emplace_back(MakeTransactionRef(tx), nInput);
}
// Copied from src/wallet/test/cashselector_tests.cpp
static CAmount make_hard_case(int utxos, std::vector<CInputCash>& utxo_pool)
{
    utxo_pool.clear();
    CAmount target = 0;
    for (int i = 0; i < utxos; ++i) {
        target += (CAmount)1 << (utxos+i);
        add_cash((CAmount)1 << (utxos+i), 2*i, utxo_pool);
        add_cash(((CAmount)1 << (utxos+i)) + ((CAmount)1 << (utxos-1-i)), 2*i + 1, utxo_pool);
    }
    return target;
}

static void BnBExhaustion(benchmark::State& state)
{
    // Setup
    std::vector<CInputCash> utxo_pool;
    CashSet selection;
    CAmount value_ret = 0;
    CAmount not_input_fees = 0;

    while (state.KeepRunning()) {
        // Benchmark
        CAmount target = make_hard_case(17, utxo_pool);
        SelectCashBnB(utxo_pool, target, 0, selection, value_ret, not_input_fees); // Should exhaust

        // Cleanup
        utxo_pool.clear();
        selection.clear();
    }
}

BENCHMARK(CashSelection, 650);
BENCHMARK(BnBExhaustion, 650);
