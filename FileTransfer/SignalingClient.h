#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <QJsonArray>
#include <QJsonObject>

#include "SocketIOClient.h"
#include "FileTransferManager.h"

class SignalingClient : public QObject
{
    Q_OBJECT

public:
    enum class Mode { JoinRoom, CreateRoom };

    struct Config {
        QString serverUrl;
        QString roomId;
        QString password;
        Mode    mode = Mode::JoinRoom;
    };

    explicit SignalingClient(const Config& config, QObject* parent = nullptr);

    void start();

    SocketIOClient* socketIO()  const { return m_sio; }
    QString              myPeerId()  const { return m_myPeerId; }
    QString              roomId()    const { return m_config.roomId; }
    FileTransferManager* ftm()       const { return m_ftm; }

signals:
    void roomCreated(const QString& roomId, const QString& password);

    void stage1Complete(const QString& roomId,
        const QString& myPeerId,
        const QJsonArray& peers);

    // Emitted when a new peer joins the same room.
    // peerId is the socket.id of the joining peer.
    void peerJoined(const QString& peerId, const QString& appType);

    // Emitted when a peer disconnects from the room.
    void peerLeft(const QString& peerId);

    void loginFailed(const QString& reason);
    void connectionFailed(const QString& reason);

private slots:
    void onCreateRoomReply(QNetworkReply* reply);
    void onLoginReply(QNetworkReply* reply);
    void onSocketIOConnected();

private:
    void doCreateRoom();
    void doLogin();
    void connectSocket();
    void joinRoom();
    void onJoinedEvent(const QJsonArray& args);
    void handleJoinSuccess(const QString& roomId, const QJsonArray& peers);

    Config                  m_config;
    QNetworkAccessManager* m_nam = nullptr;
    SocketIOClient* m_sio = nullptr;
    FileTransferManager* m_ftm = nullptr;
    QString                 m_cookie;
    QString                 m_myPeerId;
    bool                    m_stage1Done = false;
    QNetworkReply* m_createRoomReply = nullptr;
    QNetworkReply* m_loginReply = nullptr;
};