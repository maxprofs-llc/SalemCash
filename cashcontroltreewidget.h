// Copyright (c) 2018 The Salemcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SALEMCASH_QT_CASHCONTROLTREEWIDGET_H
#define SALEMCASH_QT_CASHCONTROLTREEWIDGET_H

#include <QKeyEvent>
#include <QTreeWidget>

class CashControlTreeWidget : public QTreeWidget
{
    Q_OBJECT

public:
    explicit CashControlTreeWidget(QWidget *parent = 0);

protected:
    virtual void keyPressEvent(QKeyEvent *event);
};

#endif // SALEMCASH_QT_CASHCONTROLTREEWIDGET_H
