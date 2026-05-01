#pragma once

#include <QByteArray>
#include <QObject>
#include <QSslConfiguration>
#include <QString>

class AmpController;
class ConfigStore;
class QHttpServer;
class QHttpServerRequest;
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

    // Require HTTP Basic auth on every request. Empty user disables.
    // hash is the stored password hash (Auth::hashPassword format).
    void enableAuth(const QString& user, const QByteArray& hash);

    // When true (default), peers on a private/loopback network skip the
    // auth check. False means auth is required from every IP. No-op if
    // auth isn't enabled.
    void setTrustLan(bool trust);

    // Run the listener with TLS. Empty config disables. Pass a default-
    // constructed QSslConfiguration to clear.
    void setSslConfiguration(const QSslConfiguration& cfg);

    bool listen(quint16 port = 8080);
    void close();
    quint16 serverPort() const;
    bool isSecure() const;

signals:
    void logMessage(const QString& message);

private:
    QByteArray loadAsset(const QString& relPath, QByteArray& contentType) const;
    void registerRestRoutes();
    void registerStaticRoutes();

    // Returns true if the request is allowed through. Returns false and
    // populates `out` with a 401 response if auth is enabled and the
    // request fails the check.
    bool authOk(const QHttpServerRequest& req) const;

    QHttpServer* m_server = nullptr;
    QTcpServer* m_tcpServer = nullptr;
    QString m_fsRoot;          // empty → serve from Qt resources at :/web

    AmpController* m_amp = nullptr;
    ConfigStore* m_cfg = nullptr;
    bool m_routesRegistered = false;

    // Auth state. authEnabled is implicit on m_authUser non-empty.
    QString m_authUser;
    QByteArray m_authHash;
    bool m_trustLan = true;

    // TLS state. Active iff non-default-constructed.
    QSslConfiguration m_sslConfig;
    bool m_sslEnabled = false;
};
