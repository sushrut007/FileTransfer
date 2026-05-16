#ifndef FILETRANSFER_H
#define FILETRANSFER_H

#include <QMainWindow>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QLabel>
#include <QTabWidget>
#include <QTableWidget>
#include <QSystemTrayIcon>
#include <QLineEdit>
#include <QCloseEvent>
#include <QMap>
#include <QPlainTextEdit>
#include <QComboBox>
#include "ui_FileTransfer.h"
#include "FileTransferManager.h"
#include "SignalingClient.h"
#include "LogHandler.h"

class FileTransfer : public QMainWindow
{
    Q_OBJECT

public:
    explicit FileTransfer(QWidget* parent = nullptr);
    ~FileTransfer();

protected:
    void changeEvent(QEvent* e) override;
    void closeEvent(QCloseEvent* e) override;

private slots:
    // Connection & Signaling
    void onStage1Complete(const QString& roomId, const QString& peerId,
        const QJsonArray& peers);
    void onConnectionFailed(const QString& reason);
    void onLoginFailed(const QString& reason);

    // Send File Tab
    void onBrowseSendFile();
    void onSendFileClicked();
    void onPeerSelectionChanged(int index);
    void onRefreshPeersList();

    // Incoming File Offers
    void onIncomingFileOffer(const FileTransferManager::TransferInfo& info);
    void onAcceptTransferClicked();
    void onRejectTransferClicked();

    // Transfer Status
    void onTransferAccepted(const QString& transferId);
    void onTransferRejected(const QString& transferId);
    void onTransferCancelled(const QString& transferId);
    void onTransferProgress(const QString& transferId, double progress,
        int currentChunk, int totalChunks);
    void onTransferComplete(const QString& transferId, const QString& path);
    void onTransferError(const QString& transferId, const QString& reason);
    void onDataChannelOpen(const QString& transferId);
    void onWebRtcOfferReceived(const QString& transferId, const QString& fromPeer,
        const QJsonObject& offer);

    // Outgoing transfers
    void onSendTransferInitiated(const QString& transferId, const QString& toPeerId);
    void onSendTransferProgress(const QString& transferId, double progress);
    void onSendTransferComplete(const QString& transferId);
    void onSendTransferError(const QString& transferId, const QString& reason);

    // File Operations
    void onBrowseSaveLocation();
    void onCancelTransfer();
    void onOpenTransferredFile();
    void onOpenTransferFolder();
    void onClearCompletedTransfers();

    // UI Updates
    void updatePeersList();
    void updateTransferUI(const QString& transferId);
    void showNotification(const QString& title, const QString& message);

    // Tray Icon
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);
    void toggleWindowVisibility();

    // Logging
    void onLogLine(LogHandler::Level level, const QString& timestamp, const QString& message);
    void onConnectClicked();
    void onDisconnectClicked();

private:
    void setupUI();
    void connectSignals();
    void initializeConnections();
    void loadSettings();
    void saveSettings();
    void createSettingsDialog();
    void createTrayIcon();
    QString formatFileSize(qint64 bytes) const;
    void addTransferToTable(const FileTransferManager::TransferInfo& info);
    void updateTransferRow(const QString& transferId);
    void addOutgoingTransferToTable(const QString& transferId, const QString& fileName,
        const QString& toPeerId, qint64 fileSize);

    // UI Components
    Ui::FileTransferClass ui;
    QTabWidget* m_tabWidget;

    // Send File Tab
    QComboBox* m_peerComboBox;
    QLineEdit* m_sendFilePathEdit;
    QPushButton* m_browseSendFileBtn;
    QPushButton* m_sendFileBtn;
    QPushButton* m_refreshPeersBtn;
    QLabel* m_sendStatusLabel;
    QTableWidget* m_outgoingTransfersTable;

    // Incoming Transfers Tab
    QTableWidget* m_incomingTable;
    QLabel* m_incomingStatusLabel;
    QLineEdit* m_savePathEdit;
    QPushButton* m_browseSavePathBtn;
    QPushButton* m_acceptBtn;
    QPushButton* m_rejectBtn;

    // Active Transfers Tab
    QTableWidget* m_activeTransfersTable;
    QLabel* m_activeStatusLabel;
    QPushButton* m_cancelTransferBtn;

    // Completed Transfers Tab
    QTableWidget* m_completedTable;
    QPushButton* m_openFileBtn;
    QPushButton* m_openFolderBtn;
    QPushButton* m_clearCompletedBtn;

    // Peers Tab
    QTableWidget* m_peersTable;
    QLabel* m_connectionStatusLabel;

    // Logs Tab
    QPlainTextEdit* m_logsEdit;
    QPushButton* m_clearLogsBtn;

    // Settings
    QLineEdit* m_serverUrlEdit;
    QLineEdit* m_roomIdEdit;
    QLineEdit* m_passwordEdit;
    QPushButton* m_connectBtn;
    QPushButton* m_disconnectBtn;

    // System Tray
    QSystemTrayIcon* m_trayIcon;
    QMenu* m_trayMenu;

    // Backend
    SignalingClient* m_signalingClient;
    FileTransferManager* m_fileTransferManager;

    // State
    QString m_currentRoomId;
    QString m_myPeerId;
    QString m_savePath;
    QString m_selectedTransferId;
    QString m_selectedSendFilePath;
    QString m_selectedSendToPeerId;
    QMap<QString, FileTransferManager::TransferInfo> m_transferCache;
    QMap<QString, int> m_transferRowMap;
    QMap<QString, QString> m_peerIdMap;  // Maps display name to peer ID
    QMap<QString, QString> m_outgoingTransferMap;  // Maps transfer ID to peer ID
    bool m_isConnected;

    // Configuration
    SignalingClient::Config m_config;
};

#endif // FILETRANSFER_H