#pragma once

#include <QObject>
#include <QString>

class AmpController;
class ConfigStore;
class QHttpServer;
class QTcpServer;

// Static-file HTTP server with an optional REST API.
//
// Static: serves the bundled web/ folder (web_resources.qrc) so iPad /
// Android / any browser can reach the UI at http://host:8080/ without
// needing a separate Apache or lighttpd.
//
// REST (enabled by calling attachControls()): lets the web UI enumerate
// serial ports, change the active port, and see connection state without
// anyone having to SSH in.
//
//   GET  /api/ports
//   GET  /api/config
//   POST /api/config        body: {"port":"COM4","baud":115200}
//   POST /api/connect
//   POST /api/disconnect
class HttpServer : public QObject {
    Q_OBJECT
public:
    explicit HttpServer(QObject* parent = nullptr);
    ~HttpServer() override;

    // Web files come from either a Qt resource prefix (default ":/web") or
    // a real filesystem directory (set via setFileSystemRoot).
    void setFileSystemRoot(const QString& path);

    // Enable the REST API. If not called, only static files are served.
    // Both pointers must outlive this object.
    void attachControls(AmpController* amp, ConfigStore* cfg);

    bool listen(quint16 port = 8080);
    void close();
    quint16 serverPort() const;

signals:
    void logMessage(const QString& message);

private:
    QByteArray loadAsset(const QString& relPath, QByteArray& contentType) const;
    void registerRestRoutes();
    void registerStaticRoutes();

    QHttpServer* m_server = nullptr;
    QTcpServer* m_tcpServer = nullptr;
    QString m_fsRoot;          // empty → serve from Qt resources at :/web

    AmpController* m_amp = nullptr;
    ConfigStore* m_cfg = nullptr;
    bool m_routesRegistered = false;
};
