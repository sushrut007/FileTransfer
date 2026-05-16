#include "FileTransfer.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QFileDialog>
#include <QStandardPaths>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QSettings>
#include <QDesktopServices>
#include <QUrl>
#include <QDateTime>
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QPlainTextEdit>
#include <QSplitter>

FileTransfer::FileTransfer(QWidget* parent)
    : QMainWindow(parent)
    , m_tabWidget(nullptr)
    , m_incomingTable(nullptr)
    , m_activeTransfersTable(nullptr)
    , m_completedTable(nullptr)
    , m_peersTable(nullptr)
    , m_logsEdit(nullptr)
    , m_clearLogsBtn(nullptr)
    , m_serverUrlEdit(nullptr)
    , m_roomIdEdit(nullptr)
    , m_passwordEdit(nullptr)
    , m_connectBtn(nullptr)
    , m_disconnectBtn(nullptr)
    , m_trayIcon(nullptr)
    , m_signalingClient(nullptr)
    , m_fileTransferManager(nullptr)
    , m_isConnected(false)
{
    ui.setupUi(this);
    setupUI();
    createTrayIcon();
    loadSettings();

    setWindowTitle(QStringLiteral("File Transfer Manager"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));
    resize(1200, 800);

    m_savePath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    m_savePathEdit->setText(m_savePath);

    // Setup logging
    LogHandler::instance()->install();
    connect(LogHandler::instance(), &LogHandler::logLine,
        this, &FileTransfer::onLogLine, Qt::QueuedConnection);

    qInfo() << "[UI] File Transfer Manager initialized";
}

FileTransfer::~FileTransfer()
{
    saveSettings();
}

void FileTransfer::setupUI()
{
    QWidget* centralWidget = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    // Settings Section
    QGroupBox* settingsGroupBox = new QGroupBox(QStringLiteral("Server Configuration"));
    QHBoxLayout* settingsLayout = new QHBoxLayout(settingsGroupBox);
    settingsLayout->setSpacing(5);

    // Server URL
    settingsLayout->addWidget(new QLabel(QStringLiteral("Server URL:")));
    m_serverUrlEdit = new QLineEdit();
    m_serverUrlEdit->setPlaceholderText(QStringLiteral("http://localhost:3000"));
    m_serverUrlEdit->setMaximumWidth(200);
    settingsLayout->addWidget(m_serverUrlEdit);

    // Room ID
    settingsLayout->addWidget(new QLabel(QStringLiteral("Room ID:")));
    m_roomIdEdit = new QLineEdit();
    m_roomIdEdit->setPlaceholderText(QStringLiteral("default-room"));
    m_roomIdEdit->setMaximumWidth(150);
    settingsLayout->addWidget(m_roomIdEdit);

    // Password
    settingsLayout->addWidget(new QLabel(QStringLiteral("Password:")));
    m_passwordEdit = new QLineEdit();
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(QStringLiteral("password"));
    m_passwordEdit->setMaximumWidth(150);
    settingsLayout->addWidget(m_passwordEdit);

    // Connect/Disconnect buttons
    m_connectBtn = new QPushButton(QStringLiteral("Connect"));
    m_connectBtn->setMaximumWidth(100);
    m_connectBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #4CAF50; color: white; border-radius: 5px; "
        "padding: 6px; font-weight: bold; }"
        "QPushButton:hover { background-color: #45a049; }"));
    settingsLayout->addWidget(m_connectBtn);

    m_disconnectBtn = new QPushButton(QStringLiteral("Disconnect"));
    m_disconnectBtn->setMaximumWidth(100);
    m_disconnectBtn->setEnabled(false);
    m_disconnectBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #f44336; color: white; border-radius: 5px; "
        "padding: 6px; font-weight: bold; }"
        "QPushButton:hover { background-color: #da190b; }"
        "QPushButton:disabled { background-color: #cccccc; }"));
    settingsLayout->addWidget(m_disconnectBtn);

    settingsLayout->addStretch();
    mainLayout->addWidget(settingsGroupBox);

    m_tabWidget = new QTabWidget(this);

    // ═════ TAB 0: Settings ════════════════════════════════════════════════
    QWidget* settingsTab = new QWidget();
    QVBoxLayout* settingsTabLayout = new QVBoxLayout(settingsTab);

    QLabel* settingsInfoLabel = new QLabel(QStringLiteral(
        "<b>Connection Settings:</b><br>"
        "1. Enter your server URL (e.g., http://localhost:3000)<br>"
        "2. Enter room ID to join<br>"
        "3. Enter password for authentication<br>"
        "4. Click 'Connect' to establish connection<br>"
        "<br><b>Features:</b><br>"
        "- Real-time file transfer with WebRTC<br>"
        "- Automatic ICE candidate exchange<br>"
        "- Peer discovery and management<br>"
        "- Transfer progress monitoring"));
    settingsInfoLabel->setWordWrap(true);
    settingsTabLayout->addWidget(settingsInfoLabel);
    settingsTabLayout->addStretch();

    m_tabWidget->addTab(settingsTab, QStringLiteral("📋 Info"));

    // ═════ TAB 1: Send Files ══════════════════════════════════════════════
    QWidget* sendTab = new QWidget();
    QVBoxLayout* sendLayout = new QVBoxLayout(sendTab);

    // Peer Selection
    QGroupBox* peerGroupBox = new QGroupBox(QStringLiteral("Select Recipient"));
    QHBoxLayout* peerLayout = new QHBoxLayout(peerGroupBox);
    peerLayout->addWidget(new QLabel(QStringLiteral("Send to:")));
    m_peerComboBox = new QComboBox();
    m_peerComboBox->addItem(QStringLiteral("-- Select a peer --"), "");
    m_refreshPeersBtn = new QPushButton(QStringLiteral("🔄 Refresh"));
    m_refreshPeersBtn->setMaximumWidth(100);
    m_refreshPeersBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #2196F3; color: white; border-radius: 5px; "
        "padding: 6px; font-weight: bold; }"
        "QPushButton:hover { background-color: #0b7dda; }"));
    peerLayout->addWidget(m_peerComboBox, 1);
    peerLayout->addWidget(m_refreshPeersBtn);
    sendLayout->addWidget(peerGroupBox);

    // File Selection
    QGroupBox* fileGroupBox = new QGroupBox(QStringLiteral("Select File"));
    QHBoxLayout* fileLayout = new QHBoxLayout(fileGroupBox);
    m_sendFilePathEdit = new QLineEdit();
    m_sendFilePathEdit->setReadOnly(true);
    m_sendFilePathEdit->setPlaceholderText(QStringLiteral("No file selected"));
    m_browseSendFileBtn = new QPushButton(QStringLiteral("Browse..."));
    m_browseSendFileBtn->setMaximumWidth(100);
    fileLayout->addWidget(new QLabel(QStringLiteral("File:")));
    fileLayout->addWidget(m_sendFilePathEdit, 1);
    fileLayout->addWidget(m_browseSendFileBtn);
    sendLayout->addWidget(fileGroupBox);

    // Send Status
    m_sendStatusLabel = new QLabel(QStringLiteral("Ready to send files"));
    m_sendStatusLabel->setStyleSheet(QStringLiteral("color: #888; font-size: 12px;"));
    sendLayout->addWidget(m_sendStatusLabel);

    // Outgoing Transfers Table
    m_outgoingTransfersTable = new QTableWidget();
    m_outgoingTransfersTable->setColumnCount(6);
    m_outgoingTransfersTable->setHorizontalHeaderLabels(
        QStringList() << QStringLiteral("File Name")
        << QStringLiteral("Size")
        << QStringLiteral("To Peer")
        << QStringLiteral("Progress")
        << QStringLiteral("Status")
        << QStringLiteral("Speed"));
    m_outgoingTransfersTable->horizontalHeader()->setStretchLastSection(true);
    m_outgoingTransfersTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_outgoingTransfersTable->setStyleSheet(
        QStringLiteral("QTableWidget { gridline-color: #ddd; }"
            "QHeaderView::section { background-color: #f0f0f0; padding: 5px; }"
            "QTableWidget::item { padding: 5px; }"));
    sendLayout->addWidget(m_outgoingTransfersTable, 1);

    // Send Button
    QHBoxLayout* sendBtnLayout = new QHBoxLayout();
    m_sendFileBtn = new QPushButton(QStringLiteral("Send File"));
    m_sendFileBtn->setMinimumWidth(100);
    m_sendFileBtn->setEnabled(false);
    m_sendFileBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #4CAF50; color: white; border-radius: 5px; "
        "padding: 8px; font-weight: bold; font-size: 12px; }"
        "QPushButton:hover { background-color: #45a049; }"
        "QPushButton:disabled { background-color: #cccccc; }"));
    sendBtnLayout->addStretch();
    sendBtnLayout->addWidget(m_sendFileBtn);
    sendLayout->addLayout(sendBtnLayout);

    m_tabWidget->addTab(sendTab, QStringLiteral("📤 Send Files"));

    // ═════ TAB 2: Incoming File Offers ═════════════════════════════════════
    QWidget* incomingTab = new QWidget();
    QVBoxLayout* incomingLayout = new QVBoxLayout(incomingTab);

    m_incomingStatusLabel = new QLabel(QStringLiteral("Waiting for file offers..."));
    m_incomingStatusLabel->setStyleSheet(QStringLiteral("color: #888; font-size: 12px;"));
    incomingLayout->addWidget(m_incomingStatusLabel);

    m_incomingTable = new QTableWidget();
    m_incomingTable->setColumnCount(5);
    m_incomingTable->setHorizontalHeaderLabels(
        QStringList() << QStringLiteral("File Name")
        << QStringLiteral("Size")
        << QStringLiteral("From")
        << QStringLiteral("Type")
        << QStringLiteral("Status"));
    m_incomingTable->horizontalHeader()->setStretchLastSection(true);
    m_incomingTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_incomingTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_incomingTable->setStyleSheet(
        QStringLiteral("QTableWidget { gridline-color: #ddd; }"
            "QHeaderView::section { background-color: #f0f0f0; padding: 5px; }"
            "QTableWidget::item { padding: 5px; }"));
    incomingLayout->addWidget(m_incomingTable, 1);

    QGroupBox* saveGroupBox = new QGroupBox(QStringLiteral("Save Location"));
    QHBoxLayout* saveLayout = new QHBoxLayout(saveGroupBox);
    m_savePathEdit = new QLineEdit();
    m_savePathEdit->setReadOnly(true);
    m_browseSavePathBtn = new QPushButton(QStringLiteral("Browse..."));
    saveLayout->addWidget(new QLabel(QStringLiteral("Save To:")));
    saveLayout->addWidget(m_savePathEdit, 1);
    saveLayout->addWidget(m_browseSavePathBtn);
    incomingLayout->addWidget(saveGroupBox);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    m_acceptBtn = new QPushButton(QStringLiteral("Accept"));
    m_rejectBtn = new QPushButton(QStringLiteral("Reject"));
    m_acceptBtn->setEnabled(false);
    m_rejectBtn->setEnabled(false);
    m_acceptBtn->setMinimumWidth(100);
    m_rejectBtn->setMinimumWidth(100);
    m_acceptBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #4CAF50; color: white; border-radius: 5px; "
        "padding: 6px; font-weight: bold; }"
        "QPushButton:hover { background-color: #45a049; }"
        "QPushButton:disabled { background-color: #cccccc; }"));
    m_rejectBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #f44336; color: white; border-radius: 5px; "
        "padding: 6px; font-weight: bold; }"
        "QPushButton:hover { background-color: #da190b; }"
        "QPushButton:disabled { background-color: #cccccc; }"));
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_acceptBtn);
    buttonLayout->addWidget(m_rejectBtn);
    incomingLayout->addLayout(buttonLayout);

    m_tabWidget->addTab(incomingTab, QStringLiteral("📥 Incoming Offers"));

    // ═════ TAB 3: Active Transfers ═════════════════════════════════════════
    QWidget* activeTab = new QWidget();
    QVBoxLayout* activeLayout = new QVBoxLayout(activeTab);

    m_activeStatusLabel = new QLabel(QStringLiteral("No active transfers"));
    m_activeStatusLabel->setStyleSheet(QStringLiteral("color: #888; font-size: 12px;"));
    activeLayout->addWidget(m_activeStatusLabel);

    m_activeTransfersTable = new QTableWidget();
    m_activeTransfersTable->setColumnCount(6);
    m_activeTransfersTable->setHorizontalHeaderLabels(
        QStringList() << QStringLiteral("ID")
        << QStringLiteral("File")
        << QStringLiteral("Progress")
        << QStringLiteral("Speed")
        << QStringLiteral("Status")
        << QStringLiteral("Time Left"));
    m_activeTransfersTable->horizontalHeader()->setStretchLastSection(true);
    m_activeTransfersTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_activeTransfersTable->setStyleSheet(
        QStringLiteral("QTableWidget { gridline-color: #ddd; }"
            "QHeaderView::section { background-color: #f0f0f0; padding: 5px; }"
            "QTableWidget::item { padding: 5px; }"));
    activeLayout->addWidget(m_activeTransfersTable, 1);

    QHBoxLayout* activeBtnLayout = new QHBoxLayout();
    m_cancelTransferBtn = new QPushButton(QStringLiteral("Cancel Transfer"));
    m_cancelTransferBtn->setEnabled(false);
    m_cancelTransferBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #ff9800; color: white; border-radius: 5px; "
        "padding: 6px; font-weight: bold; }"
        "QPushButton:hover { background-color: #e68900; }"
        "QPushButton:disabled { background-color: #cccccc; }"));
    activeBtnLayout->addStretch();
    activeBtnLayout->addWidget(m_cancelTransferBtn);
    activeLayout->addLayout(activeBtnLayout);

    m_tabWidget->addTab(activeTab, QStringLiteral("⬆️ Active Transfers"));

    // ═════ TAB 4: Completed Transfers ══════════════════════════════════════
    QWidget* completedTab = new QWidget();
    QVBoxLayout* completedLayout = new QVBoxLayout(completedTab);

    m_completedTable = new QTableWidget();
    m_completedTable->setColumnCount(5);
    m_completedTable->setHorizontalHeaderLabels(
        QStringList() << QStringLiteral("File Name")
        << QStringLiteral("Size")
        << QStringLiteral("Completed")
        << QStringLiteral("Location")
        << QStringLiteral("Status"));
    m_completedTable->horizontalHeader()->setStretchLastSection(true);
    m_completedTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_completedTable->setStyleSheet(
        QStringLiteral("QTableWidget { gridline-color: #ddd; }"
            "QHeaderView::section { background-color: #f0f0f0; padding: 5px; }"
            "QTableWidget::item { padding: 5px; }"));
    completedLayout->addWidget(m_completedTable, 1);

    QHBoxLayout* completedBtnLayout = new QHBoxLayout();
    m_openFileBtn = new QPushButton(QStringLiteral("Open File"));
    m_openFolderBtn = new QPushButton(QStringLiteral("Open Folder"));
    m_clearCompletedBtn = new QPushButton(QStringLiteral("Clear History"));
    m_openFileBtn->setEnabled(false);
    m_openFolderBtn->setEnabled(false);
    for (auto* btn : { m_openFileBtn, m_openFolderBtn, m_clearCompletedBtn }) {
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { background-color: #2196F3; color: white; border-radius: 5px; "
            "padding: 6px; font-weight: bold; }"
            "QPushButton:hover { background-color: #0b7dda; }"
            "QPushButton:disabled { background-color: #cccccc; }"));
    }
    completedBtnLayout->addStretch();
    completedBtnLayout->addWidget(m_openFileBtn);
    completedBtnLayout->addWidget(m_openFolderBtn);
    completedBtnLayout->addWidget(m_clearCompletedBtn);
    completedLayout->addLayout(completedBtnLayout);

    m_tabWidget->addTab(completedTab, QStringLiteral("✅ Completed"));

    // ═════ TAB 5: Connected Peers ══════════════════════════════════════════
    QWidget* peersTab = new QWidget();
    QVBoxLayout* peersLayout = new QVBoxLayout(peersTab);

    m_connectionStatusLabel = new QLabel(QStringLiteral("Not connected"));
    m_connectionStatusLabel->setStyleSheet(
        QStringLiteral("color: #f44336; font-weight: bold; font-size: 12px;"));
    peersLayout->addWidget(m_connectionStatusLabel);

    m_peersTable = new QTableWidget();
    m_peersTable->setColumnCount(3);
    m_peersTable->setHorizontalHeaderLabels(
        QStringList() << QStringLiteral("Peer ID")
        << QStringLiteral("Status")
        << QStringLiteral("Joined At"));
    m_peersTable->horizontalHeader()->setStretchLastSection(true);
    m_peersTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_peersTable->setStyleSheet(
        QStringLiteral("QTableWidget { gridline-color: #ddd; }"
            "QHeaderView::section { background-color: #f0f0f0; padding: 5px; }"
            "QTableWidget::item { padding: 5px; }"));
    peersLayout->addWidget(m_peersTable, 1);

    m_tabWidget->addTab(peersTab, QStringLiteral("👥 Connected Peers"));

    // ═════ TAB 6: Logs ═════════════════════════════════════════════════════
    QWidget* logsTab = new QWidget();
    QVBoxLayout* logsLayout = new QVBoxLayout(logsTab);

    m_logsEdit = new QPlainTextEdit();
    m_logsEdit->setReadOnly(true);
    m_logsEdit->setFont(QFont(QStringLiteral("Courier New"), 10));
    m_logsEdit->setStyleSheet(
        QStringLiteral("QPlainTextEdit { background-color: #1e1e1e; color: #d4d4d4; "
            "border: 1px solid #ddd; }"));
    logsLayout->addWidget(m_logsEdit, 1);

    QHBoxLayout* logsButtonLayout = new QHBoxLayout();
    m_clearLogsBtn = new QPushButton(QStringLiteral("Clear Logs"));
    m_clearLogsBtn->setMaximumWidth(100);
    m_clearLogsBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #2196F3; color: white; border-radius: 5px; "
        "padding: 6px; font-weight: bold; }"
        "QPushButton:hover { background-color: #0b7dda; }"));
    logsButtonLayout->addStretch();
    logsButtonLayout->addWidget(m_clearLogsBtn);
    logsLayout->addLayout(logsButtonLayout);

    m_tabWidget->addTab(logsTab, QStringLiteral("📝 Logs"));

    mainLayout->addWidget(m_tabWidget);
    setCentralWidget(centralWidget);

    connectSignals();
}

void FileTransfer::createTrayIcon()
{
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(QIcon(QStringLiteral(":/icons/app.png")));

    m_trayMenu = new QMenu(this);
    m_trayMenu->addAction(QStringLiteral("Show"), this, &FileTransfer::toggleWindowVisibility);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(QStringLiteral("Exit"), qApp, &QApplication::quit);

    m_trayIcon->setContextMenu(m_trayMenu);
    connect(m_trayIcon, &QSystemTrayIcon::activated,
        this, &FileTransfer::onTrayIconActivated);

    m_trayIcon->show();
}

void FileTransfer::connectSignals()
{
    connect(m_connectBtn, &QPushButton::clicked, this, &FileTransfer::onConnectClicked);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &FileTransfer::onDisconnectClicked);
    // Send File Signals
    connect(m_browseSendFileBtn, &QPushButton::clicked,
        this, &FileTransfer::onBrowseSendFile);
    connect(m_sendFileBtn, &QPushButton::clicked,
        this, &FileTransfer::onSendFileClicked);
    connect(m_peerComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &FileTransfer::onPeerSelectionChanged);
    connect(m_refreshPeersBtn, &QPushButton::clicked,
        this, &FileTransfer::onRefreshPeersList);
    connect(m_browseSavePathBtn, &QPushButton::clicked,
        this, &FileTransfer::onBrowseSaveLocation);
    connect(m_acceptBtn, &QPushButton::clicked,
        this, &FileTransfer::onAcceptTransferClicked);
    connect(m_rejectBtn, &QPushButton::clicked,
        this, &FileTransfer::onRejectTransferClicked);
    connect(m_cancelTransferBtn, &QPushButton::clicked,
        this, &FileTransfer::onCancelTransfer);
    connect(m_openFileBtn, &QPushButton::clicked,
        this, &FileTransfer::onOpenTransferredFile);
    connect(m_openFolderBtn, &QPushButton::clicked,
        this, &FileTransfer::onOpenTransferFolder);
    connect(m_clearCompletedBtn, &QPushButton::clicked,
        this, &FileTransfer::onClearCompletedTransfers);
    connect(m_clearLogsBtn, &QPushButton::clicked, this, [this]() {
        m_logsEdit->clear();
        qInfo() << "[UI] Logs cleared";
        });

    connect(m_incomingTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        auto selected = m_incomingTable->selectedItems();
        if (!selected.isEmpty()) {
            int row = selected.first()->row();
            m_selectedTransferId = m_incomingTable->item(row, 0)->data(Qt::UserRole).toString();
            m_acceptBtn->setEnabled(true);
            m_rejectBtn->setEnabled(true);
        }
        else {
            m_acceptBtn->setEnabled(false);
            m_rejectBtn->setEnabled(false);
        }
        });

    connect(m_activeTransfersTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        auto selected = m_activeTransfersTable->selectedItems();
        m_cancelTransferBtn->setEnabled(!selected.isEmpty());
        if (!selected.isEmpty()) {
            int row = selected.first()->row();
            m_selectedTransferId = m_activeTransfersTable->item(row, 0)->data(Qt::UserRole).toString();
        }
        });

    connect(m_completedTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        auto selected = m_completedTable->selectedItems();
        bool hasSelection = !selected.isEmpty();
        m_openFileBtn->setEnabled(hasSelection);
        m_openFolderBtn->setEnabled(hasSelection);
        if (hasSelection) {
            int row = selected.first()->row();
            m_selectedTransferId = m_completedTable->item(row, 0)->data(Qt::UserRole).toString();
        }
        });
}

void FileTransfer::onConnectClicked()
{
    m_config.serverUrl = m_serverUrlEdit->text().isEmpty() ?
        QStringLiteral("http://localhost:3000") : m_serverUrlEdit->text();
    m_config.roomId = m_roomIdEdit->text().isEmpty() ?
        QStringLiteral("default-room") : m_roomIdEdit->text();
    m_config.password = m_passwordEdit->text().isEmpty() ?
        QStringLiteral("password") : m_passwordEdit->text();
    m_config.appType = QStringLiteral("desktop");

    qInfo() << "[Connection] Connecting to" << m_config.serverUrl
        << "with room" << m_config.roomId;

    m_signalingClient = new SignalingClient(m_config, this);
    m_fileTransferManager = new FileTransferManager(m_signalingClient->socketIO(),
        m_signalingClient->myPeerId(), this);

    connect(m_signalingClient, &SignalingClient::stage1Complete,
        this, &FileTransfer::onStage1Complete);
    connect(m_signalingClient, &SignalingClient::connectionFailed,
        this, &FileTransfer::onConnectionFailed);
    connect(m_signalingClient, &SignalingClient::loginFailed,
        this, &FileTransfer::onLoginFailed);

    connect(m_fileTransferManager, &FileTransferManager::incomingFileOffer,
        this, &FileTransfer::onIncomingFileOffer);
    connect(m_fileTransferManager, &FileTransferManager::transferAccepted,
        this, &FileTransfer::onTransferAccepted);
    connect(m_fileTransferManager, &FileTransferManager::transferRejected,
        this, &FileTransfer::onTransferRejected);
    connect(m_fileTransferManager, &FileTransferManager::transferCancelled,
        this, &FileTransfer::onTransferCancelled);
    connect(m_fileTransferManager, &FileTransferManager::transferProgress,
        this, &FileTransfer::onTransferProgress);
    connect(m_fileTransferManager, &FileTransferManager::transferComplete,
        this, &FileTransfer::onTransferComplete);
    connect(m_fileTransferManager, &FileTransferManager::transferError,
        this, &FileTransfer::onTransferError);
    connect(m_fileTransferManager, &FileTransferManager::dataChannelOpen,
        this, &FileTransfer::onDataChannelOpen);
    connect(m_fileTransferManager, &FileTransferManager::webRtcOfferReceived,
        this, &FileTransfer::onWebRtcOfferReceived);
    connect(m_fileTransferManager, &FileTransferManager::sendTransferInitiated,
        this, &FileTransfer::onSendTransferInitiated);
    connect(m_fileTransferManager, &FileTransferManager::sendTransferProgress,
        this, &FileTransfer::onSendTransferProgress);
    connect(m_fileTransferManager, &FileTransferManager::sendTransferComplete,
        this, &FileTransfer::onSendTransferComplete);
    connect(m_fileTransferManager, &FileTransferManager::sendTransferError,
        this, &FileTransfer::onSendTransferError);
    m_serverUrlEdit->setReadOnly(true);
    m_roomIdEdit->setReadOnly(true);
    m_passwordEdit->setReadOnly(true);
    m_connectBtn->setEnabled(false);
    m_disconnectBtn->setEnabled(true);

    m_signalingClient->start();
}

void FileTransfer::onDisconnectClicked()
{
    qInfo() << "[Connection] Disconnecting from server";

    if (m_signalingClient) {
        // Stop heartbeat first
        m_signalingClient->stopHeartbeat();

        // Delete immediately with synchronous cleanup
        m_signalingClient->disconnect();
        m_signalingClient->deleteLater();
        m_signalingClient = nullptr;
    }

    if (m_fileTransferManager) {
        m_fileTransferManager->disconnect();
        m_fileTransferManager->deleteLater();
        m_fileTransferManager = nullptr;
    }

    m_serverUrlEdit->setReadOnly(false);
    m_roomIdEdit->setReadOnly(false);
    m_passwordEdit->setReadOnly(false);
    m_connectBtn->setEnabled(true);
    m_disconnectBtn->setEnabled(false);

    m_isConnected = false;
    m_connectionStatusLabel->setText(QStringLiteral("Not connected"));
    m_connectionStatusLabel->setStyleSheet(
        QStringLiteral("color: #f44336; font-weight: bold; font-size: 12px;"));
    m_peersTable->setRowCount(0);
    m_incomingStatusLabel->setText(QStringLiteral("Waiting for connection..."));
}

void FileTransfer::onLogLine(LogHandler::Level level, const QString& timestamp, const QString& message)
{
    QString colorCode;
    QString levelStr;
    switch (level) {
    case LogHandler::Level::Debug:
        colorCode = QStringLiteral("#888");
        levelStr = QStringLiteral("[DBG]");
        break;
    case LogHandler::Level::Info:
        colorCode = QStringLiteral("#0a0");
        levelStr = QStringLiteral("[INF]");
        break;
    case LogHandler::Level::Warning:
        colorCode = QStringLiteral("#ff6600");
        levelStr = QStringLiteral("[WRN]");
        break;
    case LogHandler::Level::Critical:
        colorCode = QStringLiteral("#f00");
        levelStr = QStringLiteral("[CRT]");
        break;
    }

    QString logEntry = QString::fromLatin1("<span style='color:%1'>%2 %3 %4</span>")
        .arg(colorCode, timestamp, levelStr, message);
    m_logsEdit->appendHtml(logEntry);
}

void FileTransfer::onStage1Complete(const QString& roomId, const QString& peerId,
    const QJsonArray& peers)
{
    m_currentRoomId = roomId;
    m_myPeerId = peerId;
    m_isConnected = true;

    qInfo() << "[Connection] Stage 1 complete. Room:" << roomId << "Peer:" << peerId;

    m_connectionStatusLabel->setText(
        QStringLiteral("Connected to Room: %1 | Your ID: %2").arg(roomId, peerId));
    m_connectionStatusLabel->setStyleSheet(
        QStringLiteral("color: #4CAF50; font-weight: bold; font-size: 12px;"));

    m_peersTable->setRowCount(0);
    for (const auto& peerVal : peers) {
        const QJsonObject peer = peerVal.toObject();
        const QString id = peer.value(QStringLiteral("id")).toString();

        int row = m_peersTable->rowCount();
        m_peersTable->insertRow(row);

        QTableWidgetItem* idItem = new QTableWidgetItem(id);
        QTableWidgetItem* statusItem = new QTableWidgetItem(QStringLiteral("Online"));
        QTableWidgetItem* timeItem = new QTableWidgetItem(
            QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")));

        statusItem->setForeground(QColor(76, 175, 80));
        m_peersTable->setItem(row, 0, idItem);
        m_peersTable->setItem(row, 1, statusItem);
        m_peersTable->setItem(row, 2, timeItem);
    }

    // Refresh peers list for sending
    QTimer::singleShot(100, this, &FileTransfer::onRefreshPeersList);

    m_incomingStatusLabel->setText(QStringLiteral("Connected - Waiting for file offers..."));
}


void FileTransfer::onConnectionFailed(const QString& reason)
{
    m_isConnected = false;
    m_connectionStatusLabel->setText(QStringLiteral("Connection Failed: %1").arg(reason));
    m_connectionStatusLabel->setStyleSheet(
        QStringLiteral("color: #f44336; font-weight: bold; font-size: 12px;"));
    qCritical() << "[Connection] Failed:" << reason;
    showNotification(QStringLiteral("Connection Error"), reason);
}

void FileTransfer::onLoginFailed(const QString& reason)
{
    m_isConnected = false;
    m_connectionStatusLabel->setText(QStringLiteral("Login Failed: %1").arg(reason));
    m_connectionStatusLabel->setStyleSheet(
        QStringLiteral("color: #f44336; font-weight: bold; font-size: 12px;"));
    qCritical() << "[Connection] Login failed:" << reason;
    showNotification(QStringLiteral("Login Error"), reason);
}

void FileTransfer::onIncomingFileOffer(const FileTransferManager::TransferInfo& info)
{
    m_transferCache[info.transferId] = info;

    int row = m_incomingTable->rowCount();
    m_incomingTable->insertRow(row);

    QTableWidgetItem* nameItem = new QTableWidgetItem(info.name);
    QTableWidgetItem* sizeItem = new QTableWidgetItem(formatFileSize(info.size));
    QTableWidgetItem* fromItem = new QTableWidgetItem(info.senderId);
    QTableWidgetItem* typeItem = new QTableWidgetItem(info.mimeType);
    QTableWidgetItem* statusItem = new QTableWidgetItem(QStringLiteral("Pending"));

    nameItem->setData(Qt::UserRole, info.transferId);
    statusItem->setForeground(QColor(255, 152, 0));

    m_incomingTable->setItem(row, 0, nameItem);
    m_incomingTable->setItem(row, 1, sizeItem);
    m_incomingTable->setItem(row, 2, fromItem);
    m_incomingTable->setItem(row, 3, typeItem);
    m_incomingTable->setItem(row, 4, statusItem);

    m_transferRowMap[info.transferId] = row;

    qInfo() << "[Transfer] Incoming file offer:" << info.name << "from" << info.senderId;
    showNotification(QStringLiteral("New File Offer"),
        QStringLiteral("File: %1 (%2 bytes)").arg(info.name).arg(info.size));
}

void FileTransfer::onAcceptTransferClicked()
{
    if (m_selectedTransferId.isEmpty()) return;
    const QString fullPath = m_savePath + QStringLiteral("/") +
        m_transferCache[m_selectedTransferId].name;
    qInfo() << "[Transfer] Accepting transfer:" << m_selectedTransferId << "to" << fullPath;
    m_fileTransferManager->acceptTransfer(m_selectedTransferId, fullPath);
}

void FileTransfer::onRejectTransferClicked()
{
    if (m_selectedTransferId.isEmpty()) return;
    qInfo() << "[Transfer] Rejecting transfer:" << m_selectedTransferId;
    m_fileTransferManager->rejectTransfer(m_selectedTransferId);
}

void FileTransfer::onTransferAccepted(const QString& transferId)
{
    if (m_transferRowMap.contains(transferId)) {
        int row = m_transferRowMap[transferId];
        QTableWidgetItem* statusItem = m_incomingTable->item(row, 4);
        if (statusItem) {
            statusItem->setText(QStringLiteral("Accepted"));
            statusItem->setForeground(QColor(76, 175, 80));
        }
    }

    if (m_transferCache.contains(transferId)) {
        const auto& info = m_transferCache[transferId];
        addTransferToTable(info);
    }
    qInfo() << "[Transfer] Transfer accepted:" << transferId;
}

void FileTransfer::onTransferRejected(const QString& transferId)
{
    if (m_transferRowMap.contains(transferId)) {
        int row = m_transferRowMap[transferId];
        m_incomingTable->removeRow(row);
        m_transferRowMap.remove(transferId);
    }
    m_transferCache.remove(transferId);
    qInfo() << "[Transfer] Transfer rejected:" << transferId;
}

void FileTransfer::onTransferCancelled(const QString& transferId)
{
    if (m_transferRowMap.contains(transferId)) {
        int row = m_transferRowMap[transferId];
        m_activeTransfersTable->removeRow(row);
        m_transferRowMap.remove(transferId);
    }
    m_transferCache.remove(transferId);
    qInfo() << "[Transfer] Transfer cancelled:" << transferId;
}

void FileTransfer::onTransferProgress(const QString& transferId, double progress,
    int currentChunk, int totalChunks)
{
    if (m_transferRowMap.contains(transferId)) {
        int row = m_transferRowMap[transferId];
        QTableWidgetItem* progressItem = m_activeTransfersTable->item(row, 2);
        if (progressItem) {
            progressItem->setText(QStringLiteral("%1%").arg(static_cast<int>(progress)));
        }
    }
    m_activeStatusLabel->setText(
        QStringLiteral("Transferring... %1% (%2/%3 chunks)")
        .arg(static_cast<int>(progress), currentChunk, totalChunks));
}

void FileTransfer::onTransferComplete(const QString& transferId, const QString& path)
{
    if (m_transferRowMap.contains(transferId)) {
        int row = m_transferRowMap[transferId];
        m_activeTransfersTable->removeRow(row);
        m_transferRowMap.remove(transferId);
    }

    if (m_transferCache.contains(transferId)) {
        const auto& info = m_transferCache[transferId];

        int row = m_completedTable->rowCount();
        m_completedTable->insertRow(row);

        QTableWidgetItem* nameItem = new QTableWidgetItem(info.name);
        QTableWidgetItem* sizeItem = new QTableWidgetItem(formatFileSize(info.size));
        QTableWidgetItem* timeItem = new QTableWidgetItem(
            QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")));
        QTableWidgetItem* pathItem = new QTableWidgetItem(path);
        QTableWidgetItem* statusItem = new QTableWidgetItem(QStringLiteral("Completed"));

        nameItem->setData(Qt::UserRole, transferId);
        pathItem->setData(Qt::UserRole, path);
        statusItem->setForeground(QColor(76, 175, 80));

        m_completedTable->setItem(row, 0, nameItem);
        m_completedTable->setItem(row, 1, sizeItem);
        m_completedTable->setItem(row, 2, timeItem);
        m_completedTable->setItem(row, 3, pathItem);
        m_completedTable->setItem(row, 4, statusItem);

        m_transferRowMap[transferId] = row;
    }

    m_activeStatusLabel->setText(QStringLiteral("All transfers completed!"));
    qInfo() << "[Transfer] Transfer complete:" << transferId << "saved to" << path;
    showNotification(QStringLiteral("Transfer Complete"),
        QStringLiteral("File saved to: %1").arg(path));
}

void FileTransfer::onTransferError(const QString& transferId, const QString& reason)
{
    if (m_transferRowMap.contains(transferId)) {
        int row = m_transferRowMap[transferId];
        QTableWidgetItem* statusItem = m_activeTransfersTable->item(row, 4);
        if (statusItem) {
            statusItem->setText(QStringLiteral("Error"));
            statusItem->setForeground(QColor(244, 67, 54));
        }
    }
    qCritical() << "[Transfer] Error in transfer" << transferId << ":" << reason;
    showNotification(QStringLiteral("Transfer Error"), reason);
}

void FileTransfer::onDataChannelOpen(const QString& transferId)
{
    qDebug() << "[WebRTC] DataChannel opened for transfer:" << transferId;
}

void FileTransfer::onWebRtcOfferReceived(const QString& transferId, const QString& fromPeer,
    const QJsonObject& offer)
{
    qDebug() << "[WebRTC] Offer received from:" << fromPeer << "for transfer:" << transferId;
}

void FileTransfer::onBrowseSaveLocation()
{
    const QString dir = QFileDialog::getExistingDirectory(this,
        QStringLiteral("Select Save Location"),
        m_savePath,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!dir.isEmpty()) {
        m_savePath = dir;
        m_savePathEdit->setText(m_savePath);
        qInfo() << "[UI] Save location changed to:" << m_savePath;
    }
}

void FileTransfer::onCancelTransfer()
{
    if (m_selectedTransferId.isEmpty()) return;
    qInfo() << "[Transfer] Cancelling transfer:" << m_selectedTransferId;
    m_fileTransferManager->cancelTransfer(m_selectedTransferId);
}

void FileTransfer::onOpenTransferredFile()
{
    if (m_selectedTransferId.isEmpty()) return;
    const QString path = m_transferCache[m_selectedTransferId].savePath;
    if (!path.isEmpty() && QFile::exists(path)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        qInfo() << "[UI] Opened file:" << path;
    }
}

void FileTransfer::onOpenTransferFolder()
{
    if (m_selectedTransferId.isEmpty()) return;
    const QString path = m_transferCache[m_selectedTransferId].savePath;
    if (!path.isEmpty()) {
        QFileInfo fileInfo(path);
        QDesktopServices::openUrl(QUrl::fromLocalFile(fileInfo.absolutePath()));
        qInfo() << "[UI] Opened folder:" << fileInfo.absolutePath();
    }
}

void FileTransfer::onClearCompletedTransfers()
{
    m_completedTable->setRowCount(0);
    m_transferRowMap.clear();
    m_transferCache.clear();
    qInfo() << "[UI] Cleared completed transfers";
}

void FileTransfer::updatePeersList()
{
}

void FileTransfer::updateTransferUI(const QString& transferId)
{
    if (!m_transferCache.contains(transferId)) return;
}

void FileTransfer::showNotification(const QString& title, const QString& message)
{
    if (m_trayIcon && m_trayIcon->isVisible()) {
        m_trayIcon->showMessage(title, message, QSystemTrayIcon::Information, 5000);
    }
}

void FileTransfer::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::DoubleClick) {
        toggleWindowVisibility();
    }
}

void FileTransfer::toggleWindowVisibility()
{
    if (isVisible()) {
        // Don't close, just hide to tray
        hide();
    }
    else {
        showNormal();
        activateWindow();
    }
}

QString FileTransfer::formatFileSize(qint64 bytes) const
{
    static constexpr qint64 KB = 1024;
    static constexpr qint64 MB = KB * 1024;
    static constexpr qint64 GB = MB * 1024;

    if (bytes >= GB) {
        return QString::number(static_cast<double>(bytes) / GB, 'f', 2) + QStringLiteral(" GB");
    }
    else if (bytes >= MB) {
        return QString::number(static_cast<double>(bytes) / MB, 'f', 2) + QStringLiteral(" MB");
    }
    else if (bytes >= KB) {
        return QString::number(static_cast<double>(bytes) / KB, 'f', 2) + QStringLiteral(" KB");
    }
    else {
        return QString::number(bytes) + QStringLiteral(" B");
    }
}

void FileTransfer::addTransferToTable(const FileTransferManager::TransferInfo& info)
{
    int row = m_activeTransfersTable->rowCount();
    m_activeTransfersTable->insertRow(row);

    QTableWidgetItem* idItem = new QTableWidgetItem(info.transferId);
    QTableWidgetItem* nameItem = new QTableWidgetItem(info.name);
    QTableWidgetItem* progressItem = new QTableWidgetItem(QStringLiteral("0%"));
    QTableWidgetItem* speedItem = new QTableWidgetItem(QStringLiteral("--"));
    QTableWidgetItem* statusItem = new QTableWidgetItem(QStringLiteral("Active"));
    QTableWidgetItem* timeItem = new QTableWidgetItem(QStringLiteral("--"));

    idItem->setData(Qt::UserRole, info.transferId);
    statusItem->setForeground(QColor(33, 150, 243));

    m_activeTransfersTable->setItem(row, 0, idItem);
    m_activeTransfersTable->setItem(row, 1, nameItem);
    m_activeTransfersTable->setItem(row, 2, progressItem);
    m_activeTransfersTable->setItem(row, 3, speedItem);
    m_activeTransfersTable->setItem(row, 4, statusItem);
    m_activeTransfersTable->setItem(row, 5, timeItem);

    m_transferRowMap[info.transferId] = row;
}

void FileTransfer::updateTransferRow(const QString& transferId)
{
    if (!m_transferRowMap.contains(transferId)) return;
}

void FileTransfer::onBrowseSendFile()
{
    const QString fileName = QFileDialog::getOpenFileName(this,
        QStringLiteral("Select File to Send"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        QStringLiteral("All Files (*)"));

    if (!fileName.isEmpty()) {
        m_selectedSendFilePath = fileName;
        QFileInfo fileInfo(fileName);
        m_sendFilePathEdit->setText(fileInfo.fileName() +
            QStringLiteral(" (") + formatFileSize(fileInfo.size()) + QStringLiteral(")"));

        // Enable send button if peer is also selected
        if (!m_selectedSendToPeerId.isEmpty()) {
            m_sendFileBtn->setEnabled(true);
        }

        qInfo() << "[Send] File selected:" << fileName;
    }
}

void FileTransfer::onPeerSelectionChanged(int index)
{
    if (index <= 0) {
        m_selectedSendToPeerId = "";
        m_sendFileBtn->setEnabled(false);
        m_sendStatusLabel->setText(QStringLiteral("Select a peer to send to"));
        return;
    }

    m_selectedSendToPeerId = m_peerComboBox->currentData().toString();
    QString peerName = m_peerComboBox->currentText();

    // Enable send button if file is also selected
    if (!m_selectedSendFilePath.isEmpty()) {
        m_sendFileBtn->setEnabled(true);
        m_sendStatusLabel->setText(QStringLiteral("Ready to send to: %1").arg(peerName));
    }

    qInfo() << "[Send] Peer selected:" << peerName << "ID:" << m_selectedSendToPeerId;
}

void FileTransfer::onRefreshPeersList()
{
    m_peerComboBox->clear();
    m_peerComboBox->addItem(QStringLiteral("-- Select a peer --"), "");
    m_peerIdMap.clear();

    for (int row = 0; row < m_peersTable->rowCount(); ++row) {
        QTableWidgetItem* idItem = m_peersTable->item(row, 0);
        if (idItem) {
            QString peerId = idItem->text();
            // Don't add self
            if (peerId != m_myPeerId) {
                QString displayName = QStringLiteral("Peer: %1").arg(peerId.left(8));
                m_peerComboBox->addItem(displayName, peerId);
                m_peerIdMap[displayName] = peerId;
            }
        }
    }

    if (m_peerComboBox->count() <= 1) {
        m_sendStatusLabel->setText(QStringLiteral("No other peers in the room"));
        m_sendStatusLabel->setStyleSheet(QStringLiteral("color: #f44336; font-size: 12px;"));
    }
    else {
        m_sendStatusLabel->setText(QStringLiteral("Select a peer to send to"));
        m_sendStatusLabel->setStyleSheet(QStringLiteral("color: #888; font-size: 12px;"));
    }

    qInfo() << "[Send] Peers list refreshed. Total peers:" << (m_peerComboBox->count() - 1);
}

void FileTransfer::onSendFileClicked()
{
    if (m_selectedSendFilePath.isEmpty() || m_selectedSendToPeerId.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Invalid Selection"),
            QStringLiteral("Please select both a file and a peer"));
        return;
    }

    QFileInfo fileInfo(m_selectedSendFilePath);
    if (!fileInfo.exists()) {
        QMessageBox::critical(this, QStringLiteral("File Error"),
            QStringLiteral("Selected file no longer exists"));
        m_sendFilePathEdit->clear();
        m_selectedSendFilePath = "";
        return;
    }

    qInfo() << "[Send] Initiating file transfer to" << m_selectedSendToPeerId
        << "File:" << m_selectedSendFilePath;

    // Send to FileTransferManager
    if (m_fileTransferManager) {
        m_fileTransferManager->sendFile(m_selectedSendFilePath, m_selectedSendToPeerId);
    }

    // Reset UI
    m_sendFilePathEdit->clear();
    m_selectedSendFilePath = "";
    m_sendFileBtn->setEnabled(false);
}

void FileTransfer::onSendTransferInitiated(const QString& transferId, const QString& toPeerId)
{
    qInfo() << "[Send] Transfer initiated. ID:" << transferId << "To:" << toPeerId;

    if (m_transferCache.contains(transferId)) {
        const auto& info = m_transferCache[transferId];
        addOutgoingTransferToTable(transferId, info.name, toPeerId, info.size);
    }

    m_sendStatusLabel->setText(QStringLiteral("Sending file to %1...").arg(toPeerId.left(8)));
    m_sendStatusLabel->setStyleSheet(QStringLiteral("color: #ff9800; font-size: 12px;"));

    showNotification(QStringLiteral("Sending File"),
        QStringLiteral("Started sending to peer: %1").arg(toPeerId.left(8)));
}

void FileTransfer::onSendTransferProgress(const QString& transferId, double progress)
{
    if (m_transferRowMap.contains(transferId)) {
        int row = m_transferRowMap[transferId];
        if (row < m_outgoingTransfersTable->rowCount()) {
            QTableWidgetItem* progressItem = m_outgoingTransfersTable->item(row, 3);
            if (progressItem) {
                progressItem->setText(QStringLiteral("%1%").arg(static_cast<int>(progress)));
            }
        }
    }
}

void FileTransfer::onSendTransferComplete(const QString& transferId)
{
    qInfo() << "[Send] Transfer complete. ID:" << transferId;

    if (m_transferRowMap.contains(transferId)) {
        int row = m_transferRowMap[transferId];
        if (row < m_outgoingTransfersTable->rowCount()) {
            QTableWidgetItem* statusItem = m_outgoingTransfersTable->item(row, 4);
            if (statusItem) {
                statusItem->setText(QStringLiteral("Completed"));
                statusItem->setForeground(QColor(76, 175, 80));
            }
        }
    }

    m_sendStatusLabel->setText(QStringLiteral("File sent successfully!"));
    m_sendStatusLabel->setStyleSheet(QStringLiteral("color: #4CAF50; font-size: 12px;"));

    showNotification(QStringLiteral("Send Complete"),
        QStringLiteral("File sent successfully"));
}

void FileTransfer::onSendTransferError(const QString& transferId, const QString& reason)
{
    qCritical() << "[Send] Transfer error. ID:" << transferId << "Reason:" << reason;

    if (m_transferRowMap.contains(transferId)) {
        int row = m_transferRowMap[transferId];
        if (row < m_outgoingTransfersTable->rowCount()) {
            QTableWidgetItem* statusItem = m_outgoingTransfersTable->item(row, 4);
            if (statusItem) {
                statusItem->setText(QStringLiteral("Error"));
                statusItem->setForeground(QColor(244, 67, 54));
            }
        }
    }

    m_sendStatusLabel->setText(QStringLiteral("Error sending file: %1").arg(reason));
    m_sendStatusLabel->setStyleSheet(QStringLiteral("color: #f44336; font-size: 12px;"));

    showNotification(QStringLiteral("Send Error"), reason);
}

void FileTransfer::addOutgoingTransferToTable(const QString& transferId,
    const QString& fileName,
    const QString& toPeerId,
    qint64 fileSize)
{
    int row = m_outgoingTransfersTable->rowCount();
    m_outgoingTransfersTable->insertRow(row);

    QTableWidgetItem* nameItem = new QTableWidgetItem(fileName);
    QTableWidgetItem* sizeItem = new QTableWidgetItem(formatFileSize(fileSize));
    QTableWidgetItem* peerItem = new QTableWidgetItem(toPeerId.left(8));
    QTableWidgetItem* progressItem = new QTableWidgetItem(QStringLiteral("0%"));
    QTableWidgetItem* statusItem = new QTableWidgetItem(QStringLiteral("Sending"));
    QTableWidgetItem* speedItem = new QTableWidgetItem(QStringLiteral("--"));

    nameItem->setData(Qt::UserRole, transferId);
    statusItem->setForeground(QColor(33, 150, 243));

    m_outgoingTransfersTable->setItem(row, 0, nameItem);
    m_outgoingTransfersTable->setItem(row, 1, sizeItem);
    m_outgoingTransfersTable->setItem(row, 2, peerItem);
    m_outgoingTransfersTable->setItem(row, 3, progressItem);
    m_outgoingTransfersTable->setItem(row, 4, statusItem);
    m_outgoingTransfersTable->setItem(row, 5, speedItem);

    m_transferRowMap[transferId] = row;
    m_outgoingTransferMap[transferId] = toPeerId;
}


void FileTransfer::changeEvent(QEvent* e)
{
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::WindowStateChange) {
        // Only minimize to tray, don't intercept close
        if (isMinimized() && m_trayIcon && m_trayIcon->isVisible()) {
            hide();
            e->ignore();
        }
    }
}

void FileTransfer::closeEvent(QCloseEvent* e)
{
    // Gracefully disconnect if connected
    if (m_signalingClient) {
        qInfo() << "[Connection] Closing application - disconnecting from server";
        onDisconnectClicked();

        // Give some time for cleanup
        QApplication::processEvents();
    }

    // If tray icon is visible AND we're minimizing (not closing), hide instead
    if (m_trayIcon && m_trayIcon->isVisible() && isMinimized()) {
        hide();
        e->ignore();
        return;
    }

    // Accept the close event and cleanup
    qInfo() << "[UI] Application closing";

    // Save settings before closing
    saveSettings();

    // Accept close event to allow application to exit
    QMainWindow::closeEvent(e);

    // Force application quit
    QApplication::quit();
}

void FileTransfer::loadSettings()
{
    QSettings settings(QStringLiteral("FileTransfer"), QStringLiteral("FileTransferManager"));
    restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("windowState")).toByteArray());
    m_savePath = settings.value(QStringLiteral("savePath"),
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).toString();
    m_serverUrlEdit->setText(settings.value(QStringLiteral("serverUrl"),
        QStringLiteral("http://localhost:3000")).toString());
    m_roomIdEdit->setText(settings.value(QStringLiteral("roomId"),
        QStringLiteral("default-room")).toString());
}

void FileTransfer::saveSettings()
{
    QSettings settings(QStringLiteral("FileTransfer"), QStringLiteral("FileTransferManager"));
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    settings.setValue(QStringLiteral("windowState"), saveState());
    settings.setValue(QStringLiteral("savePath"), m_savePath);
    settings.setValue(QStringLiteral("serverUrl"), m_serverUrlEdit->text());
    settings.setValue(QStringLiteral("roomId"), m_roomIdEdit->text());
}