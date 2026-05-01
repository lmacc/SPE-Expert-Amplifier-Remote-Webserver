#pragma once

#include <QObject>
#include <QString>

// Persists the runtime configuration the user adjusts from the web UI —
// currently the serial device name and baud rate — so the daemon reopens
// the same port across restarts / systemd respawns on the Pi without
// anyone having to SSH in.
//
// Stored as JSON at QStandardPaths::AppConfigLocation/config.json, e.g.
//   Linux:   ~/.config/spe-remote/config.json
//   Windows: %APPDATA%/spe-remote/config.json
class ConfigStore : public QObject {
    Q_OBJECT
public:
    explicit ConfigStore(QObject* parent = nullptr);

    // File path used for load/save. Resolved lazily from QStandardPaths
    // unless setFilePath() is called first (used by tests).
    QString filePath() const;
    void setFilePath(const QString& path);

    void load();
    bool save() const;

    QString portName() const     { return m_portName; }
    void setPortName(const QString& p);

    qint32 baudRate() const      { return m_baudRate; }
    void setBaudRate(qint32 b);

    quint16 wsPort() const       { return m_wsPort; }
    void setWsPort(quint16 p);

    quint16 httpPort() const     { return m_httpPort; }
    void setHttpPort(quint16 p);

signals:
    void changed();

private:
    mutable QString m_filePath;
    QString m_portName;
    qint32 m_baudRate = 115200;
    quint16 m_wsPort = 8888;
    quint16 m_httpPort = 8080;
};
