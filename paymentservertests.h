// Copyright (c) 2018 The Salemcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SALEMCASH_QT_TEST_PAYMENTSERVERTESTS_H
#define SALEMCASH_QT_TEST_PAYMENTSERVERTESTS_H

#include <qt/paymentserver.h>

#include <QObject>
#include <QTest>

class PaymentServerTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void paymentServerTests();
};

// Dummy class to receive paymentserver signals.
// If SendCashRecipient was a proper QObject, then
// we could use QSignalSpy... but it's not.
class RecipientCatcher : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    void getRecipient(const SendCashRecipient& r);

public:
    SendCashRecipient recipient;
};

#endif // SALEMCASH_QT_TEST_PAYMENTSERVERTESTS_H
