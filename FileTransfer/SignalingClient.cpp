#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include "SignalingClient.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

SignalingClient::SignalingClient(const Config& config, QObject* parent)
    : QObject(parent)
    , m_config(config)
    , m_nam(new QNetworkAccessManager(this))
    , m_sio(new SocketIOClient(this))
{
    connect(m_nam, &QNetworkAccessManager::finished,
        this, [this](QNetworkReply* reply) {
            if (reply == m_createRoomReply) {
                m_createRoomReply = nullptr;
                onCreateRoomReply(reply);
            }
            else if (reply == m_loginReply) {
                m_loginReply = nullptr;
                onLoginReply(reply);
            }
            else {
                reply->deleteLater();
            }
        });

    connect(m_sio, &SocketIOClient::socketIOConnected,
        this, &SignalingClient::onSocketIOConnected);
    connect(m_sio, &SocketIOClient::errorOccurred,
        this, [](const QString& msg) {
            qWarning() << "[SIO] Socket error (non-fatal):" << msg;
        });
    connect(m_sio, &SocketIOClient::disconnected,
        this, []() {
            qWarning() << "[SIO] Disconnected";
        });

    // "joined" push – capture my socket.id (peerId)
    m_sio->on(QStringLiteral("joined"),
        [this](const QJsonArray& args) {
            qDebug() << "[EVENT] joined – raw args:" << args;
            if (!args.isEmpty()) {
                m_myPeerId = args.at(0).toObject()
                    .value(QStringLiteral("userId")).toString();
                qInfo() << "[SIO] My peerId (socket.id):" << m_myPeerId;
            }
            onJoinedEvent(args);
        });

    // new-peer: another client joined the room – emit signal so the UI
    // can add the peer to the combo box without a manual refresh.
    m_sio->on(QStringLiteral("new-peer"),
        [this](const QJsonArray& args) {
            qDebug() << "[EVENT] new-peer:" << args;
            if (args.isEmpty()) return;
            const QJsonObject obj = args.at(0).toObject();
            const QString peerId =
                obj.value(QStringLiteral("userId")).toString();
            const QString appType =
                obj.value(QStringLiteral("appType")).toString();
            if (!peerId.isEmpty())
                emit peerJoined(peerId, appType);
        });

    // peer-left: a client disconnected – emit signal so the UI removes
    // the peer from the combo box.
    m_sio->on(QStringLiteral("peer-left"),
        [this](const QJsonArray& args) {
            qDebug() << "[EVENT] peer-left:" << args;
            if (args.isEmpty()) return;
            const QString peerId =
                args.at(0).toObject()
                .value(QStringLiteral("userId")).toString();
            if (!peerId.isEmpty())
                emit peerLeft(peerId);
        });
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

void SignalingClient::start()
{
    if (m_config.mode == Mode::CreateRoom) {
        qInfo() << "[Client] Mode: Create Room – server:" << m_config.serverUrl;
        doCreateRoom();
    }
    else {
        qInfo() << "[Client] Mode: Join Room –"
            << m_config.roomId << "server:" << m_config.serverUrl;
        doLogin();
    }
}

// ---------------------------------------------------------------------------
// Private – Create Room flow
// ---------------------------------------------------------------------------

void SignalingClient::doCreateRoom()
{
    const QUrl url(m_config.serverUrl + QStringLiteral("/api/v1/create-room"));
    QNetworkRequest request;
    request.setUrl(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
        QStringLiteral("application/json"));

    const QJsonObject body{
        { QStringLiteral("password"), m_config.password }
    };

    qDebug() << "[HTTP] POST" << url.toString();
    m_createRoomReply = m_nam->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

void SignalingClient::onCreateRoomReply(QNetworkReply* reply)
{
    reply->deleteLater();

    const int statusCode =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseErr);

    if (reply->error() != QNetworkReply::NoError
        || statusCode < 200 || statusCode >= 300)
    {
        const QString msg =
            (parseErr.error == QJsonParseError::NoError && doc.isObject())
            ? doc.object()
            .value(QStringLiteral("message"))
            .toString(reply->errorString())
            : reply->errorString();
        qWarning() << "[HTTP] create-room failed – status:" << statusCode << msg;
        emit loginFailed(msg);
        return;
    }

    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        emit loginFailed(QStringLiteral("Server returned invalid JSON"));
        return;
    }

    const QJsonObject resp = doc.object();
    if (!resp.value(QStringLiteral("success")).toBool()) {
        emit loginFailed(resp.value(QStringLiteral("message"))
            .toString(QStringLiteral("create-room rejected by server")));
        return;
    }

    const QString roomId = resp.value(QStringLiteral("roomId")).toString();
    if (roomId.isEmpty()) {
        emit loginFailed(QStringLiteral("Server did not return a roomId"));
        return;
    }

    m_config.roomId = roomId;
    qInfo() << "[HTTP] Room created – roomId:" << roomId;
    emit roomCreated(roomId, m_config.password);
    doLogin();
}

// ---------------------------------------------------------------------------
// Private – Login + Join flow
// ---------------------------------------------------------------------------

void SignalingClient::doLogin()
{
    const QUrl url(m_config.serverUrl + QStringLiteral("/api/v1/login"));
    QNetworkRequest request;
    request.setUrl(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
        QStringLiteral("application/json"));

    const QJsonObject body{
        { QStringLiteral("roomId"),   m_config.roomId   },
        { QStringLiteral("password"), m_config.password }
    };

    qDebug() << "[HTTP] POST" << url.toString();
    m_loginReply = m_nam->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

void SignalingClient::onLoginReply(QNetworkReply* reply)
{
    reply->deleteLater();

    const int statusCode =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError
        || statusCode < 200 || statusCode >= 300)
    {
        QJsonParseError err;
        const QJsonDocument doc =
            QJsonDocument::fromJson(reply->readAll(), &err);
        const QString msg =
            (err.error == QJsonParseError::NoError && doc.isObject())
            ? doc.object()
            .value(QStringLiteral("message"))
            .toString(reply->errorString())
            : reply->errorString();
        qWarning() << "[HTTP] Login failed – status:" << statusCode << msg;
        emit loginFailed(msg);
        return;
    }

    QStringList cookieParts;
    const auto headers = reply->rawHeaderPairs();
    for (int i = 0; i < headers.size(); ++i) {
        if (headers.at(i).first.toLower() == QByteArrayLiteral("set-cookie")) {
            const QString full = QString::fromUtf8(headers.at(i).second);
            const QString nameVal =
                full.split(QLatin1Char(';')).first().trimmed();
            if (!nameVal.isEmpty())
                cookieParts.append(nameVal);
        }
    }
    m_cookie = cookieParts.join(QStringLiteral("; "));
    qInfo() << "[HTTP] Login success – cookies extracted";
    connectSocket();
}

void SignalingClient::connectSocket()
{
    qDebug() << "[SIO] Connecting with session cookie...";
    m_sio->connectToServer(m_config.serverUrl, m_cookie);
}

void SignalingClient::onSocketIOConnected()
{
    qInfo() << "[SIO] Socket.IO connected – sending join-room";
    joinRoom();
}

void SignalingClient::joinRoom()
{
    const QJsonObject payload{
        { QStringLiteral("roomId"),   m_config.roomId   },
        { QStringLiteral("password"), m_config.password }
    };

    m_sio->sendEvent(QStringLiteral("join-room"), payload,
        [this](const QJsonArray& args) {
            qDebug() << "[SIO] join-room raw ACK:" << args;
            if (args.isEmpty()) {
                qWarning() << "[SIO] join-room ACK: empty";
                return;
            }
            if (!args.at(0).isObject()) return;

            const QJsonObject response = args.at(0).toObject();
            if (!response.value(QStringLiteral("success")).toBool()) {
                emit connectionFailed(response.value(QStringLiteral("message"))
                    .toString(QStringLiteral("join-room rejected")));
                return;
            }

            const QJsonObject data = response.value(QStringLiteral("data")).toObject();
            const QString     roomId = data.value(QStringLiteral("roomId")).toString();
            const QJsonArray  peers = data.value(QStringLiteral("peers")).toArray();

            if (!m_stage1Done) {
                handleJoinSuccess(roomId, peers);
            }
            else {
                // 'joined' event already completed stage1 with no peers.
                // Populate from the ACK which carries the real peer list.
                for (const QJsonValue& v : peers) {
                    const QJsonObject p = v.toObject();
                    const QString     pid = p.value(QStringLiteral("userId")).toString();
                    if (!pid.isEmpty() && pid != m_myPeerId)
                        emit peerJoined(pid,
                            p.value(QStringLiteral("appType")).toString());
                }
            }
        });
}

void SignalingClient::onJoinedEvent(const QJsonArray& args)
{
    qDebug() << "[SIO] 'joined' event payload:" << args;
    if (args.isEmpty()) return;

    const QJsonValue  first = args.at(0);
    const QJsonObject obj = first.isObject() ? first.toObject() : QJsonObject();
    const QJsonArray  peers = obj.value(QStringLiteral("peers")).toArray();

    if (m_stage1Done) {
        // ACK already finished stage1 with an empty peers list.
        // Emit peerJoined for every peer the server reports in this event.
        for (const QJsonValue& v : peers) {
            const QJsonObject p = v.toObject();
            const QString     pid = p.value(QStringLiteral("userId")).toString();
            if (!pid.isEmpty() && pid != m_myPeerId)
                emit peerJoined(pid,
                    p.value(QStringLiteral("appType")).toString());
        }
        return;
    }

    if (first.isObject()) {
        handleJoinSuccess(
            obj.value(QStringLiteral("roomId")).toString(), peers);
        return;
    }
    handleJoinSuccess(m_config.roomId, QJsonArray());
}

void SignalingClient::handleJoinSuccess(const QString& roomId,
    const QJsonArray& peers)
{
    if (m_stage1Done) return;
    m_stage1Done = true;

    qInfo() << "[SIO] \u2705 join-room success – roomId:" << roomId
        << "peers online:" << peers.size()
        << "myPeerId:" << m_myPeerId;

    if (!m_ftm) {
        m_ftm = new FileTransferManager(m_sio, m_myPeerId, this);
        qInfo() << "[Client] FileTransferManager created – peerId:" << m_myPeerId;
    }

    emit stage1Complete(roomId, m_myPeerId, peers);
}