#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include "WebRTCEngine.h"

#include <QDebug>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMetaObject>

#ifdef HAVE_LIBDATACHANNEL
#  include <rtc/rtc.hpp>
#endif

// ---------------------------------------------------------------------------
// Send-side constants
// ---------------------------------------------------------------------------
static constexpr qint64  SEND_CHUNK_SIZE = 16384;          // 16 KB
static constexpr size_t  SEND_BUFFER_HIGH = 1 * 1024 * 1024; // 1 MB – pause threshold
static constexpr size_t  SEND_BUFFER_LOW = 256 * 1024;      // 256 KB – resume threshold

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

WebRTCEngine::WebRTCEngine(QObject* parent)
    : QObject(parent)
{
#ifdef HAVE_LIBDATACHANNEL
#  ifdef QT_DEBUG
    rtc::InitLogger(rtc::LogLevel::Debug);
#  else
    rtc::InitLogger(rtc::LogLevel::Warning);
#  endif
    qInfo() << "[WebRTC] Engine initialised (libdatachannel)";
#else
    qWarning() << "[WebRTC] libdatachannel NOT compiled in – stub mode";
#endif
}

WebRTCEngine::~WebRTCEngine()
{
#ifdef HAVE_LIBDATACHANNEL
    QMutexLocker lock(&m_mutex);
    for (auto& s : m_sessions) {
        if (s->controlDc) s->controlDc->close();
        if (s->dataDc)    s->dataDc->close();
        if (s->pc)        s->pc->close();
        if (s->inFile.isOpen())  s->inFile.close();
        if (s->outFile.isOpen()) s->outFile.close();
    }
    m_sessions.clear();
#endif
}

// ---------------------------------------------------------------------------
// Public API – Receive side
// ---------------------------------------------------------------------------

void WebRTCEngine::processOffer(const QString& transferId,
    const QString& fromPeerId,
    const QJsonObject& offer,
    const QString& savePath)
{
#ifdef HAVE_LIBDATACHANNEL
    qInfo() << "[WebRTC] Processing offer for transfer:" << transferId
        << "from:" << fromPeerId
        << "save to:" << savePath;

    const QString sdp = offer.value(QStringLiteral("sdp")).toString();

    if (sdp.isEmpty()) {
        qWarning() << "[WebRTC] Offer SDP is empty for transfer:" << transferId;
        emit transferError(transferId, QStringLiteral("Empty SDP in offer"));
        return;
    }

    auto session = std::make_shared<Session>();
    session->transferId = transferId;
    session->targetPeerId = fromPeerId;
    session->savePath = savePath;
    session->isSender = false;

    {
        QMutexLocker lock(&m_mutex);
        if (m_sessions.contains(transferId)) {
            auto& old = m_sessions[transferId];
            if (old->controlDc) old->controlDc->close();
            if (old->dataDc)    old->dataDc->close();
            if (old->pc)        old->pc->close();
        }
        m_sessions.insert(transferId, session);
    }

    auto pc = std::make_shared<rtc::PeerConnection>(buildConfig());
    session->pc = pc;

    // ── ICE candidate ──────────────────────────────────────────────────────
    pc->onLocalCandidate([this, transferId, fromPeerId]
    (rtc::Candidate candidate) {
            QJsonObject cand;
            cand[QStringLiteral("candidate")] = QString::fromStdString(candidate.candidate());
            cand[QStringLiteral("sdpMid")] = QString::fromStdString(candidate.mid());
            cand[QStringLiteral("sdpMLineIndex")] = 0;

            qDebug() << "[WebRTC] Local ICE candidate for:" << transferId;

            QMetaObject::invokeMethod(this, [this, transferId, fromPeerId, cand]() {
                emit localIceCandidate(transferId, fromPeerId, cand);
                }, Qt::QueuedConnection);
        });

    // ── SDP answer ─────────────────────────────────────────────────────────
    pc->onLocalDescription([this, transferId, fromPeerId]
    (rtc::Description desc) {
            if (desc.type() != rtc::Description::Type::Answer) return;

            QJsonObject answer;
            answer[QStringLiteral("type")] = QStringLiteral("answer");
            answer[QStringLiteral("sdp")] = QString::fromStdString(std::string(desc));

            qInfo() << "[WebRTC] SDP answer ready for transfer:" << transferId;

            QMetaObject::invokeMethod(this, [this, transferId, fromPeerId, answer]() {
                emit answerReady(transferId, fromPeerId, answer);
                }, Qt::QueuedConnection);
        });

    // ── PeerConnection state ───────────────────────────────────────────────
    pc->onStateChange([this, transferId](rtc::PeerConnection::State state) {
        const QString s = [&]() -> QString {
            switch (state) {
            case rtc::PeerConnection::State::New:          return QStringLiteral("New");
            case rtc::PeerConnection::State::Connecting:   return QStringLiteral("Connecting");
            case rtc::PeerConnection::State::Connected:    return QStringLiteral("Connected");
            case rtc::PeerConnection::State::Disconnected: return QStringLiteral("Disconnected");
            case rtc::PeerConnection::State::Failed:       return QStringLiteral("Failed");
            case rtc::PeerConnection::State::Closed:       return QStringLiteral("Closed");
            default:                                       return QStringLiteral("Unknown");
            }
            }();
        qInfo() << "[WebRTC] (receiver) PeerConnection state:" << s
            << "transfer:" << transferId;

        if (state == rtc::PeerConnection::State::Failed) {
            QMetaObject::invokeMethod(this, [this, transferId]() {
                emit transferError(transferId,
                    QStringLiteral("PeerConnection failed"));
                }, Qt::QueuedConnection);
        }
        });

    // ── DataChannel dispatch ───────────────────────────────────────────────
    // "file-data"    – binary chunks (legacy text frames handled here too)
    // "file-control" – reliable ordered JSON control messages
    // Any other label is treated as file-data for backward compatibility.
    pc->onDataChannel([this, transferId, savePath, session]
    (std::shared_ptr<rtc::DataChannel> dc) {

            const QString label = QString::fromStdString(dc->label());
            qInfo() << "[WebRTC] DataChannel arrived – label:" << label
                << "transfer:" << transferId;

            // Shared finalise closure – first caller wins via completed flag.
            auto finalise = [this, session, transferId](const QString& triggeredBy) {
                if (session->completed) return;
                session->completed = true;

                if (session->outFile.isOpen()) {
                    session->outFile.flush();
                    session->outFile.close();
                }
                const QString path = session->savePath;
                qInfo() << "[WebRTC] Receive complete (" << triggeredBy << "):"
                    << transferId
                    << "bytes written:" << session->bytesReceived
                    << "saved to:" << path;
                QMetaObject::invokeMethod(this, [this, transferId, path]() {
                    emit transferComplete(transferId, path);
                    }, Qt::QueuedConnection);
                };

            if (label == QStringLiteral("file-control")) {
                session->controlDc = dc;
                bindControlChannel(dc, session, finalise);
            }
            else {
                // file-data channel (or legacy single-channel sender)
                session->dataDc = dc;

                QMetaObject::invokeMethod(this, [this, transferId]() {
                    emit dataChannelOpen(transferId);
                    }, Qt::QueuedConnection);

                session->outFile.setFileName(savePath);
                if (!session->outFile.open(QIODevice::WriteOnly)) {
                    const QString err =
                        QStringLiteral("Cannot open save file: ") + savePath;
                    qWarning() << "[WebRTC]" << err;
                    QMetaObject::invokeMethod(this, [this, transferId, err]() {
                        emit transferError(transferId, err);
                        }, Qt::QueuedConnection);
                    return;
                }

                bindDataChannel(dc, session, finalise);
            }
        });

    // ── Set remote description ─────────────────────────────────────────────
    try {
        pc->setRemoteDescription(
            rtc::Description(sdp.toStdString(),
                rtc::Description::Type::Offer));
        qInfo() << "[WebRTC] Remote description set – awaiting answer generation";
    }
    catch (const std::exception& e) {
        const QString msg = QString::fromLatin1(e.what());
        qWarning() << "[WebRTC] setRemoteDescription failed:" << msg;
        emit transferError(transferId,
            QStringLiteral("SDP set failed: ") + msg);
    }

#else
    qWarning() << "[WebRTC] STUB: processOffer called – libdatachannel unavailable."
        " transferId:" << transferId;
    Q_UNUSED(fromPeerId) Q_UNUSED(offer) Q_UNUSED(savePath)
        emit transferError(transferId,
            QStringLiteral("WebRTC not available – install libdatachannel"));
#endif
}

// ---------------------------------------------------------------------------
// Public API – Send side
// ---------------------------------------------------------------------------

void WebRTCEngine::initiateSend(const QString& transferId,
    const QString& targetPeerId,
    const QString& filePath)
{
#ifdef HAVE_LIBDATACHANNEL
    const QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isReadable()) {
        qWarning() << "[WebRTC] initiateSend: file not found or unreadable:"
            << filePath;
        emit transferError(transferId,
            QStringLiteral("File not found: ") + filePath);
        return;
    }

    qInfo() << "[WebRTC] Initiating send – transfer:" << transferId
        << "to:" << targetPeerId
        << "file:" << fi.fileName()
        << "size:" << fi.size() << "bytes";

    auto session = std::make_shared<Session>();
    session->transferId = transferId;
    session->targetPeerId = targetPeerId;
    session->filePath = filePath;
    session->fileSize = fi.size();
    session->isSender = true;

    {
        QMutexLocker lock(&m_mutex);
        if (m_sessions.contains(transferId)) {
            auto& old = m_sessions[transferId];
            if (old->controlDc) old->controlDc->close();
            if (old->dataDc)    old->dataDc->close();
            if (old->pc)        old->pc->close();
        }
        m_sessions.insert(transferId, session);
    }

    auto pc = std::make_shared<rtc::PeerConnection>(buildConfig());
    session->pc = pc;

    // ── ICE candidate ──────────────────────────────────────────────────────
    pc->onLocalCandidate([this, transferId, targetPeerId]
    (rtc::Candidate candidate) {
            QJsonObject cand;
            cand[QStringLiteral("candidate")] = QString::fromStdString(candidate.candidate());
            cand[QStringLiteral("sdpMid")] = QString::fromStdString(candidate.mid());
            cand[QStringLiteral("sdpMLineIndex")] = 0;

            QMetaObject::invokeMethod(this, [this, transferId, targetPeerId, cand]() {
                emit localIceCandidate(transferId, targetPeerId, cand);
                }, Qt::QueuedConnection);
        });

    // ── SDP offer ──────────────────────────────────────────────────────────
    pc->onLocalDescription([this, transferId, targetPeerId]
    (rtc::Description desc) {
            if (desc.type() != rtc::Description::Type::Offer) return;

            QJsonObject offer;
            offer[QStringLiteral("type")] = QStringLiteral("offer");
            offer[QStringLiteral("sdp")] = QString::fromStdString(std::string(desc));

            qInfo() << "[WebRTC] SDP offer ready for transfer:" << transferId;

            QMetaObject::invokeMethod(this, [this, transferId, targetPeerId, offer]() {
                emit offerReady(transferId, targetPeerId, offer);
                }, Qt::QueuedConnection);
        });

    // ── PeerConnection state ───────────────────────────────────────────────
    pc->onStateChange([this, transferId](rtc::PeerConnection::State state) {
        const QString s = [&]() -> QString {
            switch (state) {
            case rtc::PeerConnection::State::New:          return QStringLiteral("New");
            case rtc::PeerConnection::State::Connecting:   return QStringLiteral("Connecting");
            case rtc::PeerConnection::State::Connected:    return QStringLiteral("Connected");
            case rtc::PeerConnection::State::Disconnected: return QStringLiteral("Disconnected");
            case rtc::PeerConnection::State::Failed:       return QStringLiteral("Failed");
            case rtc::PeerConnection::State::Closed:       return QStringLiteral("Closed");
            default:                                       return QStringLiteral("Unknown");
            }
            }();
        qInfo() << "[WebRTC] (sender) PeerConnection state:" << s
            << "transfer:" << transferId;

        if (state == rtc::PeerConnection::State::Failed) {
            QMetaObject::invokeMethod(this, [this, transferId]() {
                emit transferError(transferId,
                    QStringLiteral("PeerConnection failed"));
                }, Qt::QueuedConnection);
        }
        });

    // ── Create DataChannels as offerer ─────────────────────────────────────
    // file-control is created first so it receives the lower stream ID,
    // which ensures the receiver's onDataChannel dispatch works correctly.
    auto controlDc = pc->createDataChannel("file-control");
    auto dataDc = pc->createDataChannel("file-data");

    session->controlDc = controlDc;
    session->dataDc = dataDc;

    // ── file-control open/message/error ───────────────────────────────────
    controlDc->onOpen([this, session, transferId]() {
        qInfo() << "[WebRTC] (sender) file-control open – transfer:" << transferId;
        // When both DCs are open start sending.
        if (++session->openDcCount == 2) {
            QMetaObject::invokeMethod(this, [this, session]() {
                startFileSend(session);
                }, Qt::QueuedConnection);
        }
        });

    controlDc->onMessage([this, transferId, session]
    (rtc::message_variant data) {
            // The receiver sends back progress-ack and cancel messages here.
            if (!std::holds_alternative<rtc::string>(data)) return;

            const QString text =
                QString::fromStdString(std::get<rtc::string>(data));
            const QJsonObject msg =
                QJsonDocument::fromJson(text.toUtf8()).object();
            const QString type =
                msg.value(QStringLiteral("type")).toString();

            if (type == QStringLiteral("progress-ack")) {
                const int    chunks = msg.value(QStringLiteral("chunksReceived")).toInt();
                const double progress = msg.value(QStringLiteral("progress")).toDouble();
                qDebug() << "[WebRTC] (sender) progress-ack – transfer:" << transferId
                    << "chunks:" << chunks
                    << QString::number(progress, 'f', 1) + QStringLiteral("%");
            }
            else if (type == QStringLiteral("cancel")) {
                const QString reason =
                    msg.value(QStringLiteral("reason"))
                    .toString(QStringLiteral("cancelled by receiver"));
                qWarning() << "[WebRTC] (sender) receiver cancelled:"
                    << transferId << reason;
                QMetaObject::invokeMethod(this, [this, transferId, reason]() {
                    emit peerCancelled(transferId, reason);
                    }, Qt::QueuedConnection);
            }
            else {
                qDebug() << "[WebRTC] (sender) unknown control message:"
                    << type << "transfer:" << transferId;
            }
        });

    controlDc->onClosed([transferId]() {
        qDebug() << "[WebRTC] (sender) file-control closed – transfer:" << transferId;
        });

    controlDc->onError([transferId](const std::string& err) {
        qWarning() << "[WebRTC] (sender) file-control error:"
            << QString::fromStdString(err)
            << "transfer:" << transferId;
        });

    // ── file-data open/error ───────────────────────────────────────────────
    dataDc->onOpen([this, session, transferId]() {
        qInfo() << "[WebRTC] (sender) file-data open – transfer:" << transferId;
        QMetaObject::invokeMethod(this, [this, transferId]() {
            emit dataChannelOpen(transferId);
            }, Qt::QueuedConnection);
        // When both DCs are open start sending.
        if (++session->openDcCount == 2) {
            QMetaObject::invokeMethod(this, [this, session]() {
                startFileSend(session);
                }, Qt::QueuedConnection);
        }
        });

    // Flow-control: resume sending when the send-buffer drains.
    dataDc->setBufferedAmountLowThreshold(SEND_BUFFER_LOW);
    dataDc->onBufferedAmountLow([this, session, transferId]() {
        qDebug() << "[WebRTC] (sender) buffer low – resuming chunks:"
            << transferId;
        QMetaObject::invokeMethod(this, [this, session]() {
            sendNextChunks(session);
            }, Qt::QueuedConnection);
        });

    dataDc->onClosed([transferId]() {
        qInfo() << "[WebRTC] (sender) file-data closed – transfer:" << transferId;
        });

    dataDc->onError([this, transferId](const std::string& err) {
        const QString msg = QString::fromStdString(err);
        qWarning() << "[WebRTC] (sender) file-data error:" << msg
            << "transfer:" << transferId;
        QMetaObject::invokeMethod(this, [this, transferId, msg]() {
            emit transferError(transferId, msg);
            }, Qt::QueuedConnection);
        });

    qInfo() << "[WebRTC] DataChannels created – waiting for ICE + DTLS:"
        << transferId;

#else
    qWarning() << "[WebRTC] STUB: initiateSend called – libdatachannel unavailable."
        " transferId:" << transferId;
    Q_UNUSED(targetPeerId) Q_UNUSED(filePath)
        emit transferError(transferId,
            QStringLiteral("WebRTC not available – install libdatachannel"));
#endif
}

void WebRTCEngine::applyAnswer(const QString& transferId,
    const QJsonObject& answer)
{
#ifdef HAVE_LIBDATACHANNEL
    std::shared_ptr<Session> session;
    {
        QMutexLocker lock(&m_mutex);
        if (!m_sessions.contains(transferId)) {
            qWarning() << "[WebRTC] applyAnswer: unknown transferId:" << transferId;
            return;
        }
        session = m_sessions[transferId];
    }

    if (!session->isSender) {
        qWarning() << "[WebRTC] applyAnswer called on a receiver-side session:"
            << transferId;
        return;
    }

    const QString sdp = answer.value(QStringLiteral("sdp")).toString();
    if (sdp.isEmpty()) {
        qWarning() << "[WebRTC] applyAnswer: empty SDP for transfer:" << transferId;
        emit transferError(transferId, QStringLiteral("Empty SDP in answer"));
        return;
    }

    try {
        session->pc->setRemoteDescription(
            rtc::Description(sdp.toStdString(),
                rtc::Description::Type::Answer));
        qInfo() << "[WebRTC] Remote answer applied – transfer:" << transferId;
    }
    catch (const std::exception& e) {
        const QString msg = QString::fromLatin1(e.what());
        qWarning() << "[WebRTC] applyAnswer failed:" << msg
            << "transfer:" << transferId;
        emit transferError(transferId,
            QStringLiteral("applyAnswer failed: ") + msg);
    }
#else
    Q_UNUSED(transferId) Q_UNUSED(answer)
#endif
}

// ---------------------------------------------------------------------------
// Public API – Shared
// ---------------------------------------------------------------------------

void WebRTCEngine::addIceCandidate(const QString& transferId,
    const QJsonObject& candidate)
{
#ifdef HAVE_LIBDATACHANNEL
    QMutexLocker lock(&m_mutex);
    if (!m_sessions.contains(transferId)) {
        qWarning() << "[WebRTC] addIceCandidate: unknown transferId:" << transferId;
        return;
    }
    auto& session = m_sessions[transferId];
    if (!session->pc) return;

    const std::string cand =
        candidate.value(QStringLiteral("candidate")).toString().toStdString();
    const std::string mid =
        candidate.value(QStringLiteral("sdpMid")).toString().toStdString();

    try {
        session->pc->addRemoteCandidate(rtc::Candidate(cand, mid));
        qDebug() << "[WebRTC] Remote ICE candidate added for:" << transferId;
    }
    catch (const std::exception& e) {
        qWarning() << "[WebRTC] addRemoteCandidate failed:"
            << e.what() << "transfer:" << transferId;
    }
#else
    Q_UNUSED(transferId) Q_UNUSED(candidate)
#endif
}

void WebRTCEngine::closeTransfer(const QString& transferId)
{
#ifdef HAVE_LIBDATACHANNEL
    QMutexLocker lock(&m_mutex);
    if (!m_sessions.contains(transferId)) return;
    auto& s = m_sessions[transferId];
    if (s->controlDc) { s->controlDc->close(); s->controlDc.reset(); }
    if (s->dataDc) { s->dataDc->close();    s->dataDc.reset(); }
    if (s->pc) { s->pc->close();        s->pc.reset(); }
    if (s->inFile.isOpen())  s->inFile.close();
    if (s->outFile.isOpen()) s->outFile.close();
    m_sessions.remove(transferId);
    qInfo() << "[WebRTC] Session closed for:" << transferId;
#else
    Q_UNUSED(transferId)
#endif
}

void WebRTCEngine::sendProgressUpdate(const QString& transferId,
    int chunksReceived, double progress)
{
#ifdef HAVE_LIBDATACHANNEL
    QMutexLocker lock(&m_mutex);
    if (!m_sessions.contains(transferId)) return;
    auto& s = m_sessions[transferId];
    if (!s->controlDc || !s->controlDc->isOpen()) return;

    const QJsonObject msg{
        { QStringLiteral("type"),           QStringLiteral("progress-ack") },
        { QStringLiteral("chunksReceived"), chunksReceived                  },
        { QStringLiteral("progress"),       progress                        }
    };
    const std::string json =
        QJsonDocument(msg).toJson(QJsonDocument::Compact).toStdString();
    try {
        s->controlDc->send(json);
        qDebug() << "[WebRTC] progress-ack sent – transfer:" << transferId
            << "chunks:" << chunksReceived
            << QString::number(progress, 'f', 1) + QStringLiteral("%");
    }
    catch (const std::exception& e) {
        qWarning() << "[WebRTC] sendProgressUpdate failed:"
            << e.what() << "transfer:" << transferId;
    }
#else
    Q_UNUSED(transferId) Q_UNUSED(chunksReceived) Q_UNUSED(progress)
#endif
}

void WebRTCEngine::sendCancelToSender(const QString& transferId,
    const QString& reason)
{
#ifdef HAVE_LIBDATACHANNEL
    QMutexLocker lock(&m_mutex);
    if (!m_sessions.contains(transferId)) return;
    auto& s = m_sessions[transferId];
    if (!s->controlDc || !s->controlDc->isOpen()) {
        qDebug() << "[WebRTC] sendCancelToSender: control channel unavailable"
            << "(legacy sender?) – transfer:" << transferId;
        return;
    }

    const QJsonObject msg{
        { QStringLiteral("type"),   QStringLiteral("cancel") },
        { QStringLiteral("reason"), reason                   }
    };
    const std::string json =
        QJsonDocument(msg).toJson(QJsonDocument::Compact).toStdString();
    try {
        s->controlDc->send(json);
        qInfo() << "[WebRTC] cancel sent to sender – transfer:" << transferId
            << "reason:" << reason;
    }
    catch (const std::exception& e) {
        qWarning() << "[WebRTC] sendCancelToSender failed:"
            << e.what() << "transfer:" << transferId;
    }
    // Caller must call closeTransfer() to release the session.
#else
    Q_UNUSED(transferId) Q_UNUSED(reason)
#endif
}

// ---------------------------------------------------------------------------
// Private helpers – Send side
// ---------------------------------------------------------------------------

#ifdef HAVE_LIBDATACHANNEL

void WebRTCEngine::startFileSend(std::shared_ptr<Session> session)
{
    // Must be called on the Qt thread.
    const QString transferId = session->transferId;

    qInfo() << "[WebRTC] Starting file send – transfer:" << transferId
        << "file:" << QFileInfo(session->filePath).fileName()
        << "size:" << session->fileSize << "bytes";

    session->inFile.setFileName(session->filePath);
    if (!session->inFile.open(QIODevice::ReadOnly)) {
        const QString err =
            QStringLiteral("Cannot open file for reading: ") + session->filePath;
        qWarning() << "[WebRTC]" << err;
        emit transferError(transferId, err);
        return;
    }

    // Announce the transfer to the receiver over the control channel.
    const QJsonObject startMsg{
        { QStringLiteral("type"), QStringLiteral("file-start")                       },
        { QStringLiteral("size"), static_cast<double>(session->fileSize)             },
        { QStringLiteral("name"), QFileInfo(session->filePath).fileName()            }
    };
    const std::string startJson =
        QJsonDocument(startMsg).toJson(QJsonDocument::Compact).toStdString();

    try {
        session->controlDc->send(startJson);
        qInfo() << "[WebRTC] file-start sent – transfer:" << transferId;
    }
    catch (const std::exception& e) {
        const QString err = QString::fromLatin1(e.what());
        qWarning() << "[WebRTC] Failed to send file-start:" << err;
        emit transferError(transferId,
            QStringLiteral("Failed to send file-start: ") + err);
        return;
    }

    sendNextChunks(session);
}

void WebRTCEngine::sendNextChunks(std::shared_ptr<Session> session)
{
    // Must be called on the Qt thread.
    // Sends chunks until the DataChannel send-buffer reaches the high
    // watermark, then returns. onBufferedAmountLow will re-trigger this.

    const QString transferId = session->transferId;

    if (!session->dataDc || !session->dataDc->isOpen() || session->completed)
        return;

    while (!session->completed
        && session->dataDc->isOpen()
        && session->dataDc->bufferedAmount() < SEND_BUFFER_HIGH)
    {
        if (!session->inFile.isOpen()) return;

        const QByteArray chunk = session->inFile.read(SEND_CHUNK_SIZE);

        if (chunk.isEmpty()) {
            // ── EOF: send file-end and finalise ───────────────────────────
            session->inFile.close();

            const QJsonObject endMsg{
                { QStringLiteral("type"), QStringLiteral("file-end") }
            };
            const std::string endJson =
                QJsonDocument(endMsg).toJson(QJsonDocument::Compact).toStdString();

            try {
                if (session->controlDc && session->controlDc->isOpen())
                    session->controlDc->send(endJson);
                qInfo() << "[WebRTC] file-end sent – transfer:" << transferId
                    << "total bytes sent:" << session->bytesSent;
            }
            catch (const std::exception& e) {
                qWarning() << "[WebRTC] Failed to send file-end:"
                    << e.what() << "transfer:" << transferId;
            }

            if (!session->completed) {
                session->completed = true;
                const QString path = session->filePath;
                emit transferComplete(transferId, path);
            }
            return;
        }

        // ── Send binary chunk ──────────────────────────────────────────────
        rtc::binary bin(chunk.size());
        std::memcpy(bin.data(), chunk.constData(),
            static_cast<size_t>(chunk.size()));

        try {
            session->dataDc->send(bin);
        }
        catch (const std::exception& e) {
            const QString msg = QString::fromLatin1(e.what());
            qWarning() << "[WebRTC] Chunk send failed:" << msg
                << "transfer:" << transferId;
            emit transferError(transferId,
                QStringLiteral("Send failed: ") + msg);
            return;
        }

        session->bytesSent += chunk.size();

        const qint64 sent = session->bytesSent;
        const qint64 total = session->fileSize;
        emit progressUpdate(transferId, sent, total);
    }

    qDebug() << "[WebRTC] (sender) buffer high-watermark reached – pausing:"
        << transferId
        << "sent:" << session->bytesSent << "/" << session->fileSize;
}

// ---------------------------------------------------------------------------
// Private helpers – Receive side
// ---------------------------------------------------------------------------

void WebRTCEngine::bindDataChannel(
    std::shared_ptr<rtc::DataChannel>    dc,
    std::shared_ptr<Session>             session,
    std::function<void(const QString&)>  finalise)
{
    const QString transferId = session->transferId;

    dc->onMessage([this, transferId, session, finalise]
    (rtc::message_variant data) {

            // ── TEXT: legacy senders put control messages on the data channel ──
            if (std::holds_alternative<rtc::string>(data)) {
                const QString text =
                    QString::fromStdString(std::get<rtc::string>(data));
                qDebug() << "[WebRTC] (file-data) legacy text frame:"
                    << text.left(120);

                const QJsonObject msg =
                    QJsonDocument::fromJson(text.toUtf8()).object();
                const QString type =
                    msg.value(QStringLiteral("type")).toString();

                if (type == QStringLiteral("file-start")) {
                    session->totalBytes =
                        static_cast<qint64>(
                            msg.value(QStringLiteral("size")).toDouble());
                    session->fileStartReceived = true;
                    qInfo() << "[WebRTC] (legacy) file-start – total bytes:"
                        << session->totalBytes
                        << "transfer:" << transferId;
                }
                else if (type == QStringLiteral("file-end")) {
                    qInfo() << "[WebRTC] (legacy) file-end – finalising:"
                        << transferId;
                    finalise(QStringLiteral("file-end (legacy)"));
                }
                return;
            }

            // ── BINARY: raw file data chunks ───────────────────────────────────
            if (std::holds_alternative<rtc::binary>(data)) {
                if (session->completed) return;

                const auto& bin = std::get<rtc::binary>(data);
                qint64 writeSize = static_cast<qint64>(bin.size());

                if (session->totalBytes > 0) {
                    const qint64 remaining =
                        session->totalBytes - session->bytesReceived;
                    if (remaining <= 0) return;
                    writeSize = std::min(writeSize, remaining);
                }

                const QByteArray chunk(
                    reinterpret_cast<const char*>(bin.data()),
                    static_cast<qsizetype>(writeSize));
                session->outFile.write(chunk);
                session->bytesReceived += writeSize;

                const qint64 recv = session->bytesReceived;
                const qint64 total = session->totalBytes;

                QMetaObject::invokeMethod(this, [this, transferId, recv, total]() {
                    emit progressUpdate(transferId, recv, total);
                    }, Qt::QueuedConnection);

                // Byte-count fallback – fires only if file-end never arrives.
                if (total > 0 && recv >= total)
                    finalise(QStringLiteral("byte-count"));
            }
        });

    dc->onClosed([transferId, session]() {
        qInfo() << "[WebRTC] file-data channel closed for:" << transferId;
        if (session->outFile.isOpen())
            session->outFile.close();
        });

    dc->onError([this, transferId](const std::string& err) {
        const QString msg = QString::fromStdString(err);
        qWarning() << "[WebRTC] file-data channel error:" << msg
            << "transfer:" << transferId;
        QMetaObject::invokeMethod(this, [this, transferId, msg]() {
            emit transferError(transferId, msg);
            }, Qt::QueuedConnection);
        });
}

void WebRTCEngine::bindControlChannel(
    std::shared_ptr<rtc::DataChannel>    dc,
    std::shared_ptr<Session>             session,
    std::function<void(const QString&)>  finalise)
{
    const QString transferId = session->transferId;

    dc->onMessage([this, transferId, session, finalise]
    (rtc::message_variant data) {

            if (!std::holds_alternative<rtc::string>(data)) {
                qWarning() << "[WebRTC] Unexpected binary frame on file-control"
                    << "channel for transfer:" << transferId;
                return;
            }

            const QString text =
                QString::fromStdString(std::get<rtc::string>(data));
            qDebug() << "[WebRTC] (file-control) message:" << text.left(200);

            const QJsonObject msg =
                QJsonDocument::fromJson(text.toUtf8()).object();
            const QString type =
                msg.value(QStringLiteral("type")).toString();

            if (type == QStringLiteral("file-start")) {
                session->totalBytes =
                    static_cast<qint64>(
                        msg.value(QStringLiteral("size")).toDouble());
                session->fileStartReceived = true;
                qInfo() << "[WebRTC] file-start – total bytes:"
                    << session->totalBytes
                    << "transfer:" << transferId;
            }
            else if (type == QStringLiteral("file-end")) {
                qInfo() << "[WebRTC] file-end received – finalising:" << transferId;
                finalise(QStringLiteral("file-end"));
            }
            else if (type == QStringLiteral("progress-ack")) {
                // Receiver reporting back – log only on this (sender) side.
                qDebug() << "[WebRTC] progress-ack from receiver – transfer:"
                    << transferId
                    << "chunks:" << msg.value(QStringLiteral("chunksReceived")).toInt()
                    << QString::number(
                        msg.value(QStringLiteral("progress")).toDouble(), 'f', 1)
                    + QStringLiteral("%");
            }
            else if (type == QStringLiteral("cancel")) {
                const QString reason =
                    msg.value(QStringLiteral("reason"))
                    .toString(QStringLiteral("cancelled by receiver"));
                qWarning() << "[WebRTC] Transfer cancelled by peer:"
                    << transferId << reason;
                QMetaObject::invokeMethod(this, [this, transferId, reason]() {
                    emit peerCancelled(transferId, reason);
                    }, Qt::QueuedConnection);
            }
            else {
                qDebug() << "[WebRTC] Unknown control message type:"
                    << type << "transfer:" << transferId;
            }
        });

    dc->onClosed([transferId]() {
        qDebug() << "[WebRTC] file-control channel closed for:" << transferId;
        });

    dc->onError([transferId](const std::string& err) {
        // Non-fatal – data channel may still be intact.
        qWarning() << "[WebRTC] file-control channel error:"
            << QString::fromStdString(err)
            << "transfer:" << transferId;
        });
}

rtc::Configuration WebRTCEngine::buildConfig() const
{
    rtc::Configuration config;
    config.iceServers.emplace_back("stun:stun.l.google.com:19302");
    config.iceServers.emplace_back("stun:stun1.l.google.com:19302");
    config.maxMessageSize = 65536;
    return config;
}

#endif