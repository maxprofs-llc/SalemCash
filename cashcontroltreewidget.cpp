// Copyright (c) 2018 The Salemcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/cashcontroltreewidget.h>
#include <qt/cashcontroldialog.h>

CashControlTreeWidget::CashControlTreeWidget(QWidget *parent) :
    QTreeWidget(parent)
{

}

void CashControlTreeWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space) // press spacebar -> select checkbox
    {
        event->ignore();
        if (this->currentItem()) {
            int COLUMN_CHECKBOX = 0;
            this->currentItem()->setCheckState(COLUMN_CHECKBOX, ((this->currentItem()->checkState(COLUMN_CHECKBOX) == Qt::Checked) ? Qt::Unchecked : Qt::Checked));
        }
    }
    else if (event->key() == Qt::Key_Escape) // press esc -> close dialog
    {
        event->ignore();
        CashControlDialog *cashControlDialog = static_cast<CashControlDialog*>(this->parentWidget());
        cashControlDialog->done(QDialog::Accepted);
    }
    else
    {
        this->QTreeWidget::keyPressEvent(event);
    }
}
