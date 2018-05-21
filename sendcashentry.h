// Copyright (c) 2018 The Salemcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SALEMCASH_QT_SENDCASHENTRY_H
#define SALEMCASH_QT_SENDCASHENTRY_H

#include <qt/walletmodel.h>

#include <QStackedWidget>

class WalletModel;
class PlatformStyle;

namespace Ui {
    class SendCashEntry;
}

/**
 * A single entry in the dialog for sending salemcash.
 * Stacked widget, with different UIs for payment requests
 * with a strong payee identity.
 */
class SendCashEntry : public QStackedWidget
{
    Q_OBJECT

public:
    explicit SendCashEntry(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~SendCashEntry();

    void setModel(WalletModel *model);
    bool validate();
    SendCashRecipient getValue();

    /** Return whether the entry is still empty and unedited */
    bool isClear();

    void setValue(const SendCashRecipient &value);
    void setAddress(const QString &address);
    void setAmount(const CAmount &amount);

    /** Set up the tab chain manually, as Qt messes up the tab chain by default in some cases
     *  (issue https://bugreports.qt-project.org/browse/QTBUG-10907).
     */
    QWidget *setupTabChain(QWidget *prev);

    void setFocus();

public Q_SLOTS:
    void clear();
    void checkSubtractFeeFromAmount();

Q_SIGNALS:
    void removeEntry(SendCashEntry *entry);
    void useAvailableBalance(SendCashEntry* entry);
    void payAmountChanged();
    void subtractFeeFromAmountChanged();

private Q_SLOTS:
    void deleteClicked();
    void useAvailableBalanceClicked();
    void on_payTo_textChanged(const QString &address);
    void on_addressBookButton_clicked();
    void on_pasteButton_clicked();
    void updateDisplayUnit();

private:
    SendCashRecipient recipient;
    Ui::SendCashEntry *ui;
    WalletModel *model;
    const PlatformStyle *platformStyle;

    bool updateLabel(const QString &address);
};

#endif // SALEMCASH_QT_SENDCASHENTRY_H
