#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include "FileTransferManager.h"
#include <QDebug>
#include <QDir>

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

    // ── WebRTCEngine → Socket.IO bridge ──────────────────────────────────
    connect(m_webrtc, &WebRTCEngine::answerReady, this,
        [this](const QString& tid, const QString& peer, const QJsonObject& ans) {
            sendWebRtcAnswer(tid, peer, ans);
        });

    connect(m_webrtc, &WebRTCEngine::localIceCandidate, this,
        [this](const QString& tid, const QString& peer, const QJsonObject& c) {
            sendIceCandidate(tid, peer, c);
        });

    connect(m_webrtc, &WebRTCEngine::dataChannelOpen, this,
        [this](const QString& tid) {
            if (m_transfers.contains(tid))
                m_transfers[tid].status = QStringLiteral("active");
            emit dataChannelOpen(tid);
        });

    connect(m_webrtc, &WebRTCEngine::progressUpdate, this,
        [this](const QString& tid, qint64 recv, qint64 total) {
            const double pct = total > 0
                ? (static_cast<double>(recv) / static_cast<double>(total)) * 100.0
                : 0.0;
            if (m_transfers.contains(tid)) m_transfers[tid].progress = pct;
            const int chunks = m_transfers.contains(tid)
                ? m_transfers[tid].totalChunks : 0;
            emit transferProgress(tid, pct,
                static_cast<int>(recv / 16384), chunks);
        });

    connect(m_webrtc, &WebRTCEngine::transferComplete, this,
        [this](const QString& tid, const QString& path) {
            if (m_transfers.contains(tid)) {
                m_transfers[tid].status = QStringLiteral("completed");
                m_transfers[tid].progress = 100.0;
            }
            emit transferComplete(tid, path);
        });

    connect(m_webrtc, &WebRTCEngine::transferError, this,
        [this](const QString& tid, const QString& reason) {
            if (m_transfers.contains(tid))
                m_transfers[tid].status = QStringLiteral("error");
            emit transferError(tid, reason);
        });
}

void FileTransferManager::registerListeners()
{
    m_sio->on(QStringLiteral("file-offer"),
        [this](const QJsonArray& a) { onFileOffer(a); });
    m_sio->on(QStringLiteral("file-rejected"),
        [this](const QJsonArray& a) { onFileRejected(a); });
    m_sio->on(QStringLiteral("file-cancelled"),
        [this](const QJsonArray& a) { onFileCancelled(a); });
    m_sio->on(QStringLiteral("webrtc-offer"),
        [this](const QJsonArray& a) { onWebRtcOffer(a); });
    m_sio->on(QStringLiteral("webrtc-ice-candidate"),
        [this](const QJsonArray& a) { onIceCandidate(a); });
}

// ── Public API ────────────────────────────────────────────────────────────

void FileTransferManager::acceptTransfer(const QString& transferId,
    const QString& savePath)
{
    if (!m_transfers.contains(transferId)) return;
    m_transfers[transferId].savePath = savePath;
    m_transfers[transferId].status = QStringLiteral("accepted");

    m_sio->sendEvent(QStringLiteral("file-accept"),
        QJsonObject{ { QStringLiteral("transferId"), transferId } },
        [this, transferId](const QJsonArray& args) {
            if (args.isEmpty()) return;
            const QJsonObject resp = args.at(0).toObject();
            if (resp.value(QStringLiteral("success")).toBool()) {
                m_transfers[transferId].status = QStringLiteral("waiting-webrtc");
                emit transferAccepted(transferId);
            }
            else {
                emit transferError(transferId,
                    resp.value(QStringLiteral("message")).toString());
            }
        });
}

void FileTransferManager::rejectTransfer(const QString& transferId)
{
    if (!m_transfers.contains(transferId)) return;
    m_sio->sendEvent(QStringLiteral("file-reject"),
        QJsonObject{ { QStringLiteral("transferId"), transferId } },
        [this, transferId](const QJsonArray& args) {
            if (!args.isEmpty() &&
                args.at(0).toObject().value(QStringLiteral("success")).toBool()) {
                m_transfers.remove(transferId);
                emit transferRejected(transferId);
            }
        });
}

void FileTransferManager::cancelTransfer(const QString& transferId)
{
    if (!m_transfers.contains(transferId)) return;
    m_webrtc->closeTransfer(transferId);
    m_sio->sendEvent(QStringLiteral("file-cancel"),
        QJsonObject{ { QStringLiteral("transferId"), transferId } },
        [this, transferId](const QJsonArray& args) {
            if (!args.isEmpty() &&
                args.at(0).toObject().value(QStringLiteral("success")).toBool()) {
                m_transfers.remove(transferId);
                emit transferCancelled(transferId);
            }
        });
}

// ── Private: outgoing signaling ───────────────────────────────────────────

void FileTransferManager::sendWebRtcAnswer(const QString& transferId,
    const QString& targetPeerId,
    const QJsonObject& answer)
{
    m_sio->sendEvent(QStringLiteral("webrtc-answer"),
        QJsonObject{
            { QStringLiteral("targetPeerId"), targetPeerId },
            { QStringLiteral("answer"),       answer       },
            { QStringLiteral("transferId"),   transferId   }
        });
}

void FileTransferManager::sendIceCandidate(const QString& transferId,
    const QString& targetPeerId,
    const QJsonObject& candidate)
{
    m_sio->sendEvent(QStringLiteral("webrtc-ice-candidate"),
        QJsonObject{
            { QStringLiteral("targetPeerId"), targetPeerId },
            { QStringLiteral("candidate"),    candidate    },
            { QStringLiteral("transferId"),   transferId   }
        });
}

// ── Private: incoming Socket.IO events ───────────────────────────────────

void FileTransferManager::onFileOffer(const QJsonArray& args)
{
    if (args.isEmpty()) return;
    const QJsonObject d = args.at(0).toObject();

    TransferInfo info;
    info.transferId = d.value(QStringLiteral("transferId")).toString();
    info.name = d.value(QStringLiteral("name")).toString();
    info.size = static_cast<qint64>(d.value(QStringLiteral("size")).toDouble());
    info.mimeType = d.value(QStringLiteral("type")).toString();
    info.senderId = d.value(QStringLiteral("senderId")).toString();
    info.totalChunks = d.value(QStringLiteral("totalChunks")).toInt();
    info.status = QStringLiteral("pending");

    m_transfers.insert(info.transferId, info);
    emit incomingFileOffer(info);
}

void FileTransferManager::onFileRejected(const QJsonArray& args)
{
    if (args.isEmpty()) return;
    const QString id = args.at(0).toObject()
        .value(QStringLiteral("transferId")).toString();
    m_transfers.remove(id);
    emit transferRejected(id);
}

void FileTransferManager::onFileCancelled(const QJsonArray& args)
{
    if (args.isEmpty()) return;
    const QString id = args.at(0).toObject()
        .value(QStringLiteral("transferId")).toString();
    m_webrtc->closeTransfer(id);
    m_transfers.remove(id);
    emit transferCancelled(id);
}

void FileTransferManager::onWebRtcOffer(const QJsonArray& args)
{
    if (args.isEmpty()) return;
    const QJsonObject d = args.at(0).toObject();
    const QString fromPeer = d.value(QStringLiteral("fromPeerId")).toString();
    const QString transferId = d.value(QStringLiteral("transferId")).toString();
    const QJsonObject offer = d.value(QStringLiteral("offer")).toObject();

    // Deduplication guard
    if (m_transfers.contains(transferId)) {
        const QString st = m_transfers[transferId].status;
        if (st != QStringLiteral("waiting-webrtc") &&
            st != QStringLiteral("accepted")) return;
        m_transfers[transferId].status = QStringLiteral("webrtc-processing");
    }

    QString savePath;
    if (m_transfers.contains(transferId))
        savePath = m_transfers[transferId].savePath;
    if (savePath.isEmpty())
        savePath = QDir::tempPath() + QStringLiteral("/deskshare_") + transferId;

    m_webrtc->processOffer(transferId, fromPeer, offer, savePath);
    emit webRtcOfferReceived(transferId, fromPeer, offer);
}

void FileTransferManager::onIceCandidate(const QJsonArray& args)
{
    if (args.isEmpty()) return;
    const QJsonObject d = args.at(0).toObject();
    const QString transferId = d.value(QStringLiteral("transferId")).toString();
    const QJsonObject cand = d.value(QStringLiteral("candidate")).toObject();
    m_webrtc->addIceCandidate(transferId, cand);
}

void FileTransferManager::sendFile(const QString& filePath, const QString& toPeerId)
{
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        emit sendTransferError("", "File not found: " + filePath);
        return;
    }

    QString transferId = QString::number(QDateTime::currentMSecsSinceEpoch());

    qInfo() << "[FileTransfer] Initiating send transfer" << transferId
        << "to" << toPeerId << "file:" << filePath;

    // Store transfer info
    m_outgoingTransfers[transferId] = toPeerId;

    // Store in cache for UI
    TransferInfo info;
    info.transferId = transferId;
    info.name = fileInfo.fileName();
    info.size = fileInfo.size();
    info.senderId = m_myPeerId;
    info.savePath = filePath;

    // Emit initiation signal
    emit sendTransferInitiated(transferId, toPeerId);

    // Send WebRTC offer to initiate data channel
    QJsonObject offer{
        { QStringLiteral("type"), QStringLiteral("offer") },
        { QStringLiteral("transferId"), transferId },
        { QStringLiteral("fileName"), info.name },
        { QStringLiteral("fileSize"), static_cast<int>(info.size) }
    };

    // Forward through signaling client
    m_sio->sendEvent(QStringLiteral("send-offer"),
        QJsonObject{
            { QStringLiteral("toPeerId"), toPeerId },
            { QStringLiteral("transferId"), transferId },
            { QStringLiteral("offer"), offer }
        });

    qInfo() << "[FileTransfer] Send offer sent for transfer" << transferId;
}

void FileTransferManager::cancelSendTransfer(const QString& transferId)
{
    if (m_outgoingTransfers.contains(transferId)) {
        QString toPeerId = m_outgoingTransfers[transferId];
        m_outgoingTransfers.remove(transferId);

        m_sio->sendEvent(QStringLiteral("cancel-send"),
            QJsonObject{
                { QStringLiteral("toPeerId"), toPeerId },
                { QStringLiteral("transferId"), transferId }
            });

        qInfo() << "[FileTransfer] Send transfer cancelled:" << transferId;
    }
}