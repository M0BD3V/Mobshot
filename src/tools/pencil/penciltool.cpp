// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "penciltool.h"
#include <QLineF>
#include <QPainter>
#include <QPainterPath>

namespace {
QVector<QPointF> mobshotSmoothStroke(const QVector<QPoint>& input)
{
    if (input.size() < 3) {
        QVector<QPointF> result;
        result.reserve(input.size());
        for (const QPoint& point : input) {
            result.append(point);
        }
        return result;
    }

    constexpr qreal spacing = 12.0;
    QVector<QPointF> points { input.first() };
    QPointF previous = input.first();
    qreal carried = 0.0;
    for (int i = 1; i < input.size(); ++i) {
        const QPointF target = input[i];
        qreal segment = QLineF(previous, target).length();
        while (segment > 0.0 && carried + segment >= spacing) {
            const qreal ratio = (spacing - carried) / segment;
            previous += (target - previous) * ratio;
            points.append(previous);
            segment = QLineF(previous, target).length();
            carried = 0.0;
        }
        carried += segment;
        previous = target;
    }
    if (QLineF(points.last(), input.last()).length() > 0.5) {
        points.append(input.last());
    }

    constexpr int passes = 8;
    constexpr qreal weight = 0.45;
    for (int pass = 0; pass < passes && points.size() >= 3; ++pass) {
        QVector<QPointF> next;
        next.reserve(points.size());
        next.append(points.first());
        for (int i = 1; i < points.size() - 1; ++i) {
            const QPointF average = (points[i - 1] + points[i + 1]) / 2.0;
            next.append(points[i] * (1.0 - weight) + average * weight);
        }
        next.append(points.last());
        points = std::move(next);
    }
    return points;
}

QPainterPath mobshotStrokePath(const QVector<QPointF>& points)
{
    QPainterPath path;
    if (points.isEmpty()) {
        return path;
    }
    path.moveTo(points.first());
    if (points.size() == 1) {
        return path;
    }
    if (points.size() == 2) {
        path.lineTo(points.last());
        return path;
    }
    for (int i = 1; i < points.size() - 1; ++i) {
        path.quadTo(points[i], (points[i] + points[i + 1]) / 2.0);
    }
    path.lineTo(points.last());
    return path;
}
} // namespace

PencilTool::PencilTool(QObject* parent)
  : AbstractPathTool(parent)
{}

QIcon PencilTool::icon(const QColor& background, bool inEditor) const
{
    Q_UNUSED(inEditor)
    return QIcon(iconPath(background) + "pencil.svg");
}
QString PencilTool::name() const
{
    return tr("Pencil");
}

CaptureTool::Type PencilTool::type() const
{
    return CaptureTool::TYPE_PENCIL;
}

QString PencilTool::description() const
{
    return tr("Set the Pencil as the paint tool");
}

CaptureTool* PencilTool::copy(QObject* parent)
{
    auto* tool = new PencilTool(parent);
    copyParams(this, tool);
    return tool;
}

void PencilTool::process(QPainter& painter, const QPixmap& pixmap)
{
    Q_UNUSED(pixmap)
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(m_color,
                        size(),
                        Qt::SolidLine,
                        Qt::RoundCap,
                        Qt::RoundJoin));
    painter.drawPath(mobshotStrokePath(mobshotSmoothStroke(m_points)));
}

void PencilTool::paintMousePreview(QPainter& painter,
                                   const CaptureContext& context)
{
    painter.setPen(QPen(context.color, context.toolSize + 2));
    painter.drawLine(context.mousePos, context.mousePos);
}

void PencilTool::drawStart(const CaptureContext& context)
{
    m_color = context.color;
    onSizeChanged(context.toolSize);
    m_points.append(context.mousePos);
    m_pathArea.setTopLeft(context.mousePos);
    m_pathArea.setBottomRight(context.mousePos);
}

void PencilTool::pressed(CaptureContext& context)
{
    Q_UNUSED(context)
}
