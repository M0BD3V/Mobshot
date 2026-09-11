// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QImage>
#include <QString>

struct OcrResult
{
    QString text;
    QString language;
    QString error;

    bool succeeded() const { return error.isEmpty() && !text.trimmed().isEmpty(); }
};

class OcrService
{
public:
    // Uses the Windows on-device OCR language packs. No image or text leaves
    // the computer. Portuguese and English are attempted in that order.
    static OcrResult recognize(const QImage& image);
};
