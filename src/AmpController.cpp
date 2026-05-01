#include "AmpController.h"

#include <QJsonObject>
#include <QLoggingCategory>
#include <QTimer>

namespace {
Q_LOGGING_CATEGORY(lcAmp, "spe.amp")

// Protocol frames per §3/§4 of the SPE Application Programmer's Guide.
// All commands share the 4-byte preamble 55 55 55 01 followed by a
// 2-byte opcode where both bytes are identical (the second byte is the
// mod-256 checksum of the single-byte payload, i.e. the byte itself).
constexpr char kPreamble[] = "\x55\x55\x55\x01";
// All values verified against Application Programmer's Guide Rev 1.1 §4.
constexpr char kOpRequest      = '\x90';  // STATUS
constexpr char kOpInput        = '\x01';
constexpr char kOpBandDown     = '\x02';
constexpr char kOpBandUp       = '\x03';
constexpr char kOpAntenna      = '\x04';
constexpr char kOpInductorDown = '\x05';  // L-  ATU inductor trim down
constexpr char kOpInductorUp   = '\x06';  // L+  ATU inductor trim up
constexpr char kOpCapacitorDown = '\x07'; // C-  ATU capacitor trim down
constexpr char kOpCapacitorUp   = '\x08'; // C+  ATU capacitor trim up
constexpr char kOpTune         = '\x09';
constexpr char kOpPowerOff     = '\x0A';  // SWITCH OFF — guide §4
constexpr char kOpGain         = '\x0B';  // POWER (cycles H/M/L)
constexpr char kOpDisplay      = '\x0C';  // DISPLAY backlight toggle
constexpr char kOpOperate      = '\x0D';
constexpr char kOpCat          = '\x0E';  // CAT / RIG-control toggle
constexpr char kOpArrowLeft    = '\x0F';
constexpr char kOpArrowRight   = '\x10';
constexpr char kOpSet          = '\x11';  // SET / menu key
constexpr char kOpBacklightOn  = '\x82';
constexpr char kOpBacklightOff = '\x83';

// ACK frames (amp → host, §3): 0xAA 0xAA 0xAA 0x01 <cmd> <cmd>, no CRLF.
// They share the 3-byte AA sync with STATUS frames but the 4th byte (0x01
// for ACK, 0x43 for STATUS) disambiguates.
constexpr quint8 kAckSync0 = 0xAA;
constexpr quint8 kAckCntByte = 0x01;
constexpr int kAckFrameSize = 6;

constexpr int kPollIntervalMs = 200;
constexpr int kReconnectIntervalMs = 2000;
constexpr int kExpectedFields = 22;

// DTR pulse used to emulate the front-panel power button.
//
//   kPowerOnSettleMs - briefly hold both modem-control lines low before
//                      the rising edge, so we have a clean known
//                      starting state regardless of whether the OS /
//                      FTDI driver auto-asserted DTR at port-open.
//                      Without this step the previous implementation
//                      could see DTR already high and never produce a
//                      rising edge at all, leaving the amp asleep.
//   kPowerOnPulseMs  - duration DTR is held high (the "press"). 750 ms
//                      is comfortably above the firmware's input
//                      filter and well below the multi-second hold
//                      the physical button uses for power-OFF (~3 s),
//                      so a single pulse only powers ON.
constexpr int kPowerOnSettleMs = 60;
constexpr int kPowerOnPulseMs  = 750;
// Synthetic opcode for the ackReceived/commandSent signals so the UI
// can pulse the TX LED on power-on the same way it does for real
// commands. 0xFF is reserved (not used by any documented opcode).
constexpr quint8 kSyntheticOpPowerOn = 0xFF;

QString decodeBand(QStringView code) {
    // Band codes are two-digit decimal strings ("00".."11").
    if (code == u"00") { return QStringLiteral("160m"); }
    if (code == u"01") { return QStringLiteral("80m");  }
    if (code == u"02") { return QStringLiteral("60m");  }
    if (code == u"03") { return QStringLiteral("40m");  }
    if (code == u"04") { return QStringLiteral("30m");  }
    if (code == u"05") { return QStringLiteral("20m");  }
    if (code == u"06") { return QStringLiteral("17m");  }
    if (code == u"07") { return QStringLiteral("15m");  }
    if (code == u"08") { return QStringLiteral("12m");  }
    if (code == u"09") { return QStringLiteral("10m");  }
    if (code == u"10") { return QStringLiteral("6m");   }
    if (code == u"11") { return QStringLiteral("4m");   }
    return {};
}

QString decodeWarning(QChar c) {
    switch (c.toLatin1()) {
        case 'M': return QStringLiteral("ALARM AMPLIFIER");
        case 'A': return QStringLiteral("NO SELECTED ANTENNA");
        case 'S': return QStringLiteral("SWR ANTENNA");
        case 'B': return QStringLiteral("NO VALID BAND");
        case 'P': return QStringLiteral("POWER LIMIT EXCEEDED");
        case 'O': return QStringLiteral("OVERHEATING");
        case 'Y': return QStringLiteral("ATU NOT AVAILABLE");
        case 'W': return QStringLiteral("TUNING WITH NO POWER");
        case 'K': return QStringLiteral("ATU BYPASSED");
        case 'R': return QStringLiteral("POWER SWITCH HELD BY REMOTE");
        case 'T': return QStringLiteral("COMBINER OVERHEATING");
        case 'C': return QStringLiteral("COMBINER FAULT");
        case 'N': return QStringLiteral(" ");
        default:  return {};
    }
}

QString decodeError(QChar c) {
    switch (c.toLatin1()) {
        case 'S': return QStringLiteral("SWR EXCEEDING LIMITS");
        case 'A': return QStringLiteral("AMPLIFIER PROTECTION");
        case 'D': return QStringLiteral("INPUT OVERDRIVING");
        case 'H': return QStringLiteral("EXCESS OVERHEATING");
        case 'C': return QStringLiteral("COMBINER FAULT");
        case 'N': return QStringLiteral(" ");
        default:  return {};
    }
}
}  // namespace

QJsonObject AmpStatus::toJson() const {
    // Original keys kept verbatim from server.py's json.dumps call so
    // existing web / ESP8266 clients see an identical wire format. New
    // keys (memory_bank, atu_mode, rx_antenna, pa_temp_lwr, pa_temp_cmb)
    // are additive — clients that don't know them simply ignore them.
    QJsonObject j;
    j.insert(QStringLiteral("op_status"),   opStatus);
    j.insert(QStringLiteral("tx_status"),   txStatus);
    j.insert(QStringLiteral("memory_bank"), memoryBank);
    j.insert(QStringLiteral("input"),       input);
    j.insert(QStringLiteral("band"),        band);
    j.insert(QStringLiteral("tx_antenna"),  txAntenna);
    j.insert(QStringLiteral("atu_mode"),    atuMode);
    j.insert(QStringLiteral("rx_antenna"),  rxAntenna);
    j.insert(QStringLiteral("p_level"),     pLevel);
    j.insert(QStringLiteral("p_out"),       pOut);
    j.insert(QStringLiteral("swr"),         swr);
    j.insert(QStringLiteral("voltage"),     voltage);
    j.insert(QStringLiteral("drain"),       drain);
    j.insert(QStringLiteral("pa_temp"),     paTemp);
    j.insert(QStringLiteral("pa_temp_lwr"), paTempLwr);
    j.insert(QStringLiteral("pa_temp_cmb"), paTempCmb);
    j.insert(QStringLiteral("warnings"),    warnings);
    j.insert(QStringLiteral("error"),       error);
    j.insert(QStringLiteral("aswr"),        aswr);
    return j;
}

AmpController::AmpController(QObject* parent)
    : QObject(parent)
    , m_pollTimer(new QTimer(this))
    , m_reconnectTimer(new QTimer(this)) {
    m_serial.setBaudRate(QSerialPort::Baud115200);
    m_serial.setDataBits(QSerialPort::Data8);
    m_serial.setParity(QSerialPort::NoParity);
    m_serial.setStopBits(QSerialPort::OneStop);
    m_serial.setFlowControl(QSerialPort::NoFlowControl);

    connect(&m_serial, &QSerialPort::readyRead,
            this, &AmpController::onReadyRead);
    connect(&m_serial, &QSerialPort::errorOccurred,
            this, &AmpController::onSerialError);

    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout,
            this, &AmpController::onPollTimeout);

    m_reconnectTimer->setSingleShot(true);
    m_reconnectTimer->setInterval(kReconnectIntervalMs);
    connect(m_reconnectTimer, &QTimer::timeout,
            this, &AmpController::onReconnectTimeout);
}

AmpController::~AmpController() {
    closePort();
}

void AmpController::setPortName(const QString& portName) {
    if (m_portName == portName) { return; }
    m_portName = portName;
    m_serial.setPortName(portName);
    if (m_serial.isOpen()) {
        closePort();
        openPort();
    }
}

void AmpController::setBaudRate(qint32 baud) {
    m_baudRate = baud;
    m_serial.setBaudRate(baud);
}

void AmpController::start() {
    m_stopped = false;
    openPort();
}

void AmpController::stop() {
    // User-initiated close. Clear lastError so the settings page shows
    // "Disconnected" rather than leaving a stale "port busy" message,
    // and keep m_stopped=true so the UI distinguishes this from a
    // connect-in-progress state.
    m_stopped = true;
    m_lastError.clear();
    m_pollTimer->stop();
    m_reconnectTimer->stop();
    closePort();
}

void AmpController::openPort() {
    if (m_portName.isEmpty()) {
        m_lastError = QStringLiteral("No serial port configured");
        emit logMessage(m_lastError);
        // No port name → don't schedule reconnect; we'd just re-error in 2s.
        // User will set a port via the settings page and call start() again.
        return;
    }
    m_serial.setPortName(m_portName);
    m_serial.setBaudRate(m_baudRate);
    if (!m_serial.open(QIODevice::ReadWrite)) {
        m_lastError = QStringLiteral("%1: %2")
                          .arg(m_portName, m_serial.errorString());
        emit logMessage(QStringLiteral("Open failed — ") + m_lastError);
        // Keep the reconnect loop: if the port becomes free later (another
        // app releases it, or it comes back after a USB replug) we'll
        // recover automatically. lastError() stays populated so the web UI
        // can show "Port busy, retrying…".
        m_reconnectTimer->start();
        return;
    }
    m_lastError.clear();
    m_serial.clear(QSerialPort::AllDirections);
    m_rxBuffer.clear();
    m_awaitingReply = false;
    emit connectionChanged(true);
    emit logMessage(QStringLiteral("Opened %1").arg(m_portName));
    sendRequest();
    m_pollTimer->start();
}

void AmpController::closePort() {
    if (m_serial.isOpen()) {
        m_serial.close();
        emit connectionChanged(false);
        emit logMessage(QStringLiteral("Closed %1").arg(m_portName));
    }
}

void AmpController::sendRequest() {
    if (!m_serial.isOpen()) { return; }
    QByteArray frame(kPreamble, 4);
    frame.append(kOpRequest);
    frame.append(kOpRequest);
    m_serial.write(frame);
    m_awaitingReply = true;
}

void AmpController::sendCommand(char opcode) {
    if (!m_serial.isOpen()) { return; }
    QByteArray frame(kPreamble, 4);
    frame.append(opcode);
    frame.append(opcode);
    m_serial.write(frame);
    // Pulse the front-panel TX LED. Note this is only fired for
    // user-initiated commands; routine status polling (sendRequest)
    // intentionally does NOT emit, otherwise the LED would be lit
    // continuously and become useless as a feedback signal.
    emit commandSent(static_cast<quint8>(opcode));
    // The original server also re-issues a status request after every
    // command so the UI reflects the new state on the next cycle.
    sendRequest();
}

void AmpController::toggleOperate()  { sendCommand(kOpOperate); }
void AmpController::toggleAntenna()  { sendCommand(kOpAntenna); }
void AmpController::toggleInput()    { sendCommand(kOpInput);   }
void AmpController::toggleTune()     { sendCommand(kOpTune);    }
void AmpController::toggleGain()     { sendCommand(kOpGain);    }
void AmpController::toggleCat()      { sendCommand(kOpCat);     }
void AmpController::bandDown()       { sendCommand(kOpBandDown); }
void AmpController::bandUp()         { sendCommand(kOpBandUp);   }
void AmpController::inductorDown()   { sendCommand(kOpInductorDown); }
void AmpController::inductorUp()     { sendCommand(kOpInductorUp);   }
void AmpController::capacitorDown()  { sendCommand(kOpCapacitorDown); }
void AmpController::capacitorUp()    { sendCommand(kOpCapacitorUp);   }
void AmpController::setKey()         { sendCommand(kOpSet);     }
void AmpController::powerOff()       { sendCommand(kOpPowerOff); }

void AmpController::powerOn() {
    // No documented opcode exists for power-on (the amp's CPU is asleep
    // and ignores the serial protocol when off). The community workaround
    // is to pulse DTR, which on SPE Expert hardware is wired to the
    // front-panel power button via a transistor.
    //
    // The pulse drives both edges explicitly rather than just toggling
    // from the resting state — earlier versions of this code took the
    // resting DTR level as the baseline and inverted it, but on Windows
    // the FTDI driver auto-asserts DTR at port-open, so "invert and
    // restore" produced a falling-then-rising edge instead of the
    // rising-then-falling edge the amp expects. With the explicit
    // sequence we always get the right edge regardless of platform:
    //
    //   1. Force DTR + RTS low for kPowerOnSettleMs (known idle state).
    //   2. Raise DTR — RISING EDGE = "button press".
    //   3. Hold for kPowerOnPulseMs.
    //   4. Lower DTR — FALLING EDGE = "button release".
    //
    // RTS is forced low throughout because some installations wire it
    // alongside DTR; leaving it floating-high on a fresh port-open has
    // been seen to interfere with the wake on certain firmware revs.
    if (!m_serial.isOpen()) {
        emit logMessage(QStringLiteral(
            "Power-on requested, but serial port is not open. "
            "Configure the port via ⚙ Settings first."));
        return;
    }

    // Phase 1: drive both modem-control lines low.
    m_serial.setRequestToSend(false);
    if (!m_serial.setDataTerminalReady(false)) {
        emit logMessage(QStringLiteral(
            "Power-on: setDataTerminalReady(false) failed — "
            "the serial driver may not support modem-line control."));
        return;
    }
    emit logMessage(QStringLiteral(
        "Power-on: starting DTR pulse (settle %1 ms, hold %2 ms)")
            .arg(kPowerOnSettleMs).arg(kPowerOnPulseMs));
    emit commandSent(kSyntheticOpPowerOn);

    // Phase 2: rising edge — assert DTR ("press").
    QTimer::singleShot(kPowerOnSettleMs, this, [this]() {
        if (!m_serial.isOpen()) { return; }
        m_serial.setDataTerminalReady(true);
    });

    // Phase 3: falling edge — deassert DTR ("release") after the hold.
    QTimer::singleShot(kPowerOnSettleMs + kPowerOnPulseMs, this, [this]() {
        if (!m_serial.isOpen()) { return; }
        m_serial.setDataTerminalReady(false);
        emit logMessage(QStringLiteral("Power-on: DTR pulse complete"));
        emit ackReceived(kSyntheticOpPowerOn, QStringLiteral("on"));
    });
}

void AmpController::toggleDisplay() { sendCommand(kOpDisplay);  }
void AmpController::arrowLeft()     { sendCommand(kOpArrowLeft); }
void AmpController::arrowRight()    { sendCommand(kOpArrowRight); }
void AmpController::backlightOn()   { sendCommand(kOpBacklightOn); }
void AmpController::backlightOff()  { sendCommand(kOpBacklightOff); }

void AmpController::handleCommandToken(const QString& token) {
    // Original five — keep exact token strings for ESP8266 + old web
    // client compatibility. Front-panel UI shows "PWR" instead of "GAIN"
    // but the wire token stays "gain" so old clients keep working.
    if      (token == QLatin1String("oper"))    { toggleOperate(); }
    else if (token == QLatin1String("antenna")) { toggleAntenna(); }
    else if (token == QLatin1String("input"))   { toggleInput();   }
    else if (token == QLatin1String("tune"))    { toggleTune();    }
    else if (token == QLatin1String("gain"))    { toggleGain();    }
    else if (token == QLatin1String("pwr"))     { toggleGain();    }  // alias for new UI
    else if (token == QLatin1String("cat"))     { toggleCat();     }  // 0x0E per guide §4
    // Extras from guide §4.
    else if (token == QLatin1String("band-"))         { bandDown();      }
    else if (token == QLatin1String("band+"))         { bandUp();        }
    else if (token == QLatin1String("l-"))            { inductorDown();  }
    else if (token == QLatin1String("l+"))            { inductorUp();    }
    else if (token == QLatin1String("c-"))            { capacitorDown(); }
    else if (token == QLatin1String("c+"))            { capacitorUp();   }
    else if (token == QLatin1String("set"))           { setKey();        }
    else if (token == QLatin1String("off"))           { powerOff();      }
    else if (token == QLatin1String("on"))            { powerOn();       }
    else if (token == QLatin1String("display"))       { toggleDisplay(); }
    else if (token == QLatin1String("left"))          { arrowLeft();     }
    else if (token == QLatin1String("right"))         { arrowRight();    }
    else if (token == QLatin1String("backlight_on"))  { backlightOn();   }
    else if (token == QLatin1String("backlight_off")) { backlightOff();  }
}

// Map an opcode (the byte the amp echoes back in its ACK) to the WS token
// our clients use, so the LED pulse can show *which* button was acked.
static QString opcodeToToken(quint8 op) {
    // Mapping mirrors guide §4. Keep in sync with kOp* constants above.
    switch (op) {
        case 0x01: return QStringLiteral("input");
        case 0x02: return QStringLiteral("band-");
        case 0x03: return QStringLiteral("band+");
        case 0x04: return QStringLiteral("antenna");
        case 0x05: return QStringLiteral("l-");
        case 0x06: return QStringLiteral("l+");
        case 0x07: return QStringLiteral("c-");
        case 0x08: return QStringLiteral("c+");
        case 0x09: return QStringLiteral("tune");
        case 0x0A: return QStringLiteral("off");
        case 0x0B: return QStringLiteral("gain");        // shown as PWR
        case 0x0C: return QStringLiteral("display");
        case 0x0D: return QStringLiteral("oper");
        case 0x0E: return QStringLiteral("cat");
        case 0x0F: return QStringLiteral("left");
        case 0x10: return QStringLiteral("right");
        case 0x11: return QStringLiteral("set");
        case 0x82: return QStringLiteral("backlight_on");
        case 0x83: return QStringLiteral("backlight_off");
        case 0xFF: return QStringLiteral("on");          // synthetic, DTR pulse
        default:   return {};
    }
}

void AmpController::onReadyRead() {
    m_rxBuffer.append(m_serial.readAll());
    // ACK frames (6 bytes, no CRLF) arrive ahead of the STATUS frame that
    // our sendCommand()'s trailing status request solicits. Consume them
    // from the buffer head first; whatever's left is the ASCII status line.
    extractAckFrames();
    // Status frames are CR-LF terminated ASCII lines.
    for (;;) {
        const int eol = m_rxBuffer.indexOf('\n');
        if (eol < 0) { break; }
        QByteArray line = m_rxBuffer.left(eol + 1);
        m_rxBuffer.remove(0, eol + 1);
        parseLine(line);
    }
    // Guard against runaway buffering if the amp is sending garbage.
    if (m_rxBuffer.size() > 4096) {
        m_rxBuffer.clear();
    }
}

void AmpController::extractAckFrames() {
    // Only peel ACKs off the HEAD of the buffer. ACK bytes embedded mid-
    // frame are almost certainly random checksum bytes inside a STATUS
    // frame and must not be eaten.
    while (m_rxBuffer.size() >= kAckFrameSize
           && static_cast<quint8>(m_rxBuffer[0]) == kAckSync0
           && static_cast<quint8>(m_rxBuffer[1]) == kAckSync0
           && static_cast<quint8>(m_rxBuffer[2]) == kAckSync0
           && static_cast<quint8>(m_rxBuffer[3]) == kAckCntByte) {
        const quint8 cmd = static_cast<quint8>(m_rxBuffer[4]);
        const quint8 chk = static_cast<quint8>(m_rxBuffer[5]);
        // CHK of a single data byte == the byte itself; mismatch means
        // framing slipped and we'd better leave the bytes for the line
        // parser to re-sync against.
        if (cmd != chk) { break; }
        m_rxBuffer.remove(0, kAckFrameSize);
        emit ackReceived(cmd, opcodeToToken(cmd));
    }
}

void AmpController::parseLine(const QByteArray& line) {
    const QList<QByteArray> parts = line.split(',');
    if (parts.size() != kExpectedFields) { return; }
    if (parts.last() != "\r\n") { return; }

    AmpStatus s;
    if      (parts[2] == "O") { s.opStatus = QStringLiteral("Oper"); }
    else if (parts[2] == "S") { s.opStatus = QStringLiteral("Stby"); }
    else                      { return; }

    if      (parts[3] == "R") { s.txStatus = QStringLiteral("RX"); }
    else if (parts[3] == "T") { s.txStatus = QStringLiteral("TX"); }
    else                      { return; }

    s.memoryBank = QString::fromLatin1(parts[4]);
    s.input      = QString::fromLatin1(parts[5]);
    s.band       = decodeBand(QString::fromLatin1(parts[6]));
    // Field 7 packs antenna number + ATU mode in two bytes per guide §5
    // ("1a" = antenna 1, ATU enabled; "0t" = tunable; "0b" = bypassed).
    const QByteArray antField = parts[7];
    if (antField.size() >= 2) {
        s.txAntenna = QString::fromLatin1(antField.left(1));
        s.atuMode   = QString::fromLatin1(antField.mid(1, 1));
    } else {
        s.txAntenna = QString::fromLatin1(antField);
    }
    s.rxAntenna = QString::fromLatin1(parts[8]);
    s.pLevel    = QString::fromLatin1(parts[9]);
    s.pOut      = QString::fromLatin1(parts[10]);
    s.swr       = QString::fromLatin1(parts[11]);
    s.aswr      = QString::fromLatin1(parts[12]);
    s.voltage   = QString::fromLatin1(parts[13]);
    s.drain     = QString::fromLatin1(parts[14]);
    s.paTemp    = QString::fromLatin1(parts[15]);
    s.paTempLwr = QString::fromLatin1(parts[16]);
    s.paTempCmb = QString::fromLatin1(parts[17]);

    const QString warnField = QString::fromLatin1(parts[18]);
    const QString errField  = QString::fromLatin1(parts[19]);
    s.warnings = warnField.isEmpty() ? QString() : decodeWarning(warnField[0]);
    s.error    = errField.isEmpty()  ? QString() : decodeError(errField[0]);

    m_awaitingReply = false;
    emit statusUpdated(s);
}

void AmpController::onPollTimeout() {
    // Amplifier replies to each request with exactly one status line.
    // If a reply is outstanding we skip this tick; otherwise we poke it.
    if (!m_serial.isOpen()) { return; }
    if (m_awaitingReply) { return; }
    sendRequest();
}

void AmpController::onSerialError(QSerialPort::SerialPortError error) {
    if (error == QSerialPort::NoError) { return; }
    m_lastError = m_serial.errorString();
    emit logMessage(QStringLiteral("Serial error: %1").arg(m_lastError));
    if (error == QSerialPort::ResourceError || error == QSerialPort::PermissionError
        || error == QSerialPort::DeviceNotFoundError) {
        closePort();
        m_reconnectTimer->start();
    }
}

void AmpController::onReconnectTimeout() {
    openPort();
}
