#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QtSerialPort/QSerialPort>

class QTimer;

struct AmpStatus {
    QString opStatus;    // "Oper" / "Stby"
    QString txStatus;    // "RX" / "TX"
    QString memoryBank;  // data[4]  "A"/"B" on 1.3K-FA, "x" on 2K-FA
    QString input;       // data[5]
    QString band;        // "160m" .. "4m"
    QString txAntenna;   // data[7]  first byte (antenna number)
    QString atuMode;     // data[7]  second byte: "t"=tunable, "b"=bypassed,
                         //          "a"=ATU enabled (per guide §5)
    QString rxAntenna;   // data[8]  "0r" if no separate RX antenna
    QString pLevel;      // data[9]
    QString pOut;        // data[10] watts
    QString swr;         // data[11]
    QString aswr;        // data[12]
    QString voltage;     // data[13]
    QString drain;       // data[14]
    QString paTemp;      // data[15] deg C — upper heatsink on 2K-FA
    QString paTempLwr;   // data[16] lower heatsink (2K-FA only; 1.3K-FA → 000)
    QString paTempCmb;   // data[17] combiner    (2K-FA only; 1.3K-FA → 000)
    QString warnings;
    QString error;

    QJsonObject toJson() const;
    bool isValid() const { return !opStatus.isEmpty(); }
};

class AmpController : public QObject {
    Q_OBJECT
public:
    explicit AmpController(QObject* parent = nullptr);
    ~AmpController() override;

    void setPortName(const QString& portName);
    void setBaudRate(qint32 baud);
    QString portName() const { return m_portName; }
    qint32 baudRate() const  { return m_baudRate; }

    // Introspection for the REST API / settings page.
    bool isConnected() const { return m_serial.isOpen(); }
    QString lastError() const { return m_lastError; }
    // True when the user asked us to stop (via Disconnect button / stop()).
    // Distinguishes "intentionally closed" from "trying to open / retrying"
    // so the web UI can show "Disconnected" vs "Connecting…".
    bool isStopped() const { return m_stopped; }

public slots:
    void start();
    void stop();

    // UI / websocket → amplifier button presses.
    // The first five WS tokens were empirically validated against live amps.
    // The rest are documented in the SPE Application Programmer's Guide §4 —
    // use with care, the guide's table layout is ambiguous in the PDF.
    void toggleOperate();
    void toggleAntenna();
    void toggleInput();
    void toggleTune();
    void toggleGain();        // Cycles PWR level H/M/L (mfr label = "POWER").
    void toggleCat();         // Toggles CAT/RIG-control mode on the amp.
    void bandDown();
    void bandUp();
    void inductorDown();      // ATU L- trim
    void inductorUp();        // ATU L+ trim
    void capacitorDown();     // ATU C- trim
    void capacitorUp();       // ATU C+ trim
    void setKey();            // SET menu key (enters / accepts setup screens)
    void powerOff();          // WARNING: shuts the amplifier down.
    // Pulse DTR to emulate the front-panel power button (community
    // workaround — the SPE Application Programmer's Guide does not
    // document a serial opcode for power-on). Requires the serial port
    // to already be open; the FTDI USB chip on the amp is bus-powered
    // so the COM port stays enumerable even with the amp's mains off.
    void powerOn();
    void toggleDisplay();     // Display backlight toggle.
    void arrowLeft();
    void arrowRight();
    void backlightOn();
    void backlightOff();

    // Accepts the same string tokens the Python server accepted on the
    // websocket ("oper", "antenna", "input", "tune", "gain") plus the new
    // ones listed above so the existing web client and ESP8266 device keep
    // working unchanged while newer clients can reach the extra keys.
    void handleCommandToken(const QString& token);

signals:
    void statusUpdated(const AmpStatus& status);
    void connectionChanged(bool connected);
    void logMessage(const QString& message);
    // Emitted when the amp echoes a command back in its ACK frame
    // (0xAA 0xAA 0xAA 0x01 <cmd> <cmd>). `token` is the matching WS string
    // ("oper", "antenna", …) when we can map it; empty for unknown opcodes.
    void ackReceived(quint8 opcode, const QString& token);

    // Emitted whenever sendCommand() actually wrote bytes to the serial
    // port — i.e. a user-initiated command, NOT a routine status poll.
    // The UI uses this to pulse the front-panel TX LED. `opcode` is the
    // single-byte command we sent (0x01..0x83 etc.).
    void commandSent(quint8 opcode);

private slots:
    void onReadyRead();
    void onSerialError(QSerialPort::SerialPortError error);
    void onPollTimeout();
    void onReconnectTimeout();

private:
    void openPort();
    void closePort();
    void sendRequest();
    void sendCommand(char opcode);
    void parseLine(const QByteArray& line);
    void extractAckFrames();   // consume any 6-byte ACK frames at buffer head

    QSerialPort m_serial;
    QTimer* m_pollTimer = nullptr;
    QTimer* m_reconnectTimer = nullptr;
    QByteArray m_rxBuffer;
    QString m_portName;
    QString m_lastError;
    qint32 m_baudRate = 115200;
    bool m_awaitingReply = false;
    bool m_stopped = true;   // starts idle; flipped by start()/stop()
};
