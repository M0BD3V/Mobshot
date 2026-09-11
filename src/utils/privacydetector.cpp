// SPDX-License-Identifier: GPL-3.0-or-later

#include "privacydetector.h"

#include <QRegularExpression>
#include <algorithm>
#include <functional>

namespace {

void appendMatches(QList<PrivacyDetector::Match>& output,
                   const QString& text,
                   PrivacyDetector::Kind kind,
                   const QRegularExpression& expression,
                   bool solidRedactionRecommended,
                   const std::function<bool(const QString&)>& validator = {})
{
    auto iterator = expression.globalMatch(text);
    while (iterator.hasNext()) {
        const auto match = iterator.next();
        const QString value = match.captured(0);
        if (validator && !validator(value)) {
            continue;
        }
        output.append({ kind,
                        static_cast<int>(match.capturedStart(0)),
                        static_cast<int>(match.capturedLength(0)),
                        value,
                        solidRedactionRecommended });
    }
}

QString digitsOnly(QString value)
{
    value.remove(QRegularExpression(QStringLiteral("\\D")));
    return value;
}

} // namespace

QList<PrivacyDetector::Match> PrivacyDetector::scan(const QString& text)
{
    QList<Match> matches;
    appendMatches(matches,
                  text,
                  Kind::Email,
                  QRegularExpression(QStringLiteral(
                    R"(\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}\b)"),
                                     QRegularExpression::CaseInsensitiveOption),
                  false);
    appendMatches(matches,
                  text,
                  Kind::Phone,
                  QRegularExpression(QStringLiteral(
                    R"((?<!\d)(?:\+?55\s*)?(?:\(?\d{2}\)?\s*)?9?\d{4}[-\s]?\d{4}(?!\d))")),
                  false);
    appendMatches(matches,
                  text,
                  Kind::Cpf,
                  QRegularExpression(QStringLiteral(
                    R"((?<!\d)\d{3}\.?\d{3}\.?\d{3}-?\d{2}(?!\d))")),
                  false,
                  [](const QString& value) { return isValidCpf(value); });
    appendMatches(matches,
                  text,
                  Kind::CreditCard,
                  QRegularExpression(QStringLiteral(
                    R"((?<!\d)(?:\d[ -]?){13,19}(?!\d))")),
                  true,
                  [](const QString& value) { return passesLuhn(value); });
    appendMatches(matches,
                  text,
                  Kind::Url,
                  QRegularExpression(QStringLiteral(R"(https?://[^\s<>\"]+)"),
                                     QRegularExpression::CaseInsensitiveOption),
                  false);
    appendMatches(matches,
                  text,
                  Kind::Secret,
                  QRegularExpression(QStringLiteral(
                    R"(\b(?:sk-[A-Za-z0-9_-]{16,}|gh[pousr]_[A-Za-z0-9]{20,}|AKIA[A-Z0-9]{16}|(?:token|password|senha|secret)\s*[:=]\s*[^\s,;]{6,})\b)"),
                                     QRegularExpression::CaseInsensitiveOption),
                  true);

    std::sort(matches.begin(), matches.end(), [](const Match& left, const Match& right) {
        if (left.start != right.start) {
            return left.start < right.start;
        }
        return left.length > right.length;
    });

    QList<Match> deduplicated;
    for (const Match& match : matches) {
        const bool overlaps = std::any_of(
          deduplicated.cbegin(), deduplicated.cend(), [&match](const Match& existing) {
              return match.start < existing.start + existing.length &&
                     existing.start < match.start + match.length;
          });
        if (!overlaps) {
            deduplicated.append(match);
        }
    }
    return deduplicated;
}

QString PrivacyDetector::label(Kind kind)
{
    switch (kind) {
        case Kind::Email:
            return QStringLiteral("E-mail");
        case Kind::Phone:
            return QStringLiteral("Telefone");
        case Kind::Cpf:
            return QStringLiteral("CPF");
        case Kind::CreditCard:
            return QStringLiteral("Cartão");
        case Kind::Url:
            return QStringLiteral("Link");
        case Kind::Secret:
            return QStringLiteral("Senha ou token");
    }
    return QStringLiteral("Dado sensível");
}

bool PrivacyDetector::isValidCpf(const QString& candidate)
{
    const QString digits = digitsOnly(candidate);
    if (digits.size() != 11 ||
        std::all_of(digits.cbegin(), digits.cend(), [&digits](QChar c) {
            return c == digits.front();
        })) {
        return false;
    }

    for (int digitIndex = 9; digitIndex < 11; ++digitIndex) {
        int sum = 0;
        for (int i = 0; i < digitIndex; ++i) {
            sum += digits[i].digitValue() * (digitIndex + 1 - i);
        }
        int expected = 11 - (sum % 11);
        if (expected >= 10) {
            expected = 0;
        }
        if (digits[digitIndex].digitValue() != expected) {
            return false;
        }
    }
    return true;
}

bool PrivacyDetector::passesLuhn(const QString& candidate)
{
    const QString digits = digitsOnly(candidate);
    if (digits.size() < 13 || digits.size() > 19) {
        return false;
    }
    int sum = 0;
    bool doubleDigit = false;
    for (int i = digits.size() - 1; i >= 0; --i) {
        int value = digits[i].digitValue();
        if (doubleDigit) {
            value *= 2;
            if (value > 9) {
                value -= 9;
            }
        }
        sum += value;
        doubleDigit = !doubleDigit;
    }
    return sum % 10 == 0;
}
