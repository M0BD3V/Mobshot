// SPDX-License-Identifier: GPL-3.0-or-later

#include "utils/privacydetector.h"

#include <QCoreApplication>
#include <iostream>

namespace {

bool containsKind(const QList<PrivacyDetector::Match>& matches,
                  PrivacyDetector::Kind kind)
{
    for (const auto& match : matches) {
        if (match.kind == kind) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto matches = PrivacyDetector::scan(QStringLiteral(
      "Contato joao@example.com, CPF 529.982.247-25, cartão "
      "4111 1111 1111 1111 e token=segredo-super-longo."));

    const bool passed =
      containsKind(matches, PrivacyDetector::Kind::Email) &&
      containsKind(matches, PrivacyDetector::Kind::Cpf) &&
      containsKind(matches, PrivacyDetector::Kind::CreditCard) &&
      containsKind(matches, PrivacyDetector::Kind::Secret) &&
      !containsKind(PrivacyDetector::scan(QStringLiteral("CPF 111.111.111-11")),
                    PrivacyDetector::Kind::Cpf);
    if (!passed) {
        std::cerr << "privacy detector regression" << std::endl;
        return 1;
    }
    return 0;
}
