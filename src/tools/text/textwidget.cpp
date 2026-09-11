// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "textwidget.h"

#include <QEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QSizeGrip>

TextWidget::TextWidget(QWidget* parent)
  : QTextEdit(parent)
  , m_sizeGrip(new QSizeGrip(this))
{
    // Editing and selecting text must never leak mouse gestures to the
    // fullscreen capture canvas behind this widget.
    setAttribute(Qt::WA_NoMousePropagation, true);
    setFocusPolicy(Qt::StrongFocus);
    setStyleSheet(QStringLiteral("TextWidget { background: transparent; }"));
    connect(this, &TextWidget::textChanged, this, &TextWidget::emitTextUpdated);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setContextMenuPolicy(Qt::NoContextMenu);
    setLineWrapMode(QTextEdit::WidgetWidth);

    QFontMetrics fm(font());
    m_baseSize = QSize(fm.horizontalAdvance(QLatin1Char('M')) * 24,
                       fm.lineSpacing() * 4);
    m_minSize = QSize(fm.horizontalAdvance(QLatin1Char('M')) * 8,
                      fm.lineSpacing() * 2);
    setMinimumSize(m_minSize);
    resize(m_baseSize);
}

bool TextWidget::event(QEvent* e)
{
    if (e->type() == QEvent::ShortcutOverride) {
        auto* keyEvent = static_cast<QKeyEvent*>(e);
        if (keyEvent->key() == Qt::Key_Escape) {
            keyEvent->accept();
            return true;
        }
    }

    return QTextEdit::event(e);
}

void TextWidget::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        emit editingFinished();
        e->accept();
        return;
    }

    QTextEdit::keyPressEvent(e);
}

void TextWidget::showEvent(QShowEvent* e)
{
    QTextEdit::showEvent(e);
}

void TextWidget::resizeEvent(QResizeEvent* e)
{
    QTextEdit::resizeEvent(e);
    const QSize gripSize = m_sizeGrip->sizeHint();
    m_sizeGrip->setGeometry(width() - gripSize.width(),
                            height() - gripSize.height(),
                            gripSize.width(),
                            gripSize.height());
    m_sizeGrip->raise();
    emit textAreaResized(size());
}

void TextWidget::setFont(const QFont& f)
{
    QTextEdit::setFont(f);
}

void TextWidget::setAlignment(Qt::AlignmentFlag alignment)
{
    QTextEdit::setAlignment(alignment);
}
void TextWidget::setTextColor(const QColor& c)
{
    QString s(
      QStringLiteral("TextWidget { background: transparent; color: %1; }"));
    setStyleSheet(s.arg(c.name()));
}

void TextWidget::adjustSize()
{
    resize(sizeHint().expandedTo(m_minSize));
}

void TextWidget::emitTextUpdated()
{
    emit textUpdated(this->toPlainText());
}
