// Headless daemon. Links QtCore only — no Widgets, no Gui — so it runs
// on a bare Raspberry Pi with no X server / display.
//
// Pipeline:
//   QSerialPort (AmpController)
//       ↓ statusUpdated
//   QWebSocketServer (WsBridge)  ← command tokens flow back up to amp
//       ↑ /ws clients
//   QHttpServer (HttpServer)  ← static file server for the bundled web/ UI
//       ↑ /, /css/app.css, /justgage/justgage.js ...

#include "AmpController.h"
#include "Auth.h"
#include "ConfigStore.h"
#include "HttpServer.h"
#include "WsBridge.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QSerialPortInfo>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QTextStream>
#include <QtGlobal>

#include <csignal>

namespace {
QTextStream& stderrStream() {
    static QTextStream s(stderr);
    return s;
}

void logLine(const QString& who, const QString& what) {
    stderrStream() << QDateTime::currentDateTime().toString(Qt::ISODate)
                   << ' ' << who << ": " << what << '\n';
    stderrStream().flush();
}

void installQuitHandler() {
    // Graceful shutdown on SIGINT / SIGTERM — lets destructors run so the
    // serial port and sockets close cleanly.
    auto handler = [](int) { QCoreApplication::quit(); };
    std::signal(SIGINT, handler);
    std::signal(SIGTERM, handler);
}

// Loads PEM cert and key from disk into a QSslConfiguration. Returns
// default-constructed config (and emits an error to stderr) on failure;
// callers can detect this via QSslConfiguration::localCertificate().isNull().
QSslConfiguration loadSslConfig(const QString& certPath, const QString& keyPath) {
    QSslConfiguration cfg;
    QFile certFile(certPath);
    if (!certFile.open(QIODevice::ReadOnly)) {
        logLine(QStringLiteral("tls"),
                QStringLiteral("cannot open cert: %1").arg(certPath));
        return {};
    }
    const QSslCertificate cert(&certFile, QSsl::Pem);
    if (cert.isNull()) {
        logLine(QStringLiteral("tls"),
                QStringLiteral("cert parse failed: %1").arg(certPath));
        return {};
    }

    QFile keyFile(keyPath);
    if (!keyFile.open(QIODevice::ReadOnly)) {
        logLine(QStringLiteral("tls"),
                QStringLiteral("cannot open key: %1").arg(keyPath));
        return {};
    }
    const QSslKey key(&keyFile, QSsl::Rsa, QSsl::Pem);
    if (key.isNull()) {
        // Try EC if RSA failed — many self-signed certs use EC keys.
        keyFile.seek(0);
        const QSslKey ecKey(&keyFile, QSsl::Ec, QSsl::Pem);
        if (ecKey.isNull()) {
            logLine(QStringLiteral("tls"),
                    QStringLiteral("key parse failed: %1").arg(keyPath));
            return {};
        }
        cfg.setPrivateKey(ecKey);
    } else {
        cfg.setPrivateKey(key);
    }
    cfg.setLocalCertificate(cert);
    return cfg;
}
}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("spe-remote"));
    QCoreApplication::setApplicationName(QStringLiteral("spe-remoted"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.3.0"));

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral(
        "Headless SPE 1.3K-2K-FA remote daemon.\n"
        "Talks to the amplifier over serial, publishes status over "
        "WebSocket (:8888/ws), and serves the bundled browser UI over "
        "HTTP (:8080)."));
    cli.addHelpOption();
    cli.addVersionOption();

    // All of these override the saved config on disk for this run only.
    // Without any of them we fall back to ~/.config/spe-remote/config.json,
    // which the web UI's Settings page writes to.
    QCommandLineOption portOpt(
        { QStringLiteral("p"), QStringLiteral("serial-port") },
        QStringLiteral("Serial device (e.g. /dev/ttyUSB0 or COM4). "
                       "Overrides the saved config."),
        QStringLiteral("device"));
    QCommandLineOption baudOpt(
        QStringLiteral("baud"),
        QStringLiteral("Serial baud rate. Overrides the saved config."),
        QStringLiteral("rate"));
    QCommandLineOption wsOpt(
        QStringLiteral("ws-port"),
        QStringLiteral("WebSocket listen port. Overrides the saved config."),
        QStringLiteral("port"));
    QCommandLineOption httpOpt(
        QStringLiteral("http-port"),
        QStringLiteral("HTTP listen port for the web UI. Overrides the saved "
                       "config. Set to 0 to disable."),
        QStringLiteral("port"));
    QCommandLineOption webRootOpt(
        QStringLiteral("web-root"),
        QStringLiteral("Serve web UI from this filesystem path instead of the "
                       "built-in resources. Useful for iterating on the HTML."),
        QStringLiteral("dir"));
    QCommandLineOption listPortsOpt(
        QStringLiteral("list-ports"),
        QStringLiteral("Print available serial ports and exit."));
    QCommandLineOption authUserOpt(
        QStringLiteral("auth-user"),
        QStringLiteral("Username for HTTP Basic auth on the web UI and "
                       "WebSocket. Overrides config."),
        QStringLiteral("name"));
    QCommandLineOption authPwdOpt(
        QStringLiteral("auth-password"),
        QStringLiteral("Plain password for HTTP Basic auth — hashed in "
                       "memory before use, never persisted. For long-term "
                       "use, generate a hash with --hash-password and put "
                       "it in config.json under auth_password_hash."),
        QStringLiteral("password"));
    QCommandLineOption hashPwdOpt(
        QStringLiteral("hash-password"),
        QStringLiteral("Read a password from stdin (or its argument), print "
                       "the PBKDF2 hash to stdout, and exit. Paste the result "
                       "into config.json as auth_password_hash."),
        QStringLiteral("password"));
    QCommandLineOption certOpt(
        QStringLiteral("cert-file"),
        QStringLiteral("PEM-encoded TLS certificate. If set together with "
                       "--key-file, the daemon serves HTTPS+WSS instead of "
                       "HTTP+WS."),
        QStringLiteral("path"));
    QCommandLineOption keyOpt(
        QStringLiteral("key-file"),
        QStringLiteral("PEM-encoded TLS private key (RSA or EC)."),
        QStringLiteral("path"));
    QCommandLineOption noLanTrustOpt(
        QStringLiteral("no-lan-trust"),
        QStringLiteral("Require auth from LAN peers too. Default is to "
                       "skip the password prompt for clients on a private "
                       "(RFC1918 / loopback / link-local / IPv6 ULA) "
                       "network."));

    cli.addOption(portOpt);
    cli.addOption(baudOpt);
    cli.addOption(wsOpt);
    cli.addOption(httpOpt);
    cli.addOption(webRootOpt);
    cli.addOption(listPortsOpt);
    cli.addOption(authUserOpt);
    cli.addOption(authPwdOpt);
    cli.addOption(hashPwdOpt);
    cli.addOption(certOpt);
    cli.addOption(keyOpt);
    cli.addOption(noLanTrustOpt);
    cli.process(app);

    if (cli.isSet(hashPwdOpt)) {
        const QString plain = cli.value(hashPwdOpt);
        QTextStream(stdout) << Auth::hashPassword(plain) << '\n';
        return 0;
    }

    if (cli.isSet(listPortsOpt)) {
        for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
            QTextStream(stdout)
                << info.portName() << "  " << info.description() << '\n';
        }
        return 0;
    }

    // Persistent config. CLI flags override for this run only — changes
    // made via the web UI go back to disk.
    ConfigStore cfg;
    cfg.load();
    if (cli.isSet(portOpt)) { cfg.setPortName(cli.value(portOpt)); }
    if (cli.isSet(baudOpt)) { cfg.setBaudRate(cli.value(baudOpt).toInt()); }
    if (cli.isSet(wsOpt))   { cfg.setWsPort(static_cast<quint16>(cli.value(wsOpt).toUInt())); }
    if (cli.isSet(httpOpt)) { cfg.setHttpPort(static_cast<quint16>(cli.value(httpOpt).toUInt())); }
    if (cli.isSet(authUserOpt)) { cfg.setAuthUser(cli.value(authUserOpt)); }
    if (cli.isSet(authPwdOpt)) {
        cfg.setAuthPasswordHash(Auth::hashPassword(cli.value(authPwdOpt)));
    }
    if (cli.isSet(certOpt)) { cfg.setCertFile(cli.value(certOpt)); }
    if (cli.isSet(keyOpt))  { cfg.setKeyFile(cli.value(keyOpt));   }
    if (cli.isSet(noLanTrustOpt)) { cfg.setTrustLan(false); }

    AmpController amp;
    WsBridge bridge;
    HttpServer http;

    QObject::connect(&amp,    &AmpController::statusUpdated,
                     &bridge, &WsBridge::broadcastStatus);
    QObject::connect(&amp,    &AmpController::ackReceived,
                     &bridge, &WsBridge::broadcastAck);
    QObject::connect(&bridge, &WsBridge::commandReceived,
                     &amp,    &AmpController::handleCommandToken);

    QObject::connect(&amp,    &AmpController::logMessage,
                     [](const QString& m) { logLine(QStringLiteral("amp"), m);    });
    QObject::connect(&amp,    &AmpController::connectionChanged,
                     [](bool connected) {
        logLine(QStringLiteral("amp"),
                connected ? QStringLiteral("serial up") : QStringLiteral("serial down"));
    });
    QObject::connect(&bridge, &WsBridge::logMessage,
                     [](const QString& m) { logLine(QStringLiteral("ws"),  m);    });
    QObject::connect(&http,   &HttpServer::logMessage,
                     [](const QString& m) { logLine(QStringLiteral("http"), m);   });

    amp.setBaudRate(cfg.baudRate());
    amp.setPortName(cfg.portName());

    const quint16 wsPort   = cfg.wsPort();
    const quint16 httpPort = cfg.httpPort();

    if (cli.isSet(webRootOpt)) {
        http.setFileSystemRoot(cli.value(webRootOpt));
    }

    // TLS — picked up by both the HTTP server and the WS bridge if
    // both files load successfully. If they don't, we fall back to plain
    // HTTP/WS rather than refusing to start; the operator gets a stderr
    // line explaining why.
    if (cfg.tlsConfigured()) {
        const QSslConfiguration sslCfg = loadSslConfig(cfg.certFile(),
                                                       cfg.keyFile());
        if (!sslCfg.localCertificate().isNull()) {
            http.setSslConfiguration(sslCfg);
            bridge.setSslConfiguration(sslCfg);
        } else {
            logLine(QStringLiteral("tls"),
                    QStringLiteral("TLS configured but cert/key load failed; "
                                   "starting in plain HTTP/WS mode"));
        }
    }

    // HTTP Basic auth on both the web UI and the WS upgrade.
    if (cfg.authEnabled()) {
        http.enableAuth(cfg.authUser(), cfg.authPasswordHash());
        bridge.enableAuth(cfg.authUser(), cfg.authPasswordHash());
        http.setTrustLan(cfg.trustLan());
        bridge.setTrustLan(cfg.trustLan());
    }

    // Expose ports list + config to the web UI so a headless Pi can be
    // reconfigured from a phone browser without SSH.
    http.attachControls(&amp, &cfg);

    // With no port configured we still start the HTTP server so the user
    // can pick one from the Settings page. AmpController::start() is
    // safe to call with an empty name — it just records lastError.
    amp.start();
    if (!bridge.listen(wsPort))     { return 1; }
    if (httpPort != 0 && !http.listen(httpPort)) { return 1; }

    installQuitHandler();
    QString authState = QStringLiteral("off");
    if (cfg.authEnabled()) {
        authState = cfg.trustLan() ? QStringLiteral("on (LAN trusted)")
                                   : QStringLiteral("on (everyone)");
    }
    logLine(QStringLiteral("main"),
            QStringLiteral("ready — serial=%1 %2=:%3 %4=:%5 auth=%6 config=%7")
                .arg(cfg.portName().isEmpty()
                         ? QStringLiteral("(unset; configure via web UI)")
                         : cfg.portName())
                .arg(bridge.isSecure() ? QStringLiteral("wss") : QStringLiteral("ws"))
                .arg(wsPort)
                .arg(http.isSecure() ? QStringLiteral("https") : QStringLiteral("http"))
                .arg(httpPort)
                .arg(authState)
                .arg(cfg.filePath()));
    return app.exec();
}
