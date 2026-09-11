// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "arrowtool.h"
#include "utils/confighandler.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QWidget>
#include <algorithm>
#include <cmath>

namespace {
const int ArrowWidth = 18;
const int ArrowHeight = 24;
const int MinArrowStyle = 0;
const int MaxArrowStyle = 1;

bool isValidArrowStyle(int style)
{
    return style >= MinArrowStyle && style <= MaxArrowStyle;
}

QPainterPath getArrowHead(QPoint p1, QPoint p2, const int thickness)
{
    QLineF base(p1, p2);
    // Create the vector for the position of the base  of the arrowhead
    QLineF temp(QPoint(0, 0), p2 - p1);
    int val = ArrowHeight + thickness * 4;
    if (base.length() < (val - thickness * 2)) {
        val = static_cast<int>(base.length() + thickness * 2);
    }
    temp.setLength(base.length() + thickness * 2 - val);
    // Move across the line up to the head
    QPointF bottomTranslation(temp.p2());

    // Rotate base of the arrowhead
    base.setLength(ArrowWidth + thickness * 2);
    base.setAngle(base.angle() + 90);
    // Move to the correct point
    QPointF temp2 = p1 - base.p2();
    // Center it
    QPointF centerTranslation((temp2.x() / 2), (temp2.y() / 2));

    base.translate(bottomTranslation);
    base.translate(centerTranslation);

    QPainterPath path;
    path.moveTo(p2);
    path.lineTo(base.p1());
    path.lineTo(base.p2());
    path.lineTo(p2);
    return path;
}

// gets a shorter line to prevent overlap in the point of the arrow
QLine getShorterLine(QPoint p1, QPoint p2, const int thickness)
{
    QLineF l(p1, p2);
    int val = ArrowHeight + thickness * 4;
    if (l.length() < (val - thickness * 2)) {
        // here should be 0, but then we lose "angle", so this is hack, but
        // looks not very bad
        val = thickness / 4;
        l.setLength(val);
    } else {
        l.setLength(l.length() + thickness * 2 - val);
    }
    return l.toLine();
}

QPainterPath getModernArrowHead(QPointF directionPoint,
                                QPointF tip,
                                const int thickness)
{
    QLineF line(directionPoint, tip);
    if (line.length() <= 0) {
        return {};
    }

    const QPointF direction = (tip - directionPoint) / line.length();
    const QPointF normal(-direction.y(), direction.x());
    const qreal headLength = std::max<qreal>(20.0, thickness * 4.6);
    const qreal halfWidth = std::max<qreal>(9.0, thickness * 2.1);
    const QPointF baseCenter = tip - direction * headLength;
    const QPointF baseLeft = baseCenter + normal * halfWidth;
    const QPointF baseRight = baseCenter - normal * halfWidth;
    const QPointF notch = baseCenter + direction * headLength * 0.24;

    QPainterPath path;
    path.moveTo(tip);
    path.quadTo(tip - direction * headLength * 0.42 + normal * halfWidth * 0.72,
                baseLeft);
    path.lineTo(notch);
    path.lineTo(baseRight);
    path.quadTo(tip - direction * headLength * 0.42 - normal * halfWidth * 0.72,
                tip);
    path.closeSubpath();
    return path;
}

QLineF getCurvedArrowShaft(QPointF p1, QPointF p2, const int thickness)
{
    QLineF line(p1, p2);
    if (line.length() <= 0) {
        return {};
    }

    const QPointF direction = (p2 - p1) / line.length();
    QLineF shaft(getShorterLine(p1.toPoint(), p2.toPoint(), thickness));
    const qreal notchDepth =
      std::min<qreal>(QLineF(shaft.p2(), p2).length() * 0.45,
                      (ArrowWidth + thickness * 2) / 2.0);
    constexpr qreal overlap = 1.0;

    // The curved head has a concave back, so extend the straight shaft
    // slightly into the head to avoid a visible gap without leaking past
    // the head outline at large thicknesses.
    shaft.setP2(shaft.p2() + direction * (notchDepth + overlap));
    return shaft;
}

QVector<QPointF> smoothGesture(const QVector<QPointF>& input)
{
    if (input.size() < 3) {
        return input;
    }

    QVector<QPointF> current;
    current.reserve(input.size());
    current.append(input.first());
    // A generous distance suppresses tight mouse-scale bends and makes the
    // resulting arcs broad and presentation-like.
    constexpr qreal anchorDistance = 40.0;
    for (int i = 1; i < input.size() - 1; ++i) {
        // Stable, widely spaced anchors preserve completed parts of the
        // gesture instead of refitting the whole curve when the tip moves.
        if (QLineF(current.last(), input[i]).length() >= anchorDistance) {
            current.append(input[i]);
        }
    }
    if (current.last() != input.last()) {
        current.append(input.last());
    }
    current.first() = input.first();
    current.last() = input.last();
    return current;
}

QPointF arrowDirectionPoint(const QVector<QPointF>& points, int thickness)
{
    const QPointF tip = points.last();
    const qreal minimumDistance = std::max<qreal>(16.0, thickness * 3.0);
    for (int i = points.size() - 2; i >= 0; --i) {
        if (QLineF(points[i], tip).length() >= minimumDistance) {
            return points[i];
        }
    }
    return points.first();
}

QPainterPath gesturePath(const QVector<QPointF>& points)
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

    // A Catmull-Rom spline converted to cubic Beziers passes through every
    // stable anchor. It retains multiple intentional bends while keeping
    // each transition broad and tangent-continuous.
    constexpr qreal smoothness = 0.20;
    for (int i = 0; i < points.size() - 1; ++i) {
        const QPointF p0 = i > 0 ? points[i - 1] : points[i];
        const QPointF p1 = points[i];
        const QPointF p2 = points[i + 1];
        const QPointF p3 = i + 2 < points.size() ? points[i + 2] : p2;
        const QPointF control1 = p1 + (p2 - p0) * smoothness;
        const QPointF control2 = p2 - (p3 - p1) * smoothness;
        path.cubicTo(control1, control2, p2);
    }
    return path;
}

} // unnamed namespace

ArrowTool::ArrowTool(QObject* parent)
  : AbstractTwoPointTool(parent)
{
    const int configuredArrowStyle = ConfigHandler().arrowStyle();
    if (isValidArrowStyle(configuredArrowStyle)) {
        m_arrowStyle = static_cast<ArrowStyle>(configuredArrowStyle);
    }

    setPadding(ArrowWidth / 2);
    m_supportsOrthogonalAdj = true;
    m_supportsDiagonalAdj = true;
}

QIcon ArrowTool::icon(const QColor& background, bool inEditor) const
{
    Q_UNUSED(inEditor)
    return QIcon(iconPath(background) + "arrow-bottom-left.svg");
}
QString ArrowTool::name() const
{
    return tr("Arrow");
}

CaptureTool::Type ArrowTool::type() const
{
    return CaptureTool::TYPE_ARROW;
}

QString ArrowTool::description() const
{
    return tr("Set the Arrow as the paint tool");
}

QRect ArrowTool::boundingRect() const
{
    if (!isValid()) {
        return {};
    }

    int offset = size() <= 1 ? 1 : static_cast<int>(round(size() / 2 + 0.5));

    // get min and max arrow pos
    int min_x = points().first.x();
    int min_y = points().first.y();
    int max_x = points().first.x();
    int max_y = points().first.y();
    for (int i = 0; i < m_arrowPath.elementCount(); i++) {
        QPointF pt = m_arrowPath.elementAt(i);
        if (static_cast<int>(pt.x()) < min_x) {
            min_x = static_cast<int>(pt.x());
        }
        if (static_cast<int>(pt.y()) < min_y) {
            min_y = static_cast<int>(pt.y());
        }
        if (static_cast<int>(pt.x()) > max_x) {
            max_x = static_cast<int>(pt.x());
        }
        if (static_cast<int>(pt.y()) > max_y) {
            max_y = static_cast<int>(pt.y());
        }
    }
    for (const QPointF& pt : m_gesturePoints) {
        min_x = std::min(min_x, static_cast<int>(std::floor(pt.x())));
        min_y = std::min(min_y, static_cast<int>(std::floor(pt.y())));
        max_x = std::max(max_x, static_cast<int>(std::ceil(pt.x())));
        max_y = std::max(max_y, static_cast<int>(std::ceil(pt.y())));
    }

    // get min and max line pos
    int line_pos_min_x =
      std::min(std::min(points().first.x(), points().second.x()), min_x);
    int line_pos_min_y =
      std::min(std::min(points().first.y(), points().second.y()), min_y);
    int line_pos_max_x =
      std::max(std::max(points().first.x(), points().second.x()), max_x);
    int line_pos_max_y =
      std::max(std::max(points().first.y(), points().second.y()), max_y);

    QRect rect = QRect(line_pos_min_x - offset,
                       line_pos_min_y - offset,
                       line_pos_max_x - line_pos_min_x + offset * 2,
                       line_pos_max_y - line_pos_min_y + offset * 2);

    return rect.normalized();
}

QWidget* ArrowTool::configurationWidget()
{
    auto* widget = new QWidget();
    auto* layout = new QHBoxLayout(widget);
    auto* label = new QLabel(tr("Arrow style:"), widget);
    auto* styleSelector = new QComboBox(widget);

    styleSelector->addItem(tr("Default"));
    styleSelector->addItem(tr("Curved"));
    styleSelector->setCurrentIndex(static_cast<int>(m_arrowStyle));
    connect(styleSelector,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            &ArrowTool::setArrowStyle);

    layout->addWidget(label);
    layout->addWidget(styleSelector);

    return widget;
}

CaptureTool* ArrowTool::copy(QObject* parent)
{
    auto* tool = new ArrowTool(parent);
    copyParams(this, tool);
    return tool;
}

void ArrowTool::copyParams(const ArrowTool* from, ArrowTool* to)
{
    AbstractTwoPointTool::copyParams(from, to);
    to->m_arrowPath = from->m_arrowPath;
    to->m_gesturePoints = from->m_gesturePoints;
    to->m_arrowStyle = from->m_arrowStyle;
}

void ArrowTool::process(QPainter& painter, const QPixmap& pixmap)
{
    bool isArrowReversed = ConfigHandler().reverseArrow();

    // Mobshot follows the natural gesture: start at the tail and finish at
    // the arrow head. The compatibility option intentionally reverses it.
    const QPoint& head = isArrowReversed ? points().first : points().second;
    const QPoint& tail = isArrowReversed ? points().second : points().first;

    Q_UNUSED(pixmap)
    painter.setPen(QPen(color(), size()));
    if (m_arrowStyle == ArrowStyle::Default) {
        painter.drawLine(getShorterLine(tail, head, size()));
        m_arrowPath = getModernArrowHead(tail, head, size());
        painter.fillPath(m_arrowPath, QBrush(color()));
        return;
    }

    QVector<QPointF> points = smoothGesture(m_gesturePoints);
    if (isArrowReversed) {
        std::reverse(points.begin(), points.end());
    }
    if (points.size() < 2) {
        points = { tail, head };
    }

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(color(), size(), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(gesturePath(points));
    const QPointF headDirection = arrowDirectionPoint(points, size());
    m_arrowPath = getModernArrowHead(headDirection, points.last(), size());
    painter.fillPath(m_arrowPath, QBrush(color()));
}

void ArrowTool::pressed(CaptureContext& context)
{
    Q_UNUSED(context)
}

void ArrowTool::drawStart(const CaptureContext& context)
{
    AbstractTwoPointTool::drawStart(context);
    m_gesturePoints = { context.mousePos };
}

void ArrowTool::drawMove(const QPoint& point)
{
    AbstractTwoPointTool::drawMove(point);
    if (m_gesturePoints.isEmpty() ||
        QLineF(m_gesturePoints.last(), point).length() >= 2.0) {
        m_gesturePoints.append(point);
    }
}

void ArrowTool::drawEnd(const QPoint& point)
{
    AbstractTwoPointTool::drawMove(point);
    if (m_gesturePoints.isEmpty() || m_gesturePoints.last() != point) {
        m_gesturePoints.append(point);
    }
}

void ArrowTool::move(const QPoint& pos)
{
    const QPoint oldPos = *AbstractTwoPointTool::pos();
    const QPoint delta = pos - oldPos;
    AbstractTwoPointTool::move(pos);
    for (QPointF& point : m_gesturePoints) {
        point += delta;
    }
}

void ArrowTool::setArrowStyle(int style)
{
    if (!isValidArrowStyle(style)) {
        style = static_cast<int>(ArrowStyle::Default);
    }
    m_arrowStyle = static_cast<ArrowStyle>(style);
    ConfigHandler().setArrowStyle(style);
}
