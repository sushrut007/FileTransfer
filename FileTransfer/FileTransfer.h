#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include <QMainWindow>
#include <QApplication>
#include <QDir>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QTextEdit>
#include <QLabel>
#include <QProgressBar>
#include <QGroupBox>
#include <QTableWidget>
#include <QRadioButton>
#include <QJsonArray>
#include <QJsonObject>

#include "SignalingClient.h"
#include "FileTransferManager.h"
#include "LogHandler.h"

class FileTransfer : public QMainWindow
{
    Q_OBJECT

public:
    explicit FileTransfer(QWidget* parent = nullptr);

private slots:
    // Connection panel
    void onConnectClicked();
    void onModeChanged();
    void onClearLogsClicked();
    void onExportLogsClicked();

    // Send file
    void onSendFileClicked();

    // SignalingClient
    void onRoomCreated(const QString& roomId, const QString& password);
    void onStage1Complete(const QString& roomId,
        const QString& peerId,
        const QJsonArray& peers);
    void onLoginFailed(const QString& reason);
    void onConnectionFailed(const QString& reason);

    // Peer presence – live updates to the peer combo
    void onPeerJoined(const QString& peerId, const QString& appType);
    void onPeerLeft(const QString& peerId);

    // FileTransferManager – receive side
    void onIncomingFileOffer(const FileTransferManager::TransferInfo& info);
    void onTransferAccepted(const QString& transferId);
    void onTransferRejected(const QString& transferId);
    void onTransferCancelled(const QString& transferId);
    void onTransferProgress(const QString& transferId, double progress,
        int chunksReceived, int totalChunks);
    void onTransferComplete(const QString& transferId,
        const QString& filePath);
    void onTransferError(const QString& transferId, const QString& reason);

    // FileTransferManager – send side
    void onOutgoingFileOfferSent(const QString& transferId);
    void onOutgoingFileOfferAccepted(const QString& transferId);
    void onOutgoingFileOfferRejected(const QString& transferId);
    void onSendProgress(const QString& transferId, double progress,
        int chunksSent, int totalChunks);
    void onSendComplete(const QString& transferId);
    void onSendError(const QString& transferId, const QString& reason);

    // WebRTC signaling (logged only)
    void onWebRtcOfferReceived(const QString& transferId,
        const QString& fromPeerId,
        const QJsonObject& offer);

    // Transfer table buttons
    void onAcceptClicked();
    void onRejectClicked();
    void onCancelClicked();

    // Log handler
    void onLogLine(LogHandler::Level level,
        const QString& timestamp,
        const QString& message);

private:
    void buildUi();
    void setConnectionState(bool connecting, bool connected = false);
    void populatePeerCombo(const QJsonArray& peers);

    // Add / remove a single peer in the combo without clearing it.
    void addPeerToCombo(const QString& peerId);
    void removePeerFromCombo(const QString& peerId);
    void updatePeersLabel();

    void connectFtmSignals();
    void appendLog(LogHandler::Level level,
        const QString& timestamp,
        const QString& message);

    // Transfer table helpers
    void upsertTransferRow(const FileTransferManager::TransferInfo& info);
    void updateTransferRow(const QString& transferId,
        const QString& status,
        double         progress = -1.0);
    int  findTransferRow(const QString& transferId) const;

    // ── Connection panel ──────────────────────────────────────────────────
    QLineEdit* m_serverEdit = nullptr;
    QRadioButton* m_joinRadio = nullptr;
    QRadioButton* m_createRadio = nullptr;
    QWidget* m_joinWidget = nullptr;
    QLineEdit* m_roomIdEdit = nullptr;
    QWidget* m_createWidget = nullptr;
    QLabel* m_genRoomLabel = nullptr;
    QLineEdit* m_passwordEdit = nullptr;
    QPushButton* m_connectBtn = nullptr;

    // ── Status panel ──────────────────────────────────────────────────────
    QLabel* m_statusLabel = nullptr;
    QLabel* m_roomLabel = nullptr;
    QLabel* m_roomIdCopyLabel = nullptr;
    QLabel* m_peerIdLabel = nullptr;
    QLabel* m_peersLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;

    // ── Send File panel ───────────────────────────────────────────────────
    QComboBox* m_peerCombo = nullptr;
    QLabel* m_selectedFileLabel = nullptr;
    QPushButton* m_browseBtn = nullptr;
    QPushButton* m_sendBtn = nullptr;
    QString       m_pendingSendPath;

    // ── Transfer panel ────────────────────────────────────────────────────
    QTableWidget* m_transferTable = nullptr;
    QPushButton* m_acceptBtn = nullptr;
    QPushButton* m_rejectBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;

    // ── Log panel ─────────────────────────────────────────────────────────
    QTextEdit* m_logView = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QPushButton* m_exportBtn = nullptr;

    // ── Backend ───────────────────────────────────────────────────────────
    SignalingClient* m_client = nullptr;
    FileTransferManager* m_ftm = nullptr;
};