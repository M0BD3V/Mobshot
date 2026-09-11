// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>

class PrivacyDetector
{
public:
    enum class Kind
    {
        Email,
        Phone,
        Cpf,
        CreditCard,
        Url,
        Secret
    };

    struct Match
    {
        Kind kind;
        int start;
        int length;
        QString value;
        bool solidRedactionRecommended;
    };

    static QList<Match> scan(const QString& text);
    static QString label(Kind kind);

private:
    static bool isValidCpf(const QString& candidate);
    static bool passesLuhn(const QString& candidate);
};
