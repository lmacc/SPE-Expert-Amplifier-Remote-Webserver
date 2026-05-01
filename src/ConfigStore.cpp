#include "ConfigStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

ConfigStore::ConfigStore(QObject* parent) : QObject(parent) {}

QString ConfigStore::filePath() const {
    if (!m_filePath.isEmpty()) { return m_filePath; }
    const QString dir = QStandardPaths::writableLocation(
        QStandardPaths::AppConfigLocation);
    m_filePath = QDir(dir).filePath(QStringLiteral("config.json"));
    return m_filePath;
}

void ConfigStore::setFilePath(const QString& path) {
    m_filePath = path;
}

void ConfigStore::load() {
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly)) { return; }

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) { return; }
    const QJsonObject o = doc.object();

    if (o.contains(QLatin1String("port")))      { m_portName = o.value(QLatin1String("port")).toString(); }
    if (o.contains(QLatin1String("baud")))      { m_baudRate = o.value(QLatin1String("baud")).toInt(m_baudRate); }
    if (o.contains(QLatin1String("ws_port")))   { m_wsPort   = static_cast<quint16>(o.value(QLatin1String("ws_port")).toInt(m_wsPort)); }
    if (o.contains(QLatin1String("http_port"))) { m_httpPort = static_cast<quint16>(o.value(QLatin1String("http_port")).toInt(m_httpPort)); }
    if (o.contains(QLatin1String("auth_user"))) {
        m_authUser = o.value(QLatin1String("auth_user")).toString();
    }
    if (o.contains(QLatin1String("auth_password_hash"))) {
        m_authPasswordHash = o.value(QLatin1String("auth_password_hash"))
                                 .toString().toUtf8();
    }
    if (o.contains(QLatin1String("cert_file"))) {
        m_certFile = o.value(QLatin1String("cert_file")).toString();
    }
    if (o.contains(QLatin1String("key_file"))) {
        m_keyFile = o.value(QLatin1String("key_file")).toString();
    }
}

bool ConfigStore::save() const {
    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonObject o;
    o.insert(QLatin1String("port"),               m_portName);
    o.insert(QLatin1String("baud"),               static_cast<int>(m_baudRate));
    o.insert(QLatin1String("ws_port"),            static_cast<int>(m_wsPort));
    o.insert(QLatin1String("http_port"),          static_cast<int>(m_httpPort));
    o.insert(QLatin1String("auth_user"),          m_authUser);
    o.insert(QLatin1String("auth_password_hash"), QString::fromUtf8(m_authPasswordHash));
    o.insert(QLatin1String("cert_file"),          m_certFile);
    o.insert(QLatin1String("key_file"),           m_keyFile);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return true;
}

void ConfigStore::setPortName(const QString& p) {
    if (m_portName == p) { return; }
    m_portName = p;
    emit changed();
}

void ConfigStore::setBaudRate(qint32 b) {
    if (m_baudRate == b) { return; }
    m_baudRate = b;
    emit changed();
}

void ConfigStore::setWsPort(quint16 p) {
    if (m_wsPort == p) { return; }
    m_wsPort = p;
    emit changed();
}

void ConfigStore::setHttpPort(quint16 p) {
    if (m_httpPort == p) { return; }
    m_httpPort = p;
    emit changed();
}

void ConfigStore::setAuthUser(const QString& u) {
    if (m_authUser == u) { return; }
    m_authUser = u;
    emit changed();
}

void ConfigStore::setAuthPasswordHash(const QByteArray& h) {
    if (m_authPasswordHash == h) { return; }
    m_authPasswordHash = h;
    emit changed();
}

void ConfigStore::setCertFile(const QString& p) {
    if (m_certFile == p) { return; }
    m_certFile = p;
    emit changed();
}

void ConfigStore::setKeyFile(const QString& p) {
    if (m_keyFile == p) { return; }
    m_keyFile = p;
    emit changed();
}
