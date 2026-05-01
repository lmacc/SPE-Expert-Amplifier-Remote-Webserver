#pragma once
//
// MainWindow — thin host shell for the AmpFaceWidget.
//
// The whole visible UI is the synthetic chassis (AmpFaceWidget). All
// connection plumbing lives in SettingsDialog, opened from the cog
// button on the chassis bottom strip — there's no toolbar and no
// menu-driven configuration. The window itself only exists to:
//
//   • own the AmpFaceWidget as central widget,
//   • route AmpController signals to the face (status → LCD,
//     ackReceived → RX pulse, commandSent → TX pulse,
//     connectionChanged → bottom-strip LED + label),
//   • forward face button presses to AmpController, with a confirm
//     gate on POWER OFF,
//   • own the SettingsDialog instance so it survives across opens,
//   • surface the Help → About menu.
//

#include <QMainWindow>
#include <QString>

class AmpController;
class WsBridge;
class HttpServer;
class AmpFaceWidget;
class SettingsDialog;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(AmpController* amp, WsBridge* bridge, HttpServer* http,
               QWidget* parent = nullptr);

private slots:
    void onConnectionChanged(bool connected);
    void onLog(const QString& message);
    void onAboutClicked();
    void onSettingsRequested();
    // Chassis hotspot fired — forward to AmpController, with a confirm
    // dialog on "off" so a misclick can't kill the amp.
    void onFaceButtonPressed(const QString& token);

private:
    void buildUi();
    void buildMenus();

    AmpController* m_amp = nullptr;
    WsBridge*      m_bridge = nullptr;
    HttpServer*    m_http = nullptr;

    AmpFaceWidget*  m_face = nullptr;
    SettingsDialog* m_settings = nullptr;  // lazy-constructed on first ⚙
};
