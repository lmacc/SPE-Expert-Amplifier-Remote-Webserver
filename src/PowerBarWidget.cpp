#include "PowerBarWidget.h"

#include <QLinearGradient>
#include <QPainter>

namespace {
// Tick positions and labels as they appear in SPE_Remote.png.
struct Tick {
    double watts;
    const char* label;
};
constexpr Tick kTicks[] = {
    {  50.0, "50 W"   },
    { 250.0, "250 W"  },
    { 500.0, "500 W"  },
    { 750.0, "750 W"  },
    {1000.0, "1.0 kW" },
    {1300.0, "1.3 kW" },
    {1500.0, "1.5 kW" },
};
}

PowerBarWidget::PowerBarWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(50);
}

void PowerBarWidget::setWatts(double watts) {
    if (qFuzzyCompare(m_watts, watts)) { return; }
    m_watts = watts;
    update();
}

void PowerBarWidget::setMaxWatts(double maxWatts) {
    m_maxWatts = maxWatts;
    update();
}

QSize PowerBarWidget::sizeHint() const        { return { 560, 54 }; }
QSize PowerBarWidget::minimumSizeHint() const { return { 300, 50 }; }

void PowerBarWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Tick-label row sits above the bar.
    QFont labelFont = font();
    labelFont.setPointSizeF(labelFont.pointSizeF() * 0.8);
    p.setFont(labelFont);
    const int labelH = p.fontMetrics().height();
    const int barTop = labelH + 4;
    const int barH = height() - barTop - 4;
    const QRect barRect(6, barTop, width() - 12, barH);

    for (const Tick& t : kTicks) {
        const double frac = t.watts / m_maxWatts;
        const int x = barRect.left() + static_cast<int>(frac * barRect.width());
        p.setPen(QColor(180, 180, 180));
        p.drawText(QRect(x - 30, 0, 60, labelH), Qt::AlignCenter,
                   QString::fromLatin1(t.label));
        p.drawLine(x, barTop - 2, x, barTop);
    }

    // Background track.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(40, 42, 48));
    p.drawRoundedRect(barRect, 3, 3);

    // Filled portion (cyan → amber → red as we climb the scale).
    const double frac = qBound(0.0, m_watts / m_maxWatts, 1.0);
    if (frac > 0.0) {
        QLinearGradient g(barRect.topLeft(), barRect.topRight());
        g.setColorAt(0.00, QColor( 90, 200, 220));
        g.setColorAt(0.65, QColor(230, 190,  60));
        g.setColorAt(1.00, QColor(220,  70,  60));
        QRect fill = barRect;
        fill.setWidth(static_cast<int>(barRect.width() * frac));
        p.setBrush(g);
        p.drawRoundedRect(fill, 3, 3);
    }

    // Centered readout overlay.
    QFont valueFont = font();
    valueFont.setBold(true);
    p.setFont(valueFont);
    p.setPen(QColor(240, 240, 240));
    p.drawText(barRect, Qt::AlignCenter,
               QStringLiteral("%1 W").arg(QString::number(m_watts, 'f', 0)));
}
