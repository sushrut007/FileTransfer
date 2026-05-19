#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include "FileTransferManager.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QUuid>

static constexpr qint64 CHUNK_SIZE = 16384; // 16 KB – must match WebRTCEngine

FileTransferManager::FileTransferManager(SocketIOClient* sio,
    const QString& myPeerId,
    QObject* parent)
    : QObject(parent)
    , m_sio(sio)
    , m_webrtc(new WebRTCEngine(this))
    , m_myPeerId(myPeerId)
{
    qRegisterMetaType<FileTransferManager::TransferInfo>();

    registerListeners();
    wireWebRtcSignals();

    qInfo() << "[FT] FileTransferManager ready – myPeerId:" << m_myPeerId;
}

// ---------------------------------------------------------------------------
// Private setup
// ---------------------------------------------------------------------------

void FileTransferManager::wireWebRtcSignals()
{
    connect(m_webrtc, &WebRTCEngine::answerReady,
        this, [this](const QString& transferId,
            const QString& targetPeerId,
            const QJsonObject& answer) {
                qInfo() << "[FT] WebRTC answer ready – relaying:" << transferId;
                sendWebRtcAnswer(transferId, targetPeerId, answer);
        });

    connect(m_webrtc, &WebRTCEngine::offerReady,
        this, [this](const QString& transferId,
            const QString& targetPeerId,
            const QJsonObject& offer) {
                qInfo() << "[FT] WebRTC offer ready – relaying to peer:"
                    << targetPeerId << "transfer:" << transferId;
                sendWebRtcOffer(transferId, targetPeerId, offer);
        });

    connect(m_webrtc, &WebRTCEngine::localIceCandidate,
        this, [this](const QString& transferId,
            const QString& targetPeerId,
            const QJsonObject& cand) {
                qDebug() << "[FT] Local ICE candidate – relaying:" << transferId;
                sendIceCandidate(transferId, targetPeerId, cand);
        });

    connect(m_webrtc, &WebRTCEngine::dataChannelOpen,
        this, [this](const QString& transferId) {
            qInfo() << "[FT] DataChannel OPEN – transfer active:" << transferId;
            if (m_transfers.contains(transferId))
                m_transfers[transferId].status = QStringLiteral("active");
            emit dataChannelOpen(transferId);
        });

    connect(m_webrtc, &WebRTCEngine::progressUpdate,
        this, [this](const QString& transferId,
            qint64 bytesProcessed, qint64 totalBytes) {

                const double pct =
                    (totalBytes > 0)
                    ? (static_cast<double>(bytesProcessed) /
                        static_cast<double>(totalBytes)) * 100.0
                    : 0.0;

                const int chunks =
                    static_cast<int>(bytesProcessed / CHUNK_SIZE);

                const int totalChunks =
                    m_transfers.contains(transferId)
                    ? m_transfers[transferId].totalChunks : 0;

                if (m_transfers.contains(transferId))
                    m_transfers[transferId].progress = pct;

                emit transferProgress(transferId, pct, chunks, totalChunks);

                // Receive side only: ack progress back over control channel.
                if (m_transfers.contains(transferId)
                    && m_transfers[transferId].direction == Direction::Incoming)
                {
                    m_webrtc->sendProgressUpdate(transferId, chunks, pct);
                }
                else if (m_transfers.contains(transferId)
                    && m_transfers[transferId].direction == Direction::Outgoing)
                {
                    // Forward send-side progress as dedicated signal so the
                    // UI send-progress handler updates the table correctly.
                    emit sendProgress(transferId, pct, chunks, totalChunks);
                }
        });

    connect(m_webrtc, &WebRTCEngine::transferComplete,
        this, [this](const QString& transferId, const QString& filePath) {
            if (!m_transfers.contains(transferId)) return;
            TransferInfo& info = m_transfers[transferId];
            info.status = QStringLiteral("completed");
            info.progress = 100.0;
            qInfo() << "[FT] \u2705 Transfer complete:" << transferId
                << "path:" << filePath;
            if (info.direction == Direction::Outgoing)
                emit sendComplete(transferId);
            else
                emit transferComplete(transferId, filePath);
        });

    connect(m_webrtc, &WebRTCEngine::transferError,
        this, [this](const QString& transferId, const QString& reason) {
            if (m_transfers.contains(transferId)) {
                m_transfers[transferId].status = QStringLiteral("error");
                if (m_transfers[transferId].direction == Direction::Outgoing)
                    emit sendError(transferId, reason);
                else
                    emit transferError(transferId, reason);
            }
            else {
                emit transferError(transferId, reason);
            }
            qWarning() << "[FT] WebRTC error:" << transferId << reason;
        });

    connect(m_webrtc, &WebRTCEngine::peerCancelled,
        this, [this](const QString& transferId, const QString& reason) {
            qWarning() << "[FT] Transfer cancelled by peer:"
                << transferId << reason;
            m_webrtc->closeTransfer(transferId);
            m_transfers.remove(transferId);
            emit transferCancelled(transferId);
        });
}

void FileTransferManager::registerListeners()
{
    m_sio->on(QStringLiteral("file-offer"),
        [this](const QJsonArray& a) { onFileOffer(a); });
    m_sio->on(QStringLiteral("file-accepted"),
        [this](const QJsonArray& a) { onFileAccepted(a); });
    m_sio->on(QStringLiteral("file-rejected"),
        [this](const QJsonArray& a) { onFileRejected(a); });
    m_sio->on(QStringLiteral("file-complete"),
        [this](const QJsonArray& a) { onFileComplete(a); });
    m_sio->on(QStringLiteral("webrtc-offer"),
        [this](const QJsonArray& a) { onWebRtcOffer(a); });
    m_sio->on(QStringLiteral("webrtc-answer"),
        [this](const QJsonArray& a) { onWebRtcAnswer(a); });
    m_sio->on(QStringLiteral("webrtc-ice-candidate"),
        [this](const QJsonArray& a) { onIceCandidate(a); });
}

// ---------------------------------------------------------------------------
// Public API – Send side
// ---------------------------------------------------------------------------

void FileTransferManager::sendFile(const QString& targetPeerId,
    const QString& filePath)
{
    const QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isReadable()) {
        qWarning() << "[FT] sendFile: file not found or unreadable:" << filePath;
        emit transferError(QString{},
            QStringLiteral("File not found: ") + filePath);
        return;
    }

    TransferInfo info;
    info.transferId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    info.name = fi.fileName();
    info.size = fi.size();
    info.mimeType = QMimeDatabase().mimeTypeForFile(fi).name();
    info.peerId = targetPeerId;
    info.totalChunks = static_cast<int>(
        (fi.size() + CHUNK_SIZE - 1) / CHUNK_SIZE);
    info.filePath = filePath;
    info.status = QStringLiteral("offering");
    info.direction = Direction::Outgoing;

    m_transfers.insert(info.transferId, info);

    qInfo() << "[FT] \U0001F4E4 Sending file-offer:"
        << info.name
        << QString::number(info.size / 1024.0, 'f', 1) + QStringLiteral(" KB")
        << "to peer:" << targetPeerId
        << "chunks:" << info.totalChunks
        << "transferId:" << info.transferId;

    // ── Notify the UI immediately so the row appears before the ACK ───────
    // This was previously missing, which caused outgoing transfers to never
    // appear in the transfer table.
    emit outgoingFileOfferSent(info.transferId);

    m_sio->sendEvent(QStringLiteral("file-offer"),
        QJsonObject{
            { QStringLiteral("transferId"),   info.transferId  },
            { QStringLiteral("targetPeerId"), targetPeerId     },
            { QStringLiteral("name"),         info.name        },
            { QStringLiteral("size"),   static_cast<double>(info.size) },
            { QStringLiteral("type"),         info.mimeType    },
            { QStringLiteral("totalChunks"),  info.totalChunks },
            { QStringLiteral("senderId"),     m_myPeerId       }
        },
        [this, transferId = info.transferId](const QJsonArray& args) {
            if (args.isEmpty()) return;
            const QJsonObject resp = args.at(0).toObject();
            if (!resp.value(QStringLiteral("success")).toBool()) {
                const QString msg =
                    resp.value(QStringLiteral("message"))
                    .toString(QStringLiteral("file-offer rejected by server"));
                qWarning() << "[FT] file-offer server ACK failed:" << msg;
                m_transfers.remove(transferId);
                emit sendError(transferId, msg);
            }
            else {
                qInfo() << "[FT] file-offer server ACK OK – waiting for peer:"
                    << transferId;
            }
        });
}

// ---------------------------------------------------------------------------
// Public API – Shared
// ---------------------------------------------------------------------------

void FileTransferManager::acceptTransfer(const QString& transferId,
    const QString& savePath)
{
    if (!m_transfers.contains(transferId)) return;

    m_transfers[transferId].filePath = savePath;
    m_transfers[transferId].status = QStringLiteral("accepted");

    qInfo() << "[FT] Accepting transfer:" << transferId
        << "save to:" << savePath;

    m_sio->sendEvent(QStringLiteral("file-accept"),
        QJsonObject{ { QStringLiteral("transferId"), transferId } },
        [this, transferId](const QJsonArray& args) {
            if (args.isEmpty()) return;
            const QJsonObject resp = args.at(0).toObject();
            if (resp.value(QStringLiteral("success")).toBool()) {
                m_transfers[transferId].status =
                    QStringLiteral("waiting-webrtc");
                qInfo() << "[FT] file-accept ACK OK – waiting for WebRTC offer:"
                    << transferId;
                emit transferAccepted(transferId);
            }
            else {
                const QString msg =
                    resp.value(QStringLiteral("message"))
                    .toString(QStringLiteral("file-accept failed"));
                qWarning() << "[FT] file-accept failed:" << msg;
                emit transferError(transferId, msg);
            }
        });
}

void FileTransferManager::rejectTransfer(const QString& transferId)
{
    if (!m_transfers.contains(transferId)) return;
    m_sio->sendEvent(QStringLiteral("file-reject"),
        QJsonObject{ { QStringLiteral("transferId"), transferId } },
        [this, transferId](const QJsonArray& args) {
            if (!args.isEmpty()
                && args.at(0).toObject()
                .value(QStringLiteral("success")).toBool()) {
                m_transfers.remove(transferId);
                emit transferRejected(transferId);
            }
        });
}

void FileTransferManager::cancelTransfer(const QString& transferId)
{
    if (!m_transfers.contains(transferId)) return;
    qInfo() << "[FT] Cancelling transfer:" << transferId;
    m_webrtc->sendCancelToSender(transferId, QStringLiteral("user cancelled"));
    m_webrtc->closeTransfer(transferId);
    m_transfers.remove(transferId);
    emit transferCancelled(transferId);
}

// ---------------------------------------------------------------------------
// WebRTC signaling relay helpers
// ---------------------------------------------------------------------------

void FileTransferManager::sendWebRtcOffer(const QString& transferId,
    const QString& targetPeerId, const QJsonObject& offer)
{
    qInfo() << "[FT] Sending WebRTC offer to:" << targetPeerId
        << "transferId:" << transferId;
    m_sio->sendEvent(QStringLiteral("webrtc-offer"),
        QJsonObject{
            { QStringLiteral("targetPeerId"), targetPeerId },
            { QStringLiteral("offer"),        offer        },
            { QStringLiteral("transferId"),   transferId   }
        },
        [transferId](const QJsonArray& args) {
            if (!args.isEmpty()
                && args.at(0).toObject()
                .value(QStringLiteral("success")).toBool())
                qInfo() << "[FT] webrtc-offer relayed successfully:"
                << transferId;
        });
}

void FileTransferManager::sendWebRtcAnswer(const QString& transferId,
    const QString& targetPeerId, const QJsonObject& answer)
{
    qInfo() << "[FT] Sending WebRTC answer to:" << targetPeerId
        << "transferId:" << transferId;
    m_sio->sendEvent(QStringLiteral("webrtc-answer"),
        QJsonObject{
            { QStringLiteral("targetPeerId"), targetPeerId },
            { QStringLiteral("answer"),       answer       },
            { QStringLiteral("transferId"),   transferId   }
        },
        [transferId](const QJsonArray& args) {
            if (!args.isEmpty()
                && args.at(0).toObject()
                .value(QStringLiteral("success")).toBool())
                qInfo() << "[FT] webrtc-answer relayed successfully:"
                << transferId;
        });
}

void FileTransferManager::sendIceCandidate(const QString& transferId,
    const QString& targetPeerId, const QJsonObject& candidate)
{
    m_sio->sendEvent(QStringLiteral("webrtc-ice-candidate"),
        QJsonObject{
            { QStringLiteral("targetPeerId"), targetPeerId },
            { QStringLiteral("candidate"),    candidate    },
            { QStringLiteral("transferId"),   transferId   }
        });
}

void FileTransferManager::reportProgress(const QString& transferId,
    int chunksReceived, double progress)
{
    if (m_transfers.contains(transferId))
        m_transfers[transferId].progress = progress;
    m_webrtc->sendProgressUpdate(transferId, chunksReceived, progress);
}

// ---------------------------------------------------------------------------
// Socket.IO event handlers
// ---------------------------------------------------------------------------

void FileTransferManager::onFileOffer(const QJsonArray& args)
{
    if (args.isEmpty()) return;
    const QJsonObject d = args.at(0).toObject();

    TransferInfo info;
    info.transferId = d.value(QStringLiteral("transferId")).toString();
    info.name = d.value(QStringLiteral("name")).toString();
    info.size = static_cast<qint64>(
        d.value(QStringLiteral("size")).toDouble());
    info.mimeType = d.value(QStringLiteral("type")).toString();
    info.peerId = d.value(QStringLiteral("senderId")).toString();
    info.peerUserId = d.value(QStringLiteral("senderUserId")).toString();
    info.totalChunks = d.value(QStringLiteral("totalChunks")).toInt();
    info.status = QStringLiteral("pending");
    info.direction = Direction::Incoming;

    m_transfers.insert(info.transferId, info);

    qInfo() << "[FT] \U0001F4E5 Incoming file-offer:"
        << info.name
        << QString::number(info.size / 1024.0, 'f', 1) + QStringLiteral(" KB")
        << "from peer:" << info.peerId
        << "chunks:" << info.totalChunks;

    emit incomingFileOffer(info);
}

void FileTransferManager::onFileAccepted(const QJsonArray& args)
{
    if (args.isEmpty()) return;
    const QString id =
        args.at(0).toObject().value(QStringLiteral("transferId")).toString();

    if (!m_transfers.contains(id)) return;
    TransferInfo& info = m_transfers[id];

    if (info.direction == Direction::Outgoing) {
        info.status = QStringLiteral("webrtc-connecting");
        qInfo() << "[FT] \u2705 File offer accepted by peer:" << info.peerId
            << "– initiating WebRTC send for transfer:" << id;
        m_webrtc->initiateSend(id, info.peerId, info.filePath);
        emit outgoingFileOfferAccepted(id);
    }
    else {
        info.status = QStringLiteral("active");
        emit transferAccepted(id);
    }
}

void FileTransferManager::onFileRejected(const QJsonArray& args)
{
    if (args.isEmpty()) return;
    const QString id =
        args.at(0).toObject().value(QStringLiteral("transferId")).toString();

    if (!m_transfers.contains(id)) return;
    const bool isOutgoing = m_transfers[id].direction == Direction::Outgoing;
    m_transfers.remove(id);

    if (isOutgoing)
        emit outgoingFileOfferRejected(id);
    else
        emit transferRejected(id);
}

void FileTransferManager::onFileComplete(const QJsonArray& args)
{
    if (args.isEmpty()) return;
    const QString id =
        args.at(0).toObject().value(QStringLiteral("transferId")).toString();
    if (m_transfers.contains(id)) {
        m_transfers[id].status = QStringLiteral("completed");
        m_transfers[id].progress = 100.0;
        emit transferComplete(id, m_transfers[id].filePath);
    }
}

void FileTransferManager::onWebRtcOffer(const QJsonArray& args)
{
    if (args.isEmpty()) return;
    const QJsonObject d = args.at(0).toObject();
    const QString fromPeerId = d.value(QStringLiteral("fromPeerId")).toString();
    const QString transferId = d.value(QStringLiteral("transferId")).toString();
    const QJsonObject offer = d.value(QStringLiteral("offer")).toObject();

    if (m_transfers.contains(transferId)) {
        const QString status = m_transfers[transferId].status;
        if (status != QStringLiteral("waiting-webrtc")
            && status != QStringLiteral("accepted"))
        {
            qDebug() << "[FT] Ignoring duplicate webrtc-offer for:"
                << transferId << "(status:" << status << ")";
            return;
        }
        m_transfers[transferId].status = QStringLiteral("webrtc-processing");
    }

    qInfo() << "[FT] \U0001F4E1 WebRTC offer received from:" << fromPeerId
        << "transferId:" << transferId;

    QString savePath;
    if (m_transfers.contains(transferId))
        savePath = m_transfers[transferId].filePath;
    if (savePath.isEmpty()) {
        savePath = QDir::tempPath() + QStringLiteral("/filetransfer_") + transferId;
        qWarning() << "[FT] savePath not set – using temp:" << savePath;
    }

    m_webrtc->processOffer(transferId, fromPeerId, offer, savePath);
    emit webRtcOfferReceived(transferId, fromPeerId, offer);
}

void FileTransferManager::onWebRtcAnswer(const QJsonArray& args)
{
    if (args.isEmpty()) return;
    const QJsonObject d = args.at(0).toObject();
    const QString transferId = d.value(QStringLiteral("transferId")).toString();
    const QJsonObject answer = d.value(QStringLiteral("answer")).toObject();

    if (!m_transfers.contains(transferId)
        || m_transfers[transferId].direction != Direction::Outgoing)
    {
        qDebug() << "[FT] webrtc-answer: ignoring (not an outgoing transfer):"
            << transferId;
        return;
    }

    qInfo() << "[FT] WebRTC answer received – applying to sender session:"
        << transferId;
    m_webrtc->applyAnswer(transferId, answer);
}

void FileTransferManager::onIceCandidate(const QJsonArray& args)
{
    if (args.isEmpty()) return;
    const QJsonObject d = args.at(0).toObject();
    const QString fromPeerId = d.value(QStringLiteral("fromPeerId")).toString();
    const QString transferId = d.value(QStringLiteral("transferId")).toString();
    const QJsonObject cand = d.value(QStringLiteral("candidate")).toObject();

    qDebug() << "[FT] Remote ICE candidate from:" << fromPeerId
        << "transfer:" << transferId;

    m_webrtc->addIceCandidate(transferId, cand);
    emit iceRecandidateReceived(transferId, fromPeerId, cand);
}