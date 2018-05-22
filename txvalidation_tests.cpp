// Copyright (c) 2018 The SalemCash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation.h>
#include <txmempool.h>
#include <amount.h>
#include <consensus/validation.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <test/test_salemcash.h>

#include <boost/test/unit_test.hpp>


BOOST_AUTO_TEST_SUITE(txvalidation_tests)

/**
 * Ensure that the mempool won't accept cashbase transactions.
 */
BOOST_FIXTURE_TEST_CASE(tx_mempool_reject_cashbase, TestChain100Setup)
{
    CScript scriptPubKey = CScript() << ToByteVector(cashbaseKey.GetPubKey()) << OP_CHECKSIG;
    CMutableTransaction cashbaseTx;

    cashbaseTx.nVersion = 1;
    cashbaseTx.vin.resize(1);
    cashbaseTx.vout.resize(1);
    cashbaseTx.vin[0].scriptSig = CScript() << OP_11 << OP_EQUAL;
    cashbaseTx.vout[0].nValue = 1 * CENT;
    cashbaseTx.vout[0].scriptPubKey = scriptPubKey;

    assert(CTransaction(cashbaseTx).IsCashBase());

    CValidationState state;

    LOCK(cs_main);

    unsigned int initialPoolSize = mempool.size();

    BOOST_CHECK_EQUAL(
            false,
            AcceptToMemoryPool(mempool, state, MakeTransactionRef(cashbaseTx),
                nullptr /* pfMissingInputs */,
                nullptr /* plTxnReplaced */,
                true /* bypass_limits */,
                0 /* nAbsurdFee */));

    // Check that the transaction hasn't been added to mempool.
    BOOST_CHECK_EQUAL(mempool.size(), initialPoolSize);

    // Check that the validation state reflects the unsuccessful attempt.
    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "cashbase");

    int nDoS;
    BOOST_CHECK_EQUAL(state.IsInvalid(nDoS), true);
    BOOST_CHECK_EQUAL(nDoS, 100);
}

BOOST_AUTO_TEST_SUITE_END()
