#include "HttpServer.h"

#include "AmpController.h"
#include "Auth.h"
#include "ConfigStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
#  include <QHttpHeaders>
#endif
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSerialPortInfo>
#include <QSslServer>
#include <QTcpServer>

namespace {
QByteArray mimeFor(const QString& path) {
    if (path.endsWith(QLatin1String(".html"), Qt::CaseInsensitive)) {
        return "text/html; charset=utf-8";
    }
    if (path.endsWith(QLatin1String(".css"), Qt::CaseInsensitive)) {
        return "text/css; charset=utf-8";
    }
    if (path.endsWith(QLatin1String(".js"), Qt::CaseInsensitive)) {
        return "application/javascript; charset=utf-8";
    }
    if (path.endsWith(QLatin1String(".png"), Qt::CaseInsensitive)) {
        return "image/png";
    }
    if (path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)) {
        return "image/svg+xml";
    }
    if (path.endsWith(QLatin1String(".ico"), Qt::CaseInsensitive)) {
        return "image/x-icon";
    }
    return "application/octet-stream";
}

// Reject any request path containing .. so it can't escape the web root.
bool isSafeRelativePath(const QString& path) {
    if (path.contains(QLatin1String(".."))) { return false; }
    if (path.startsWith(QLatin1Char('/')))  { return false; }
    return true;
}

// Qt 6.7 changed QHttpServerRequest::headers() return type from
// QList<std::pair<QByteArray, QByteArray>> to QHttpHeaders. Helper hides
// the difference.
QByteArray headerValue(const QHttpServerRequest& req, const char* name) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    return req.headers().value(QAnyStringView(QLatin1String(name))).toByteArray();
#else
    const auto headers = req.headers();
    for (const auto& kv : headers) {
        if (kv.first.compare(name, Qt::CaseInsensitive) == 0) {
            return kv.second;
        }
    }
    return {};
#endif
}

QHttpServerResponse unauthorized() {
    // Qt 6.7 reworked the headers API: addHeader/setHeader were replaced
    // by setHeaders(QHttpHeaders). Build the response the right way for
    // whichever Qt this is.
    QHttpServerResponse rsp("text/plain",
                            QByteArrayLiteral("401 Unauthorized\n"),
                            QHttpServerResponse::StatusCode::Unauthorized);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    QHttpHeaders h;
    h.append(QHttpHeaders::WellKnownHeader::WWWAuthenticate,
             QByteArrayLiteral("Basic realm=\"SPE Remote\""));
    rsp.setHeaders(std::move(h));
#else
    rsp.addHeader("WWW-Authenticate", "Basic realm=\"SPE Remote\"");
#endif
    return rsp;
}
}  // namespace

HttpServer::HttpServer(QObject* parent)
    : QObject(parent), m_server(new QHttpServer(this)) {
    // Routes are registered lazily in listen(), so that REST routes (added
    // by attachControls) always win over the wildcard static-file routes.
    // QHttpServer matches by registration order; if the wildcards go first,
    // /api/ports looks like the two-segment path "api/ports" and gets sent
    // to the asset loader, which returns 404 with an empty body — the web
    // UI then crashes decoding the empty body as JSON.
}

void HttpServer::registerStaticRoutes() {
    // Root path → index.html.
    m_server->route(QStringLiteral("/"),
                    [this](const QHttpServerRequest& req) {
        if (!authOk(req)) { return unauthorized(); }
        QByteArray ctype;
        QByteArray body = loadAsset(QStringLiteral("index.html"), ctype);
        if (body.isNull()) {
            return QHttpServerResponse(QHttpServerResponse::StatusCode::NotFound);
        }
        return QHttpServerResponse(ctype, body);
    });

    // QHttpServer's <arg> placeholder matches a single path segment, so we
    // register one route per depth. The original web/ folder uses up to two
    // segments (e.g. /css/app.css, /justgage/raphael-2.1.4.min.js); depth 3
    // is wired up for future assets.
    auto serve = [this](const QString& relPath) {
        if (!isSafeRelativePath(relPath)) {
            return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);
        }
        QByteArray ctype;
        QByteArray body = loadAsset(relPath, ctype);
        if (body.isNull()) {
            return QHttpServerResponse(QHttpServerResponse::StatusCode::NotFound);
        }
        return QHttpServerResponse(ctype, body);
    };

    m_server->route(QStringLiteral("/<arg>"),
                    [this, serve](const QString& a, const QHttpServerRequest& req) {
        if (!authOk(req)) { return unauthorized(); }
        return serve(a);
    });
    m_server->route(QStringLiteral("/<arg>/<arg>"),
                    [this, serve](const QString& a, const QString& b,
                                  const QHttpServerRequest& req) {
        if (!authOk(req)) { return unauthorized(); }
        return serve(a + QLatin1Char('/') + b);
    });
    m_server->route(QStringLiteral("/<arg>/<arg>/<arg>"),
                    [this, serve](const QString& a, const QString& b, const QString& c,
                                  const QHttpServerRequest& req) {
        if (!authOk(req)) { return unauthorized(); }
        return serve(a + QLatin1Char('/') + b + QLatin1Char('/') + c);
    });
}

HttpServer::~HttpServer() {
    close();
}

void HttpServer::setFileSystemRoot(const QString& path) {
    m_fsRoot = path;
}

void HttpServer::attachControls(AmpController* amp, ConfigStore* cfg) {
    m_amp = amp;
    m_cfg = cfg;
    // Route registration is deferred to listen() so REST handlers are
    // guaranteed to be matched before the wildcard static ones.
}

void HttpServer::enableAuth(const QString& user, const QByteArray& hash) {
    m_authUser = user;
    m_authHash = hash;
}

void HttpServer::setSslConfiguration(const QSslConfiguration& cfg) {
    m_sslConfig = cfg;
    m_sslEnabled = !cfg.localCertificate().isNull();
}

bool HttpServer::authOk(const QHttpServerRequest& req) const {
    if (m_authUser.isEmpty()) { return true; }
    return Auth::checkBasic(headerValue(req, "Authorization"),
                            m_authUser, m_authHash);
}

bool HttpServer::isSecure() const {
    return m_sslEnabled;
}

namespace {
// FTDI's USB Vendor ID — SPE Expert amplifiers ship with an FTDI USB-to-
// serial chip, so marking these as "likely SPE" lets the web UI auto-
// select the right port on a fresh Pi.
constexpr quint16 kFtdiVid = 0x0403;

QJsonObject portInfoToJson(const QSerialPortInfo& info) {
    QJsonObject o;
    o.insert(QStringLiteral("name"),         info.portName());
    o.insert(QStringLiteral("description"),  info.description());
    o.insert(QStringLiteral("manufacturer"), info.manufacturer());
    if (info.hasVendorIdentifier()) {
        o.insert(QStringLiteral("vid"), static_cast<int>(info.vendorIdentifier()));
    }
    if (info.hasProductIdentifier()) {
        o.insert(QStringLiteral("pid"), static_cast<int>(info.productIdentifier()));
    }
    o.insert(QStringLiteral("likelySpe"),
             info.hasVendorIdentifier() && info.vendorIdentifier() == kFtdiVid);
    return o;
}

QHttpServerResponse jsonResponse(const QJsonObject& o,
                                 QHttpServerResponse::StatusCode code
                                     = QHttpServerResponse::StatusCode::Ok) {
    QHttpServerResponse rsp("application/json",
                            QJsonDocument(o).toJson(QJsonDocument::Compact),
                            code);
    return rsp;
}

QHttpServerResponse jsonResponse(const QJsonArray& a) {
    return QHttpServerResponse("application/json",
                               QJsonDocument(a).toJson(QJsonDocument::Compact));
}
}  // namespace

void HttpServer::registerRestRoutes() {
    // GET /api/health — unauthenticated, used by uptime monitors and the
    // systemd ExecStartPost check. Reports daemon liveness only, no
    // configuration secrets.
    m_server->route(QStringLiteral("/api/health"), [this] {
        QJsonObject o;
        o.insert(QStringLiteral("ok"),        true);
        o.insert(QStringLiteral("connected"), m_amp ? m_amp->isConnected() : false);
        o.insert(QStringLiteral("secure"),    m_sslEnabled);
        o.insert(QStringLiteral("auth"),      !m_authUser.isEmpty());
        return jsonResponse(o);
    });

    // GET /api/ports — list every serial device the OS sees, with enough
    // identifying info (description, VID/PID, manufacturer) for the user
    // to pick the right one. FTDI devices get likelySpe=true so the web
    // UI can star them.
    m_server->route(QStringLiteral("/api/ports"),
                    [this](const QHttpServerRequest& req) {
        if (!authOk(req)) { return unauthorized(); }
        QJsonArray arr;
        for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
            arr.append(portInfoToJson(info));
        }
        return jsonResponse(arr);
    });

    // GET /api/config — current runtime state. Polled by the main page to
    // drive the connection LED + error banner.
    m_server->route(QStringLiteral("/api/config"),
                    QHttpServerRequest::Method::Get,
                    [this](const QHttpServerRequest& req) {
        if (!authOk(req)) { return unauthorized(); }
        QJsonObject o;
        o.insert(QStringLiteral("port"),      m_cfg->portName());
        o.insert(QStringLiteral("baud"),      m_cfg->baudRate());
        o.insert(QStringLiteral("connected"), m_amp->isConnected());
        o.insert(QStringLiteral("stopped"),   m_amp->isStopped());
        o.insert(QStringLiteral("lastError"), m_amp->lastError());
        o.insert(QStringLiteral("auth"),      m_cfg->authEnabled());
        o.insert(QStringLiteral("secure"),    m_sslEnabled);
        return jsonResponse(o);
    });

    // POST /api/config { "port": "COM4", "baud": 115200 }
    // Writes the config file, tells the AmpController to reopen. Returns
    // immediately; the UI polls /api/config to see the outcome a moment
    // later (open attempt is synchronous but we keep the handler short).
    m_server->route(QStringLiteral("/api/config"),
                    QHttpServerRequest::Method::Post,
                    [this](const QHttpServerRequest& req) {
        if (!authOk(req)) { return unauthorized(); }
        const QJsonDocument doc = QJsonDocument::fromJson(req.body());
        if (!doc.isObject()) {
            QJsonObject err;
            err.insert(QStringLiteral("ok"),    false);
            err.insert(QStringLiteral("error"), QStringLiteral("JSON body required"));
            return jsonResponse(err, QHttpServerResponse::StatusCode::BadRequest);
        }
        const QJsonObject body = doc.object();
        if (body.contains(QStringLiteral("port"))) {
            m_cfg->setPortName(body.value(QStringLiteral("port")).toString());
        }
        if (body.contains(QStringLiteral("baud"))) {
            m_cfg->setBaudRate(body.value(QStringLiteral("baud")).toInt(115200));
        }
        m_cfg->save();

        m_amp->setPortName(m_cfg->portName());
        m_amp->setBaudRate(m_cfg->baudRate());
        m_amp->stop();
        m_amp->start();

        QJsonObject o;
        o.insert(QStringLiteral("ok"), true);
        return jsonResponse(o);
    });

    m_server->route(QStringLiteral("/api/connect"),
                    QHttpServerRequest::Method::Post,
                    [this](const QHttpServerRequest& req) {
        if (!authOk(req)) { return unauthorized(); }
        m_amp->start();
        QJsonObject o;
        o.insert(QStringLiteral("ok"), true);
        return jsonResponse(o);
    });

    m_server->route(QStringLiteral("/api/disconnect"),
                    QHttpServerRequest::Method::Post,
                    [this](const QHttpServerRequest& req) {
        if (!authOk(req)) { return unauthorized(); }
        m_amp->stop();
        QJsonObject o;
        o.insert(QStringLiteral("ok"), true);
        return jsonResponse(o);
    });
}

bool HttpServer::listen(quint16 port) {
    // First call only: register REST routes (if attachControls was used)
    // BEFORE static wildcard routes. Order is load-bearing — see the
    // comment in the constructor.
    if (!m_routesRegistered) {
        if (m_amp && m_cfg) { registerRestRoutes(); }
        registerStaticRoutes();
        m_routesRegistered = true;
    }

    // Qt 6.8 made QHttpServer::listen(QHostAddress, port) the canonical entry
    // point; 6.4–6.7 only expose listen(tcpServer). Use the backwards-compat
    // path so the same code builds against Qt 6.4+.
    if (m_sslEnabled) {
        // QSslServer (Qt 6.4+) is a QTcpServer subclass that completes
        // the TLS handshake before exposing the connection. QHttpServer
        // sees an already-listening QTcpServer and treats it the same.
        auto* ssl = new QSslServer(this);
        ssl->setSslConfiguration(m_sslConfig);
        m_tcpServer = ssl;
    } else {
        m_tcpServer = new QTcpServer(this);
    }
    if (!m_tcpServer->listen(QHostAddress::Any, port)) {
        emit logMessage(QStringLiteral("HTTP listen failed: %1")
                            .arg(m_tcpServer->errorString()));
        m_tcpServer->deleteLater();
        m_tcpServer = nullptr;
        return false;
    }
    // Qt 6.5+ QAbstractHttpServer::bind returns void; the only failure mode is
    // the QTcpServer::listen above.
    m_server->bind(m_tcpServer);
    emit logMessage(QStringLiteral("%1 listening on :%2")
                        .arg(m_sslEnabled ? QStringLiteral("HTTPS")
                                          : QStringLiteral("HTTP"))
                        .arg(m_tcpServer->serverPort()));
    return true;
}

void HttpServer::close() {
    if (m_tcpServer) {
        m_tcpServer->close();
        m_tcpServer->deleteLater();
        m_tcpServer = nullptr;
    }
}

quint16 HttpServer::serverPort() const {
    return m_tcpServer ? m_tcpServer->serverPort() : 0;
}

QByteArray HttpServer::loadAsset(const QString& relPath, QByteArray& contentType) const {
    // Prefer a real directory if the operator pointed us at one — useful for
    // iterating on the web UI without a rebuild.
    QString fullPath;
    if (!m_fsRoot.isEmpty()) {
        fullPath = QDir(m_fsRoot).filePath(relPath);
    } else {
        fullPath = QStringLiteral(":/web/") + relPath;
    }
    QFile f(fullPath);
    if (!f.open(QIODevice::ReadOnly)) { return {}; }
    contentType = mimeFor(relPath);
    return f.readAll();
}
