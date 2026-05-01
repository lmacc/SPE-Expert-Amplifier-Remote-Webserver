#include "SettingsDialog.h"

#include "AmpController.h"
#include "HttpServer.h"
#include "WsBridge.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSerialPortInfo>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
// Common SPE baud rates plus a few extras the user might pick when
// debugging with a different cable. The combo is editable so anything
// the QSerialPort backend accepts can be typed in.
const int kBaudRates[] = { 9600, 19200, 38400, 57600, 115200, 230400 };
}

SettingsDialog::SettingsDialog(AmpController* amp, WsBridge* bridge,
                               HttpServer* http, QWidget* parent)
    : QDialog(parent), m_amp(amp), m_bridge(bridge), m_http(http) {
    setWindowTitle(QStringLiteral("Connection settings"));
    setModal(true);
    buildUi();
    refreshPortList();
    syncFromAmp();
    onConnectionChanged(m_amp->isConnected());

    connect(m_amp, &AmpController::connectionChanged,
            this, &SettingsDialog::onConnectionChanged);
}

void SettingsDialog::buildUi() {
    auto* form = new QFormLayout;

    // --- Serial port row: combo + refresh button -----------------------
    auto* portRow = new QHBoxLayout;
    m_portCombo = new QComboBox;
    m_portCombo->setEditable(true);
    m_portCombo->setMinimumWidth(180);
    m_portCombo->setToolTip(QStringLiteral(
        "Serial device the amplifier is plugged into. Use ↻ to rescan."));
    auto* refreshBtn = new QPushButton(QStringLiteral("↻"));
    refreshBtn->setFixedWidth(28);
    refreshBtn->setToolTip(QStringLiteral("Rescan serial ports"));
    connect(refreshBtn, &QPushButton::clicked,
            this, &SettingsDialog::refreshPortList);
    portRow->addWidget(m_portCombo, 1);
    portRow->addWidget(refreshBtn, 0);
    form->addRow(QStringLiteral("Serial port:"), portRow);

    // --- Baud rate combo (editable) ------------------------------------
    m_baudCombo = new QComboBox;
    m_baudCombo->setEditable(true);
    for (int b : kBaudRates) {
        m_baudCombo->addItem(QString::number(b), b);
    }
    form->addRow(QStringLiteral("Baud rate:"), m_baudCombo);

    // --- WebSocket port -------------------------------------------------
    m_wsPortSpin = new QSpinBox;
    m_wsPortSpin->setRange(1, 65535);
    m_wsPortSpin->setValue(8888);
    m_wsPortSpin->setToolTip(QStringLiteral(
        "Port the WebSocket server binds to. The web UI connects here."));
    form->addRow(QStringLiteral("WebSocket port:"), m_wsPortSpin);

    // --- HTTP port (0 = disable embedded web UI) -----------------------
    m_httpPortSpin = new QSpinBox;
    m_httpPortSpin->setRange(0, 65535);
    m_httpPortSpin->setValue(8080);
    m_httpPortSpin->setToolTip(QStringLiteral(
        "Embedded web-UI port. 0 disables it (desktop-only mode)."));
    form->addRow(QStringLiteral("HTTP port:"), m_httpPortSpin);

    // --- Status label + Connect button ---------------------------------
    m_statusLabel = new QLabel(QStringLiteral("Disconnected"));
    m_statusLabel->setStyleSheet(QStringLiteral("color: #9aa0ac;"));
    m_connectBtn = new QPushButton(QStringLiteral("Connect"));
    m_connectBtn->setDefault(true);
    connect(m_connectBtn, &QPushButton::clicked,
            this, &SettingsDialog::onConnectClicked);

    auto* connectRow = new QHBoxLayout;
    connectRow->addWidget(m_statusLabel, 1);
    connectRow->addWidget(m_connectBtn, 0);

    // --- Close button (the only thing we need from the standard box) ---
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::accept);

    auto* outer = new QVBoxLayout(this);
    outer->addLayout(form);
    outer->addSpacing(6);
    outer->addLayout(connectRow);
    outer->addSpacing(6);
    outer->addWidget(box);
    setLayout(outer);
    setMinimumWidth(360);
}

void SettingsDialog::refreshPortList() {
    const QString current = m_portCombo->currentText();
    m_portCombo->clear();
    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
        const QString label = info.description().isEmpty()
            ? info.portName()
            : QStringLiteral("%1 — %2").arg(info.portName(), info.description());
        m_portCombo->addItem(label, info.portName());
    }
    if (!current.isEmpty()) {
        // Match against the userData (port name) first, then the visible text.
        for (int i = 0; i < m_portCombo->count(); ++i) {
            if (m_portCombo->itemData(i).toString() == current ||
                m_portCombo->itemText(i) == current) {
                m_portCombo->setCurrentIndex(i);
                return;
            }
        }
        m_portCombo->setEditText(current);
    }
}

void SettingsDialog::syncFromAmp() {
    // Pre-fill the form with whatever the controller is currently using
    // so the dialog reflects state, not defaults.
    const QString port = m_amp->portName();
    if (!port.isEmpty()) {
        for (int i = 0; i < m_portCombo->count(); ++i) {
            if (m_portCombo->itemData(i).toString() == port) {
                m_portCombo->setCurrentIndex(i);
                break;
            }
        }
        if (m_portCombo->currentData().toString() != port) {
            m_portCombo->setEditText(port);
        }
    }

    const qint32 baud = m_amp->baudRate();
    const int bidx = m_baudCombo->findData(baud);
    if (bidx >= 0) {
        m_baudCombo->setCurrentIndex(bidx);
    } else {
        m_baudCombo->setEditText(QString::number(baud));
    }

    const quint16 wsp = m_bridge->serverPort();
    if (wsp != 0) { m_wsPortSpin->setValue(wsp); }

    const quint16 hp = m_http->serverPort();
    if (hp != 0) { m_httpPortSpin->setValue(hp); }
}

void SettingsDialog::onConnectClicked() {
    if (m_amp->isConnected()) {
        m_amp->stop();
        m_bridge->close();
        m_http->close();
        return;
    }

    // Resolve the user's selected port: prefer userData (real port name),
    // fall back to the visible text for hand-typed values.
    QString port = m_portCombo->currentData().toString();
    if (port.isEmpty()) { port = m_portCombo->currentText().trimmed(); }
    // Strip the " — description" suffix if the user picked from the combo
    // but data() came back empty for some reason.
    const int dash = port.indexOf(QStringLiteral(" — "));
    if (dash > 0) { port = port.left(dash); }
    if (port.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("Select a serial port first."));
        return;
    }

    bool baudOk = false;
    const int baud = m_baudCombo->currentText().toInt(&baudOk);
    if (baudOk && baud > 0) { m_amp->setBaudRate(baud); }

    m_amp->setPortName(port);
    m_amp->start();
    m_bridge->listen(static_cast<quint16>(m_wsPortSpin->value()));
    const int httpPort = m_httpPortSpin->value();
    if (httpPort > 0) {
        m_http->listen(static_cast<quint16>(httpPort));
    } else {
        m_http->close();
    }
}

void SettingsDialog::onConnectionChanged(bool connected) {
    if (connected) {
        m_connectBtn->setText(QStringLiteral("Disconnect"));
        m_statusLabel->setText(QStringLiteral("Connected — %1 @ %2")
                                   .arg(m_amp->portName())
                                   .arg(m_amp->baudRate()));
        m_statusLabel->setStyleSheet(QStringLiteral("color: #4ecc7b;"));
    } else {
        m_connectBtn->setText(QStringLiteral("Connect"));
        const QString err = m_amp->lastError();
        if (!err.isEmpty() && !m_amp->isStopped()) {
            m_statusLabel->setText(err);
            m_statusLabel->setStyleSheet(QStringLiteral("color: #e66060;"));
        } else {
            m_statusLabel->setText(QStringLiteral("Disconnected"));
            m_statusLabel->setStyleSheet(QStringLiteral("color: #9aa0ac;"));
        }
    }
}
