// Copyright (c) 2018 The Salemcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SALEMCASH_QT_WALLETMODELTRANSACTION_H
#define SALEMCASH_QT_WALLETMODELTRANSACTION_H

#include <qt/walletmodel.h>

#include <QObject>

class SendCashRecipient;

class CReserveKey;
class CWallet;
class CWalletTx;

/** Data model for a walletmodel transaction. */
class WalletModelTransaction
{
public:
    explicit WalletModelTransaction(const QList<SendCashRecipient> &recipients);

    QList<SendCashRecipient> getRecipients() const;

    CTransactionRef& getTransaction();
    unsigned int getTransactionSize();

    void setTransactionFee(const CAmount& newFee);
    CAmount getTransactionFee() const;

    CAmount getTotalTransactionAmount() const;

    void newPossibleKeyChange(CWallet *wallet);
    CReserveKey *getPossibleKeyChange();

    void reassignAmounts(int nChangePosRet); // needed for the subtract-fee-from-amount feature

private:
    QList<SendCashRecipient> recipients;
    CTransactionRef walletTransaction;
    std::unique_ptr<CReserveKey> keyChange;
    CAmount fee;
};

#endif // SALEMCASH_QT_WALLETMODELTRANSACTION_H
