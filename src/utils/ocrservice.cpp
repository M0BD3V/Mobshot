// SPDX-License-Identifier: GPL-3.0-or-later

#include "ocrservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryFile>

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
    const QString bundledRuntime =
      QCoreApplication::applicationDirPath() + QStringLiteral("/EasyOCR");
    const QString userRuntime =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
      QStringLiteral("/EasyOCR");
    const QString runtimeRoot = QFileInfo::exists(
                                  bundledRuntime + QStringLiteral("/python.exe"))
      ? bundledRuntime
      : userRuntime;
    QString python = qEnvironmentVariable("MOBSHOT_EASYOCR_PYTHON");
    if (python.isEmpty()) {
        const QString bundled = runtimeRoot + QStringLiteral("/python.exe");
        python = QFileInfo::exists(bundled)
          ? bundled
          : QStandardPaths::findExecutable(QStringLiteral("python"));
    }
    if (python.isEmpty()) {
        return { {}, {}, QStringLiteral(
                 "EasyOCR não está instalado. Reinstale o Mobshot com o componente OCR local.") };
    }

    QTemporaryFile input(QDir::tempPath() + QStringLiteral("/mobshot-ocr-XXXXXX.png"));
    input.setAutoRemove(true);
    if (!input.open()) {
        return { {}, {}, QStringLiteral("Não foi possível preparar a imagem para o OCR.") };
    }
    const QString imagePath = input.fileName();
    input.close();
    if (!image.save(imagePath, "PNG")) {
        return { {}, {}, QStringLiteral("Não foi possível converter a seleção para OCR.") };
    }

    const QString script = QStringLiteral(
      "import easyocr,sys,warnings\n"
      "warnings.filterwarnings('ignore')\n"
      "reader=easyocr.Reader(['pt','en'],gpu=False,verbose=False)\n"
      "items=reader.readtext(sys.argv[1],detail=1,paragraph=False,decoder='greedy',canvas_size=1920,mag_ratio=1.0)\n"
      "items.sort(key=lambda r:(sum(p[1] for p in r[0])/4,sum(p[0] for p in r[0])/4))\n"
      "print('\\n'.join(r[1] for r in items))\n");
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("EASYOCR_MODULE_PATH"),
                       runtimeRoot + QStringLiteral("/models"));
    environment.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    process.setProcessEnvironment(environment);
    process.start(python, { QStringLiteral("-c"), script, imagePath });
    if (!process.waitForFinished(300000)) {
        process.kill();
        return { {}, {}, QStringLiteral("O EasyOCR excedeu o tempo de análise.") };
    }
    const QString text = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (process.exitCode() != 0) {
        const QString error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        return { {}, {}, QStringLiteral("Falha no EasyOCR: %1").arg(error) };
    }
    if (text.isEmpty()) {
        return { {}, {}, QStringLiteral("Nenhum texto foi encontrado na área selecionada.") };
    }
    return { text, QStringLiteral("pt+en (EasyOCR)"), {} };
#endif
}
