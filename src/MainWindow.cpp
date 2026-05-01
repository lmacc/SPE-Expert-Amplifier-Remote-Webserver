#include "MainWindow.h"

#include "AboutDialog.h"
#include "AmpController.h"
#include "AmpFaceWidget.h"
#include "HttpServer.h"
#include "SettingsDialog.h"
#include "WsBridge.h"

#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>

MainWindow::MainWindow(AmpController* amp, WsBridge* bridge, HttpServer* http,
                       QWidget* parent)
    : QMainWindow(parent), m_amp(amp), m_bridge(bridge), m_http(http) {
    setWindowTitle(QStringLiteral("SPE 1.3K-2K-FA — Remote (Qt6 port)"));
    buildUi();
    buildMenus();

    // Amp → face: status frames drive the LCD and pulse RX. ackReceived
    // also pulses RX (the amp echoes a 6-byte ACK after every command),
    // and commandSent pulses TX whenever we wrote a command to the wire.
    connect(m_amp,  &AmpController::statusUpdated,
            m_face, &AmpFaceWidget::setAmpStatus);
    connect(m_amp,  &AmpController::statusUpdated,
            m_face, [this](const AmpStatus&) { m_face->pulseRx(); });
    connect(m_amp,  &AmpController::ackReceived,
            m_face, [this](quint8, const QString&) { m_face->pulseRx(); });
    connect(m_amp,  &AmpController::commandSent,
            m_face, [this](quint8) { m_face->pulseTx(); });

    // Face → amp: chassis button click runs the command.
    connect(m_face, &AmpFaceWidget::buttonPressed,
            this,   &MainWindow::onFaceButtonPressed);
    // Face cog → settings dialog.
    connect(m_face, &AmpFaceWidget::settingsRequested,
            this,   &MainWindow::onSettingsRequested);

    // Connection state drives the bottom-strip LED + label and the
    // status-bar message. Logs from amp / bridge / http all share the
    // status bar.
    connect(m_amp,    &AmpController::connectionChanged,
            this,     &MainWindow::onConnectionChanged);
    connect(m_amp,    &AmpController::logMessage,
            this,     &MainWindow::onLog);
    connect(m_bridge, &WsBridge::logMessage,
            this,     &MainWindow::onLog);
    connect(m_http,   &HttpServer::logMessage,
            this,     &MainWindow::onLog);

    onConnectionChanged(false);
}

void MainWindow::buildUi() {
    m_face = new AmpFaceWidget(this);
    setCentralWidget(m_face);
    statusBar()->showMessage(QStringLiteral("Disconnected"));
    // Default size matches the chassis aspect ratio. The chassis itself
    // reflows below 600 px wide so dragging smaller is safe.
    resize(940, 520);
}

void MainWindow::buildMenus() {
    // Only Help is left — connection settings have moved to the chassis
    // cog button and the calibration overlay died with the photo UI.
    auto* help = menuBar()->addMenu(QStringLiteral("&Help"));
    auto* about = help->addAction(QStringLiteral("&About…"));
    connect(about, &QAction::triggered, this, &MainWindow::onAboutClicked);
}

void MainWindow::onAboutClicked() {
    AboutDialog dlg(this);
    dlg.exec();
}

void MainWindow::onSettingsRequested() {
    if (!m_settings) {
        m_settings = new SettingsDialog(m_amp, m_bridge, m_http, this);
    }
    m_settings->show();
    m_settings->raise();
    m_settings->activateWindow();
}

void MainWindow::onFaceButtonPressed(const QString& token) {
    // POWER OFF gets a modal confirm so a stray click can't shut the amp
    // down. Matches the confirm() dialog the web UI pops up.
    if (token == QLatin1String("off")) {
        const auto reply = QMessageBox::question(
            this,
            QStringLiteral("Shut the amplifier down?"),
            QStringLiteral(
                "This sends POWER OFF (0x0C).\n\n"
                "You'll need to walk over and press the physical power "
                "button to bring it back up."),
            QMessageBox::Ok | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (reply != QMessageBox::Ok) { return; }
    }
    m_amp->handleCommandToken(token);
}

void MainWindow::onConnectionChanged(bool connected) {
    if (!connected) {
        statusBar()->showMessage(QStringLiteral("Disconnected"));
        const QString err = m_amp->lastError();
        const QString details = (!err.isEmpty() && !m_amp->isStopped())
            ? err
            : QStringLiteral("Disconnected — click ⚙ to configure");
        m_face->setConnectionState(false, details);
        return;
    }
    const quint16 httpPort = m_http->serverPort();
    QString msg = QStringLiteral("Serial connected — WS :%1")
                      .arg(m_bridge->serverPort());
    if (httpPort != 0) {
        msg += QStringLiteral(", HTTP :%1").arg(httpPort);
    }
    statusBar()->showMessage(msg);
    m_face->setConnectionState(true, QStringLiteral("%1 @ %2")
                                          .arg(m_amp->portName())
                                          .arg(m_amp->baudRate()));
}

void MainWindow::onLog(const QString& message) {
    statusBar()->showMessage(message, 5000);
}
