#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QString>
#include <QJsonArray>
#include "SocketIOClient.h"

class SignalingClient : public QObject
{
    Q_OBJECT
public:
    struct Config {
        QString serverUrl;
        QString roomId;
        QString password;
        QString appType = QStringLiteral("auto-ftp-manager");
    };

    explicit SignalingClient(const Config& config, QObject* parent = nullptr);

    void start();
    void stopHeartbeat() {
        if (m_heartbeat) m_heartbeat->stop();
    }

    SocketIOClient* socketIO()  const { return m_sio; }
    QString         myPeerId()  const { return m_myPeerId; }

signals:
    void stage1Complete(const QString& roomId,
        const QString& peerId,
        const QJsonArray& peers);
    void loginFailed(const QString& reason);
    void connectionFailed(const QString& reason);

private slots:
    void onLoginReply(QNetworkReply* reply);
    void onSocketIOConnected();

private:
    void doLogin();
    void connectSocket();
    void joinRoom();
    void handleJoinSuccess(const QString& roomId, const QJsonArray& peers);

    Config                  m_config;
    QNetworkAccessManager* m_nam = nullptr;
    SocketIOClient* m_sio = nullptr;
    QTimer* m_heartbeat = nullptr;
    QString                 m_cookie;
    QString                 m_myPeerId;
    bool                    m_stage1Done = false;
};