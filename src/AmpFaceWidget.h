#pragma once
//
// AmpFaceWidget — synthetic SPE Expert front panel.
//
// Pure Qt-painted UI that mirrors the look of resources/web/index.html:
// dark chassis with a brushed-metal gradient, two clusters of real
// QPushButton widgets (3 left, 4 right) flanking a green-on-black LCD,
// and a bottom strip carrying RX / TX activity LEDs, a connection
// status LED + label, and a settings cog. No photograph is loaded —
// everything is drawn by Qt so it stays crisp at any DPI and matches
// the web UI side-by-side.
//
// Buttons:
//   LEFT:  INPUT, ANT, TUNE
//   RIGHT: PWR (cycles HIGH/MID/LOW; wire token still "gain"),
//          CAT  (sends 0x0E, mfr §4),
//          OP   (toggles Operate/Standby; tints amber while Operate),
//          ON   (pulses DTR to wake the amp — community workaround,
//                no opcode exists in the programmer's guide),
//          OFF  (POWER OFF — handled by the parent which adds a confirm).
//
// Secondary key strip (between main panel and foot LEDs): menu / setup
// keys that map directly onto guide §4 opcodes — ← → SET DISP L- L+
// C- C+. Smaller than the primary chassis buttons; secondary visual
// weight to communicate "tuning / setup" rather than "routine operate".
//
// Signals:
//   buttonPressed(token)    — forward to AmpController::handleCommandToken.
//   settingsRequested()     — fired when the cog is clicked. The parent
//                             pops a SettingsDialog (port / baud / WS /
//                             HTTP / Connect).
//
// The LCD repaints from setAmpStatus(); pulseRx() flashes the RX LED
// on every frame received and pulseTx() flashes TX on every command.
// setConnectionState() updates the bottom-strip connection LED + label.
//
#include <QString>
#include <QWidget>

class QPushButton;
class QLabel;
class LedIndicator;
class LcdWidget;
struct AmpStatus;

class AmpFaceWidget : public QWidget {
    Q_OBJECT
public:
    explicit AmpFaceWidget(QWidget* parent = nullptr);

public slots:
    void setAmpStatus(const AmpStatus& status);
    // Pulse the RX LED — call on every status / ACK frame from the amp.
    void pulseRx();
    // Pulse the TX LED — call on every command sent to the amp.
    void pulseTx();
    // Update the bottom-strip connection LED + status text.
    // `details` is shown verbatim ("COM3 @ 115200", "No port — click ⚙",
    // "Port busy", etc.) so the parent owns the formatting.
    void setConnectionState(bool connected, const QString& details);

signals:
    // Token matches AmpController's handleCommandToken vocabulary
    // ("input"/"antenna"/"tune"/"gain"/"cat"/"oper"/"off").
    void buttonPressed(const QString& token);
    // Cog was clicked — parent should open the settings dialog.
    void settingsRequested();

private:
    void buildUi();
    void styleAmpButton(QPushButton* btn, bool danger = false);

    // Cluster widgets
    QPushButton* m_inputBtn = nullptr;
    QPushButton* m_antBtn   = nullptr;
    QPushButton* m_tuneBtn  = nullptr;
    QPushButton* m_pwrBtn   = nullptr;
    QPushButton* m_catBtn   = nullptr;
    QPushButton* m_opBtn    = nullptr;
    QPushButton* m_onBtn    = nullptr;
    QPushButton* m_offBtn   = nullptr;

    // LCD + bottom-strip indicators
    LcdWidget*    m_lcd      = nullptr;
    LedIndicator* m_rxLed    = nullptr;
    LedIndicator* m_txLed    = nullptr;
    LedIndicator* m_connLed  = nullptr;
    QLabel*       m_connLabel = nullptr;
    QPushButton*  m_settingsBtn = nullptr;
};
