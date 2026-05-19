#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include "FileTransfer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QScrollBar>
#include <QHeaderView>
#include <QFileDialog>
#include <QFileInfo>
#include <QTextStream>
#include <QDateTime>
#include <QMessageBox>
#include <QStyle>
#include <QTimer>
#include <QClipboard>
#include <QDebug>

static const char* kColorDebug = "#9E9E9E";
static const char* kColorInfo = "#4FC3F7";
static const char* kColorWarning = "#FFB74D";
static const char* kColorCritical = "#EF5350";

enum TCol {
    TC_DIR = 0,
    TC_ID,
    TC_NAME,
    TC_SIZE,
    TC_PEER,
    TC_STATUS,
    TC_PROGRESS,
    TC_COUNT
};

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

FileTransfer::FileTransfer(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("File Transfer Client"));
    setMinimumSize(1200, 720);
    buildUi();

    qRegisterMetaType<LogHandler::Level>();
    qRegisterMetaType<FileTransferManager::TransferInfo>();

    connect(LogHandler::instance(), &LogHandler::logLine,
        this, &FileTransfer::onLogLine, Qt::QueuedConnection);

    qInfo() << "[UI] client ready – fill in credentials and click Connect";
}

// ---------------------------------------------------------------------------
// UI construction  (unchanged – reproduced in full for completeness)
// ---------------------------------------------------------------------------

void FileTransfer::buildUi()
{
    setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget {
            background-color: #1E1E2E; color: #CDD6F4;
            font-family: "Segoe UI", sans-serif; font-size: 13px;
        }
        QGroupBox {
            border: 1px solid #45475A; border-radius: 6px;
            margin-top: 10px; padding: 8px;
            font-weight: bold; color: #89B4FA;
        }
        QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
        QLineEdit, QComboBox {
            background-color: #313244; border: 1px solid #45475A;
            border-radius: 4px; padding: 4px 8px; color: #CDD6F4;
        }
        QLineEdit:focus, QComboBox:focus { border-color: #89B4FA; }
        QLineEdit:read-only { color: #A6E3A1; background-color: #1E1E2E;
                              border-color: #313244; }
        QComboBox::drop-down {
            border: none; width: 24px;
            background-color: #45475A;
            border-top-right-radius: 4px;
            border-bottom-right-radius: 4px;
        }
        QComboBox::down-arrow {
            width: 8px; height: 8px;
            background-color: #CDD6F4;
        }
        QComboBox QAbstractItemView {
            background-color: #313244; color: #CDD6F4;
            selection-background-color: #45475A;
        }
        QRadioButton { color: #CDD6F4; spacing: 6px; }
        QRadioButton::indicator {
            width: 14px; height: 14px; border-radius: 7px;
            border: 2px solid #45475A; background: #313244;
        }
        QRadioButton::indicator:checked { background: #89B4FA; border-color: #89B4FA; }
        QPushButton {
            background-color: #89B4FA; color: #1E1E2E; border: none;
            border-radius: 4px; padding: 6px 18px; font-weight: bold;
        }
        QPushButton:hover    { background-color: #B4BEFE; }
        QPushButton:pressed  { background-color: #74C7EC; }
        QPushButton:disabled { background-color: #45475A; color: #6C7086; }
        QPushButton#clearBtn   { background-color: #45475A; color: #CDD6F4; }
        QPushButton#exportBtn  { background-color: #A6E3A1; color: #1E1E2E; }
        QPushButton#acceptBtn  { background-color: #A6E3A1; color: #1E1E2E; }
        QPushButton#rejectBtn  { background-color: #F38BA8; color: #1E1E2E; }
        QPushButton#cancelBtn  { background-color: #FAB387; color: #1E1E2E; }
        QPushButton#sendBtn    { background-color: #CBA6F7; color: #1E1E2E; }
        QPushButton#browseBtn  { background-color: #45475A; color: #CDD6F4; }
        QPushButton#copyBtn    { background-color: #45475A; color: #CDD6F4;
                                 padding: 4px 10px; font-size: 11px; }
        QTableWidget {
            background-color: #11111B; border: 1px solid #313244;
            border-radius: 4px; gridline-color: #313244;
        }
        QTableWidget::item          { padding: 4px; }
        QTableWidget::item:selected { background-color: #45475A; }
        QHeaderView::section {
            background-color: #313244; color: #89B4FA;
            border: none; padding: 4px 8px; font-weight: bold;
        }
        QTextEdit {
            background-color: #11111B; border: 1px solid #313244;
            border-radius: 4px;
            font-family: "Cascadia Code", "Consolas", monospace; font-size: 12px;
        }
        QLabel#statusLabel[connected="true"]    { color: #A6E3A1; font-weight: bold; }
        QLabel#statusLabel[connected="false"]   { color: #F38BA8; font-weight: bold; }
        QLabel#statusLabel[connected="pending"] { color: #FAB387; font-weight: bold; }
        QLabel#peerIdLabel    { color: #CBA6F7; font-size: 11px; }
        QLabel#genRoomLabel   { color: #A6E3A1; font-weight: bold; font-size: 13px; }
        QLabel#fileLabel      { color: #A6E3A1; font-size: 11px; }
        QLabel#fileLabelEmpty { color: #6C7086; font-size: 11px; font-style: italic; }
        QLabel#hintLabel      { color: #6C7086; font-size: 10px; font-style: italic; }
        QProgressBar {
            border: 1px solid #45475A; border-radius: 4px;
            background: #313244; height: 6px;
        }
        QProgressBar::chunk { background-color: #89B4FA; border-radius: 4px; }
        QScrollBar:vertical { background: #1E1E2E; width: 8px; }
        QScrollBar::handle:vertical {
            background: #45475A; border-radius: 4px; min-height: 20px;
        }
    )"));

    auto* central = new QWidget(this);
    auto* rootVBox = new QVBoxLayout(central);
    rootVBox->setContentsMargins(10, 10, 10, 10);
    rootVBox->setSpacing(8);
    setCentralWidget(central);

    auto* mainSplit = new QSplitter(Qt::Horizontal, central);
    mainSplit->setHandleWidth(6);

    auto* leftWidget = new QWidget(mainSplit);
    auto* leftVBox = new QVBoxLayout(leftWidget);
    leftVBox->setContentsMargins(0, 0, 0, 0);
    leftVBox->setSpacing(8);

    // ── Connection group ──────────────────────────────────────────────────
    auto* connGroup = new QGroupBox(QStringLiteral("Connection"), leftWidget);
    auto* connForm = new QFormLayout(connGroup);
    connForm->setSpacing(8);

    m_serverEdit = new QLineEdit(
        QStringLiteral("http://localhost:3000"), connGroup);
    connForm->addRow(QStringLiteral("Server URL"), m_serverEdit);

    auto* modeBox = new QHBoxLayout();
    m_joinRadio = new QRadioButton(QStringLiteral("Join Room"), connGroup);
    m_createRadio = new QRadioButton(QStringLiteral("Create Room"), connGroup);
    m_joinRadio->setChecked(true);
    modeBox->addWidget(m_joinRadio);
    modeBox->addSpacing(16);
    modeBox->addWidget(m_createRadio);
    modeBox->addStretch();
    auto* modeWidget = new QWidget(connGroup);
    modeWidget->setLayout(modeBox);
    connForm->addRow(QStringLiteral("Mode"), modeWidget);

    connect(m_joinRadio, &QRadioButton::toggled,
        this, &FileTransfer::onModeChanged);
    connect(m_createRadio, &QRadioButton::toggled,
        this, &FileTransfer::onModeChanged);

    m_joinWidget = new QWidget(connGroup);
    auto* joinForm = new QFormLayout(m_joinWidget);
    joinForm->setContentsMargins(0, 0, 0, 0);
    joinForm->setSpacing(8);
    m_roomIdEdit = new QLineEdit(m_joinWidget);
    m_roomIdEdit->setPlaceholderText(QStringLiteral("e.g. J9bD9R"));
    joinForm->addRow(QStringLiteral("Room ID"), m_roomIdEdit);

    m_createWidget = new QWidget(connGroup);
    auto* createForm = new QFormLayout(m_createWidget);
    createForm->setContentsMargins(0, 0, 0, 0);
    createForm->setSpacing(8);

    auto* genRow = new QHBoxLayout();
    m_genRoomLabel = new QLabel(QStringLiteral("\u2014"), m_createWidget);
    m_genRoomLabel->setObjectName(QStringLiteral("genRoomLabel"));
    m_genRoomLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* copyBtn = new QPushButton(QStringLiteral("\u29C9 Copy"), m_createWidget);
    copyBtn->setObjectName(QStringLiteral("copyBtn"));
    copyBtn->setFixedWidth(60);
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        const QString id = m_genRoomLabel->text();
        if (!id.isEmpty() && id != QStringLiteral("\u2014"))
            QApplication::clipboard()->setText(id);
        });
    genRow->addWidget(m_genRoomLabel, 1);
    genRow->addWidget(copyBtn);
    auto* genRowWidget = new QWidget(m_createWidget);
    genRowWidget->setLayout(genRow);

    auto* hintLabel = new QLabel(
        QStringLiteral("Room ID is generated by the server after clicking Connect."),
        m_createWidget);
    hintLabel->setObjectName(QStringLiteral("hintLabel"));
    hintLabel->setWordWrap(true);
    createForm->addRow(QStringLiteral("Room ID"), genRowWidget);
    createForm->addRow(QString(), hintLabel);
    m_createWidget->setVisible(false);

    connForm->addRow(m_joinWidget);
    connForm->addRow(m_createWidget);

    m_passwordEdit = new QLineEdit(connGroup);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(QStringLiteral("Room password"));
    connForm->addRow(QStringLiteral("Password"), m_passwordEdit);

    m_connectBtn = new QPushButton(QStringLiteral("Connect"), connGroup);
    connect(m_connectBtn, &QPushButton::clicked,
        this, &FileTransfer::onConnectClicked);
    connForm->addRow(QString(), m_connectBtn);

    // ── Status group ──────────────────────────────────────────────────────
    auto* statusGroup = new QGroupBox(QStringLiteral("Status"), leftWidget);
    auto* statusForm = new QFormLayout(statusGroup);
    statusForm->setSpacing(8);

    m_statusLabel = new QLabel(QStringLiteral("Disconnected"), statusGroup);
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
    m_statusLabel->setProperty("connected", QStringLiteral("false"));

    m_roomLabel = new QLabel(QStringLiteral("\u2014"), statusGroup);

    auto* roomIdRow = new QHBoxLayout();
    m_roomIdCopyLabel = new QLabel(QStringLiteral("\u2014"), statusGroup);
    m_roomIdCopyLabel->setObjectName(QStringLiteral("genRoomLabel"));
    m_roomIdCopyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* copyRoomBtn = new QPushButton(QStringLiteral("\u29C9"), statusGroup);
    copyRoomBtn->setObjectName(QStringLiteral("copyBtn"));
    copyRoomBtn->setFixedWidth(28);
    copyRoomBtn->setToolTip(QStringLiteral("Copy room ID"));
    connect(copyRoomBtn, &QPushButton::clicked, this, [this]() {
        const QString id = m_roomIdCopyLabel->text();
        if (!id.isEmpty() && id != QStringLiteral("\u2014"))
            QApplication::clipboard()->setText(id);
        });
    roomIdRow->addWidget(m_roomIdCopyLabel, 1);
    roomIdRow->addWidget(copyRoomBtn);
    auto* roomIdRowWidget = new QWidget(statusGroup);
    roomIdRowWidget->setLayout(roomIdRow);

    m_peerIdLabel = new QLabel(QStringLiteral("\u2014"), statusGroup);
    m_peerIdLabel->setObjectName(QStringLiteral("peerIdLabel"));
    m_peerIdLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_peerIdLabel->setWordWrap(true);

    m_peersLabel = new QLabel(QStringLiteral("\u2014"), statusGroup);

    m_progressBar = new QProgressBar(statusGroup);
    m_progressBar->setRange(0, 0);
    m_progressBar->setVisible(false);
    m_progressBar->setFixedHeight(6);

    statusForm->addRow(QStringLiteral("State"), m_statusLabel);
    statusForm->addRow(QStringLiteral("Room"), m_roomLabel);
    statusForm->addRow(QStringLiteral("Room ID"), roomIdRowWidget);
    statusForm->addRow(QStringLiteral("My Peer ID"), m_peerIdLabel);
    statusForm->addRow(QStringLiteral("Peers"), m_peersLabel);
    statusForm->addRow(QString(), m_progressBar);

    // ── Send File group ───────────────────────────────────────────────────
    auto* sendGroup = new QGroupBox(QStringLiteral("Send File"), leftWidget);
    auto* sendVBox = new QVBoxLayout(sendGroup);
    sendVBox->setSpacing(8);

    auto* peerRow = new QHBoxLayout();
    auto* peerLbl = new QLabel(QStringLiteral("Target Peer:"), sendGroup);
    peerLbl->setFixedWidth(82);
    m_peerCombo = new QComboBox(sendGroup);
    m_peerCombo->setEditable(true);
    m_peerCombo->setPlaceholderText(QStringLiteral("Peer ID or select\u2026"));
    m_peerCombo->setMinimumWidth(160);
    m_peerCombo->setEnabled(false);
    peerRow->addWidget(peerLbl);
    peerRow->addWidget(m_peerCombo, 1);

    auto* fileRow = new QHBoxLayout();
    m_selectedFileLabel = new QLabel(
        QStringLiteral("No file chosen"), sendGroup);
    m_selectedFileLabel->setObjectName(QStringLiteral("fileLabelEmpty"));
    m_selectedFileLabel->setWordWrap(true);
    m_selectedFileLabel->setMinimumHeight(32);

    m_browseBtn = new QPushButton(QStringLiteral("Browse\u2026"), sendGroup);
    m_browseBtn->setObjectName(QStringLiteral("browseBtn"));
    m_browseBtn->setFixedWidth(76);
    m_browseBtn->setEnabled(false);
    connect(m_browseBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Select File to Send"),
            QDir::homePath(), QStringLiteral("All Files (*)"));
        if (path.isEmpty()) return;
        m_pendingSendPath = path;
        m_selectedFileLabel->setText(QFileInfo(path).fileName());
        m_selectedFileLabel->setObjectName(QStringLiteral("fileLabel"));
        m_selectedFileLabel->style()->unpolish(m_selectedFileLabel);
        m_selectedFileLabel->style()->polish(m_selectedFileLabel);
        m_sendBtn->setEnabled(true);
        qInfo() << "[UI] File selected for sending:" << path;
        });

    fileRow->addWidget(m_selectedFileLabel, 1);
    fileRow->addWidget(m_browseBtn);

    m_sendBtn = new QPushButton(
        QStringLiteral("\u2191  Send File"), sendGroup);
    m_sendBtn->setObjectName(QStringLiteral("sendBtn"));
    m_sendBtn->setEnabled(false);
    m_sendBtn->setMinimumHeight(34);
    connect(m_sendBtn, &QPushButton::clicked,
        this, &FileTransfer::onSendFileClicked);

    sendVBox->addLayout(peerRow);
    sendVBox->addLayout(fileRow);
    sendVBox->addWidget(m_sendBtn);

    leftVBox->addWidget(connGroup);
    leftVBox->addWidget(statusGroup);
    leftVBox->addWidget(sendGroup);
    leftVBox->addStretch();

    // ── RIGHT panel ───────────────────────────────────────────────────────
    auto* rightSplit = new QSplitter(Qt::Vertical, mainSplit);
    rightSplit->setHandleWidth(6);

    auto* ftGroup = new QGroupBox(
        QStringLiteral("File Transfers"), rightSplit);
    auto* ftVBox = new QVBoxLayout(ftGroup);

    m_transferTable = new QTableWidget(0, TC_COUNT, ftGroup);
    m_transferTable->setHorizontalHeaderLabels({
        QStringLiteral("Dir"),
        QStringLiteral("Transfer ID"),
        QStringLiteral("File Name"),
        QStringLiteral("Size"),
        QStringLiteral("Peer"),
        QStringLiteral("Status"),
        QStringLiteral("Progress")
        });
    m_transferTable->horizontalHeader()
        ->setSectionResizeMode(TC_NAME, QHeaderView::Stretch);
    m_transferTable->horizontalHeader()
        ->setSectionResizeMode(TC_DIR, QHeaderView::Fixed);
    m_transferTable->horizontalHeader()
        ->setSectionResizeMode(TC_ID, QHeaderView::ResizeToContents);
    m_transferTable->horizontalHeader()
        ->setSectionResizeMode(TC_PROGRESS, QHeaderView::Fixed);
    m_transferTable->setColumnWidth(TC_DIR, 36);
    m_transferTable->setColumnWidth(TC_PROGRESS, 150);
    m_transferTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_transferTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_transferTable->verticalHeader()->setVisible(false);

    auto* ftBtnRow = new QHBoxLayout();
    m_acceptBtn = new QPushButton(QStringLiteral("Accept"), ftGroup);
    m_rejectBtn = new QPushButton(QStringLiteral("Reject"), ftGroup);
    m_cancelBtn = new QPushButton(QStringLiteral("Cancel"), ftGroup);
    m_acceptBtn->setObjectName(QStringLiteral("acceptBtn"));
    m_rejectBtn->setObjectName(QStringLiteral("rejectBtn"));
    m_cancelBtn->setObjectName(QStringLiteral("cancelBtn"));
    m_acceptBtn->setEnabled(false);
    m_rejectBtn->setEnabled(false);
    m_cancelBtn->setEnabled(false);
    connect(m_acceptBtn, &QPushButton::clicked,
        this, &FileTransfer::onAcceptClicked);
    connect(m_rejectBtn, &QPushButton::clicked,
        this, &FileTransfer::onRejectClicked);
    connect(m_cancelBtn, &QPushButton::clicked,
        this, &FileTransfer::onCancelClicked);
    ftBtnRow->addStretch();
    ftBtnRow->addWidget(m_acceptBtn);
    ftBtnRow->addWidget(m_rejectBtn);
    ftBtnRow->addWidget(m_cancelBtn);

    ftVBox->addWidget(m_transferTable);
    ftVBox->addLayout(ftBtnRow);

    connect(m_transferTable, &QTableWidget::itemSelectionChanged,
        this, [this]() {
            const int row = m_transferTable->currentRow();
            if (row < 0) {
                m_acceptBtn->setEnabled(false);
                m_rejectBtn->setEnabled(false);
                m_cancelBtn->setEnabled(false);
                return;
            }
            const QString status =
                m_transferTable->item(row, TC_STATUS)
                ? m_transferTable->item(row, TC_STATUS)->text() : QString();
            const QString dir =
                m_transferTable->item(row, TC_DIR)
                ? m_transferTable->item(row, TC_DIR)
                ->data(Qt::UserRole).toString()
                : QStringLiteral("in");
            m_acceptBtn->setEnabled(
                status == QStringLiteral("pending") && dir == QStringLiteral("in"));
            m_rejectBtn->setEnabled(
                status == QStringLiteral("pending") && dir == QStringLiteral("in"));
            m_cancelBtn->setEnabled(
                status == QStringLiteral("active")
                || status == QStringLiteral("negotiating")
                || status == QStringLiteral("webrtc-connecting")
                || status == QStringLiteral("sending"));
        });

    auto* logGroup = new QGroupBox(
        QStringLiteral("Activity Log"), rightSplit);
    auto* logVBox = new QVBoxLayout(logGroup);

    m_logView = new QTextEdit(logGroup);
    m_logView->setReadOnly(true);
    m_logView->setLineWrapMode(QTextEdit::NoWrap);

    auto* logBtnRow = new QHBoxLayout();
    m_clearBtn = new QPushButton(QStringLiteral("Clear"), logGroup);
    m_exportBtn = new QPushButton(QStringLiteral("Export"), logGroup);
    m_clearBtn->setObjectName(QStringLiteral("clearBtn"));
    m_exportBtn->setObjectName(QStringLiteral("exportBtn"));
    m_clearBtn->setFixedWidth(80);
    m_exportBtn->setFixedWidth(80);
    connect(m_clearBtn, &QPushButton::clicked,
        this, &FileTransfer::onClearLogsClicked);
    connect(m_exportBtn, &QPushButton::clicked,
        this, &FileTransfer::onExportLogsClicked);
    logBtnRow->addStretch();
    logBtnRow->addWidget(m_clearBtn);
    logBtnRow->addWidget(m_exportBtn);

    logVBox->addWidget(m_logView);
    logVBox->addLayout(logBtnRow);

    rightSplit->addWidget(ftGroup);
    rightSplit->addWidget(logGroup);
    rightSplit->setSizes({ 300, 400 });

    mainSplit->addWidget(leftWidget);
    mainSplit->addWidget(rightSplit);
    mainSplit->setStretchFactor(0, 0);
    mainSplit->setStretchFactor(1, 1);
    mainSplit->setSizes({ 360, 840 });

    rootVBox->addWidget(mainSplit);
}

// ---------------------------------------------------------------------------
// Mode radio
// ---------------------------------------------------------------------------

void FileTransfer::onModeChanged()
{
    const bool isCreate = m_createRadio->isChecked();
    m_joinWidget->setVisible(!isCreate);
    m_createWidget->setVisible(isCreate);
    if (isCreate) {
        m_genRoomLabel->setText(QStringLiteral("\u2014"));
        m_connectBtn->setText(QStringLiteral("Create \u0026 Connect"));
    }
    else {
        m_connectBtn->setText(QStringLiteral("Connect"));
    }
}

// ---------------------------------------------------------------------------
// UI state helpers
// ---------------------------------------------------------------------------

void FileTransfer::setConnectionState(bool connecting, bool connected)
{
    m_connectBtn->setEnabled(!connecting && !connected);
    m_joinRadio->setEnabled(!connecting && !connected);
    m_createRadio->setEnabled(!connecting && !connected);
    m_progressBar->setVisible(connecting);

    const bool canSend = connected && m_ftm != nullptr;
    m_peerCombo->setEnabled(canSend);
    m_browseBtn->setEnabled(canSend);

    if (connecting) {
        m_statusLabel->setText(QStringLiteral("Connecting\u2026"));
        m_statusLabel->setProperty("connected", QStringLiteral("pending"));
    }
    else if (connected) {
        m_statusLabel->setText(QStringLiteral("Connected \u2713"));
        m_statusLabel->setProperty("connected", QStringLiteral("true"));
    }
    else {
        m_statusLabel->setText(QStringLiteral("Disconnected"));
        m_statusLabel->setProperty("connected", QStringLiteral("false"));
        m_roomLabel->setText(QStringLiteral("\u2014"));
        m_roomIdCopyLabel->setText(QStringLiteral("\u2014"));
        m_peerIdLabel->setText(QStringLiteral("\u2014"));
        m_peersLabel->setText(QStringLiteral("\u2014"));
        m_peerCombo->clear();
        m_peerCombo->setEnabled(false);
        m_browseBtn->setEnabled(false);
        m_sendBtn->setEnabled(false);
        m_pendingSendPath.clear();
        m_selectedFileLabel->setText(QStringLiteral("No file chosen"));
        m_selectedFileLabel->setObjectName(QStringLiteral("fileLabelEmpty"));
        m_selectedFileLabel->style()->unpolish(m_selectedFileLabel);
        m_selectedFileLabel->style()->polish(m_selectedFileLabel);
    }
    QStyle* s = QApplication::style();
    s->unpolish(m_statusLabel);
    s->polish(m_statusLabel);
    m_statusLabel->update();
}

void FileTransfer::populatePeerCombo(const QJsonArray& peers)
{
    m_peerCombo->clear();
    for (const QJsonValue& v : peers) {
        const QString id =
            v.isObject()
            ? v.toObject().value(QStringLiteral("userId")).toString()
            : v.toString();
        if (!id.isEmpty())
            m_peerCombo->addItem(id, id);
    }
    updatePeersLabel();
    qInfo() << "[UI] Peer combo populated with" << m_peerCombo->count()
        << "peer(s)";
}

void FileTransfer::addPeerToCombo(const QString& peerId)
{
    // Guard: do not add duplicates or our own ID.
    for (int i = 0; i < m_peerCombo->count(); ++i)
        if (m_peerCombo->itemData(i).toString() == peerId) return;
    if (m_client && peerId == m_client->myPeerId()) return;

    m_peerCombo->addItem(peerId, peerId);
    updatePeersLabel();
    qInfo() << "[UI] Peer joined – added to combo:" << peerId
        << "| total peers:" << m_peerCombo->count();
}

void FileTransfer::removePeerFromCombo(const QString& peerId)
{
    for (int i = 0; i < m_peerCombo->count(); ++i) {
        if (m_peerCombo->itemData(i).toString() == peerId) {
            m_peerCombo->removeItem(i);
            break;
        }
    }
    updatePeersLabel();
    qInfo() << "[UI] Peer left – removed from combo:" << peerId
        << "| total peers:" << m_peerCombo->count();
}

void FileTransfer::updatePeersLabel()
{
    m_peersLabel->setText(
        QString::number(m_peerCombo->count())
        + QStringLiteral(" peer(s) online"));
}

// ---------------------------------------------------------------------------
// Transfer table helpers
// ---------------------------------------------------------------------------

int FileTransfer::findTransferRow(const QString& transferId) const
{
    for (int r = 0; r < m_transferTable->rowCount(); ++r) {
        const QTableWidgetItem* it = m_transferTable->item(r, TC_ID);
        if (it && it->data(Qt::UserRole).toString() == transferId)
            return r;
    }
    return -1;
}

void FileTransfer::upsertTransferRow(
    const FileTransferManager::TransferInfo& info)
{
    int row = findTransferRow(info.transferId);
    if (row < 0) {
        row = m_transferTable->rowCount();
        m_transferTable->insertRow(row);
    }

    const QString sizeStr =
        info.size >= 1024 * 1024
        ? QStringLiteral("%1 MB")
        .arg(QString::number(info.size / 1024.0 / 1024.0, 'f', 2))
        : QStringLiteral("%1 KB")
        .arg(QString::number(info.size / 1024.0, 'f', 1));

    const bool isOut =
        info.direction == FileTransferManager::TransferDirection::Outgoing;

    auto* dirItem = new QTableWidgetItem(
        isOut ? QStringLiteral("\u2191") : QStringLiteral("\u2193"));
    dirItem->setTextAlignment(Qt::AlignCenter);
    dirItem->setForeground(isOut
        ? QColor(QStringLiteral("#CBA6F7"))
        : QColor(QStringLiteral("#A6E3A1")));
    dirItem->setData(Qt::UserRole,
        isOut ? QStringLiteral("out") : QStringLiteral("in"));
    m_transferTable->setItem(row, TC_DIR, dirItem);

    auto* idItem = new QTableWidgetItem(
        info.transferId.left(10) + QStringLiteral("\u2026"));
    idItem->setData(Qt::UserRole, info.transferId);
    m_transferTable->setItem(row, TC_ID, idItem);
    m_transferTable->setItem(row, TC_NAME, new QTableWidgetItem(info.name));
    m_transferTable->setItem(row, TC_SIZE, new QTableWidgetItem(sizeStr));
    m_transferTable->setItem(row, TC_PEER,
        new QTableWidgetItem(info.peerId.left(12) + QStringLiteral("\u2026")));
    m_transferTable->setItem(row, TC_STATUS, new QTableWidgetItem(info.status));

    auto* bar = qobject_cast<QProgressBar*>(
        m_transferTable->cellWidget(row, TC_PROGRESS));
    if (!bar) {
        bar = new QProgressBar(m_transferTable);
        bar->setRange(0, 100);
        bar->setTextVisible(true);
        bar->setStyleSheet(
            QStringLiteral("QProgressBar { background:#313244; border:none; "
                "border-radius:3px; height:14px; color:#CDD6F4; } "
                "QProgressBar::chunk { background:%1; border-radius:3px; }")
            .arg(isOut
                ? QStringLiteral("#CBA6F7")
                : QStringLiteral("#89B4FA")));
        m_transferTable->setCellWidget(row, TC_PROGRESS, bar);
    }
    bar->setValue(static_cast<int>(info.progress));
}

void FileTransfer::updateTransferRow(const QString& transferId,
    const QString& status, double progress)
{
    const int row = findTransferRow(transferId);
    if (row < 0) return;
    if (!status.isEmpty())
        m_transferTable->setItem(row, TC_STATUS,
            new QTableWidgetItem(status));
    if (progress >= 0.0)
        if (auto* bar = qobject_cast<QProgressBar*>(
            m_transferTable->cellWidget(row, TC_PROGRESS)))
            bar->setValue(static_cast<int>(progress));
}

// ---------------------------------------------------------------------------
// Connect button
// ---------------------------------------------------------------------------

void FileTransfer::onConnectClicked()
{
    const QString server = m_serverEdit->text().trimmed();
    const QString password = m_passwordEdit->text();
    const bool    isCreate = m_createRadio->isChecked();
    const QString roomId = isCreate
        ? QString{}
    : m_roomIdEdit->text().trimmed();

    if (server.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Missing Fields"),
            QStringLiteral("Please fill in Server URL and Password."));
        return;
    }
    if (!isCreate && roomId.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Missing Fields"),
            QStringLiteral("Please fill in the Room ID to join."));
        return;
    }

    if (m_ftm) { m_ftm = nullptr; }
    if (m_client) { m_client->deleteLater(); m_client = nullptr; }

    SignalingClient::Config cfg;
    cfg.serverUrl = server;
    cfg.roomId = roomId;
    cfg.password = password;
    cfg.mode = isCreate ? SignalingClient::Mode::CreateRoom
        : SignalingClient::Mode::JoinRoom;

    m_client = new SignalingClient(cfg, this);
    connect(m_client, &SignalingClient::roomCreated,
        this, &FileTransfer::onRoomCreated);
    connect(m_client, &SignalingClient::stage1Complete,
        this, &FileTransfer::onStage1Complete);
    connect(m_client, &SignalingClient::loginFailed,
        this, &FileTransfer::onLoginFailed);
    connect(m_client, &SignalingClient::connectionFailed,
        this, &FileTransfer::onConnectionFailed);

    // ── Live peer presence ─────────────────────────────────────────────────
    connect(m_client, &SignalingClient::peerJoined,
        this, &FileTransfer::onPeerJoined, Qt::QueuedConnection);
    connect(m_client, &SignalingClient::peerLeft,
        this, &FileTransfer::onPeerLeft, Qt::QueuedConnection);

    setConnectionState(true);
    m_client->start();
}

// ---------------------------------------------------------------------------
// Send File button
// ---------------------------------------------------------------------------

void FileTransfer::onSendFileClicked()
{
    if (!m_ftm) {
        QMessageBox::warning(this, QStringLiteral("Not Connected"),
            QStringLiteral("Connect to a room before sending a file."));
        return;
    }
    const QString targetPeer = m_peerCombo->currentText().trimmed();
    if (targetPeer.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("No Peer Selected"),
            QStringLiteral("Enter or select the Peer ID of the recipient."));
        return;
    }
    if (m_pendingSendPath.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("No File Chosen"),
            QStringLiteral("Click Browse\u2026 to choose a file before sending."));
        return;
    }

    qInfo() << "[UI] Initiating send:"
        << QFileInfo(m_pendingSendPath).fileName()
        << "\u2192 peer:" << targetPeer;

    m_ftm->sendFile(targetPeer, m_pendingSendPath);

    m_pendingSendPath.clear();
    m_selectedFileLabel->setText(QStringLiteral("No file chosen"));
    m_selectedFileLabel->setObjectName(QStringLiteral("fileLabelEmpty"));
    m_selectedFileLabel->style()->unpolish(m_selectedFileLabel);
    m_selectedFileLabel->style()->polish(m_selectedFileLabel);
    m_sendBtn->setEnabled(false);
}

// ---------------------------------------------------------------------------
// Wire FileTransferManager signals
// ---------------------------------------------------------------------------

void FileTransfer::connectFtmSignals()
{
    if (!m_ftm) return;

    connect(m_ftm, &FileTransferManager::incomingFileOffer,
        this, &FileTransfer::onIncomingFileOffer, Qt::QueuedConnection);
    connect(m_ftm, &FileTransferManager::transferAccepted,
        this, &FileTransfer::onTransferAccepted, Qt::QueuedConnection);
    connect(m_ftm, &FileTransferManager::transferRejected,
        this, &FileTransfer::onTransferRejected, Qt::QueuedConnection);
    connect(m_ftm, &FileTransferManager::transferCancelled,
        this, &FileTransfer::onTransferCancelled, Qt::QueuedConnection);
    connect(m_ftm, &FileTransferManager::transferProgress,
        this, &FileTransfer::onTransferProgress, Qt::QueuedConnection);
    connect(m_ftm, &FileTransferManager::transferComplete,
        this, &FileTransfer::onTransferComplete, Qt::QueuedConnection);
    connect(m_ftm, &FileTransferManager::transferError,
        this, &FileTransfer::onTransferError, Qt::QueuedConnection);
    connect(m_ftm, &FileTransferManager::webRtcOfferReceived,
        this, &FileTransfer::onWebRtcOfferReceived, Qt::QueuedConnection);
    connect(m_ftm, &FileTransferManager::outgoingFileOfferSent,
        this, &FileTransfer::onOutgoingFileOfferSent, Qt::QueuedConnection);
    connect(m_ftm, &FileTransferManager::outgoingFileOfferAccepted,
        this, &FileTransfer::onOutgoingFileOfferAccepted, Qt::QueuedConnection);
    connect(m_ftm, &FileTransferManager::outgoingFileOfferRejected,
        this, &FileTransfer::onOutgoingFileOfferRejected, Qt::QueuedConnection);
    connect(m_ftm, &FileTransferManager::sendProgress,
        this, &FileTransfer::onSendProgress, Qt::QueuedConnection);
    connect(m_ftm, &FileTransferManager::sendComplete,
        this, &FileTransfer::onSendComplete, Qt::QueuedConnection);
    connect(m_ftm, &FileTransferManager::sendError,
        this, &FileTransfer::onSendError, Qt::QueuedConnection);
}

// ---------------------------------------------------------------------------
// SignalingClient handlers
// ---------------------------------------------------------------------------

void FileTransfer::onRoomCreated(const QString& roomId,
    const QString& /*password*/)
{
    m_genRoomLabel->setText(roomId);
    m_roomIdCopyLabel->setText(roomId);
    qInfo() << "[Stage1] \U0001F3E0 Room created – ID:" << roomId;
    QMessageBox::information(this,
        QStringLiteral("Room Created"),
        QStringLiteral(
            "Your room has been created.\n\n"
            "Room ID:  %1\n\n"
            "Share this ID with the peers you want to invite.\n"
            "Connecting now\u2026").arg(roomId));
}

void FileTransfer::onStage1Complete(const QString& roomId,
    const QString& peerId, const QJsonArray& peers)
{
    m_ftm = m_client->ftm();
    connectFtmSignals();

    m_peerCombo->setEnabled(true);
    m_browseBtn->setEnabled(true);
    populatePeerCombo(peers);   // fills combo + updates label

    setConnectionState(false, true);
    m_roomLabel->setText(roomId);
    m_roomIdCopyLabel->setText(roomId);

    if (!peerId.isEmpty()) {
        m_peerIdLabel->setText(peerId);
    }
    else {
        m_peerIdLabel->setText(QStringLiteral("(pending\u2026)"));
        QTimer::singleShot(3000, this, [this]() {
            if (m_client && !m_client->myPeerId().isEmpty())
                m_peerIdLabel->setText(m_client->myPeerId());
            });
    }

    qInfo() << "[Stage1] \u2705 Joined room:" << roomId
        << "| Peers:" << peers.size() << "| FTM ready";
}

void FileTransfer::onLoginFailed(const QString& reason)
{
    setConnectionState(false, false);
    qCritical() << "[Stage1] \u274C Login failed:" << reason;
    QMessageBox::critical(this, QStringLiteral("Login Failed"), reason);
}

void FileTransfer::onConnectionFailed(const QString& reason)
{
    setConnectionState(false, false);
    qCritical() << "[Stage1] \u274C Connection failed:" << reason;
    QMessageBox::critical(this, QStringLiteral("Connection Failed"), reason);
}

// ---------------------------------------------------------------------------
// Live peer presence handlers  ← NEW
// ---------------------------------------------------------------------------

void FileTransfer::onPeerJoined(const QString& peerId,
    const QString& appType)
{
    addPeerToCombo(peerId);
    qInfo() << "[UI] \U0001F7E2 Peer joined:"
        << peerId << "appType:" << appType;
}

void FileTransfer::onPeerLeft(const QString& peerId)
{
    removePeerFromCombo(peerId);
    qInfo() << "[UI] \U0001F534 Peer left:" << peerId;
}

// ---------------------------------------------------------------------------
// FileTransferManager handlers – receive side
// ---------------------------------------------------------------------------

void FileTransfer::onIncomingFileOffer(
    const FileTransferManager::TransferInfo& info)
{
    qInfo() << "[FT] \U0001F4E5 File offer from peer:"
        << info.name
        << QString::number(info.size / 1024.0, 'f', 1) + QStringLiteral(" KB")
        << "| chunks:" << info.totalChunks
        << "| from:" << info.peerId;

    upsertTransferRow(info);
    raise();
    activateWindow();

    const int btn = QMessageBox::question(this,
        QStringLiteral("Incoming File Transfer"),
        QStringLiteral(
            "A peer wants to send you a file:\n\n"
            "  Name:  %1\n"
            "  Size:  %2 KB\n"
            "  From:  %3\n\n"
            "Accept the transfer?")
        .arg(info.name,
            QString::number(info.size / 1024.0, 'f', 1),
            info.peerId),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);

    if (btn != QMessageBox::Yes) {
        if (m_ftm) m_ftm->rejectTransfer(info.transferId);
        return;
    }

    const QString savePath = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save Received File"),
        QDir::homePath() + QDir::separator() + info.name,
        QStringLiteral("All Files (*)"));

    if (savePath.isEmpty()) {
        if (m_ftm) m_ftm->rejectTransfer(info.transferId);
        return;
    }

    qInfo() << "[FT] Accepted – save to:" << savePath;
    if (m_ftm) m_ftm->acceptTransfer(info.transferId, savePath);
}

void FileTransfer::onTransferAccepted(const QString& transferId)
{
    updateTransferRow(transferId, QStringLiteral("waiting-webrtc"));
    qInfo() << "[FT] \u2705 Transfer accepted – waiting for WebRTC:" << transferId;
}

void FileTransfer::onTransferRejected(const QString& transferId)
{
    updateTransferRow(transferId, QStringLiteral("rejected"));
    qInfo() << "[FT] Transfer rejected –" << transferId;
}

void FileTransfer::onTransferCancelled(const QString& transferId)
{
    updateTransferRow(transferId, QStringLiteral("cancelled"));
    qWarning() << "[FT] Transfer cancelled –" << transferId;
}

void FileTransfer::onTransferProgress(const QString& transferId,
    double progress, int chunksReceived, int totalChunks)
{
    updateTransferRow(transferId, QStringLiteral("active"), progress);
    qDebug() << "[FT] Progress:" << transferId
        << QString::number(progress, 'f', 1) + QStringLiteral("%")
        << chunksReceived << "/" << totalChunks;
}

void FileTransfer::onTransferComplete(const QString& transferId,
    const QString& filePath)
{
    updateTransferRow(transferId, QStringLiteral("completed"), 100.0);
    qInfo() << "[FT] \u2705 Receive complete –" << transferId
        << "saved to:" << filePath;
    QMessageBox::information(this,
        QStringLiteral("Transfer Complete"),
        QStringLiteral("File received successfully.\n\nSaved to:\n%1")
        .arg(filePath));
}

void FileTransfer::onTransferError(const QString& transferId,
    const QString& reason)
{
    updateTransferRow(transferId, QStringLiteral("error"));
    qCritical() << "[FT] \u274C Transfer error –" << transferId << reason;
}

void FileTransfer::onWebRtcOfferReceived(const QString& transferId,
    const QString& fromPeerId, const QJsonObject& offer)
{
    qInfo() << "[WebRTC] Offer received – transfer:" << transferId
        << "from:" << fromPeerId;
    qDebug() << "[WebRTC] SDP type:"
        << offer.value(QStringLiteral("type")).toString();
    updateTransferRow(transferId, QStringLiteral("negotiating"));
}

// ---------------------------------------------------------------------------
// FileTransferManager handlers – send side
// ---------------------------------------------------------------------------

void FileTransfer::onOutgoingFileOfferSent(const QString& transferId)
{
    // The FTM emits this immediately after inserting the TransferInfo, so
    // the row is guaranteed to exist in transfers() by the time we get here.
    if (m_ftm) {
        const auto map = m_ftm->transfers();
        if (map.contains(transferId))
            upsertTransferRow(map[transferId]);
    }
    updateTransferRow(transferId, QStringLiteral("offering"));
    qInfo() << "[FT] \U0001F4E4 Outgoing offer sent – waiting for peer:"
        << transferId;
}

void FileTransfer::onOutgoingFileOfferAccepted(const QString& transferId)
{
    updateTransferRow(transferId, QStringLiteral("webrtc-connecting"));
    qInfo() << "[FT] \u2705 Peer accepted our offer – WebRTC starting:"
        << transferId;
}

void FileTransfer::onOutgoingFileOfferRejected(const QString& transferId)
{
    updateTransferRow(transferId, QStringLiteral("rejected"));
    qWarning() << "[FT] Peer rejected our file offer –" << transferId;
    QMessageBox::information(this,
        QStringLiteral("Offer Rejected"),
        QStringLiteral(
            "The peer declined your file transfer request.\n\nID: %1\u2026")
        .arg(transferId.left(16)));
}

void FileTransfer::onSendProgress(const QString& transferId,
    double progress, int chunksSent, int totalChunks)
{
    updateTransferRow(transferId, QStringLiteral("sending"), progress);
    qDebug() << "[FT] Send progress:" << transferId
        << QString::number(progress, 'f', 1) + QStringLiteral("%")
        << chunksSent << "/" << totalChunks;
}

void FileTransfer::onSendComplete(const QString& transferId)
{
    updateTransferRow(transferId, QStringLiteral("completed"), 100.0);
    qInfo() << "[FT] \u2705 Send complete –" << transferId;
    QMessageBox::information(this,
        QStringLiteral("File Sent"),
        QStringLiteral("File sent successfully.\n\nTransfer ID:\n%1")
        .arg(transferId));
}

void FileTransfer::onSendError(const QString& transferId,
    const QString& reason)
{
    updateTransferRow(transferId, QStringLiteral("error"));
    qCritical() << "[FT] \u274C Send error –" << transferId << reason;
    QMessageBox::critical(this,
        QStringLiteral("Send Error"),
        QStringLiteral("Failed to send file.\n\nReason: %1\nID: %2")
        .arg(reason, transferId.left(16)));
}

// ---------------------------------------------------------------------------
// Transfer table buttons
// ---------------------------------------------------------------------------

void FileTransfer::onAcceptClicked()
{
    const int row = m_transferTable->currentRow();
    if (row < 0 || !m_ftm) return;
    const QTableWidgetItem* idItem = m_transferTable->item(row, TC_ID);
    if (!idItem) return;
    const QString transferId = idItem->data(Qt::UserRole).toString();
    const QString fileName =
        m_transferTable->item(row, TC_NAME)
        ? m_transferTable->item(row, TC_NAME)->text()
        : QStringLiteral("received_file");
    const QString savePath = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save Received File"),
        QDir::homePath() + QDir::separator() + fileName,
        QStringLiteral("All Files (*)"));
    if (savePath.isEmpty()) return;
    m_ftm->acceptTransfer(transferId, savePath);
    m_acceptBtn->setEnabled(false);
    m_rejectBtn->setEnabled(false);
}

void FileTransfer::onRejectClicked()
{
    const int row = m_transferTable->currentRow();
    if (row < 0 || !m_ftm) return;
    const QTableWidgetItem* idItem = m_transferTable->item(row, TC_ID);
    if (!idItem) return;
    m_ftm->rejectTransfer(idItem->data(Qt::UserRole).toString());
    m_acceptBtn->setEnabled(false);
    m_rejectBtn->setEnabled(false);
}

void FileTransfer::onCancelClicked()
{
    const int row = m_transferTable->currentRow();
    if (row < 0 || !m_ftm) return;
    const QTableWidgetItem* idItem = m_transferTable->item(row, TC_ID);
    if (!idItem) return;
    m_ftm->cancelTransfer(idItem->data(Qt::UserRole).toString());
    m_cancelBtn->setEnabled(false);
}

// ---------------------------------------------------------------------------
// Log panel
// ---------------------------------------------------------------------------

void FileTransfer::onClearLogsClicked() { m_logView->clear(); }

void FileTransfer::onExportLogsClicked()
{
    const QString path = QFileDialog::getSaveFileName(this,
        QStringLiteral("Export Log"),
        QStringLiteral("FileTransfer-log-%1.txt").arg(
            QDateTime::currentDateTime()
            .toString(QStringLiteral("yyyyMMdd-HHmmss"))),
        QStringLiteral("Text Files (*.txt);;All Files (*)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << m_logView->toPlainText();
        qInfo() << "[UI] Log exported to:" << path;
    }
    else {
        QMessageBox::critical(this, QStringLiteral("Export Failed"),
            QStringLiteral("Could not write to: ") + path);
    }
}

void FileTransfer::onLogLine(LogHandler::Level level,
    const QString& timestamp, const QString& message)
{
    appendLog(level, timestamp, message);
}

void FileTransfer::appendLog(LogHandler::Level level,
    const QString& timestamp, const QString& message)
{
    const char* color = kColorDebug;
    const char* prefix = "DBG";
    switch (level) {
    case LogHandler::Level::Debug:    color = kColorDebug;    prefix = "DBG"; break;
    case LogHandler::Level::Info:     color = kColorInfo;     prefix = "INF"; break;
    case LogHandler::Level::Warning:  color = kColorWarning;  prefix = "WRN"; break;
    case LogHandler::Level::Critical: color = kColorCritical; prefix = "ERR"; break;
    }
    const QString html = QStringLiteral(
        "<span style='color:#6C7086;'>[%1]</span> "
        "<span style='color:%2;font-weight:bold;'>[%3]</span> "
        "<span style='color:%2;'>%4</span>")
        .arg(timestamp.toHtmlEscaped(),
            QLatin1String(color),
            QLatin1String(prefix),
            message.toHtmlEscaped());
    m_logView->append(html);
    m_logView->verticalScrollBar()->setValue(
        m_logView->verticalScrollBar()->maximum());
}