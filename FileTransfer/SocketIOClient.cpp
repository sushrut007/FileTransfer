#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include "SocketIOClient.h"
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QJsonParseError>
#include <QJsonDocument>
#include <QDebug>

SocketIOClient::SocketIOClient(QObject* parent)
    : QObject(parent)
    , m_ws(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
    , m_pingTimer(new QTimer(this))
{
    connect(m_ws, &QWebSocket::connected, this, &SocketIOClient::onWsConnected);
    connect(m_ws, &QWebSocket::disconnected, this, &SocketIOClient::onWsDisconnected);
    connect(m_ws, &QWebSocket::textMessageReceived, this, &SocketIOClient::onTextMessageReceived);
    connect(m_ws, &QWebSocket::errorOccurred, this, &SocketIOClient::onWsErrorOccurred);
    connect(m_pingTimer, &QTimer::timeout, this, &SocketIOClient::onPingWatchdog);
}

SocketIOClient::~SocketIOClient() { disconnectFromServer(); }

void SocketIOClient::connectToServer(const QString& url, const QString& cookie)
{
    m_cookie = cookie;
    QString wsUrl = url;
    wsUrl.replace(QRegularExpression(QStringLiteral("^http")), QStringLiteral("ws"));
    while (wsUrl.endsWith(QLatin1Char('/'))) wsUrl.chop(1);
    wsUrl += QStringLiteral("/socket.io/?EIO=4&transport=websocket");

    QNetworkRequest request;
    request.setUrl(QUrl(wsUrl));
    request.setRawHeader("User-Agent", "DeskshareQt/1.0");
    if (!m_cookie.isEmpty())
        request.setRawHeader("Cookie", m_cookie.toUtf8());
    m_ws->open(request);
}

void SocketIOClient::disconnectFromServer()
{
    m_pingTimer->stop();
    if (m_ws->state() != QAbstractSocket::UnconnectedState)
        m_ws->close();
    m_sioConnected = false;
}

void SocketIOClient::sendEvent(const QString& event,
    const QJsonObject& payload,
    std::function<void(const QJsonArray&)> ack)
{
    if (!m_sioConnected) return;
    QJsonArray arr;
    arr.append(event);
    arr.append(payload);
    QString packet = QStringLiteral("42");
    if (ack) {
        const int id = m_ackCounter++;
        m_ackCallbacks.insert(id, ack);
        packet += QString::number(id);
    }
    packet += QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    sendRaw(packet);
}

void SocketIOClient::on(const QString& event,
    std::function<void(const QJsonArray&)> callback)
{
    m_listeners.insert(event, callback);
}

bool SocketIOClient::isConnected() const { return m_sioConnected; }

void SocketIOClient::onWsConnected() { emit connected(); }
void SocketIOClient::onWsDisconnected() {
    m_pingTimer->stop();
    m_sioConnected = false;
    emit disconnected();
}
void SocketIOClient::onWsErrorOccurred(QAbstractSocket::SocketError) {
    emit errorOccurred(m_ws->errorString());
}
void SocketIOClient::onTextMessageReceived(const QString& message) {
    if (!message.isEmpty()) handleEngineIO(message);
}
void SocketIOClient::onPingWatchdog() {
    qWarning() << "[EIO] Ping watchdog – connection may be stale";
}
void SocketIOClient::sendRaw(const QString& data) {
    if (m_ws->state() == QAbstractSocket::ConnectedState)
        m_ws->sendTextMessage(data);
}

void SocketIOClient::handleEngineIO(const QString& packet)
{
    if (packet.isEmpty()) return;
    const QChar type = packet.at(0);

    if (type == QLatin1Char('0')) {                          // EIO Open
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(packet.mid(1).toUtf8(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject obj = doc.object();
            m_pingInterval = obj.value(QStringLiteral("pingInterval")).toInt(25000);
            m_pingTimeout = obj.value(QStringLiteral("pingTimeout")).toInt(20000);
        }
        m_pingWatchdogMs = m_pingInterval + m_pingTimeout + 2000;
        m_pingTimer->start(m_pingWatchdogMs);
        sendRaw(QStringLiteral("40"));                       // SIO connect
    }
    else if (type == QLatin1Char('2')) {                   // EIO Ping
        sendRaw(QStringLiteral("3"));                        // Pong
        m_pingTimer->start(m_pingWatchdogMs);
    }
    else if (type == QLatin1Char('4')) {
        handleSocketIO(packet.mid(1));
    }
    else if (type == QLatin1Char('1')) {
        disconnectFromServer();
    }
}

void SocketIOClient::handleSocketIO(const QString& payload)
{
    if (payload.isEmpty()) return;
    const QChar type = payload.at(0);

    if (type == QLatin1Char('0')) {                          // SIO Connected
        m_sioConnected = true;
        emit socketIOConnected();
    }
    else if (type == QLatin1Char('2')) {                   // Incoming event
        QString data = payload.mid(1);
        int ackId = -1;
        const int bracket = data.indexOf(QLatin1Char('['));
        if (bracket > 0) { ackId = data.left(bracket).toInt(); data = data.mid(bracket); }
        parseEvent(data, ackId);
    }
    else if (type == QLatin1Char('3')) {                   // ACK
        QString data = payload.mid(1);
        const int bracket = data.indexOf(QLatin1Char('['));
        if (bracket < 0) return;
        const int id = data.left(bracket).toInt();
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(data.mid(bracket).toUtf8(), &err);
        if (err.error == QJsonParseError::NoError && doc.isArray() && m_ackCallbacks.contains(id))
            m_ackCallbacks.take(id)(doc.array());
    }
    else if (type == QLatin1Char('1')) {
        m_sioConnected = false;
        emit disconnected();
    }
}

void SocketIOClient::parseEvent(const QString& data, int /*ackId*/)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        qWarning() << "[SIO] Parse error:" << err.errorString() << "data:" << data;
        return;
    }
    QJsonArray arr = doc.array();
    if (arr.isEmpty()) return;
    const QString event = arr.at(0).toString();
    arr.removeFirst();
    qInfo() << "[SIO] Incoming event:" << event << "payload:" << QJsonDocument(arr).toJson(QJsonDocument::Compact);
    if (m_listeners.contains(event)) m_listeners[event](arr);
}