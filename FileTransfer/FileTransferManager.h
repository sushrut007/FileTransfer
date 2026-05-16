#pragma once
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
    struct TransferInfo {
        QString transferId;
        QString name;
        qint64  size = 0;
        QString mimeType;
        QString senderId;
        int     totalChunks = 0;
        double  progress = 0.0;
        QString status;          // pending|accepted|waiting-webrtc|
        // webrtc-processing|active|completed|
        // rejected|cancelled|error
        QString savePath;
    };

    explicit FileTransferManager(SocketIOClient* sio,
        const QString& myPeerId,
        QObject* parent = nullptr);

    void acceptTransfer(const QString& transferId, const QString& savePath);
    void rejectTransfer(const QString& transferId);
    void cancelTransfer(const QString& transferId);
    // Sending files
    void sendFile(const QString& filePath, const QString& toPeerId);
    void cancelSendTransfer(const QString& transferId);


signals:
    void incomingFileOffer(const FileTransferManager::TransferInfo& info);
    void transferAccepted(const QString& transferId);
    void transferRejected(const QString& transferId);
    void transferCancelled(const QString& transferId);
    void transferProgress(const QString& transferId, double progress,
        int chunksReceived, int totalChunks);
    void transferComplete(const QString& transferId, const QString& savePath);
    void transferError(const QString& transferId, const QString& reason);
    void webRtcOfferReceived(const QString& transferId,
        const QString& fromPeerId,
        const QJsonObject& offer);
    void dataChannelOpen(const QString& transferId);
    void sendTransferInitiated(const QString& transferId, const QString& toPeerId);
    void sendTransferProgress(const QString& transferId, double progress);
    void sendTransferComplete(const QString& transferId);
    void sendTransferError(const QString& transferId, const QString& reason);

private:
    void registerListeners();
    void sendWebRtcAnswer(const QString& transferId,
        const QString& targetPeerId,
        const QJsonObject& answer);
    void sendIceCandidate(const QString& transferId,
        const QString& targetPeerId,
        const QJsonObject& candidate);

    // Socket.IO event handlers
    void onFileOffer(const QJsonArray& args);
    void onFileRejected(const QJsonArray& args);
    void onFileCancelled(const QJsonArray& args);
    void onWebRtcOffer(const QJsonArray& args);
    void onIceCandidate(const QJsonArray& args);

    SocketIOClient* m_sio;
    WebRTCEngine* m_webrtc;
    QString                     m_myPeerId;
    QMap<QString, TransferInfo> m_transfers;
    QMap<QString, QString> m_outgoingTransfers;
};
Q_DECLARE_METATYPE(FileTransferManager::TransferInfo)