#pragma once

#include <QList>
#include <QObject>
#include <QString>

class QWebSocket;
class QWebSocketServer;
struct AmpStatus;

class WsBridge : public QObject {
    Q_OBJECT
public:
    explicit WsBridge(QObject* parent = nullptr);
    ~WsBridge() override;

    bool listen(quint16 port = 8888);
    void close();
    quint16 serverPort() const;

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
    QWebSocketServer* m_server = nullptr;
    QList<QWebSocket*> m_clients;
    QString m_lastJson;  // only resend when changed, matching server.py
};
