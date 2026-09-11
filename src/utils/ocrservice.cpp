// SPDX-License-Identifier: GPL-3.0-or-later

#include "ocrservice.h"

#if defined(Q_OS_WIN) && defined(_MSC_VER)
#include <windows.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/base.h>

#include <cstring>

namespace {

struct __declspec(uuid("5B0D3235-4DBA-4D44-865D-BCF3ED1D74A4"))
  IMemoryBufferByteAccess : ::IUnknown
{
    virtual HRESULT __stdcall GetBuffer(BYTE** value, UINT32* capacity) = 0;
};

winrt::Windows::Graphics::Imaging::SoftwareBitmap toSoftwareBitmap(
  const QImage& source)
{
    using namespace winrt::Windows::Graphics::Imaging;
    const QImage image = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    SoftwareBitmap bitmap(BitmapPixelFormat::Bgra8,
                          image.width(),
                          image.height(),
                          BitmapAlphaMode::Premultiplied);
    auto buffer = bitmap.LockBuffer(BitmapBufferAccessMode::Write);
    const auto plane = buffer.GetPlaneDescription(0);
    auto reference = buffer.CreateReference();
    auto bytes = reference.as<IMemoryBufferByteAccess>();
    BYTE* destination = nullptr;
    UINT32 capacity = 0;
    winrt::check_hresult(bytes->GetBuffer(&destination, &capacity));
    destination += plane.StartIndex;
    for (int y = 0; y < image.height(); ++y) {
        std::memcpy(destination + y * plane.Stride,
                    image.constScanLine(y),
                    static_cast<size_t>(image.bytesPerLine()));
    }
    return bitmap;
}

} // namespace
#endif

OcrResult OcrService::recognize(const QImage& image)
{
    if (image.isNull()) {
        return { {}, {}, QStringLiteral("A área selecionada está vazia.") };
    }

#if defined(Q_OS_WIN) && defined(_MSC_VER)
    using winrt::Windows::Globalization::Language;
    using winrt::Windows::Media::Ocr::OcrEngine;
    try {
        try {
            winrt::init_apartment();
        } catch (const winrt::hresult_error& error) {
            if (error.code() != RPC_E_CHANGED_MODE) {
                throw;
            }
        }

        const auto bitmap = toSoftwareBitmap(image);
        for (const wchar_t* tag : { L"pt-BR", L"en-US" }) {
            auto engine = OcrEngine::TryCreateFromLanguage(Language(tag));
            if (!engine) {
                continue;
            }
            const auto result = engine.RecognizeAsync(bitmap).get();
            const QString text = QString::fromStdWString(result.Text().c_str()).trimmed();
            if (!text.isEmpty()) {
                return { text, QString::fromWCharArray(tag), {} };
            }
        }
        return { {}, {}, QStringLiteral(
                 "Nenhum texto foi encontrado. Verifique também se os pacotes "
                 "de OCR Português (Brasil) ou Inglês estão instalados no Windows.") };
    } catch (const winrt::hresult_error& error) {
        return { {}, {}, QStringLiteral("Falha no OCR local do Windows: %1")
                            .arg(QString::fromStdWString(error.message().c_str())) };
    }
#else
    Q_UNUSED(image)
    return { {}, {}, QStringLiteral(
             "O OCR local requer o build oficial do Mobshot para Windows (MSVC).") };
#endif
}
