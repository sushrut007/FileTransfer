#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QMap>

#include "SocketIOClient.h"
#include "WebRTCEngine.h"

class FileTransferManager : public QObject
{
    Q_OBJECT

public:
    // Renamed from Direction for clarity; fully backward-compatible because
    // the old Direction enum had the same enumerators.
    enum class TransferDirection { Incoming, Outgoing };
    // Keep the short alias so existing call-sites need no change.
    using Direction = TransferDirection;

    struct TransferInfo {
        QString           transferId;
        QString           name;
        qint64            size = 0;
        QString           mimeType;
        // Incoming: socket.id of the remote sender
        // Outgoing: socket.id of the intended receiver
        QString           peerId;
        QString           peerUserId;
        int               totalChunks = 0;
        double            progress = 0.0;
        QString           status;
        // Incoming: local path chosen by the user when accepting
        // Outgoing: absolute path of the file being sent
        QString           filePath;
        TransferDirection direction = TransferDirection::Incoming;
    };

    explicit FileTransferManager(SocketIOClient* sio,
        const QString& myPeerId,
        QObject* parent = nullptr);

    // ── Receive-side actions ──────────────────────────────────────────────
    void acceptTransfer(const QString& transferId, const QString& savePath);
    void rejectTransfer(const QString& transferId);

    // Cancel an in-progress transfer (both directions).
    // Notifies the peer over the file-control DataChannel; no Socket.IO
    // event is emitted.
    void cancelTransfer(const QString& transferId);

    // ── Send-side actions ─────────────────────────────────────────────────
    // Sends a file-offer to targetPeerId and begins the WebRTC handshake
    // once the peer accepts. filePath must be an absolute, readable path.
    void sendFile(const QString& targetPeerId, const QString& filePath);

    // ── WebRTC signaling helpers (called internally via signals) ──────────
    void sendWebRtcOffer(const QString& transferId,
        const QString& targetPeerId,
        const QJsonObject& offer);
    void sendWebRtcAnswer(const QString& transferId,
        const QString& targetPeerId,
        const QJsonObject& answer);
    void sendIceCandidate(const QString& transferId,
        const QString& targetPeerId,
        const QJsonObject& candidate);

    // ── Progress reporting ────────────────────────────────────────────────
    // Sends a progress-ack to the sender over the file-control DataChannel.
    void reportProgress(const QString& transferId,
        int    chunksReceived,
        double progress);

    QMap<QString, TransferInfo> transfers() const { return m_transfers; }

signals:
    // ── Receive-side signals ──────────────────────────────────────────────

    // Emitted when a remote peer sends us a file-offer.
    void incomingFileOffer(const FileTransferManager::TransferInfo& info);

    // Emitted when we (the receiver) have successfully sent file-accept back.
    void transferAccepted(const QString& transferId);

    // Emitted when we (the receiver) have rejected a file-offer.
    void transferRejected(const QString& transferId);

    // ── Send-side signals ─────────────────────────────────────────────────

    // Emitted immediately after sendFile() dispatches the file-offer event
    // to the server and receives a successful server ACK.
    void outgoingFileOfferSent(const QString& transferId);

    // Emitted when the remote receiver accepts our file-offer and WebRTC
    // negotiation has started.
    void outgoingFileOfferAccepted(const QString& transferId);

    // Emitted when the remote receiver rejects our file-offer.
    void outgoingFileOfferRejected(const QString& transferId);

    // Outgoing-transfer progress: mirrors transferProgress but is only
    // emitted for Outgoing sessions so the UI can distinguish directions
    // without inspecting TransferInfo every time.
    // chunksReceived here means chunks sent; total is totalChunks.
    void sendProgress(const QString& transferId, double progress,
        int chunksSent, int totalChunks);

    // Emitted when the remote receiver has received the complete file
    // (triggered by the sender-side transferComplete from WebRTCEngine).
    void sendComplete(const QString& transferId);

    // Emitted on any fatal error on the outgoing transfer only.
    // Use transferError for errors that apply to both directions.
    void sendError(const QString& transferId, const QString& reason);

    // ── Shared signals ────────────────────────────────────────────────────

    // Emitted for both incoming and outgoing transfers when cancelled.
    void transferCancelled(const QString& transferId);

    // Unified progress signal for both directions.
    void transferProgress(const QString& transferId, double progress,
        int chunksReceived, int totalChunks);

    // Unified completion signal for both directions.
    // filePath is the save path (incoming) or source path (outgoing).
    void transferComplete(const QString& transferId, const QString& filePath);

    // Unified error signal for both directions.
    void transferError(const QString& transferId, const QString& reason);

    // ── WebRTC / signaling diagnostic signals (UI / logging only) ────────
    void webRtcOfferReceived(const QString& transferId,
        const QString& fromPeerId,
        const QJsonObject& offer);
    void iceRecandidateReceived(const QString& transferId,
        const QString& fromPeerId,
        const QJsonObject& candidate);
    void dataChannelOpen(const QString& transferId);

private:
    void registerListeners();
    void wireWebRtcSignals();

    // Socket.IO event handlers
    void onFileOffer(const QJsonArray& args);
    void onFileAccepted(const QJsonArray& args);
    void onFileRejected(const QJsonArray& args);
    void onFileComplete(const QJsonArray& args);
    void onWebRtcOffer(const QJsonArray& args);
    void onWebRtcAnswer(const QJsonArray& args);
    void onIceCandidate(const QJsonArray& args);

    SocketIOClient* m_sio;
    WebRTCEngine* m_webrtc;
    QString                     m_myPeerId;
    QMap<QString, TransferInfo> m_transfers;
};

Q_DECLARE_METATYPE(FileTransferManager::TransferInfo)