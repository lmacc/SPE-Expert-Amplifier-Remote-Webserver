#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QSslConfiguration>
#include <QString>

class QWebSocket;
class QWebSocketServer;
struct AmpStatus;

class WsBridge : public QObject {
    Q_OBJECT
public:
    explicit WsBridge(QObject* parent = nullptr);
    ~WsBridge() override;

    // Require HTTP Basic auth on the WebSocket upgrade. Empty user disables.
    // hash format matches Auth::hashPassword. Must be called before listen().
    void enableAuth(const QString& user, const QByteArray& hash);

    // When true (default), peers on a private/loopback network skip the
    // auth check on the upgrade handshake. No-op if auth isn't enabled.
    void setTrustLan(bool trust);

    // Run as wss:// instead of ws://. Empty config disables. Must be called
    // before listen() — secure mode is fixed at QWebSocketServer construction.
    void setSslConfiguration(const QSslConfiguration& cfg);

    bool listen(quint16 port = 8888);
    void close();
    quint16 serverPort() const;
    bool isSecure() const;

public slots:
    void broadcastStatus(const AmpStatus& status);
    // Forward a command-ACK notification to every connected client.
    // Shape: {"ack":"<token>","opcode":<int>,"ts":<ms>}. Fresh clients
    // ignore unknown fields, so this is additive.
    void broadcastAck(quint8 opcode, const QString& token);

signals:
    void commandReceived(const QString& token);
    void clientCountChanged(int count);
    void logMessage(const QString& message);

private slots:
    void onNewConnection();
    void onTextMessage(const QString& message);
    void onClientDisconnected();

private:
    void ensureServer();

    QWebSocketServer* m_server = nullptr;
    QList<QWebSocket*> m_clients;
    QString m_lastJson;  // only resend when changed, matching server.py

    QString m_authUser;
    QByteArray m_authHash;
    bool m_trustLan = true;

    QSslConfiguration m_sslConfig;
    bool m_sslEnabled = false;
};
