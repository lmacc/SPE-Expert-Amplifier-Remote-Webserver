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
#include "ConfigStore.h"
#include "HttpServer.h"
#include "WsBridge.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QSerialPortInfo>
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

    cli.addOption(portOpt);
    cli.addOption(baudOpt);
    cli.addOption(wsOpt);
    cli.addOption(httpOpt);
    cli.addOption(webRootOpt);
    cli.addOption(listPortsOpt);
    cli.process(app);

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
    logLine(QStringLiteral("main"),
            QStringLiteral("ready — serial=%1 ws=:%2 http=:%3 config=%4")
                .arg(cfg.portName().isEmpty()
                         ? QStringLiteral("(unset; configure via web UI)")
                         : cfg.portName())
                .arg(wsPort).arg(httpPort).arg(cfg.filePath()));
    return app.exec();
}
