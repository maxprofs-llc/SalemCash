// Copyright (c) 2018 The Salemcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SALEMCASH_QT_TEST_RPC_NESTED_TESTS_H
#define SALEMCASH_QT_TEST_RPC_NESTED_TESTS_H

#include <QObject>
#include <QTest>

#include <txdb.h>
#include <txmempool.h>

class RPCNestedTests : public QObject
{
    Q_OBJECT

    private Q_SLOTS:
    void rpcNestedTests();
};

#endif // SALEMCASH_QT_TEST_RPC_NESTED_TESTS_H
