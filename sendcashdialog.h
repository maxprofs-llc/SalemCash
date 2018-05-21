// Copyright (c) 2018 The Salemcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SALEMCASH_QT_SENDCASHDIALOG_H
#define SALEMCASH_QT_SENDCASHDIALOG_H

#include <qt/walletmodel.h>

#include <QDialog>
#include <QMessageBox>
#include <QString>
#include <QTimer>

class ClientModel;
class PlatformStyle;
class SendCashEntry;
class SendCashRecipient;

namespace Ui {
    class SendCashDialog;
}

QT_BEGIN_NAMESPACE
class QUrl;
QT_END_NAMESPACE

/** Dialog for sending salemcash */
class SendCashDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SendCashDialog(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~SendCashDialog();

    void setClientModel(ClientModel *clientModel);
    void setModel(WalletModel *model);

    /** Set up the tab chain manually, as Qt messes up the tab chain by default in some cases (issue https://bugreports.qt-project.org/browse/QTBUG-10907).
     */
    QWidget *setupTabChain(QWidget *prev);

    void setAddress(const QString &address);
    void pasteEntry(const SendCashRecipient &rv);
    bool handlePaymentRequest(const SendCashRecipient &recipient);

public Q_SLOTS:
    void clear();
    void reject();
    void accept();
    SendCashEntry *addEntry();
    void updateTabsAndLabels();
    void setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance,
                    const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance);

Q_SIGNALS:
    void CashSent(const uint256& txid);

private:
    Ui::SendCashDialog *ui;
    ClientModel *clientModel;
    WalletModel *model;
    bool fNewRecipientAllowed;
    bool fFeeMinimized;
    const PlatformStyle *platformStyle;

    // Process WalletModel::SendCashReturn and generate a pair consisting
    // of a message and message flags for use in Q_EMIT message().
    // Additional parameter msgArg can be used via .arg(msgArg).
    void processSendCashReturn(const WalletModel::SendCashReturn &sendCashReturn, const QString &msgArg = QString());
    void minimizeFeeSection(bool fMinimize);
    void updateFeeMinimizedLabel();
    // Update the passed in CCashControl with state from the GUI
    void updateCashControlState(CCashControl& ctrl);

private Q_SLOTS:
    void on_sendButton_clicked();
    void on_buttonChooseFee_clicked();
    void on_buttonMinimizeFee_clicked();
    void removeEntry(SendCashEntry* entry);
    void useAvailableBalance(SendCashEntry* entry);
    void updateDisplayUnit();
    void cashControlFeatureChanged(bool);
    void cashControlButtonClicked();
    void cashControlChangeChecked(int);
    void cashControlChangeEdited(const QString &);
    void cashControlUpdateLabels();
    void cashControlClipboardQuantity();
    void cashControlClipboardAmount();
    void cashControlClipboardFee();
    void cashControlClipboardAfterFee();
    void cashControlClipboardBytes();
    void cashControlClipboardLowOutput();
    void cashControlClipboardChange();
    void setMinimumFee();
    void updateFeeSectionControls();
    void updateMinFeeLabel();
    void updateSmartFeeLabel();

Q_SIGNALS:
    // Fired when a message should be reported to the user
    void message(const QString &title, const QString &message, unsigned int style);
};


#define SEND_CONFIRM_DELAY   3

class SendConfirmationDialog : public QMessageBox
{
    Q_OBJECT

public:
    SendConfirmationDialog(const QString &title, const QString &text, int secDelay = SEND_CONFIRM_DELAY, QWidget *parent = 0);
    int exec();

private Q_SLOTS:
    void countDown();
    void updateYesButton();

private:
    QAbstractButton *yesButton;
    QTimer countDownTimer;
    int secDelay;
};

#endif // SALEMCASH_QT_SENDCASHDIALOG_H
