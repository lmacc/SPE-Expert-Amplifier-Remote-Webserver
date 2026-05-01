#include "WsBridge.h"

#include "AmpController.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QWebSocket>
#include <QWebSocketServer>

WsBridge::WsBridge(QObject* parent)
    : QObject(parent)
    , m_server(new QWebSocketServer(QStringLiteral("SPE Remote"),
                                    QWebSocketServer::NonSecureMode, this)) {
    connect(m_server, &QWebSocketServer::newConnection,
            this, &WsBridge::onNewConnection);
}

WsBridge::~WsBridge() {
    close();
}

bool WsBridge::listen(quint16 port) {
    if (!m_server->listen(QHostAddress::Any, port)) {
        emit logMessage(QStringLiteral("WebSocket listen failed: %1")
                            .arg(m_server->errorString()));
        return false;
    }
    emit logMessage(QStringLiteral("WebSocket listening on :%1/ws")
                        .arg(m_server->serverPort()));
    return true;
}

void WsBridge::close() {
    for (QWebSocket* c : std::as_const(m_clients)) {
        c->close();
        c->deleteLater();
    }
    m_clients.clear();
    if (m_server->isListening()) {
        m_server->close();
    }
}

quint16 WsBridge::serverPort() const {
    return m_server->serverPort();
}

void WsBridge::onNewConnection() {
    while (auto* sock = m_server->nextPendingConnection()) {
        // Original server doesn't enforce a /ws path — it accepts any — but
        // we keep the same behaviour so the existing web client works.
        connect(sock, &QWebSocket::textMessageReceived,
                this, &WsBridge::onTextMessage);
        connect(sock, &QWebSocket::disconnected,
                this, &WsBridge::onClientDisconnected);
        m_clients.append(sock);
        emit clientCountChanged(m_clients.size());
        emit logMessage(QStringLiteral("Client connected (%1 total)").arg(m_clients.size()));
        // Send latest snapshot immediately so a fresh client isn't blank.
        if (!m_lastJson.isEmpty()) {
            sock->sendTextMessage(m_lastJson);
        }
    }
}

void WsBridge::onTextMessage(const QString& message) {
    // Python server accepted: oper / antenna / input / gain / tune.
    emit commandReceived(message);
}

void WsBridge::onClientDisconnected() {
    auto* sock = qobject_cast<QWebSocket*>(sender());
    if (!sock) { return; }
    m_clients.removeAll(sock);
    sock->deleteLater();
    emit clientCountChanged(m_clients.size());
    emit logMessage(QStringLiteral("Client disconnected (%1 remaining)").arg(m_clients.size()));
}

void WsBridge::broadcastStatus(const AmpStatus& status) {
    const QByteArray payload = QJsonDocument(status.toJson())
                                   .toJson(QJsonDocument::Compact);
    const QString text = QString::fromUtf8(payload);
    if (text == m_lastJson) { return; }
    m_lastJson = text;
    for (QWebSocket* c : std::as_const(m_clients)) {
        c->sendTextMessage(text);
    }
}

void WsBridge::broadcastAck(quint8 opcode, const QString& token) {
    // Send as a distinct message so clients can tell it from status.
    // Old web clients (pre-ACK) just see an object without op_status /
    // p_out / etc. and silently skip the gauge-refresh path.
    QJsonObject o;
    o.insert(QStringLiteral("ack"),    token.isEmpty()
                                           ? QStringLiteral("?")
                                           : token);
    o.insert(QStringLiteral("opcode"), static_cast<int>(opcode));
    o.insert(QStringLiteral("ts"),     QDateTime::currentMSecsSinceEpoch());
    const QByteArray payload = QJsonDocument(o).toJson(QJsonDocument::Compact);
    const QString text = QString::fromUtf8(payload);
    for (QWebSocket* c : std::as_const(m_clients)) {
        c->sendTextMessage(text);
    }
}
