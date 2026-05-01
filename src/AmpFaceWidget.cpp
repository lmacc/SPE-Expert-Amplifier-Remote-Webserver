#include "AmpFaceWidget.h"

#include "AmpController.h"

#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPushButton>
#include <QRadialGradient>
#include <QSizePolicy>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

// =================================================================== //
//  LedIndicator — small circular LED with a glow halo                 //
// =================================================================== //
//
// Idles grey. flash(style) lights it for ~250 ms; setStyle(style) lights
// it steadily until cleared. Five styles cover everything we need:
// off, RX (green), TX (blue), warn (amber), err (red).
//
// Painted manually so the colour ramps stay consistent with the web UI
// without depending on QSS box-shadow workarounds.
//
class LedIndicator : public QWidget {
public:
    enum Style { Off, RxGreen, TxBlue, Warn, Err };

    explicit LedIndicator(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(14, 14);
    }

    void setStyle(Style s) {
        if (m_steady == s) { return; }
        m_steady = s;
        if (!m_flashActive) { update(); }
    }

    void flash(Style s, int ms = 250) {
        m_flashStyle  = s;
        m_flashActive = true;
        update();
        QTimer::singleShot(ms, this, [this]() {
            m_flashActive = false;
            update();
        });
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter g(this);
        g.setRenderHint(QPainter::Antialiasing);
        const Style s = m_flashActive ? m_flashStyle : m_steady;

        QColor base;
        switch (s) {
            case RxGreen: base = QColor(0x4e, 0xcc, 0x7b); break;
            case TxBlue:  base = QColor(0x4e, 0xa1, 0xff); break;
            case Warn:    base = QColor(0xe8, 0xc5, 0x33); break;
            case Err:     base = QColor(0xe6, 0x60, 0x60); break;
            case Off:
            default:      base = QColor(0x4a, 0x4e, 0x58); break;
        }

        const QRectF c = QRectF(rect()).adjusted(2, 2, -2, -2);

        // Glow halo for "lit" states. Off LEDs get no halo so the panel
        // doesn't look overactive at idle.
        if (s != Off) {
            QRadialGradient halo(c.center(), c.width());
            QColor h = base; h.setAlpha(160);
            halo.setColorAt(0.0, h);
            h.setAlpha(0);
            halo.setColorAt(1.0, h);
            g.setPen(Qt::NoPen);
            g.setBrush(halo);
            g.drawEllipse(c.adjusted(-3, -3, 3, 3));
        }

        // The LED itself with a subtle inner shadow so it reads as a
        // recessed pill. Identical visual weight to the web UI's CSS
        // box-shadow + radial gradient combo.
        g.setPen(QPen(QColor(0, 0, 0, 140), 1));
        g.setBrush(base);
        g.drawEllipse(c);
    }

private:
    Style m_steady     = Off;
    Style m_flashStyle = Off;
    bool  m_flashActive = false;
};

// =================================================================== //
//  LcdWidget — green-on-black status display                          //
// =================================================================== //
//
// Paints PA OUT bar (0–1500 W) + I PA bar (0–50 A) + two stat rows
// (BAND/TUNE/ANT/INP and T/V/SWR/TX) + an error strip. Same colour
// vocabulary as the web UI's `.amp-lcd`.
//
class LcdWidget : public QWidget {
public:
    explicit LcdWidget(QWidget* parent = nullptr) : QWidget(parent) {
        // Min height bumped from 130 to 150 to give 6 rows breathing
        // room (PA bar + I bar + SWR bar + 2 stats rows + error strip).
        setMinimumSize(360, 150);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setStatus(double pOut, double drain, double paTemp, double voltage,
                   double aswr, const QString& band, const QString& ant,
                   const QString& input, const QString& tx,
                   const QString& tune, const QString& error,
                   const QString& warnings) {
        m_pOut = pOut; m_drain = drain; m_paTemp = paTemp;
        m_voltage = voltage; m_aswr = aswr;
        m_band = band.isEmpty() ? QStringLiteral("--") : band;
        m_ant  = ant.isEmpty()  ? QStringLiteral("--") : ant;
        m_input = input.isEmpty() ? QStringLiteral("--") : input;
        m_tx   = tx.isEmpty()   ? QStringLiteral("--") : tx;
        m_tune = tune.isEmpty() ? QStringLiteral("--") : tune;
        m_error = error;
        m_warnings = warnings;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter g(this);
        g.setRenderHint(QPainter::Antialiasing);

        const QRectF lcd = QRectF(rect()).adjusted(0, 0, -1, -1);

        // Background + border + soft inner glow (matches the web UI's
        // box-shadow: inset 0 1px 6px rgba(0,0,0,0.65) on .amp-lcd).
        g.setPen(QPen(QColor(0x24, 0x33, 0x1a), 1));
        g.setBrush(QColor(0x0a, 0x1a, 0x0c));
        g.drawRoundedRect(lcd, 4, 4);

        const QColor kKey(0xc8, 0xff, 0xc8);
        const QColor kVal(0xff, 0xfa, 0xd0);
        const QColor kErr(0xff, 0x85, 0x85);

        const int   lcdFontPx = qBound(10, int(height() / 9.5), 17);
        const QFont mono = makeMono(lcdFontPx);
        const QFont small = makeMono(qBound(9, int(lcdFontPx * 0.85), 14));
        QFontMetrics fm(mono);

        const qreal padX = lcd.width()  * 0.025;
        const qreal padY = lcd.height() * 0.07;
        // Six rows: PA bar | I bar | SWR bar | stats1 | stats2 |
        // (optional) error. The SWR bar replaces the numeric SWR cell
        // that used to live in stats2 — at-a-glance is faster than a
        // float when a high-SWR fold-back is brewing.
        const qreal rowH = (lcd.height() - 2 * padY) / 6.0;

        // Bar rows --------------------------------------------------------
        g.setFont(mono);
        drawBarRow(g, lcd, fm, lcd.top() + padY + 0 * rowH, rowH,
                   QStringLiteral("PA"), m_pOut, 0.0, kPaOutMax,
                   QStringLiteral("W"), BarStyle::PaThreshold, 0,
                   kKey, kVal, padX);
        drawBarRow(g, lcd, fm, lcd.top() + padY + 1 * rowH, rowH,
                   QStringLiteral("I"),  m_drain, 0.0, kIPaMax,
                   QStringLiteral("A"), BarStyle::AmberFixed, 1,
                   kKey, kVal, padX);
        drawBarRow(g, lcd, fm, lcd.top() + padY + 2 * rowH, rowH,
                   QStringLiteral("SWR"), m_aswr, kSwrMin, kSwrMax,
                   QString(), BarStyle::SwrThreshold, 2,
                   kKey, kVal, padX);

        // Stat rows -------------------------------------------------------
        g.setFont(small);
        QFontMetrics sfm(small);
        drawStatsRow(g, sfm, lcd, lcd.top() + padY + 3 * rowH, rowH, padX,
                     {{QStringLiteral("BAND"), m_band},
                      {QStringLiteral("TUNE"), m_tune},
                      {QStringLiteral("ANT"),  m_ant},
                      {QStringLiteral("INP"),  m_input}},
                     kKey, kVal);
        // Stats2 now holds T and V only — SWR moved up to the bar above.
        drawStatsRow(g, sfm, lcd, lcd.top() + padY + 4 * rowH, rowH, padX,
                     {{QStringLiteral("T"),
                       QString::number(m_paTemp, 'f', 0) + QChar(0x00b0)},
                      {QStringLiteral("V"),
                       QString::number(m_voltage, 'f', 1)}},
                     kKey, kVal);
        // TX/RX pill in the right-hand corner of the stats2 row.
        drawTxPill(g, sfm, lcd, lcd.top() + padY + 4 * rowH, rowH, padX);

        // Error strip — only shown when something is wrong.
        if (!m_error.isEmpty() || !m_warnings.isEmpty()) {
            const QRectF errRect(lcd.left() + padX,
                                 lcd.top() + padY + 5 * rowH,
                                 lcd.width() - 2 * padX, rowH);
            g.setPen(kErr);
            g.drawText(errRect, Qt::AlignLeft | Qt::AlignVCenter,
                       (m_error.isEmpty() ? m_warnings : m_error));
        }
    }

private:
    // Bar fill colour logic. PaThreshold ramps green → amber → red as
    // value crosses fixed wattage thresholds; AmberFixed is the always-
    // amber I-PA fill; SwrThreshold ramps green → amber → red as the
    // SWR ratio crosses 1.5 / 2.0 (regardless of bar fill percentage).
    enum class BarStyle { PaThreshold, AmberFixed, SwrThreshold };

    static constexpr double kPaOutMax = 1500.0;
    static constexpr double kIPaMax   = 50.0;
    // SWR bar maps 1.00–3.00 across the bar, with anything > 3.0
    // clamped at full. Below 1.0 (which the protocol never emits in
    // TX) reads as empty.
    static constexpr double kSwrMin   = 1.0;
    static constexpr double kSwrMax   = 3.0;

    static QFont makeMono(int px) {
        QFont f(QStringLiteral("Consolas"));
        f.setStyleHint(QFont::Monospace);
        f.setPixelSize(px);
        return f;
    }

    static void drawBarRow(QPainter& g, const QRectF& lcd,
                           const QFontMetrics& fm,
                           qreal rowY, qreal rowH,
                           const QString& key, double value,
                           double minVal, double maxVal,
                           const QString& unit, BarStyle style, int decimals,
                           const QColor& kKey, const QColor& kVal, qreal padX) {
        // Label — width sized for the longest label we use ("SWR") so
        // all three bars line their bar-start columns up.
        g.setPen(kKey);
        const qreal keyW = fm.horizontalAdvance(QStringLiteral("SWR")) + 4;
        const QRectF keyRect(lcd.left() + padX, rowY, keyW, rowH);
        g.drawText(keyRect, Qt::AlignRight | Qt::AlignVCenter, key);

        // Value (right-aligned)
        const QString valStr = (value > 0)
            ? QString::number(value, 'f', decimals)
            : QStringLiteral("----");
        const int valW   = fm.horizontalAdvance(QStringLiteral("9999"));
        const int unitW  = fm.horizontalAdvance(unit) + 4;
        const QRectF valRect(lcd.right() - padX - valW - unitW, rowY,
                             valW, rowH);
        g.setPen(kVal);
        g.drawText(valRect, Qt::AlignRight | Qt::AlignVCenter, valStr);

        const QRectF unitRect(valRect.right() + 2, rowY, unitW, rowH);
        g.setPen(kKey);
        g.drawText(unitRect, Qt::AlignLeft | Qt::AlignVCenter, unit);

        // Bar
        const QRectF barRect(keyRect.right() + 4, rowY + rowH * 0.30,
                             valRect.left() - keyRect.right() - 8,
                             rowH * 0.40);
        g.setPen(QPen(QColor(0x1b, 0x35, 0x20), 1));
        g.setBrush(QColor(0x06, 0x12, 0x0a));
        g.drawRect(barRect);

        // Percentage along the bar — generic min/max span so both 0..N
        // bars (PA, I) and 1..3 bars (SWR) work with the same code.
        const double span = (maxVal > minVal) ? (maxVal - minVal) : 1.0;
        const double pct  = qBound(0.0, (value - minVal) / span, 1.0);
        QRectF fillRect = barRect.adjusted(1, 1, -1, -1);
        fillRect.setWidth(fillRect.width() * pct);
        QLinearGradient grad(fillRect.topLeft(), fillRect.topRight());

        // Colour ramp picked by caller. PaThreshold compares raw watts;
        // SwrThreshold compares the SWR ratio; AmberFixed is constant.
        QColor c0, c1;
        switch (style) {
            case BarStyle::PaThreshold:
                if      (value > 1400.0) { c0 = QColor(0xd1, 0x4a, 0x4a); c1 = QColor(0xff, 0x85, 0x85); }
                else if (value > 1200.0) { c0 = QColor(0xd9, 0xb5, 0x4a); c1 = QColor(0xff, 0xe0, 0x7a); }
                else                     { c0 = QColor(0x4f, 0xb2, 0x4f); c1 = QColor(0x86, 0xf9, 0x8a); }
                break;
            case BarStyle::AmberFixed:
                c0 = QColor(0xd1, 0x9a, 0x4a); c1 = QColor(0xff, 0xd1, 0x85);
                break;
            case BarStyle::SwrThreshold:
                if      (value > 2.0) { c0 = QColor(0xd1, 0x4a, 0x4a); c1 = QColor(0xff, 0x85, 0x85); }
                else if (value > 1.5) { c0 = QColor(0xd9, 0xb5, 0x4a); c1 = QColor(0xff, 0xe0, 0x7a); }
                else                  { c0 = QColor(0x4f, 0xb2, 0x4f); c1 = QColor(0x86, 0xf9, 0x8a); }
                break;
        }
        grad.setColorAt(0, c0);
        grad.setColorAt(1, c1);
        g.setPen(Qt::NoPen);
        g.setBrush(grad);
        g.drawRect(fillRect);

        // 25 / 50 / 75 % ticks — same as the web UI's lcd-tick spans.
        g.setPen(QPen(QColor(255, 255, 255, 50), 1));
        for (int i = 1; i <= 3; ++i) {
            const qreal tx = barRect.left() + barRect.width() * (i / 4.0);
            g.drawLine(QPointF(tx, barRect.top() + 1),
                       QPointF(tx, barRect.bottom() - 1));
        }
    }

    static void drawStatsRow(QPainter& g, const QFontMetrics& sfm,
                             const QRectF& lcd, qreal rowY, qreal rowH,
                             qreal padX,
                             const QList<QPair<QString, QString>>& cells,
                             const QColor& kKey, const QColor& kVal) {
        // 4 cells across the row, last cell reserved for TX pill if caller
        // passes only 3 entries (the TX pill is drawn separately).
        const qreal usable = lcd.width() - 2 * padX;
        const qreal cellW  = usable / 4.0;
        for (int i = 0; i < cells.size(); ++i) {
            const QRectF cell(lcd.left() + padX + i * cellW, rowY,
                              cellW - 4, rowH);
            g.setPen(kKey);
            const QString k = cells[i].first + QLatin1Char(' ');
            g.drawText(cell, Qt::AlignLeft | Qt::AlignVCenter, k);
            const int keyAdv = sfm.horizontalAdvance(k);
            const QRectF vRect(cell.left() + keyAdv, cell.top(),
                               cell.width() - keyAdv, cell.height());
            g.setPen(kVal);
            g.drawText(vRect, Qt::AlignLeft | Qt::AlignVCenter,
                       cells[i].second);
        }
    }

    void drawTxPill(QPainter& g, const QFontMetrics& sfm, const QRectF& lcd,
                    qreal rowY, qreal rowH, qreal padX) const {
        const QString txText = m_tx.isEmpty() ? QStringLiteral("--") : m_tx;
        const int w = sfm.horizontalAdvance(txText) + 12;
        const QRectF pill(lcd.right() - padX - w,
                          rowY + rowH * 0.15,
                          w, rowH * 0.70);
        if (m_tx == QLatin1String("TX")) {
            g.setBrush(QColor(0xa8, 0x32, 0x32));
            g.setPen(Qt::NoPen);
            g.drawRoundedRect(pill, 3, 3);
            g.setPen(Qt::white);
        } else {
            g.setBrush(QColor(0x1d, 0x2d, 0x1a));
            g.setPen(Qt::NoPen);
            g.drawRoundedRect(pill, 3, 3);
            g.setPen(QColor(0x86, 0xf9, 0x8a));
        }
        g.drawText(pill, Qt::AlignCenter, txText);
    }

    double  m_pOut = 0.0, m_drain = 0.0, m_paTemp = 0.0, m_voltage = 0.0,
            m_aswr = 0.0;
    QString m_band = QStringLiteral("--");
    QString m_ant  = QStringLiteral("--");
    QString m_input = QStringLiteral("--");
    QString m_tx   = QStringLiteral("--");
    QString m_tune = QStringLiteral("--");
    QString m_error;
    QString m_warnings;
};

// =================================================================== //
//  AmpFaceWidget                                                      //
// =================================================================== //

namespace {

double toNum(const QString& s) {
    bool ok = false;
    const double v = s.trimmed().toDouble(&ok);
    return ok ? v : 0.0;
}

// Translate p_level codes (H / M / L) to the user-facing label shown
// inside the PWR button. Empty when the amp hasn't reported yet.
QString pwrLabelFor(const QString& code) {
    if (code == QLatin1String("H")) { return QStringLiteral("HIGH"); }
    if (code == QLatin1String("M")) { return QStringLiteral("MID"); }
    if (code == QLatin1String("L")) { return QStringLiteral("LOW"); }
    return QStringLiteral("---");
}

// QSS for the chassis itself + brand label. Applied to the AmpFaceWidget
// via a stylesheet on the top-level widget; per-button styling is set
// individually in styleAmpButton() so we can flip the OFF button to a
// red-accent variant without selector gymnastics.
const char* kChassisQss = R"qss(
AmpFaceWidget {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0    #2a2d35,
        stop:0.18 #1a1d23,
        stop:0.82 #1a1d23,
        stop:1    #14161b);
    border: 1px solid #353842;
    border-radius: 8px;
}
QLabel#brandLabel {
    color: #9aa0ac;
    font-size: 11px;
    letter-spacing: 2px;
    qproperty-alignment: 'AlignLeft';
}
QLabel#connLabel {
    color: #9aa0ac;
    font-size: 11px;
    letter-spacing: 1px;
}
QLabel.ledLabel {
    color: #9aa0ac;
    font-size: 10px;
    letter-spacing: 1px;
}
QLabel#brandText {
    color: #6a6f78;
    font-size: 11px;
    letter-spacing: 1px;
}
QFrame#footRule {
    background: #2a2d35;
    max-height: 1px;
    min-height: 1px;
    border: none;
}
QPushButton#cogBtn {
    background: transparent;
    border: none;
    color: #4ea1ff;
    font-size: 18px;
    padding: 2px 8px;
    border-radius: 4px;
}
QPushButton#cogBtn:hover { background: #2c3039; }
)qss";

const char* kButtonNormalQss = R"qss(
QPushButton {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0    #4a4e58,
        stop:0.35 #3a3e48,
        stop:1    #2a2d35);
    border: 1px solid #4a4e58;
    border-radius: 6px;
    color: #e6e7ea;
    font-weight: 600;
    font-size: 13px;
    letter-spacing: 1px;
    padding: 6px 8px;
    min-width: 86px;
    min-height: 44px;
}
QPushButton:hover {
    border-color: #4ea1ff;
}
QPushButton:pressed {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #2a2d35, stop:1 #1f2228);
    border-color: #4ea1ff;
}
QPushButton[active="true"] {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #b8852e, stop:1 #8a6020);
    border-color: #e8b048;
    color: #fff8e0;
}
)qss";

const char* kButtonMenuQss = R"qss(
QPushButton {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0    #353841,
        stop:1    #23262d);
    border: 1px solid #353841;
    border-radius: 4px;
    color: #c2c6cf;
    font-weight: 500;
    font-size: 11px;
    letter-spacing: 1px;
    padding: 4px 6px;
    min-width: 48px;
    min-height: 26px;
}
QPushButton:hover {
    border-color: #4ea1ff;
    color: #fff;
}
QPushButton:pressed {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #1f2228, stop:1 #16181d);
    border-color: #4ea1ff;
}
)qss";

const char* kButtonDangerQss = R"qss(
QPushButton {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0    #4a4e58,
        stop:0.35 #3a3e48,
        stop:1    #2a2d35);
    border: 1px solid #6a3232;
    border-radius: 6px;
    color: #e6e7ea;
    font-weight: 600;
    font-size: 13px;
    letter-spacing: 1px;
    padding: 6px 8px;
    min-width: 86px;
    min-height: 44px;
}
QPushButton:hover {
    border-color: #e66060;
    color: #fff;
}
QPushButton:pressed {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #2a2d35, stop:1 #1f2228);
    border-color: #e66060;
}
)qss";

}  // namespace

AmpFaceWidget::AmpFaceWidget(QWidget* parent) : QWidget(parent) {
    // QSS stylesheets only paint the widget's own background when this
    // attribute is set; otherwise the chassis gradient would only apply
    // to descendants.
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QString::fromLatin1(kChassisQss));
    setMinimumSize(820, 280);
    buildUi();
}

void AmpFaceWidget::buildUi() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 12, 14, 8);
    outer->setSpacing(8);

    // Brand strip ----------------------------------------------------------
    auto* brand = new QLabel(QStringLiteral("SPE EXPERT 1.3K-FA · REMOTE"));
    brand->setObjectName(QStringLiteral("brandLabel"));
    outer->addWidget(brand);

    // Main panel: left cluster | LCD | right cluster ----------------------
    auto* panel = new QHBoxLayout;
    panel->setSpacing(12);

    auto* leftCluster = new QVBoxLayout;
    leftCluster->setSpacing(8);
    m_inputBtn = new QPushButton(QStringLiteral("INPUT"));
    m_antBtn   = new QPushButton(QStringLiteral("ANT"));
    m_tuneBtn  = new QPushButton(QStringLiteral("TUNE"));
    m_inputBtn->setToolTip(QStringLiteral("Cycle radio input"));
    m_antBtn->setToolTip(QStringLiteral("Cycle TX antenna"));
    m_tuneBtn->setToolTip(QStringLiteral("Tune ATU"));
    for (auto* b : {m_inputBtn, m_antBtn, m_tuneBtn}) {
        styleAmpButton(b);
        leftCluster->addWidget(b);
    }
    auto* leftWrap = new QWidget;
    leftWrap->setLayout(leftCluster);
    leftWrap->setFixedWidth(110);
    panel->addWidget(leftWrap);

    // LCD takes the middle, stretches.
    m_lcd = new LcdWidget;
    panel->addWidget(m_lcd, /*stretch*/ 1);

    auto* rightCluster = new QVBoxLayout;
    rightCluster->setSpacing(8);
    m_pwrBtn = new QPushButton(QStringLiteral("PWR\n---"));
    m_catBtn = new QPushButton(QStringLiteral("CAT"));
    m_opBtn  = new QPushButton(QStringLiteral("OP"));
    m_onBtn  = new QPushButton(QStringLiteral("ON"));
    m_offBtn = new QPushButton(QStringLiteral("OFF"));
    m_pwrBtn->setToolTip(QStringLiteral("Cycle PWR level (HIGH / MID / LOW)"));
    m_catBtn->setToolTip(QStringLiteral("Toggle CAT / RIG-control"));
    m_opBtn->setToolTip(QStringLiteral("Operate / Standby"));
    m_onBtn->setToolTip(QStringLiteral(
        "Power ON — pulses DTR to wake the amp (requires an open serial port)"));
    m_offBtn->setToolTip(QStringLiteral("Power OFF — confirms first"));
    styleAmpButton(m_pwrBtn);
    styleAmpButton(m_catBtn);
    styleAmpButton(m_opBtn);
    styleAmpButton(m_onBtn);
    styleAmpButton(m_offBtn, /*danger=*/ true);
    for (auto* b : {m_pwrBtn, m_catBtn, m_opBtn, m_onBtn, m_offBtn}) {
        rightCluster->addWidget(b);
    }
    auto* rightWrap = new QWidget;
    rightWrap->setLayout(rightCluster);
    rightWrap->setFixedWidth(110);
    panel->addWidget(rightWrap);

    outer->addLayout(panel, /*stretch*/ 1);

    // Secondary key strip — menu / setup keys from guide §4. These are
    // tuning-time keys (ATU L/C trim, ←/→ menu navigation, SET, DISPLAY
    // toggle), so they get a smaller, flatter visual weight than the
    // primary chassis buttons.
    auto* menuStrip = new QHBoxLayout;
    menuStrip->setSpacing(6);
    menuStrip->setContentsMargins(0, 0, 0, 0);
    // Stretches on both sides so the strip centers within the chassis
    // width, leaving even gutters either side instead of hugging the
    // left edge.
    menuStrip->addStretch(1);
    auto* menuLabel = new QLabel(QStringLiteral("MENU"));
    menuLabel->setStyleSheet(QStringLiteral(
        "color:#6a6f78;font-size:10px;letter-spacing:2px;"
        "padding-right:6px;"));
    menuStrip->addWidget(menuLabel);

    struct MenuKey { const char* label; const char* token; const char* tip; };
    const MenuKey keys[] = {
        { "←",  "left",    "LEFT arrow (menu navigation)" },
        { "→",  "right",   "RIGHT arrow (menu navigation)" },
        { "SET",     "set",     "SET key — enter / accept setup screens" },
        { "DISP",    "display", "DISPLAY backlight toggle" },
        { "L−", "l-",      "ATU inductor trim DOWN" },
        { "L+",      "l+",      "ATU inductor trim UP" },
        { "C−", "c-",      "ATU capacitor trim DOWN" },
        { "C+",      "c+",      "ATU capacitor trim UP" },
    };
    for (const auto& k : keys) {
        auto* b = new QPushButton(QString::fromUtf8(k.label));
        b->setStyleSheet(QString::fromLatin1(kButtonMenuQss));
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::TabFocus);
        b->setToolTip(QString::fromLatin1(k.tip));
        const QString token = QString::fromLatin1(k.token);
        connect(b, &QPushButton::clicked, this,
                [this, token]() { emit buttonPressed(token); });
        menuStrip->addWidget(b);
    }
    menuStrip->addStretch(1);
    outer->addLayout(menuStrip);

    // Bottom strip: thin rule, then RX/TX/conn LEDs + brand + cog ---------
    auto* rule = new QFrame;
    rule->setObjectName(QStringLiteral("footRule"));
    rule->setFrameShape(QFrame::NoFrame);
    rule->setFixedHeight(1);
    outer->addWidget(rule);

    auto* foot = new QHBoxLayout;
    foot->setSpacing(10);

    auto addLedGroup = [foot](LedIndicator* led, const QString& label,
                              const QString& tip) {
        led->setToolTip(tip);
        foot->addWidget(led);
        auto* lbl = new QLabel(label);
        lbl->setProperty("class", "ledLabel");
        lbl->setStyleSheet(QStringLiteral(
            "color:#9aa0ac;font-size:10px;letter-spacing:1px;"));
        foot->addWidget(lbl);
    };

    m_rxLed = new LedIndicator(this);
    addLedGroup(m_rxLed, QStringLiteral("RX"),
                QStringLiteral("Pulses on every frame received from the amp"));

    m_txLed = new LedIndicator(this);
    addLedGroup(m_txLed, QStringLiteral("TX"),
                QStringLiteral("Pulses on every command sent to the amp"));

    m_connLed = new LedIndicator(this);
    m_connLed->setStyle(LedIndicator::Warn);
    m_connLed->setToolTip(QStringLiteral("Serial connection state"));
    foot->addWidget(m_connLed);
    m_connLabel = new QLabel(QStringLiteral("No port"));
    m_connLabel->setObjectName(QStringLiteral("connLabel"));
    m_connLabel->setStyleSheet(QStringLiteral(
        "color:#9aa0ac;font-size:11px;letter-spacing:1px;"));
    foot->addWidget(m_connLabel);

    foot->addStretch(1);

    auto* brandText = new QLabel(QStringLiteral("SPE Remote · Qt6 port"));
    brandText->setObjectName(QStringLiteral("brandText"));
    brandText->setStyleSheet(QStringLiteral(
        "color:#6a6f78;font-size:11px;letter-spacing:1px;"));
    foot->addWidget(brandText);

    m_settingsBtn = new QPushButton(QStringLiteral("\u2699"));
    m_settingsBtn->setObjectName(QStringLiteral("cogBtn"));
    m_settingsBtn->setToolTip(QStringLiteral("Settings (port / baud)"));
    m_settingsBtn->setCursor(Qt::PointingHandCursor);
    // The cog uses its own minimal stylesheet — no panel-button look.
    m_settingsBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; border: none; "
        "color: #4ea1ff; font-size: 18px; padding: 2px 8px; "
        "border-radius: 4px; min-width: 0; min-height: 0; }"
        "QPushButton:hover { background: #2c3039; }"));
    foot->addWidget(m_settingsBtn);

    outer->addLayout(foot);

    // Wire the seven panel buttons to the buttonPressed signal. The
    // wire token for PWR stays "gain" so existing WS clients (ESP8266,
    // legacy web UI) keep working unchanged.
    connect(m_inputBtn, &QPushButton::clicked, this,
            [this]() { emit buttonPressed(QStringLiteral("input")); });
    connect(m_antBtn,   &QPushButton::clicked, this,
            [this]() { emit buttonPressed(QStringLiteral("antenna")); });
    connect(m_tuneBtn,  &QPushButton::clicked, this,
            [this]() { emit buttonPressed(QStringLiteral("tune")); });
    connect(m_pwrBtn,   &QPushButton::clicked, this,
            [this]() { emit buttonPressed(QStringLiteral("gain")); });
    connect(m_catBtn,   &QPushButton::clicked, this,
            [this]() { emit buttonPressed(QStringLiteral("cat")); });
    connect(m_opBtn,    &QPushButton::clicked, this,
            [this]() { emit buttonPressed(QStringLiteral("oper")); });
    connect(m_onBtn,    &QPushButton::clicked, this,
            [this]() { emit buttonPressed(QStringLiteral("on")); });
    connect(m_offBtn,   &QPushButton::clicked, this,
            [this]() { emit buttonPressed(QStringLiteral("off")); });
    connect(m_settingsBtn, &QPushButton::clicked, this,
            &AmpFaceWidget::settingsRequested);
}

void AmpFaceWidget::styleAmpButton(QPushButton* btn, bool danger) {
    btn->setStyleSheet(QString::fromLatin1(danger ? kButtonDangerQss
                                                  : kButtonNormalQss));
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFocusPolicy(Qt::TabFocus);
}

void AmpFaceWidget::setAmpStatus(const AmpStatus& s) {
    m_lcd->setStatus(toNum(s.pOut), toNum(s.drain), toNum(s.paTemp),
                     toNum(s.voltage), toNum(s.aswr),
                     s.band, s.txAntenna, s.input, s.txStatus,
                     /*tune*/ QString(),
                     s.error.trimmed(), s.warnings.trimmed());

    // PWR sublabel — second line of the PWR button.
    m_pwrBtn->setText(QStringLiteral("PWR\n%1").arg(pwrLabelFor(s.pLevel)));

    // OP amber tint while the amp reports Operate.
    const bool oper = (s.opStatus == QLatin1String("Oper"));
    m_opBtn->setProperty("active", oper);
    // Force the QSS engine to reapply the property selector.
    m_opBtn->style()->unpolish(m_opBtn);
    m_opBtn->style()->polish(m_opBtn);
}

void AmpFaceWidget::pulseRx() {
    if (m_rxLed) { m_rxLed->flash(LedIndicator::RxGreen); }
}

void AmpFaceWidget::pulseTx() {
    if (m_txLed) { m_txLed->flash(LedIndicator::TxBlue); }
}

void AmpFaceWidget::setConnectionState(bool connected, const QString& details) {
    if (!m_connLed || !m_connLabel) { return; }
    if (connected) {
        m_connLed->setStyle(LedIndicator::RxGreen);
    } else if (details.contains(QLatin1String("error"), Qt::CaseInsensitive)
            || details.contains(QLatin1String("busy"),  Qt::CaseInsensitive)) {
        m_connLed->setStyle(LedIndicator::Err);
    } else {
        m_connLed->setStyle(LedIndicator::Warn);
    }
    m_connLabel->setText(details);
}
