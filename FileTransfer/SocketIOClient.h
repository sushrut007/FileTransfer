#pragma once
#include <QObject>
#include <QWebSocket>
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QString>
#include <functional>

class SocketIOClient : public QObject
{
    Q_OBJECT
        Q_DISABLE_COPY(SocketIOClient)
public:
    explicit SocketIOClient(QObject* parent = nullptr);
    ~SocketIOClient();

    void connectToServer(const QString& url, const QString& cookie = {});
    void disconnectFromServer();

    void sendEvent(const QString& event,
        const QJsonObject& payload,
        std::function<void(const QJsonArray&)> ack = nullptr);

    void on(const QString& event,
        std::function<void(const QJsonArray&)> callback);

    bool isConnected() const;

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString& message);
    void socketIOConnected();

private slots:
    void onWsConnected();
    void onWsDisconnected();
    void onWsErrorOccurred(QAbstractSocket::SocketError error);
    void onTextMessageReceived(const QString& message);
    void onPingWatchdog();

private:
    void sendRaw(const QString& data);
    void handleEngineIO(const QString& packet);
    void handleSocketIO(const QString& payload);
    void parseEvent(const QString& data, int ackId);

    QWebSocket* m_ws = nullptr;
    QTimer* m_pingTimer = nullptr;

    QMap<QString, std::function<void(const QJsonArray&)>> m_listeners;
    QMap<int, std::function<void(const QJsonArray&)>> m_ackCallbacks;

    int     m_ackCounter = 0;
    int     m_pingInterval = 25000;
    int     m_pingTimeout = 20000;
    int     m_pingWatchdogMs = 47000;
    bool    m_sioConnected = false;
    QString m_cookie;
};
