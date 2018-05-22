// Copyright (c) 2018 The Salemcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SALEMCASH_QT_TEST_WALLETTESTS_H
#define SALEMCASH_QT_TEST_WALLETTESTS_H

#include <QObject>
#include <QTest>

class WalletTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void walletTests();
};

#endif // SALEMCASH_QT_TEST_WALLETTESTS_H
