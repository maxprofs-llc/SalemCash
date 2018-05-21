// Copyright (c) 2018 The Salemcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SALEMCASH_QT_SALEMCASHADDRESSVALIDATOR_H
#define SALEMCASH_QT_SALEMCASHADDRESSVALIDATOR_H

#include <QValidator>

/** Base58 entry widget validator, checks for valid characters and
 * removes some whitespace.
 */
class SalemcashAddressEntryValidator : public QValidator
{
    Q_OBJECT

public:
    explicit SalemcashAddressEntryValidator(QObject *parent);

    State validate(QString &input, int &pos) const;
};

/** Salemcash address widget validator, checks for a valid Salemcash address.
 */
class SalemcashAddressCheckValidator : public QValidator
{
    Q_OBJECT

public:
    explicit SalemcashAddressCheckValidator(QObject *parent);

    State validate(QString &input, int &pos) const;
};

#endif // SALEMCASH_QT_SALEMCASHADDRESSVALIDATOR_H
