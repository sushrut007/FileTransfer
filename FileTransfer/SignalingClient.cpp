#include "SignalingClient.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

SignalingClient::SignalingClient(const Config& config, QObject* parent)
    : QObject(parent)
    , m_config(config)
    , m_nam(new QNetworkAccessManager(this))
    , m_sio(new SocketIOClient(this))
    , m_heartbeat(new QTimer(this))
{
    connect(m_nam, &QNetworkAccessManager::finished,
        this, &SignalingClient::onLoginReply);
    connect(m_sio, &SocketIOClient::socketIOConnected,
        this, &SignalingClient::onSocketIOConnected);
    connect(m_sio, &SocketIOClient::errorOccurred, this, [this](const QString& error) {
        qCritical() << "[SignalingClient] SocketIO error:" << error;
        emit connectionFailed(error);
        });
    connect(m_sio, &SocketIOClient::disconnected, this, [this]() {
        m_heartbeat->stop();
        qWarning() << "[SignalingClient] SocketIO disconnected";
        });
    connect(m_heartbeat, &QTimer::timeout, this, [this]() {
        if (m_sio->isConnected()) {
            qDebug() << "[SignalingClient] Sending health-check";
            m_sio->sendEvent(QStringLiteral("health-check"), QJsonObject{});
        }
        });

    // Capture myPeerId from the server "login-success" event
    m_sio->on(QStringLiteral("login-success"), [this](const QJsonArray& args) {
        qInfo() << "[SignalingClient] login-success event received:"
            << QJsonDocument(args).toJson(QJsonDocument::Compact);
        if (args.isEmpty()) {
            qWarning() << "[SignalingClient] login-success args is empty!";
            return;
        }
        const QJsonObject obj = args.at(0).toObject();
        m_myPeerId = obj.value(QStringLiteral("userId")).toString();
        qInfo() << "[SignalingClient] My Peer ID set to:" << m_myPeerId;
        if (m_myPeerId.isEmpty()) {
            qWarning() << "[SignalingClient] Peer ID is empty from login-success!";
        }
        });

    m_sio->on(QStringLiteral("joined"), [this](const QJsonArray& args) {
        qInfo() << "[SignalingClient] joined event received:"
            << QJsonDocument(args).toJson(QJsonDocument::Compact);
        if (args.isEmpty()) {
            qWarning() << "[SignalingClient] joined args is empty!";
            return;
        }
        const QJsonObject obj = args.at(0).toObject();
        if (m_stage1Done) {
            qDebug() << "[SignalingClient] Stage 1 already done, ignoring joined event";
            return;
        }
        handleJoinSuccess(obj.value(QStringLiteral("roomId")).toString(),
            obj.value(QStringLiteral("peers")).toArray());
        });

    m_sio->on(QStringLiteral("login-failed"), [this](const QJsonArray& args) {
        qCritical() << "[SignalingClient] login-failed event received:"
            << QJsonDocument(args).toJson(QJsonDocument::Compact);
        if (!args.isEmpty()) {
            emit loginFailed(args.at(0).toObject().value(QStringLiteral("reason")).toString());
        }
        });

    m_sio->on(QStringLiteral("error"), [this](const QJsonArray& args) {
        qCritical() << "[SignalingClient] error event received:"
            << QJsonDocument(args).toJson(QJsonDocument::Compact);
        if (!args.isEmpty()) {
            emit connectionFailed(args.at(0).toObject().value(QStringLiteral("reason")).toString());
        }
        });
}

void SignalingClient::start()
{
    qInfo() << "[SignalingClient] Starting connection process";
    doLogin();
}

void SignalingClient::doLogin()
{
    qInfo() << "[SignalingClient] Sending REST login request to" << m_config.serverUrl;
    QNetworkRequest req;
    req.setUrl(QUrl(m_config.serverUrl + QStringLiteral("/login")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject loginData{
        { QStringLiteral("roomId"),   m_config.roomId   },
        { QStringLiteral("password"), m_config.password },
        { QStringLiteral("username"), QStringLiteral("QtClient") }
    };

    qInfo() << "[SignalingClient] Login payload:" << QJsonDocument(loginData).toJson(QJsonDocument::Compact);
    m_nam->post(req, QJsonDocument(loginData).toJson(QJsonDocument::Compact));
}

void SignalingClient::onLoginReply(QNetworkReply* reply)
{
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    qInfo() << "[SignalingClient] REST login reply received with status:" << status;

    if (reply->error() != QNetworkReply::NoError) {
        qCritical() << "[SignalingClient] Network error:" << reply->errorString();
        emit loginFailed(reply->errorString());
        return;
    }

    if (status < 200 || status >= 300) {
        qCritical() << "[SignalingClient] HTTP error:" << status;
        emit loginFailed(QString::fromLatin1("HTTP %1").arg(status));
        return;
    }

    // Parse JSON body
    QByteArray responseData = reply->readAll();
    qInfo() << "[SignalingClient] REST response:" << QString::fromUtf8(responseData);

    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    QJsonObject obj = doc.object();

    if (!obj.value("success").toBool()) {
        QString reason = obj.value("reason").toString();
        qCritical() << "[SignalingClient] Login failed:" << reason;
        emit loginFailed(reason);
        return;
    }

    // Extract cookie from REST response
    m_cookie = obj.value("cookie").toString();
    qInfo() << "[SignalingClient] REST login successful, cookie:" << m_cookie;

    // NOW connect Socket.IO
    connectSocket();
}

void SignalingClient::connectSocket()
{
    qInfo() << "[SignalingClient] Connecting to Socket.IO at" << m_config.serverUrl;
    m_sio->connectToServer(m_config.serverUrl, m_cookie);
}

void SignalingClient::onSocketIOConnected()
{
    qInfo() << "[SignalingClient] SocketIO connected! Now sending login event to server";

    // CRITICAL: Send the login event to the server to authenticate
    QJsonObject loginPayload{
        { QStringLiteral("roomId"),   m_config.roomId   },
        { QStringLiteral("password"), m_config.password },
        { QStringLiteral("username"), QStringLiteral("QtClient") }
    };

    qInfo() << "[SignalingClient] Sending login event with payload:"
        << QJsonDocument(loginPayload).toJson(QJsonDocument::Compact);

    m_sio->sendEvent(QStringLiteral("login"), loginPayload);

    // Give server time to process login before joining room
    QTimer::singleShot(500, this, &SignalingClient::joinRoom);

    m_heartbeat->start(20000);
}

void SignalingClient::joinRoom()
{
    qInfo() << "[SignalingClient] Sending join-room event for room:" << m_config.roomId;

    m_sio->sendEvent(QStringLiteral("join_room"),
        QJsonObject{ { QStringLiteral("roomId"), m_config.roomId } });

    // No ACK callback — wait for "joined" event
}

void SignalingClient::handleJoinSuccess(const QString& roomId,
    const QJsonArray& peers)
{
    qInfo() << "[SignalingClient] handleJoinSuccess called with roomId:" << roomId
        << "peers count:" << peers.size();

    if (m_stage1Done) {
        qDebug() << "[SignalingClient] Stage 1 already done, ignoring duplicate";
        return;
    }

    m_stage1Done = true;
    qInfo() << "[SignalingClient] Emitting stage1Complete with myPeerId:" << m_myPeerId;
    emit stage1Complete(roomId, m_myPeerId, peers);
}