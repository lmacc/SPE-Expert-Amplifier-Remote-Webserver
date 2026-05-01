#include "AmpController.h"
#include "ConfigStore.h"
#include "HttpServer.h"
#include "MainWindow.h"
#include "WsBridge.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("spe-remote-qt"));
    QCoreApplication::setOrganizationName(QStringLiteral("spe-remote"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.3.0"));

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral(
        "Qt6 port of the SPE 1.3K-2K-FA WebSocket remote-control server."));
    cli.addHelpOption();
    cli.addVersionOption();

    QCommandLineOption portOpt(
        { QStringLiteral("p"), QStringLiteral("serial-port") },
        QStringLiteral("Serial device, e.g. COM4 or /dev/ttyUSB0."),
        QStringLiteral("device"));
    QCommandLineOption baudOpt(
        QStringLiteral("baud"),
        QStringLiteral("Serial baud rate. Overrides the saved config."),
        QStringLiteral("rate"));
    QCommandLineOption wsPortOpt(
        QStringLiteral("ws-port"),
        QStringLiteral("WebSocket listen port. Overrides the saved config."),
        QStringLiteral("port"));
    QCommandLineOption httpPortOpt(
        QStringLiteral("http-port"),
        QStringLiteral("HTTP listen port for the web UI. Overrides the saved "
                       "config. Set to 0 to disable."),
        QStringLiteral("port"));
    QCommandLineOption webRootOpt(
        QStringLiteral("web-root"),
        QStringLiteral("Serve web UI from this filesystem path instead of "
                       "the built-in resources."),
        QStringLiteral("dir"));
    QCommandLineOption autostartOpt(
        QStringLiteral("autostart"),
        QStringLiteral("Open serial + WebSocket + HTTP immediately on launch."));

    cli.addOption(portOpt);
    cli.addOption(baudOpt);
    cli.addOption(wsPortOpt);
    cli.addOption(httpPortOpt);
    cli.addOption(webRootOpt);
    cli.addOption(autostartOpt);
    cli.process(app);

    // Persistent config shared with the web UI — CLI flags override, and
    // anything changed in the browser's Settings page comes back here.
    ConfigStore cfg;
    cfg.load();
    if (cli.isSet(portOpt))     { cfg.setPortName(cli.value(portOpt)); }
    if (cli.isSet(baudOpt))     { cfg.setBaudRate(cli.value(baudOpt).toInt()); }
    if (cli.isSet(wsPortOpt))   { cfg.setWsPort(static_cast<quint16>(cli.value(wsPortOpt).toUInt())); }
    if (cli.isSet(httpPortOpt)) { cfg.setHttpPort(static_cast<quint16>(cli.value(httpPortOpt).toUInt())); }

    AmpController amp;
    WsBridge bridge;
    HttpServer http;

    // Link: amp status → WS broadcast, WS command tokens → amp button presses.
    QObject::connect(&amp,    &AmpController::statusUpdated,
                     &bridge, &WsBridge::broadcastStatus);
    QObject::connect(&amp,    &AmpController::ackReceived,
                     &bridge, &WsBridge::broadcastAck);
    QObject::connect(&bridge, &WsBridge::commandReceived,
                     &amp,    &AmpController::handleCommandToken);

    amp.setBaudRate(cfg.baudRate());
    amp.setPortName(cfg.portName());
    if (cli.isSet(webRootOpt)) { http.setFileSystemRoot(cli.value(webRootOpt)); }
    http.attachControls(&amp, &cfg);

    MainWindow w(&amp, &bridge, &http);
    w.show();

    // Autostart also applies when --serial-port isn't given, since the
    // saved config (or the web Settings page) can supply one.
    if (cli.isSet(autostartOpt)) {
        amp.start();
        bridge.listen(cfg.wsPort());
        if (cfg.httpPort() != 0) { http.listen(cfg.httpPort()); }
    }

    return app.exec();
}
