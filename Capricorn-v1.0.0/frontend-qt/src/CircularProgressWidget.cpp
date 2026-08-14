#include "CircularProgressWidget.h"

#include <QFont>
#include <QPainter>
#include <QPaintEvent>

CircularProgressWidget::CircularProgressWidget(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void CircularProgressWidget::setProgress(int progress) {
    progress = qBound(0, progress, 100);
    if (m_progress == progress) return;
    m_progress = progress;
    update();
}

void CircularProgressWidget::setPrimaryText(const QString &text) {
    if (m_primaryText == text) return;
    m_primaryText = text;
    update();
}

void CircularProgressWidget::setSecondaryText(const QString &text) {
    if (m_secondaryText == text) return;
    m_secondaryText = text;
    update();
}

void CircularProgressWidget::setAccentColor(const QColor &color) {
    m_accent = color;
    update();
}

void CircularProgressWidget::setTrackColor(const QColor &color) {
    m_track = color;
    update();
}

QSize CircularProgressWidget::sizeHint() const { return {92, 92}; }
QSize CircularProgressWidget::minimumSizeHint() const { return {82, 82}; }

void CircularProgressWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal side = qMin(width(), height());
    const qreal stroke = 7.0;
    const QRectF ring((width() - side) / 2.0 + stroke,
                      (height() - side) / 2.0 + stroke,
                      side - stroke * 2.0,
                      side - stroke * 2.0);

    QPen trackPen(m_track, stroke, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(trackPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawArc(ring, 0, 360 * 16);

    QPen progressPen(m_accent, stroke, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(progressPen);
    painter.drawArc(ring, 90 * 16, -qRound(360.0 * 16.0 * m_progress / 100.0));

    painter.setPen(QColor(QStringLiteral("#202432")));
    QFont primary = font();
    primary.setPointSizeF(9.2);
    primary.setWeight(QFont::Bold);
    painter.setFont(primary);
    if (m_secondaryText.trimmed().isEmpty()) {
        painter.drawText(rect().adjusted(6, 6, -6, -6), Qt::AlignCenter, m_primaryText);
    } else {
        painter.drawText(rect().adjusted(6, 19, -6, -23), Qt::AlignCenter, m_primaryText);
        painter.setPen(QColor(QStringLiteral("#777C88")));
        QFont secondary = font();
        secondary.setPointSizeF(7.0);
        secondary.setWeight(QFont::Normal);
        painter.setFont(secondary);
        painter.drawText(rect().adjusted(6, 50, -6, -8), Qt::AlignHCenter | Qt::AlignTop, m_secondaryText);
    }
}
