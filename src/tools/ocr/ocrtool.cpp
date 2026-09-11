// SPDX-License-Identifier: GPL-3.0-or-later

#include "ocrtool.h"

OcrTool::OcrTool(QObject* parent)
  : AbstractActionTool(parent)
{}

bool OcrTool::closeOnButtonPressed() const
{
    return false;
}

QIcon OcrTool::icon(const QColor& background, bool inEditor) const
{
    Q_UNUSED(inEditor)
    return QIcon(iconPath(background) + "format-text.svg");
}

QString OcrTool::name() const
{
    return tr("Copiar texto");
}

QString OcrTool::description() const
{
    return tr("Reconhecer o texto da seleção e copiar para a área de transferência");
}

CaptureTool* OcrTool::copy(QObject* parent)
{
    return new OcrTool(parent);
}

CaptureTool::Type OcrTool::type() const
{
    return CaptureTool::TYPE_OCR;
}

void OcrTool::pressed(CaptureContext& context)
{
    Q_UNUSED(context)
    emit requestAction(REQ_COPY_TEXT);
}
