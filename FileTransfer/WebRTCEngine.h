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
#include <QJsonDocument>
#include <QString>
#include <QByteArray>
#include <QMap>
#include <QMutex>
#include <QFile>
#include <QFileInfo>
#include <memory>
#include <functional>
#include <atomic>

#ifdef HAVE_LIBDATACHANNEL
#  include <rtc/rtc.hpp>
#endif

class WebRTCEngine : public QObject
{
    Q_OBJECT

public:
    explicit WebRTCEngine(QObject* parent = nullptr);
    ~WebRTCEngine();

    // ── Receive-side API ──────────────────────────────────────────────────
    // Creates a PeerConnection as answerer; generates an SDP answer and
    // opens both DataChannels to receive the incoming file.
    void processOffer(const QString& transferId,
        const QString& fromPeerId,
        const QJsonObject& offer,
        const QString& savePath);

    // ── Send-side API ─────────────────────────────────────────────────────
    // Creates a PeerConnection as offerer; opens "file-control" and
    // "file-data" DataChannels and emits offerReady() with the SDP offer.
    // Call applyAnswer() once the remote SDP answer arrives.
    void initiateSend(const QString& transferId,
        const QString& targetPeerId,
        const QString& filePath);

    // Applies the remote SDP answer received from the receiver (sender side).
    void applyAnswer(const QString& transferId,
        const QJsonObject& answer);

    // ── Shared API ────────────────────────────────────────────────────────
    void addIceCandidate(const QString& transferId,
        const QJsonObject& candidate);

    void closeTransfer(const QString& transferId);

    // ── P2P control-channel outbound helpers ──────────────────────────────
    // Send a progress-ack to the sender over the file-control channel
    // (receive-side use). No-op if control channel is absent or closed.
    void sendProgressUpdate(const QString& transferId,
        int    chunksReceived,
        double progress);

    // Send a cancel message to the sender over the file-control channel,
    // then return. The caller must call closeTransfer() afterwards.
    void sendCancelToSender(const QString& transferId,
        const QString& reason = {});

signals:
    // ── Receive-side signals ──────────────────────────────────────────────
    void answerReady(const QString& transferId,
        const QString& targetPeerId,
        const QJsonObject& answer);

    // ── Send-side signals ─────────────────────────────────────────────────
    // Emitted when the SDP offer has been generated; relay to the receiver
    // via the signalling server (Socket.IO webrtc-offer event).
    void offerReady(const QString& transferId,
        const QString& targetPeerId,
        const QJsonObject& offer);

    // ── Shared signals ────────────────────────────────────────────────────
    void localIceCandidate(const QString& transferId,
        const QString& targetPeerId,
        const QJsonObject& candidate);

    // Emitted on both sides when the file-data DataChannel becomes open.
    void dataChannelOpen(const QString& transferId);

    // Progress update (bytes processed so far / total file bytes).
    void progressUpdate(const QString& transferId,
        qint64 bytesProcessed,
        qint64 totalBytes);

    // Emitted when the transfer is fully complete (both send and receive).
    void transferComplete(const QString& transferId,
        const QString& path);

    void transferError(const QString& transferId, const QString& reason);

    // Emitted when the remote peer sends a cancel over the file-control
    // DataChannel (replaces Socket.IO file-cancelled).
    void peerCancelled(const QString& transferId, const QString& reason);

private:

#ifdef HAVE_LIBDATACHANNEL
    struct Session {
        QString                              transferId;
        QString                              targetPeerId;
        std::shared_ptr<rtc::PeerConnection> pc;

        // ── DataChannels ───────────────────────────────────────────────────
        // dataDc   : "file-data"    – binary chunks
        // controlDc: "file-control" – reliable ordered JSON control messages
        //
        // Legacy receivers open a single channel; controlDc stays null and
        // text frames on dataDc are handled for backward compatibility.
        std::shared_ptr<rtc::DataChannel>    dataDc;
        std::shared_ptr<rtc::DataChannel>    controlDc;

        // ── Receive-side state ─────────────────────────────────────────────
        QString                              savePath;
        QFile                                outFile;
        qint64                               bytesReceived = 0;
        qint64                               totalBytes = 0;
        bool                                 fileStartReceived = false;

        // ── Send-side state ────────────────────────────────────────────────
        bool                                 isSender = false;
        QString                              filePath;
        QFile                                inFile;
        qint64                               bytesSent = 0;
        qint64                               fileSize = 0;
        // Counts how many of the two send-side DCs have fired onOpen.
        // When it reaches 2 the file send begins. Atomic because the
        // onOpen callbacks arrive from libdatachannel threads.
        std::atomic<int>                     openDcCount{ 0 };

        // ── Shared ────────────────────────────────────────────────────────
        bool                                 completed = false;
    };

    QMap<QString, std::shared_ptr<Session>> m_sessions;
    QMutex                                  m_mutex;

    rtc::Configuration buildConfig() const;

    // Receive-side DataChannel binders
    void bindDataChannel(std::shared_ptr<rtc::DataChannel>   dc,
        std::shared_ptr<Session>            session,
        std::function<void(const QString&)> finalise);

    void bindControlChannel(std::shared_ptr<rtc::DataChannel>  dc,
        std::shared_ptr<Session>            session,
        std::function<void(const QString&)> finalise);

    // Send-side helpers
    // Opens the file, sends file-start, then calls sendNextChunks().
    void startFileSend(std::shared_ptr<Session> session);

    // Reads and sends chunks while the DataChannel send-buffer is below the
    // high-watermark. Sends file-end and emits transferComplete on EOF.
    // Must be called on the Qt thread.
    void sendNextChunks(std::shared_ptr<Session> session);
#endif
};