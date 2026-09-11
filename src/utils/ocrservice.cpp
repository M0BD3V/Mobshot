// SPDX-License-Identifier: GPL-3.0-or-later

#include "ocrservice.h"

#include <QCoreApplication>
#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTemporaryFile>
#include <QThread>
#include <QtEndian>

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

#if !defined(Q_OS_WIN) || !defined(_MSC_VER)
namespace {
constexpr quint16 OcrWorkerPort = 47631;

QString workerScript()
{
    return QStringLiteral(
      "import io,socket,struct,sys,warnings\n"
      "warnings.filterwarnings('ignore')\n"
      "import easyocr,numpy as np\n"
      "from PIL import Image\n"
      "reader=easyocr.Reader(['pt','en'],gpu=False,verbose=False)\n"
      "def receive(c,n):\n"
      " data=b''\n"
      " while len(data)<n:\n"
      "  part=c.recv(n-len(data))\n"
      "  if not part: raise ConnectionError()\n"
      "  data+=part\n"
      " return data\n"
      "server=socket.socket();server.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)\n"
      "server.bind(('127.0.0.1',int(sys.argv[1])));server.listen(2)\n"
      "while True:\n"
      " c,_=server.accept()\n"
      " try:\n"
      "  size=struct.unpack('!I',receive(c,4))[0]\n"
      "  image=np.asarray(Image.open(io.BytesIO(receive(c,size))).convert('RGB'))\n"
      "  items=reader.readtext(image,detail=1,paragraph=False,decoder='greedy',canvas_size=1920,mag_ratio=1.0)\n"
      "  items.sort(key=lambda r:(sum(p[1] for p in r[0])/4,sum(p[0] for p in r[0])/4))\n"
      "  payload=('OK\\n'+'\\n'.join(r[1] for r in items)).encode('utf-8')\n"
      " except Exception as e: payload=('ERR\\n'+str(e)).encode('utf-8')\n"
      " try:c.sendall(struct.pack('!I',len(payload))+payload)\n"
      " finally:c.close()\n");
}

bool connectToWorker(QTcpSocket& socket, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    do {
        socket.abort();
        socket.connectToHost(QHostAddress::LocalHost, OcrWorkerPort);
        if (socket.waitForConnected(500)) {
            return true;
        }
        QThread::msleep(150);
    } while (timer.elapsed() < timeoutMs);
    return false;
}

QByteArray readBytes(QTcpSocket& socket, qsizetype count, int timeoutMs)
{
    QByteArray result;
    while (result.size() < count) {
        if (socket.bytesAvailable() == 0 && !socket.waitForReadyRead(timeoutMs)) {
            return {};
        }
        result += socket.read(count - result.size());
    }
    return result;
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

    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
        return { {}, {}, QStringLiteral("Não foi possível converter a seleção para OCR.") };
    }

    QTcpSocket socket;
    if (!connectToWorker(socket, 300)) {
        const QString scriptPath = QStandardPaths::writableLocation(
                                     QStandardPaths::TempLocation) +
                                   QStringLiteral("/mobshot-ocr-worker.py");
        QFile scriptFile(scriptPath);
        if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
            scriptFile.write(workerScript().toUtf8()) < 0) {
            return { {}, {}, QStringLiteral("Não foi possível iniciar o motor de OCR.") };
        }
        scriptFile.close();

        QProcess launcher;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("EASYOCR_MODULE_PATH"),
                           runtimeRoot + QStringLiteral("/models"));
        environment.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
        launcher.setProcessEnvironment(environment);
        launcher.setProgram(python);
        launcher.setArguments({ QStringLiteral("-u"),
                                scriptPath,
                                QString::number(OcrWorkerPort) });
        if (!launcher.startDetached() || !connectToWorker(socket, 180000)) {
            return { {}, {}, QStringLiteral("O motor EasyOCR não conseguiu iniciar.") };
        }
    }

    QByteArray header(4, Qt::Uninitialized);
    qToBigEndian<quint32>(static_cast<quint32>(png.size()), header.data());
    if (socket.write(header) != header.size() || socket.write(png) != png.size() ||
        !socket.waitForBytesWritten(30000)) {
        return { {}, {}, QStringLiteral("Falha ao enviar a imagem ao EasyOCR.") };
    }
    const QByteArray responseHeader = readBytes(socket, 4, 300000);
    if (responseHeader.size() != 4) {
        return { {}, {}, QStringLiteral("O EasyOCR excedeu o tempo de análise.") };
    }
    const quint32 responseSize = qFromBigEndian<quint32>(responseHeader.constData());
    const QByteArray response = readBytes(socket, responseSize, 300000);
    if (response.size() != responseSize) {
        return { {}, {}, QStringLiteral("A resposta do EasyOCR foi interrompida.") };
    }
    if (response.startsWith("ERR\n")) {
        return { {}, {}, QStringLiteral("Falha no EasyOCR: %1")
                          .arg(QString::fromUtf8(response.mid(4)).trimmed()) };
    }
    const QString text = QString::fromUtf8(response.mid(3)).trimmed();
    if (text.isEmpty()) {
        return { {}, {}, QStringLiteral("Nenhum texto foi encontrado na área selecionada.") };
    }
    return { text, QStringLiteral("pt+en (EasyOCR)"), {} };
#endif
}
