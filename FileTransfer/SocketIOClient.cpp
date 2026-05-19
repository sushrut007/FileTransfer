// Must come before ANY Windows or Qt headers to prevent macro collisions
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
#include <QDebug>

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

SocketIOClient::SocketIOClient(QObject* parent)
    : QObject(parent)
    , m_ws(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
    , m_pingTimer(new QTimer(this))
{
    connect(m_ws, &QWebSocket::connected,
        this, &SocketIOClient::onWsConnected);
    connect(m_ws, &QWebSocket::disconnected,
        this, &SocketIOClient::onWsDisconnected);
    connect(m_ws, &QWebSocket::textMessageReceived,
        this, &SocketIOClient::onTextMessageReceived);
    connect(m_ws, &QWebSocket::errorOccurred,
        this, &SocketIOClient::onWsErrorOccurred);

    // Engine.IO v4: server sends ping every pingInterval ms.
    // If the server's ping goes unanswered within pingTimeout, it disconnects.
    // m_pingTimer is used as a WATCHDOG – if no ping arrives from the server
    // within (pingInterval + pingTimeout) ms, we reconnect proactively.
    connect(m_pingTimer, &QTimer::timeout,
        this, &SocketIOClient::onPingWatchdog);
}

SocketIOClient::~SocketIOClient()
{
    disconnectFromServer();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void SocketIOClient::connectToServer(const QString& url, const QString& cookie)
{
    m_cookie = cookie;

    QString wsUrl = url;
    wsUrl.replace(QRegularExpression(QStringLiteral("^http")),
        QStringLiteral("ws"));
    while (wsUrl.endsWith(QLatin1Char('/')))
        wsUrl.chop(1);
    wsUrl += QStringLiteral("/socket.io/?EIO=4&transport=websocket");

    QNetworkRequest request;
    request.setUrl(QUrl(wsUrl));
    request.setRawHeader(QByteArrayLiteral("User-Agent"),
        QByteArrayLiteral("FileTransferQt/1.0"));
    if (!m_cookie.isEmpty())
        request.setRawHeader(QByteArrayLiteral("Cookie"),
            m_cookie.toUtf8());

    qDebug() << "[WS] Connecting to:" << wsUrl;
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
    if (!m_sioConnected) {
        qWarning() << "[SIO] Cannot sendEvent – not connected:" << event;
        return;
    }

    QJsonArray arr;
    arr.append(event);
    arr.append(payload);

    QString packet = QStringLiteral("42"); // EIO Message + SIO Event

    if (ack) {
        const int id = m_ackCounter++;
        m_ackCallbacks.insert(id, ack);
        packet += QString::number(id);
    }

    packet += QString::fromUtf8(
        QJsonDocument(arr).toJson(QJsonDocument::Compact));
    sendRaw(packet);
    qDebug() << "[SIO] → sendEvent:" << event;
}

void SocketIOClient::on(const QString& event,
    std::function<void(const QJsonArray&)> callback)
{
    m_listeners.insert(event, callback);
}

bool SocketIOClient::isConnected() const
{
    return m_sioConnected;
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void SocketIOClient::onWsConnected()
{
    qDebug() << "[WS] WebSocket connected – awaiting Engine.IO handshake";
    emit connected();
}

void SocketIOClient::onWsDisconnected()
{
    qDebug() << "[WS] WebSocket disconnected";
    m_pingTimer->stop();
    m_sioConnected = false;
    emit disconnected();
}

void SocketIOClient::onWsErrorOccurred(QAbstractSocket::SocketError /*error*/)
{
    const QString msg = m_ws->errorString();
    qWarning() << "[WS] Error:" << msg;
    emit errorOccurred(msg);
}

void SocketIOClient::onTextMessageReceived(const QString& message)
{
    if (!message.isEmpty())
        handleEngineIO(message);
}

void SocketIOClient::onPingWatchdog()
{
    // Server ping watchdog fired – no ping received in time.
    // Non-fatal on a local server; logged at debug level only.
    qDebug() << "[EIO] Ping watchdog fired – no ping from server in"
        << m_pingWatchdogMs << "ms. Connection may be stale.";
}

// ---------------------------------------------------------------------------
// Protocol handling
// ---------------------------------------------------------------------------

void SocketIOClient::sendRaw(const QString& data)
{
    if (m_ws->state() == QAbstractSocket::ConnectedState)
        m_ws->sendTextMessage(data);
}

void SocketIOClient::handleEngineIO(const QString& packet)
{
    if (packet.isEmpty()) return;

    const QChar type = packet.at(0);

    if (type == QLatin1Char('0'))
    {
        // Engine.IO Open – parse handshake JSON
        QJsonParseError err;
        const QJsonDocument doc =
            QJsonDocument::fromJson(packet.mid(1).toUtf8(), &err);

        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject obj = doc.object();
            m_pingInterval = obj.value(QStringLiteral("pingInterval")).toInt(25000);
            m_pingTimeout = obj.value(QStringLiteral("pingTimeout")).toInt(20000);
        }

        // Engine.IO v4: server sends ping, client replies pong.
        // Set watchdog to (pingInterval + pingTimeout + 2s grace).
        m_pingWatchdogMs = m_pingInterval + m_pingTimeout + 2000;
        m_pingTimer->start(m_pingWatchdogMs);

        qDebug() << "[EIO] Open – pingInterval:" << m_pingInterval
            << "ms  pingTimeout:" << m_pingTimeout
            << "ms  watchdog:" << m_pingWatchdogMs << "ms";

        // Send Socket.IO connect packet for default namespace
        sendRaw(QStringLiteral("40"));
        qDebug() << "[SIO] → Connect (namespace /)";
    }
    else if (type == QLatin1Char('2'))
    {
        // Engine.IO v4: SERVER sends ping '2', CLIENT must reply pong '3'
        qDebug() << "[EIO] ← ping from server → sending pong";
        sendRaw(QStringLiteral("3"));

        // Reset the watchdog each time a ping is received
        m_pingTimer->start(m_pingWatchdogMs);
    }
    else if (type == QLatin1Char('3'))
    {
        // Engine.IO v4: pong from server (response to a client-initiated ping)
        // In v4 the server does NOT send unsolicited pongs, so this is
        // logged but no action is needed.
        qDebug() << "[EIO] ← pong from server (unexpected in EIO v4)";
    }
    else if (type == QLatin1Char('4'))
    {
        handleSocketIO(packet.mid(1));
    }
    else if (type == QLatin1Char('1'))
    {
        qDebug() << "[EIO] Server close";
        disconnectFromServer();
    }
}

void SocketIOClient::handleSocketIO(const QString& payload)
{
    if (payload.isEmpty()) return;

    const QChar type = payload.at(0);

    if (type == QLatin1Char('0'))
    {
        m_sioConnected = true;
        qDebug() << "[SIO] ← Connected to namespace /";
        emit socketIOConnected();
    }
    else if (type == QLatin1Char('2'))
    {
        // Incoming event – format: 2[ackId]["event", data]
        QString data = payload.mid(1);
        int     ackId = -1;
        const int bracket = data.indexOf(QLatin1Char('['));
        if (bracket > 0) {
            ackId = data.left(bracket).toInt();
            data = data.mid(bracket);
        }
        parseEvent(data, ackId);
    }
    else if (type == QLatin1Char('3'))
    {
        // ACK for our sendEvent
        QString data = payload.mid(1);
        const int bracket = data.indexOf(QLatin1Char('['));
        if (bracket < 0) return;

        const int     id = data.left(bracket).toInt();
        const QString json = data.mid(bracket);

        QJsonParseError err;
        const QJsonDocument doc =
            QJsonDocument::fromJson(json.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isArray()) return;

        if (m_ackCallbacks.contains(id)) {
            m_ackCallbacks.take(id)(doc.array());
            qDebug() << "[SIO] ← ACK for id:" << id;
        }
    }
    else if (type == QLatin1Char('4'))
    {
        qWarning() << "[SIO] Connect error:" << payload.mid(1);
        emit errorOccurred(
            QStringLiteral("Socket.IO connect error: ") + payload.mid(1));
    }
    else if (type == QLatin1Char('1'))
    {
        qDebug() << "[SIO] Server disconnected namespace";
        m_sioConnected = false;
        emit disconnected();
    }
}

void SocketIOClient::parseEvent(const QString& data, int /*ackId*/)
{
    QJsonParseError err;
    const QJsonDocument doc =
        QJsonDocument::fromJson(data.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) return;

    QJsonArray arr = doc.array();
    if (arr.isEmpty()) return;

    const QString event = arr.at(0).toString();
    arr.removeFirst();

    qDebug() << "[SIO] ← event:" << event;

    if (m_listeners.contains(event))
        m_listeners[event](arr);
}