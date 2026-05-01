#pragma once
//
// SettingsDialog — modal "connection settings" dialog opened from the
// AmpFaceWidget cog button. Replaces the old MainWindow toolbar so the
// desktop UI matches the synthetic chassis used in the web UI: nothing
// chrome-y around the panel, all configuration tucked behind ⚙.
//
// Holds the four settings the user changes day-to-day:
//   • Serial port name (combo, populated from QSerialPortInfo)
//   • Baud rate         (combo, common values + freeform edit)
//   • WebSocket port    (spinbox)
//   • HTTP port         (spinbox; 0 = disable web UI)
//
// Plus a Connect / Disconnect button so the user doesn't have to close
// the dialog to flip the link. The dialog mutates AmpController, WsBridge
// and HttpServer directly — same wiring the toolbar used to do — and
// reacts to AmpController::connectionChanged so the button label tracks
// the live state even if the connection drops on its own.
//
// Settings are not persisted from here: the embedded HTTP /api/config
// endpoint already writes ConfigStore on save, and the daemon picks the
// same file up on next launch. Keeping persistence out of this dialog
// avoids two writers fighting over the JSON file.
//

#include <QDialog>
#include <QString>

class AmpController;
class WsBridge;
class HttpServer;
class QComboBox;
class QSpinBox;
class QPushButton;
class QLabel;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(AmpController* amp, WsBridge* bridge, HttpServer* http,
                   QWidget* parent = nullptr);

private slots:
    void refreshPortList();
    void onConnectClicked();
    void onConnectionChanged(bool connected);

private:
    void buildUi();
    void syncFromAmp();

    AmpController* m_amp = nullptr;
    WsBridge*      m_bridge = nullptr;
    HttpServer*    m_http = nullptr;

    QComboBox*   m_portCombo    = nullptr;
    QComboBox*   m_baudCombo    = nullptr;
    QSpinBox*    m_wsPortSpin   = nullptr;
    QSpinBox*    m_httpPortSpin = nullptr;
    QPushButton* m_connectBtn   = nullptr;
    QLabel*      m_statusLabel  = nullptr;
};
