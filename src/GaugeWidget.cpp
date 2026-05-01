#include "GaugeWidget.h"

#include <QConicalGradient>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace {
constexpr int kStartAngle = 225;   // degrees, Qt convention (0° = 3 o'clock, CCW)
constexpr int kSweepAngle = -270;  // negative = clockwise
}

GaugeWidget::GaugeWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(110, 90);
}

void GaugeWidget::setTitle(const QString& title) {
    m_title = title;
    update();
}

void GaugeWidget::setRange(double min, double max) {
    m_min = min;
    m_max = max;
    update();
}

void GaugeWidget::setUnit(const QString& unit) {
    m_unit = unit;
    update();
}

void GaugeWidget::setValue(double value) {
    if (qFuzzyCompare(m_value, value)) { return; }
    m_value = value;
    update();
}

void GaugeWidget::setValueFormat(char format, int precision) {
    m_format = format;
    m_precision = precision;
    update();
}

QSize GaugeWidget::sizeHint() const       { return { 140, 110 }; }
QSize GaugeWidget::minimumSizeHint() const { return { 110, 90 }; }

void GaugeWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal diameter = qMin(width(), height() * 5 / 4) - 8;
    const QRectF arcRect(
        (width() - diameter) / 2.0,
        4.0,
        diameter,
        diameter);

    // Background track.
    QPen trackPen(QColor(60, 60, 64));
    trackPen.setWidthF(diameter * 0.10);
    trackPen.setCapStyle(Qt::FlatCap);
    p.setPen(trackPen);
    p.drawArc(arcRect, kStartAngle * 16, kSweepAngle * 16);

    // Value arc with green→yellow→red gradient along the sweep.
    const double span = m_max - m_min;
    const double fraction = span > 0 ? qBound(0.0, (m_value - m_min) / span, 1.0) : 0.0;
    if (fraction > 0.0) {
        QConicalGradient g(arcRect.center(), kStartAngle);
        g.setColorAt(0.00, QColor( 70, 200, 120));
        g.setColorAt(0.55, QColor(230, 190,  60));
        g.setColorAt(1.00, QColor(220,  70,  60));
        QPen arcPen(QBrush(g), diameter * 0.10);
        arcPen.setCapStyle(Qt::FlatCap);
        p.setPen(arcPen);
        p.drawArc(arcRect, kStartAngle * 16,
                  static_cast<int>(kSweepAngle * fraction) * 16);
    }

    // Numeric value, centered.
    p.setPen(QColor(235, 235, 235));
    QFont valueFont = font();
    valueFont.setPointSizeF(valueFont.pointSizeF() * 1.3);
    valueFont.setBold(true);
    p.setFont(valueFont);
    const QString valueText =
        QString::number(m_value, m_format, m_precision)
        + (m_unit.isEmpty() ? QString() : QStringLiteral(" ") + m_unit);
    p.drawText(arcRect, Qt::AlignCenter, valueText);

    // Min / max range labels under the arc ends.
    p.setPen(QColor(160, 160, 160));
    QFont smallFont = font();
    smallFont.setPointSizeF(smallFont.pointSizeF() * 0.75);
    p.setFont(smallFont);
    const QRectF labelStrip(0, arcRect.bottom() - 14, width(), 14);
    p.drawText(labelStrip, Qt::AlignLeft  | Qt::AlignVCenter,
               QStringLiteral("  %1").arg(QString::number(m_min, 'f', 0)));
    p.drawText(labelStrip, Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("%1  ").arg(QString::number(m_max, 'f', 0)));

    // Title below the gauge.
    p.setPen(QColor(200, 200, 200));
    const QRectF titleRect(0, height() - 16, width(), 14);
    p.drawText(titleRect, Qt::AlignCenter, m_title);
}
