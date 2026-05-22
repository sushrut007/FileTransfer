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
#include <QScrollArea>
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
#include <QFrame>
#include <QSizePolicy>
#include <QApplication>

// ── Log-level colours (match design spec) ────────────────────────────────────
static const char* kColorDebug = "#8B949E";   // grey
static const char* kColorInfo = "#22D3EE";   // cyan
static const char* kColorWarning = "#F59E0B";   // amber
static const char* kColorCritical = "#EF4444";   // red

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
    setWindowTitle(QStringLiteral("FileMitra \u2014 File Transfer"));
    setWindowIcon(QIcon(QStringLiteral(":/assets/FileMitraLogo.png")));
    setMinimumSize(960, 640);
    buildUi();


    qRegisterMetaType<LogHandler::Level>();
    qRegisterMetaType<FileTransferManager::TransferInfo>();

    connect(LogHandler::instance(), &LogHandler::logLine,
        this, &FileTransfer::onLogLine, Qt::QueuedConnection);

    qInfo() << "[UI] FileMitra client ready \u2013 fill in credentials and click Connect";
}

// ---------------------------------------------------------------------------
// buildUi
// ---------------------------------------------------------------------------
void FileTransfer::buildUi()
{
    // ── Global stylesheet (design spec: dark #0D1117, accent #3882F6) ─────
    setStyleSheet(QStringLiteral(R"(
        /* ── Base ── */
        QMainWindow, QWidget {
            background-color: #0D1117;
            color: #E6EDF3;
            font-family: "Inter", "Segoe UI", sans-serif;
            font-size: 14px;
        }
        QScrollArea, QScrollArea > QWidget > QWidget {
            background-color: transparent;
            border: none;
        }

        /* ── Cards ── */
       QGroupBox {
            background-color: #161B22;
            border: 1px solid #30363D;
            border-radius: 10px;
            margin-top: 0px;
            padding-top: 28px;
            padding-left: 12px;
            padding-right: 12px;
            padding-bottom: 12px;
            font-size: 12px;
            font-weight: 700;
            color: #E6EDF3;
            letter-spacing: 0.8px;
            text-transform: uppercase;
        }
        QGroupBox::title {
            subcontrol-origin: padding;
            subcontrol-position: top left;
            left: 14px;
            top: 8px;
            padding: 0 4px;
            background-color: transparent;
            color: #E6EDF3;
            font-size: 13px;
            font-weight: 700;
            letter-spacing: 0.8px;
        }

        /* ── Inputs ── */
        QLineEdit, QComboBox {
            background-color: #0D1117;
            border: 1px solid #30363D;
            border-radius: 8px;
            padding: 8px 12px;
            color: #E6EDF3;
            font-size: 13px;
            min-height: 20px;
            selection-background-color: #3882F6;
        }
        QLineEdit:focus, QComboBox:focus { border-color: #3882F6; background-color: #0D1117; }
        QLineEdit:hover, QComboBox:hover { border-color: #60A5FA; }
        QLineEdit:read-only { color: #22C55E; background-color: #0F1F12; border-color: #22C55E; }
        QComboBox::drop-down  { border: none; width: 28px; background: transparent; }
        QComboBox::down-arrow {
            image: none;
            border-left: 4px solid transparent;
            border-right: 4px solid transparent;
            border-top: 5px solid #8B949E;
        }
        QComboBox QAbstractItemView {
            background-color: #161B22;
            color: #E6EDF3;
            border: 1px solid #30363D;
            border-radius: 8px;
            selection-background-color: #1F2D50;
            outline: none;
            padding: 4px;
        }

        /* ── Radio buttons (used as toggle pills) ── */
        QRadioButton { color: #E6EDF3; spacing: 6px; font-size: 13px; }
        QRadioButton::indicator { width:14px; height:14px; border-radius:7px; border:2px solid #30363D; background:#0D1117; }
        QRadioButton::indicator:hover   { border-color: #3882F6; }
        QRadioButton::indicator:checked { background:#3882F6; border-color:#3882F6; }

        /* ── Buttons (base) ── */
        QPushButton {
            background-color: #3882F6;
            color: #FFFFFF;
            border: none;
            border-radius: 8px;
            padding: 8px 16px;
            font-weight: 600;
            font-size: 13px;
        }
        QPushButton:hover   { background-color: #60A5FA; }
        QPushButton:pressed { background-color: #2563EB; }
        QPushButton:disabled { background-color: #1C2128; color: #484F58; border: 1px solid #30363D; }

        /* Connect / primary */
        QPushButton#connectBtn {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #3882F6,stop:1 #60A5FA);
            color: #FFFFFF; border: none; border-radius: 8px;
            padding: 10px 0; font-size: 14px; font-weight: 700;
        }
        QPushButton#connectBtn:hover    { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #60A5FA,stop:1 #93C5FD); }
        QPushButton#connectBtn:disabled { background:#1C2128; color:#484F58; }

        /* Send */
        QPushButton#sendBtn {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #3882F6,stop:1 #60A5FA);
            color:#FFFFFF; border:none; border-radius:8px;
            padding:10px 0; font-size:14px; font-weight:700;
        }
        QPushButton#sendBtn:hover    { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #60A5FA,stop:1 #93C5FD); }
        QPushButton#sendBtn:disabled { background:#1C2128; color:#484F58; border:1px solid #30363D; }

        /* Browse / secondary */
        QPushButton#browseBtn {
            background-color: #21262D;
            color: #E6EDF3;
            border: 1px solid #30363D;
            border-radius: 8px;
        }
        QPushButton#browseBtn:hover    { background-color: #30363D; border-color: #8B949E; }
        QPushButton#browseBtn:disabled { color:#484F58; border-color:#21262D; }

        /* Clear log */
        QPushButton#clearBtn {
            background-color: #21262D;
            color: #8B949E;
            border: 1px solid #30363D;
            border-radius: 8px;
        }
        QPushButton#clearBtn:hover { background-color:#30363D; color:#E6EDF3; }

        /* Export log */
        QPushButton#exportBtn {
            background-color: #0F2618;
            color: #22C55E;
            border: 1px solid #22C55E;
            border-radius: 8px;
        }
        QPushButton#exportBtn:hover { background-color:#163520; }

        /* Accept */
        QPushButton#acceptBtn {
            background-color: #0F2618;
            color: #22C55E;
            border: 1px solid #22C55E;
            border-radius: 8px;
        }
        QPushButton#acceptBtn:hover    { background-color:#163520; }
        QPushButton#acceptBtn:disabled { background-color:#1C2128; color:#1A3D26; border-color:#21262D; }

        /* Reject */
        QPushButton#rejectBtn {
            background-color: #270D0D;
            color: #EF4444;
            border: 1px solid #EF4444;
            border-radius: 8px;
        }
        QPushButton#rejectBtn:hover    { background-color:#3A1212; }
        QPushButton#rejectBtn:disabled { background-color:#1C2128; color:#3A1515; border-color:#21262D; }

        /* Cancel */
        QPushButton#cancelBtn {
            background-color: #211A0A;
            color: #F59E0B;
            border: 1px solid #F59E0B;
            border-radius: 8px;
        }
        QPushButton#cancelBtn:hover    { background-color:#2E2310; }
        QPushButton#cancelBtn:disabled { background-color:#1C2128; color:#2E2208; border-color:#21262D; }

        /* Copy / icon buttons */
        QPushButton#copyBtn {
            background-color: #21262D;
            color: #8B949E;
            border: 1px solid #30363D;
            padding: 3px 7px;
            font-size: 11px;
            border-radius: 6px;
        }
        QPushButton#copyBtn:hover { background-color:#30363D; color:#E6EDF3; }

        /* Eye / show password */
        QPushButton#eyeBtn {
            background: transparent;
            border: none;
            color: #8B949E;
            padding: 0 4px;
            font-size: 14px;
        }
        QPushButton#eyeBtn:hover { color: #E6EDF3; }

        /* ── Transfer table ── */
        QTableWidget {
            background-color: #0D1117;
            border: 1px solid #30363D;
            border-radius: 8px;
            gridline-color: #161B22;
            outline: none;
        }
        QTableWidget::item            { padding: 6px 10px; border-bottom: 1px solid #161B22; }
        QTableWidget::item:selected   { background-color: #1F2D50; color: #E6EDF3; }
        QTableWidget::item:hover      { background-color: #161B22; }
        QHeaderView::section {
            background-color: #161B22;
            color: #8B949E;
            border: none;
            border-bottom: 1px solid #30363D;
            padding: 8px 10px;
            font-weight: 600;
            font-size: 11px;
            letter-spacing: 0.6px;
            text-transform: uppercase;
        }

        /* ── Log viewer ── */
        QTextEdit {
            background-color: #010409;
            border: 1px solid #21262D;
            border-radius: 8px;
            font-family: "JetBrains Mono", "Cascadia Code", "Consolas", monospace;
            font-size: 12px;
            color: #E6EDF3;
            padding: 8px;
        }

        /* ── Progress bars ── */
        QProgressBar {
            border: none;
            border-radius: 6px;
            background: #21262D;
            text-align: center;
            font-size: 10px;
            color: #E6EDF3;
        }
        QProgressBar::chunk {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #3882F6,stop:1 #60A5FA);
            border-radius: 6px;
        }

        /* ── Splitter ── */
        QSplitter::handle           { background-color: #21262D; }
        QSplitter::handle:hover     { background-color: #30363D; }
        QSplitter::handle:horizontal{ width: 4px; }
        QSplitter::handle:vertical  { height: 4px; }

        /* ── Scrollbars ── */
        QScrollBar:vertical           { background:transparent; width:6px; margin:0; }
        QScrollBar::handle:vertical   { background:#30363D; border-radius:3px; min-height:20px; }
        QScrollBar::handle:vertical:hover { background:#484F58; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
        QScrollBar:horizontal         { background:transparent; height:6px; }
        QScrollBar::handle:horizontal { background:#30363D; border-radius:3px; min-width:20px; }
        QScrollBar::handle:horizontal:hover { background:#484F58; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; }

        /* ── Frames ── */
        QFrame[frameShape="4"], QFrame[frameShape="5"] { color: #21262D; }

        /* ── Tooltips ── */
        QToolTip {
            background-color: #161B22;
            color: #E6EDF3;
            border: 1px solid #30363D;
            border-radius: 6px;
            padding: 4px 8px;
            font-size: 12px;
        }

        /* ── Named labels ── */
        QLabel#statusLabel  { font-size:14px; font-weight:700; background:transparent; border:none; }
        QLabel#peerIdLabel  {
            font-family:"JetBrains Mono",monospace; font-size:12px; color:#60A5FA;
            background:transparent; border:none;
        }
        QLabel#genRoomLabel {
            font-family:"JetBrains Mono",monospace; font-size:13px; font-weight:700;
            color:#22C55E; letter-spacing:2px;
            background-color:#0F1F12; border:1px solid #22C55E;
            border-radius:8px; padding:6px 10px;
        }
        QLabel#hintLabel      { color:#484F58; font-size:11px; font-style:italic; }
        QLabel#fileLabel      { color:#22C55E; font-size:13px; font-weight:600; }
        QLabel#fileLabelEmpty { color:#484F58; font-size:13px; font-style:italic; }
        QLabel#fileSizeLabel  { color:#8B949E; font-size:12px; }
    )"));

    // ── Root ─────────────────────────────────────────────────────────────
    auto* central = new QWidget(this);
    auto* rootVBox = new QVBoxLayout(central);
    rootVBox->setContentsMargins(0, 0, 0, 0);
    rootVBox->setSpacing(0);
    setCentralWidget(central);

    // ── Header (52px, #161B22) ─────────────────────────────────────────
    auto* header = new QWidget(central);
    header->setFixedHeight(52);
    header->setStyleSheet(QStringLiteral(
        "QWidget { background-color:#161B22; border-bottom:1px solid #21262D; }"));
    auto* hLay = new QHBoxLayout(header);
    hLay->setContentsMargins(16, 0, 16, 0);
    hLay->setSpacing(10);

    // Logo: blue rounded-rect with arrow
    auto* hLogo = new QLabel(header);
    hLogo->setFixedSize(32, 32);
    hLogo->setAlignment(Qt::AlignCenter);
    QPixmap logo(":/assets/FileMitraLogo.png");
    hLogo->setPixmap(logo.scaledToWidth(32, Qt::SmoothTransformation));
    hLogo->setText("");  // Clear the text
    //hLogo->setStyleSheet(QStringLiteral(
    //    "background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #3882F6,stop:1 #60A5FA);"
    //    "color:#FFFFFF; font-size:14px; font-weight:700;"
    //    "border-radius:8px; border:none;"));

    auto* hTitle = new QLabel(QStringLiteral("FileMitra"), header);
    hTitle->setStyleSheet(QStringLiteral(
        "font-size:16px; font-weight:700; color:#E6EDF3; background:transparent; border:none;"));

    auto* hSub = new QLabel(QStringLiteral("File Transfer Client"), header);
    hSub->setStyleSheet(QStringLiteral(
        "font-size:11px; color:#484F58; background:transparent; border:none;"));

    auto* hStack = new QVBoxLayout();
    hStack->setSpacing(1);
    hStack->addWidget(hTitle);
    hStack->addWidget(hSub);

    auto* hVer = new QLabel(QStringLiteral("v1.0"), header);
    hVer->setStyleSheet(QStringLiteral(
        "background-color:#21262D; color:#8B949E; border:1px solid #30363D;"
        "border-radius:12px; padding:2px 10px; font-size:11px; font-weight:600;"));

    hLay->addWidget(hLogo);
    hLay->addLayout(hStack);
    hLay->addStretch();
    hLay->addWidget(hVer);
    rootVBox->addWidget(header);

    // ── Content ───────────────────────────────────────────────────────────
    auto* content = new QWidget(central);
    auto* contentLay = new QVBoxLayout(content);
    contentLay->setContentsMargins(10, 10, 10, 10);
    contentLay->setSpacing(0);
    rootVBox->addWidget(content, 1);

    auto* mainSplit = new QSplitter(Qt::Horizontal, content);
    mainSplit->setChildrenCollapsible(false);

    // ── LEFT panel (scrollable, 330px) ────────────────────────────────────
    auto* leftScroll = new QScrollArea(mainSplit);
    leftScroll->setWidgetResizable(true);
    leftScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    leftScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    leftScroll->setFrameShape(QFrame::NoFrame);
    leftScroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    leftScroll->setMinimumWidth(300);
    leftScroll->setMaximumWidth(380);

    auto* leftWidget = new QWidget();
    leftWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    auto* leftVBox = new QVBoxLayout(leftWidget);
    leftVBox->setContentsMargins(2, 2, 8, 8);
    leftVBox->setSpacing(10);
    leftScroll->setWidget(leftWidget);

    // ── Helpers ──────────────────────────────────────────────────────────
    auto mkFieldLbl = [](const QString& t, QWidget* p) {
        auto* l = new QLabel(t, p);
        l->setStyleSheet(QStringLiteral(
            "font-size:13px; font-weight:500; color:#8B949E; border:none; background:transparent;"));
        return l;
        };

    // ── CONNECTION card ──────────────────────────────────────────────────
    auto* connGroup = new QGroupBox(QStringLiteral("Connection"), leftWidget);
    auto* connVBox = new QVBoxLayout(connGroup);
    connVBox->setContentsMargins(10, 10, 10, 10);
    connVBox->setSpacing(6);

    // Server URL
    connVBox->addWidget(mkFieldLbl(QStringLiteral("Server URL"), connGroup));
    m_serverEdit = new QLineEdit(QStringLiteral("http://localhost:3000"), connGroup);
    m_serverEdit->setPlaceholderText(QStringLiteral("wss://your-server:port"));
    connVBox->addWidget(m_serverEdit);

    // Mode toggles
    auto* modeLbl = mkFieldLbl(QStringLiteral("Mode"), connGroup);
    connVBox->addWidget(modeLbl);

    auto* modeRow = new QHBoxLayout();
    modeRow->setSpacing(8);
    m_joinRadio = new QRadioButton(QStringLiteral("Join Room"), connGroup);
    m_createRadio = new QRadioButton(QStringLiteral("Create Room"), connGroup);
    m_joinRadio->setChecked(true);

    auto styleToggle = [](QRadioButton* btn, const QString& activeColor) {
        btn->setStyleSheet(QStringLiteral(
            "QRadioButton {"
            "  background-color:#21262D; border:1px solid #30363D;"
            "  border-radius:8px; padding:8px 12px;"
            "  color:#8B949E; font-weight:500; font-size:13px; }"
            "QRadioButton:hover { border-color:#60A5FA; color:#E6EDF3; }"
            "QRadioButton::indicator { width:0; height:0; margin:0; }"
            "QRadioButton:checked {"
            "  background-color:%1; border-color:%1; color:#FFFFFF; font-weight:600; }"
        ).arg(activeColor));
        };
    styleToggle(m_joinRadio, QStringLiteral("#3882F6"));
    styleToggle(m_createRadio, QStringLiteral("#3882F6"));
    modeRow->addWidget(m_joinRadio, 1);
    modeRow->addWidget(m_createRadio, 1);
    connVBox->addLayout(modeRow);

    connect(m_joinRadio, &QRadioButton::toggled, this, &FileTransfer::onModeChanged);
    connect(m_createRadio, &QRadioButton::toggled, this, &FileTransfer::onModeChanged);

    // Join sub-widget
    m_joinWidget = new QWidget(connGroup);
    m_joinWidget->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* joinL = new QVBoxLayout(m_joinWidget);
    joinL->setContentsMargins(0, 0, 0, 0); joinL->setSpacing(6);
    joinL->addWidget(mkFieldLbl(QStringLiteral("Room ID"), m_joinWidget));
    m_roomIdEdit = new QLineEdit(m_joinWidget);
    m_roomIdEdit->setPlaceholderText(QStringLiteral("Enter room ID"));
    joinL->addWidget(m_roomIdEdit);
    connVBox->addWidget(m_joinWidget);

    // Create sub-widget
    m_createWidget = new QWidget(connGroup);
    m_createWidget->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* createL = new QVBoxLayout(m_createWidget);

    auto* genRowLay = new QHBoxLayout(); genRowLay->setSpacing(6);
    m_genRoomLabel = new QLabel(QStringLiteral("\u2014"), m_createWidget);
    m_genRoomLabel->setObjectName(QStringLiteral("genRoomLabel"));
    m_genRoomLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_genRoomLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* copyBtn = new QPushButton(QStringLiteral("\u29C9"), m_createWidget);
    copyBtn->setObjectName(QStringLiteral("copyBtn"));
    copyBtn->setFixedSize(32, 32);
    copyBtn->setToolTip(QStringLiteral("Copy Room ID"));
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        const QString id = m_genRoomLabel->text();
        if (!id.isEmpty() && id != QStringLiteral("\u2014"))
            QApplication::clipboard()->setText(id);
        });
    genRowLay->addWidget(m_genRoomLabel, 1);
    genRowLay->addWidget(copyBtn);
    createL->addLayout(genRowLay);

    auto* hintLbl = new QLabel(
        QStringLiteral("\u24D8  Room ID is generated after connecting."), m_createWidget);
    hintLbl->setObjectName(QStringLiteral("hintLabel"));
    hintLbl->setWordWrap(true);
    createL->addWidget(hintLbl);
    m_createWidget->setVisible(false);
    connVBox->addWidget(m_createWidget);

    // Password + eye toggle
    connVBox->addWidget(mkFieldLbl(QStringLiteral("Password"), connGroup));
    auto* pwRow = new QWidget(connGroup);
    pwRow->setStyleSheet(QStringLiteral(
        "QWidget { background-color:#0D1117; border:1px solid #30363D; border-radius:8px; }"));
    auto* pwLay = new QHBoxLayout(pwRow);
    pwLay->setContentsMargins(0, 0, 6, 0);
    pwLay->setSpacing(0);
    m_passwordEdit = new QLineEdit(pwRow);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(QStringLiteral("Room password"));
    m_passwordEdit->setStyleSheet(QStringLiteral(
        "QLineEdit { border:none; background:transparent; padding:8px 12px;"
        "  color:#E6EDF3; font-size:13px; border-radius:8px; }"));
    auto* eyeBtn = new QPushButton(QStringLiteral("\U0001F441"), pwRow);
    eyeBtn->setObjectName(QStringLiteral("eyeBtn"));
    eyeBtn->setFixedSize(28, 28);
    eyeBtn->setCheckable(true);
    eyeBtn->setToolTip(QStringLiteral("Show/hide password"));
    connect(eyeBtn, &QPushButton::toggled, this, [this](bool checked) {
        m_passwordEdit->setEchoMode(
            checked ? QLineEdit::Normal : QLineEdit::Password);
        });
    pwLay->addWidget(m_passwordEdit, 1);
    pwLay->addWidget(eyeBtn);
    connVBox->addWidget(pwRow);
    connVBox->addSpacing(4);

    // Connect button
    m_connectBtn = new QPushButton(QStringLiteral("Connect"), connGroup);
    m_connectBtn->setObjectName(QStringLiteral("connectBtn"));
    m_connectBtn->setMinimumHeight(40);
    m_connectBtn->setCursor(Qt::PointingHandCursor);
    connect(m_connectBtn, &QPushButton::clicked, this, &FileTransfer::onConnectClicked);
    connVBox->addWidget(m_connectBtn);

    // ── STATUS card ───────────────────────────────────────────────────────
    auto* statusGroup = new QGroupBox(QStringLiteral("Status"), leftWidget);
    auto* statusGrid = new QGridLayout(statusGroup);
    statusGrid->setContentsMargins(12, 12, 12, 12);
    statusGrid->setHorizontalSpacing(12);
    statusGrid->setVerticalSpacing(10);
    statusGrid->setColumnStretch(1, 1);

    auto mkMetaLbl = [](const QString& t, QWidget* p) {
        auto* l = new QLabel(t, p);
        l->setStyleSheet(QStringLiteral(
            "color:#484F58; font-size:12px; font-weight:600; letter-spacing:0.5px;"
            "background:transparent; border:none;"));
        l->setFixedWidth(74);
        return l;
        };

    // Status dot + label row
    auto* dotLbl = new QLabel(QStringLiteral("\u25CF"), statusGroup);
    dotLbl->setObjectName(QStringLiteral("statusDot"));
    dotLbl->setStyleSheet(QStringLiteral(
        "color:#EF4444; font-size:12px; background:transparent; border:none;"));
    dotLbl->setFixedWidth(16);

    m_statusLabel = new QLabel(QStringLiteral("Disconnected"), statusGroup);
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
    m_statusLabel->setProperty("connected", QStringLiteral("false"));
    m_statusLabel->setStyleSheet(QStringLiteral(
        "font-size:13px; font-weight:700; color:#EF4444; background:transparent; border:none;"));
    m_statusLabel->setProperty("dotPtr",
        QVariant::fromValue(static_cast<QObject*>(dotLbl)));

    auto* stateW = new QWidget(statusGroup);
    stateW->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* stateL = new QHBoxLayout(stateW);
    stateL->setContentsMargins(0, 0, 0, 0); stateL->setSpacing(6);
    stateL->addWidget(dotLbl);
    stateL->addWidget(m_statusLabel);
    stateL->addStretch();

    // Room label
    m_roomLabel = new QLabel(QStringLiteral("\u2014"), statusGroup);
    m_roomLabel->setStyleSheet(QStringLiteral(
        "color:#E6EDF3; font-size:13px; background:transparent; border:none;"));

    // Room ID row
    auto* ridW = new QWidget(statusGroup);
    ridW->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* ridL = new QHBoxLayout(ridW);
    ridL->setContentsMargins(0, 0, 0, 0); ridL->setSpacing(4);

    m_roomIdCopyLabel = new QLabel(QStringLiteral("\u2014"), statusGroup);
    m_roomIdCopyLabel->setObjectName(QStringLiteral("roomIdCopyLabel")); // was "genRoomLabel"
    m_roomIdCopyLabel->setStyleSheet(QStringLiteral(
        "color:#E6EDF3; font-size:13px; background:transparent; border:none;"));
    m_roomIdCopyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_roomIdCopyLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* copyRoomBtn = new QPushButton(QStringLiteral("\u29C9"), statusGroup);
    copyRoomBtn->setObjectName(QStringLiteral("copyBtn"));
    copyRoomBtn->setFixedSize(24, 24);
    copyRoomBtn->setToolTip(QStringLiteral("Copy Room ID"));
    connect(copyRoomBtn, &QPushButton::clicked, this, [this]() {
        const QString id = m_roomIdCopyLabel->text();
        if (!id.isEmpty() && id != QStringLiteral("\u2014"))
            QApplication::clipboard()->setText(id);
        });
    ridL->addWidget(m_roomIdCopyLabel, 1);
    ridL->addWidget(copyRoomBtn);

    m_peerIdLabel = new QLabel(QStringLiteral("\u2014"), statusGroup);
    m_peerIdLabel->setObjectName(QStringLiteral("peerIdLabel"));
    m_peerIdLabel->setStyleSheet(QStringLiteral(
        "color:#60A5FA; font-family:'JetBrains Mono',monospace; font-size:13px;"
        "background:transparent; border:none;"));
    m_peerIdLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_peerIdLabel->setWordWrap(true);

    m_peersLabel = new QLabel(QStringLiteral("\u2014"), statusGroup);
    m_peersLabel->setStyleSheet(QStringLiteral(
        "color:#E6EDF3; font-size:13px; font-weight:600; background:transparent; border:none;"));

    m_progressBar = new QProgressBar(statusGroup);
    m_progressBar->setRange(0, 0);
    m_progressBar->setVisible(false);
    m_progressBar->setFixedHeight(4);
    m_progressBar->setTextVisible(false);
    m_progressBar->setStyleSheet(QStringLiteral(
        "QProgressBar { background:#21262D; border:none; border-radius:2px; }"
        "QProgressBar::chunk { background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "  stop:0 #3882F6,stop:1 #60A5FA); border-radius:2px; }"));

    auto* statusSep = new QFrame(statusGroup);
    statusSep->setFrameShape(QFrame::HLine);
    statusSep->setMinimumHeight(1);
    statusSep->setFixedHeight(1);
    statusSep->setStyleSheet(QStringLiteral(
        "QFrame { background-color:#30363D; border:none; max-height:1px; }"));

    int sg = 0;
    statusGrid->addWidget(stateW, sg, 0, 1, 2); ++sg;
    statusGrid->addWidget(statusSep, sg, 0, 1, 2); ++sg;
    statusGrid->addWidget(mkMetaLbl(QStringLiteral("ROOM"), statusGroup), sg, 0, Qt::AlignTop);
    statusGrid->addWidget(m_roomLabel, sg, 1); ++sg;
    statusGrid->addWidget(mkMetaLbl(QStringLiteral("ROOM ID"), statusGroup), sg, 0, Qt::AlignTop);
    statusGrid->addWidget(ridW, sg, 1); ++sg;
    statusGrid->addWidget(mkMetaLbl(QStringLiteral("MY PEER"), statusGroup), sg, 0, Qt::AlignTop);
    statusGrid->addWidget(m_peerIdLabel, sg, 1); ++sg;
    statusGrid->addWidget(mkMetaLbl(QStringLiteral("ONLINE"), statusGroup), sg, 0, Qt::AlignTop);
    statusGrid->addWidget(m_peersLabel, sg, 1); ++sg;
    statusGrid->addWidget(m_progressBar, sg, 0, 1, 2);

    // ── SEND FILE card ────────────────────────────────────────────────────
    auto* sendGroup = new QGroupBox(QStringLiteral("Send File"), leftWidget);
    auto* sendVBox = new QVBoxLayout(sendGroup);
    sendVBox->setContentsMargins(10, 10, 10, 10);
    sendVBox->setSpacing(6);

    sendVBox->addWidget(mkFieldLbl(QStringLiteral("Target Peer"), sendGroup));
    m_peerCombo = new QComboBox(sendGroup);
    m_peerCombo->setEditable(true);
    m_peerCombo->setPlaceholderText(QStringLiteral("Enter peer ID or name"));
    m_peerCombo->setMinimumHeight(36);
    m_peerCombo->setEnabled(false);
    sendVBox->addWidget(m_peerCombo);

    // Drop zone
    auto* dropZone = new QWidget(sendGroup);
    dropZone->setObjectName(QStringLiteral("dropZone"));
    dropZone->setStyleSheet(QStringLiteral(
        "QWidget#dropZone {"
        "  background-color:#0D1117;"
        "  border:1.5px dashed #30363D;"
        "  border-radius:10px; }"));
    dropZone->setMinimumHeight(80);
    dropZone->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* dropLay = new QVBoxLayout(dropZone);
    dropLay->setContentsMargins(12, 10, 12, 10);
    dropLay->setSpacing(4);

    // Upload icon row
    auto* uploadIconLbl = new QLabel(QStringLiteral("\u2B06"), dropZone);
    uploadIconLbl->setAlignment(Qt::AlignCenter);
    uploadIconLbl->setStyleSheet(QStringLiteral(
        "font-size:20px; color:#30363D; background:transparent; border:none;"));

    // File name label
    m_selectedFileLabel = new QLabel(
        QStringLiteral("Drag & drop a file here"), dropZone);
    m_selectedFileLabel->setObjectName(QStringLiteral("fileLabelEmpty"));
    m_selectedFileLabel->setAlignment(Qt::AlignCenter);
    m_selectedFileLabel->setWordWrap(true);
    m_selectedFileLabel->setStyleSheet(QStringLiteral(
        "background:transparent; border:none; color:#484F58; font-size:12px; font-style:italic;"));
    m_selectedFileLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    // "or click" + browse row
    auto* browseRow = new QHBoxLayout();
    browseRow->setSpacing(6);
    browseRow->setContentsMargins(0, 0, 0, 0);
    auto* orLbl = new QLabel(QStringLiteral("or click"), dropZone);
    orLbl->setStyleSheet(QStringLiteral(
        "color:#484F58; font-size:12px; background:transparent; border:none;"));
    m_browseBtn = new QPushButton(QStringLiteral("Browse"), dropZone);
    m_browseBtn->setObjectName(QStringLiteral("browseBtn"));
    m_browseBtn->setMinimumHeight(30);
    m_browseBtn->setEnabled(false);
    m_browseBtn->setCursor(Qt::PointingHandCursor);
    connect(m_browseBtn, &QPushButton::clicked, this, [this, uploadIconLbl]() {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Select File to Send"),
            QDir::homePath(), QStringLiteral("All Files (*)"));
        if (path.isEmpty()) return;
        m_pendingSendPath = path;
        const QFileInfo fi(path);
        m_selectedFileLabel->setText(fi.fileName());
        m_selectedFileLabel->setObjectName(QStringLiteral("fileLabel"));
        m_selectedFileLabel->setStyleSheet(QStringLiteral(
            "background:transparent; border:none; color:#22C55E;"
            "font-size:12px; font-weight:600;"));
        uploadIconLbl->setStyleSheet(QStringLiteral(
            "font-size:20px; color:#22C55E; background:transparent; border:none;"));
        m_sendBtn->setEnabled(true);
        qInfo() << "[UI] File selected for sending:" << path;
        });
    browseRow->addStretch();
    browseRow->addWidget(orLbl);
    browseRow->addWidget(m_browseBtn);
    browseRow->addStretch();

    dropLay->addWidget(uploadIconLbl);
    dropLay->addWidget(m_selectedFileLabel);
    dropLay->addLayout(browseRow);
    sendVBox->addWidget(dropZone);

    m_sendBtn = new QPushButton(QStringLiteral("\u2191  Send File"), sendGroup);
    m_sendBtn->setObjectName(QStringLiteral("sendBtn"));
    m_sendBtn->setEnabled(false);
    m_sendBtn->setMinimumHeight(40);
    m_sendBtn->setCursor(Qt::PointingHandCursor);
    connect(m_sendBtn, &QPushButton::clicked, this, &FileTransfer::onSendFileClicked);
    sendVBox->addWidget(m_sendBtn);

    leftVBox->addWidget(connGroup);
    leftVBox->addWidget(statusGroup);
    leftVBox->addWidget(sendGroup, 1);

    // ── RIGHT panel ───────────────────────────────────────────────────────
    auto* rightSplit = new QSplitter(Qt::Vertical, mainSplit);
    rightSplit->setChildrenCollapsible(false);
    rightSplit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // ── FILE TRANSFERS card ───────────────────────────────────────────────
    auto* ftGroup = new QGroupBox(QStringLiteral("File Transfers"), rightSplit);
    ftGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* ftVBox = new QVBoxLayout(ftGroup);
    ftVBox->setContentsMargins(10, 10, 10, 10);
    ftVBox->setSpacing(8);

    m_transferTable = new QTableWidget(0, TC_COUNT, ftGroup);
    m_transferTable->setHorizontalHeaderLabels({
        QStringLiteral("Dir"),
        QStringLiteral("ID"),
        QStringLiteral("File Name"),
        QStringLiteral("Size"),
        QStringLiteral("Peer"),
        QStringLiteral("Status"),
        QStringLiteral("Progress")
        });
    m_transferTable->horizontalHeader()->setSectionResizeMode(TC_NAME, QHeaderView::Stretch);
    m_transferTable->horizontalHeader()->setSectionResizeMode(TC_DIR, QHeaderView::ResizeToContents);
    m_transferTable->horizontalHeader()->setSectionResizeMode(TC_ID, QHeaderView::ResizeToContents);
    m_transferTable->horizontalHeader()->setSectionResizeMode(TC_SIZE, QHeaderView::ResizeToContents);
    m_transferTable->horizontalHeader()->setSectionResizeMode(TC_PEER, QHeaderView::ResizeToContents);
    m_transferTable->horizontalHeader()->setSectionResizeMode(TC_STATUS, QHeaderView::ResizeToContents);
    m_transferTable->horizontalHeader()->setSectionResizeMode(TC_PROGRESS, QHeaderView::Fixed);
    m_transferTable->setColumnWidth(TC_PROGRESS, 160);
    m_transferTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_transferTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_transferTable->verticalHeader()->setVisible(false);
    m_transferTable->setShowGrid(false);
    m_transferTable->setAlternatingRowColors(true);
    m_transferTable->setStyleSheet(m_transferTable->styleSheet() +
        QStringLiteral("QTableWidget { alternate-background-color:#0D1117; }"));
    m_transferTable->verticalHeader()->setDefaultSectionSize(40);
    m_transferTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Empty state
    auto* emptyLbl = new QLabel(ftGroup);
    emptyLbl->setAlignment(Qt::AlignCenter);
    emptyLbl->setText(QStringLiteral(
        "<div style='text-align:center;'>"
        "<div style='font-size:28px;color:#21262D;'>\u21C4</div>"
        "<div style='color:#484F58;font-size:13px;margin-top:6px;'>No active transfers</div>"
        "</div>"));
    emptyLbl->setTextFormat(Qt::RichText);
    emptyLbl->setStyleSheet(QStringLiteral("background:transparent; border:none;"));
    emptyLbl->setVisible(true);
    connect(m_transferTable->model(), &QAbstractItemModel::rowsInserted,
        emptyLbl, [emptyLbl, this]() { emptyLbl->setVisible(m_transferTable->rowCount() == 0); });
    connect(m_transferTable->model(), &QAbstractItemModel::rowsRemoved,
        emptyLbl, [emptyLbl, this]() { emptyLbl->setVisible(m_transferTable->rowCount() == 0); });

    auto* ftStack = new QWidget(ftGroup);
    auto* ftStackLay = new QVBoxLayout(ftStack);
    ftStackLay->setContentsMargins(0, 0, 0, 0);
    ftStackLay->setSpacing(0);
    ftStackLay->addWidget(m_transferTable, 1);
    ftStackLay->addWidget(emptyLbl);
    ftVBox->addWidget(ftStack, 1);

    // Transfer action buttons
    auto* ftBtnRow = new QHBoxLayout();
    ftBtnRow->setSpacing(8);
    m_acceptBtn = new QPushButton(QStringLiteral("\u2714  Accept"), ftGroup);
    m_rejectBtn = new QPushButton(QStringLiteral("\u2716  Reject"), ftGroup);
    m_cancelBtn = new QPushButton(QStringLiteral("\u25A0  Cancel"), ftGroup);
    m_acceptBtn->setObjectName(QStringLiteral("acceptBtn"));
    m_rejectBtn->setObjectName(QStringLiteral("rejectBtn"));
    m_cancelBtn->setObjectName(QStringLiteral("cancelBtn"));
    for (auto* b : { m_acceptBtn, m_rejectBtn, m_cancelBtn }) {
        b->setMinimumHeight(34);
        b->setEnabled(false);
        b->setCursor(Qt::PointingHandCursor);
    }
    connect(m_acceptBtn, &QPushButton::clicked, this, &FileTransfer::onAcceptClicked);
    connect(m_rejectBtn, &QPushButton::clicked, this, &FileTransfer::onRejectClicked);
    connect(m_cancelBtn, &QPushButton::clicked, this, &FileTransfer::onCancelClicked);
    ftBtnRow->addStretch();
    ftBtnRow->addWidget(m_acceptBtn);
    ftBtnRow->addWidget(m_rejectBtn);
    ftBtnRow->addWidget(m_cancelBtn);
    ftVBox->addLayout(ftBtnRow);

    connect(m_transferTable, &QTableWidget::itemSelectionChanged, this, [this]() {
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
            ? m_transferTable->item(row, TC_DIR)->data(Qt::UserRole).toString()
            : QStringLiteral("in");
        m_acceptBtn->setEnabled(status == QStringLiteral("pending") && dir == QStringLiteral("in"));
        m_rejectBtn->setEnabled(status == QStringLiteral("pending") && dir == QStringLiteral("in"));
        m_cancelBtn->setEnabled(
            status == QStringLiteral("active") ||
            status == QStringLiteral("negotiating") ||
            status == QStringLiteral("webrtc-connecting") ||
            status == QStringLiteral("sending"));
        });

    // ── ACTIVITY LOG card ─────────────────────────────────────────────────
    auto* logGroup = new QGroupBox(QStringLiteral("Activity Log"), rightSplit);
    logGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* logVBox = new QVBoxLayout(logGroup);
    logVBox->setContentsMargins(10, 10, 10, 10);
    logVBox->setSpacing(6);

    m_logView = new QTextEdit(logGroup);
    m_logView->setReadOnly(true);
    m_logView->setLineWrapMode(QTextEdit::NoWrap);
    m_logView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    logVBox->addWidget(m_logView, 1);

    auto* logBtnRow = new QHBoxLayout();
    logBtnRow->setSpacing(8);
    m_clearBtn = new QPushButton(QStringLiteral("Clear"), logGroup);
    m_exportBtn = new QPushButton(QStringLiteral("Export"), logGroup);
    m_clearBtn->setObjectName(QStringLiteral("clearBtn"));
    m_exportBtn->setObjectName(QStringLiteral("exportBtn"));
    for (auto* b : { m_clearBtn, m_exportBtn }) {
        b->setMinimumWidth(80);
        b->setMinimumHeight(32);
        b->setCursor(Qt::PointingHandCursor);
    }
    connect(m_clearBtn, &QPushButton::clicked, this, &FileTransfer::onClearLogsClicked);
    connect(m_exportBtn, &QPushButton::clicked, this, &FileTransfer::onExportLogsClicked);
    logBtnRow->addStretch();
    logBtnRow->addWidget(m_clearBtn);
    logBtnRow->addWidget(m_exportBtn);
    logVBox->addLayout(logBtnRow);

    // ── Assemble ──────────────────────────────────────────────────────────
    rightSplit->addWidget(ftGroup);
    rightSplit->addWidget(logGroup);
    rightSplit->setStretchFactor(0, 3);
    rightSplit->setStretchFactor(1, 2);

    mainSplit->addWidget(leftScroll);
    mainSplit->addWidget(rightSplit);
    mainSplit->setStretchFactor(0, 0);
    mainSplit->setStretchFactor(1, 1);

    contentLay->addWidget(mainSplit);
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

    QString dotColor, labelColor, labelText, connProp;
    if (connecting) {
        dotColor = labelColor = QStringLiteral("#F59E0B");
        labelText = QStringLiteral("Connecting\u2026");
        connProp = QStringLiteral("pending");
    }
    else if (connected) {
        dotColor = labelColor = QStringLiteral("#22C55E");
        labelText = QStringLiteral("Connected");
        connProp = QStringLiteral("true");
    }
    else {
        dotColor = labelColor = QStringLiteral("#EF4444");
        labelText = QStringLiteral("Disconnected");
        connProp = QStringLiteral("false");
        m_roomLabel->setText(QStringLiteral("\u2014"));
        m_roomIdCopyLabel->setText(QStringLiteral("\u2014"));
        m_peerIdLabel->setText(QStringLiteral("\u2014"));
        m_peersLabel->setText(QStringLiteral("\u2014"));
        m_peerCombo->clear();
        m_peerCombo->setEnabled(false);
        m_browseBtn->setEnabled(false);
        m_sendBtn->setEnabled(false);
        m_pendingSendPath.clear();
        m_selectedFileLabel->setText(QStringLiteral("Drag & drop a file here"));
        m_selectedFileLabel->setObjectName(QStringLiteral("fileLabelEmpty"));
        m_selectedFileLabel->setStyleSheet(QStringLiteral(
            "background:transparent; border:none; color:#484F58;"
            "font-size:12px; font-style:italic;"));
    }

    m_statusLabel->setText(labelText);
    m_statusLabel->setProperty("connected", connProp);
    m_statusLabel->setStyleSheet(
        QStringLiteral("font-size:13px; font-weight:700; color:%1;"
            "background:transparent; border:none;").arg(labelColor));

    if (auto* dot = qobject_cast<QLabel*>(
        m_statusLabel->property("dotPtr").value<QObject*>()))
        dot->setStyleSheet(
            QStringLiteral("color:%1; font-size:12px; background:transparent; border:none;")
            .arg(dotColor));

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
    qInfo() << "[UI] Peer combo populated with" << m_peerCombo->count() << "peer(s)";
}

void FileTransfer::addPeerToCombo(const QString& peerId)
{
    for (int i = 0; i < m_peerCombo->count(); ++i)
        if (m_peerCombo->itemData(i).toString() == peerId) return;
    if (m_client && peerId == m_client->myPeerId()) return;
    m_peerCombo->addItem(peerId, peerId);
    updatePeersLabel();
    qInfo() << "[UI] Peer joined \u2013 added to combo:" << peerId
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
    qInfo() << "[UI] Peer left \u2013 removed from combo:" << peerId
        << "| total peers:" << m_peerCombo->count();
}

void FileTransfer::updatePeersLabel()
{
    const int count = m_peerCombo->count();
    m_peersLabel->setText(
        count == 0
        ? QStringLiteral("No peers online")
        : QString::number(count)
        + (count == 1 ? QStringLiteral(" peer online")
            : QStringLiteral(" peers online")));
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

void FileTransfer::upsertTransferRow(const FileTransferManager::TransferInfo& info)
{
    int row = findTransferRow(info.transferId);
    if (row < 0) {
        row = m_transferTable->rowCount();
        m_transferTable->insertRow(row);
    }

    const QString sizeStr =
        info.size >= 1024 * 1024
        ? QStringLiteral("%1 MB").arg(QString::number(info.size / 1024.0 / 1024.0, 'f', 2))
        : QStringLiteral("%1 KB").arg(QString::number(info.size / 1024.0, 'f', 1));

    const bool isOut =
        info.direction == FileTransferManager::TransferDirection::Outgoing;

    // Direction
    auto* dirItem = new QTableWidgetItem(isOut ? QStringLiteral("\u2191") : QStringLiteral("\u2193"));
    dirItem->setTextAlignment(Qt::AlignCenter);
    dirItem->setForeground(isOut ? QColor(QStringLiteral("#60A5FA"))
        : QColor(QStringLiteral("#22C55E")));
    dirItem->setData(Qt::UserRole, isOut ? QStringLiteral("out") : QStringLiteral("in"));
    dirItem->setToolTip(isOut ? QStringLiteral("Outgoing") : QStringLiteral("Incoming"));
    m_transferTable->setItem(row, TC_DIR, dirItem);

    // ID
    auto* idItem = new QTableWidgetItem(info.transferId.left(8) + QStringLiteral("\u2026"));
    idItem->setData(Qt::UserRole, info.transferId);
    idItem->setForeground(QColor(QStringLiteral("#484F58")));
    idItem->setFont([] { return QFont(QStringLiteral("JetBrains Mono"), 10); }());
    m_transferTable->setItem(row, TC_ID, idItem);
    m_transferTable->setItem(row, TC_NAME, new QTableWidgetItem(info.name));
    m_transferTable->setItem(row, TC_SIZE, new QTableWidgetItem(sizeStr));
    m_transferTable->setItem(row, TC_PEER,
        new QTableWidgetItem(info.peerId.left(12) + QStringLiteral("\u2026")));

    // Status
    auto* statusItem = new QTableWidgetItem(info.status);
    const QColor sc =
        info.status == QStringLiteral("completed") ? QColor(QStringLiteral("#22C55E")) :
        info.status == QStringLiteral("error") ? QColor(QStringLiteral("#EF4444")) :
        info.status == QStringLiteral("rejected") ? QColor(QStringLiteral("#EF4444")) :
        info.status == QStringLiteral("cancelled") ? QColor(QStringLiteral("#F59E0B")) :
        info.status == QStringLiteral("pending") ? QColor(QStringLiteral("#F59E0B")) :
        QColor(QStringLiteral("#3882F6"));
    statusItem->setForeground(sc);
    m_transferTable->setItem(row, TC_STATUS, statusItem);

    // Progress bar
    auto* bar = qobject_cast<QProgressBar*>(m_transferTable->cellWidget(row, TC_PROGRESS));
    if (!bar) {
        bar = new QProgressBar(m_transferTable);
        bar->setRange(0, 100);
        bar->setTextVisible(true);
        bar->setStyleSheet(QStringLiteral(
            "QProgressBar { background:#21262D; border:none; border-radius:6px;"
            "  height:18px; color:#E6EDF3; font-size:10px; font-weight:600; }"
            "QProgressBar::chunk { background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "  stop:0 #3882F6,stop:1 #60A5FA); border-radius:6px; }"));
        m_transferTable->setCellWidget(row, TC_PROGRESS, bar);
    }
    bar->setValue(static_cast<int>(info.progress));
}

void FileTransfer::updateTransferRow(const QString& transferId,
    const QString& status, double progress)
{
    const int row = findTransferRow(transferId);
    if (row < 0) return;
    if (!status.isEmpty()) {
        auto* si = new QTableWidgetItem(status);
        const QColor sc =
            status == QStringLiteral("completed") ? QColor(QStringLiteral("#22C55E")) :
            status == QStringLiteral("error") ? QColor(QStringLiteral("#EF4444")) :
            status == QStringLiteral("rejected") ? QColor(QStringLiteral("#EF4444")) :
            status == QStringLiteral("cancelled") ? QColor(QStringLiteral("#F59E0B")) :
            status == QStringLiteral("pending") ? QColor(QStringLiteral("#F59E0B")) :
            QColor(QStringLiteral("#3882F6"));
        si->setForeground(sc);
        m_transferTable->setItem(row, TC_STATUS, si);
    }
    if (progress >= 0.0)
        if (auto* b = qobject_cast<QProgressBar*>(
            m_transferTable->cellWidget(row, TC_PROGRESS)))
            b->setValue(static_cast<int>(progress));
}

// ---------------------------------------------------------------------------
// Connect button
// ---------------------------------------------------------------------------
void FileTransfer::onConnectClicked()
{
    const QString server = m_serverEdit->text().trimmed();
    const QString password = m_passwordEdit->text();
    const bool    isCreate = m_createRadio->isChecked();
    const QString roomId = isCreate ? QString{} : m_roomIdEdit->text().trimmed();

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
    connect(m_client, &SignalingClient::roomCreated, this, &FileTransfer::onRoomCreated);
    connect(m_client, &SignalingClient::stage1Complete, this, &FileTransfer::onStage1Complete);
    connect(m_client, &SignalingClient::loginFailed, this, &FileTransfer::onLoginFailed);
    connect(m_client, &SignalingClient::connectionFailed, this, &FileTransfer::onConnectionFailed);
    connect(m_client, &SignalingClient::peerJoined, this, &FileTransfer::onPeerJoined, Qt::QueuedConnection);
    connect(m_client, &SignalingClient::peerLeft, this, &FileTransfer::onPeerLeft, Qt::QueuedConnection);

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
    m_selectedFileLabel->setText(QStringLiteral("Drag & drop a file here"));
    m_selectedFileLabel->setObjectName(QStringLiteral("fileLabelEmpty"));
    m_selectedFileLabel->setStyleSheet(QStringLiteral(
        "background:transparent; border:none; color:#484F58;"
        "font-size:12px; font-style:italic;"));
    m_sendBtn->setEnabled(false);
}

// ---------------------------------------------------------------------------
// Wire FileTransferManager signals
// ---------------------------------------------------------------------------
void FileTransfer::connectFtmSignals()
{
    if (!m_ftm) return;
    connect(m_ftm, &FileTransferManager::incomingFileOffer, this, &FileTransfer::onIncomingFileOffer, Qt::QueuedConnection);
    connect(m_ftm, &FileTransferManager::transferAccepted, this, &FileTransfer::onTransferAccepted, Qt::QueuedConnection);
    connect(m_ftm, &FileTransferManager::transferRejected, this, &FileTransfer::onTransferRejected, Qt::QueuedConnection);
    connect(m_ftm, &FileTransferManager::transferCancelled, this, &FileTransfer::onTransferCancelled, Qt::QueuedConnection);
    connect(m_ftm, &FileTransferManager::transferProgress, this, &FileTransfer::onTransferProgress, Qt::QueuedConnection);
    connect(m_ftm, &FileTransferManager::transferComplete, this, &FileTransfer::onTransferComplete, Qt::QueuedConnection);
    connect(m_ftm, &FileTransferManager::transferError, this, &FileTransfer::onTransferError, Qt::QueuedConnection);
    connect(m_ftm, &FileTransferManager::webRtcOfferReceived, this, &FileTransfer::onWebRtcOfferReceived, Qt::QueuedConnection);
    connect(m_ftm, &FileTransferManager::outgoingFileOfferSent, this, &FileTransfer::onOutgoingFileOfferSent, Qt::QueuedConnection);
    connect(m_ftm, &FileTransferManager::outgoingFileOfferAccepted, this, &FileTransfer::onOutgoingFileOfferAccepted, Qt::QueuedConnection);
    connect(m_ftm, &FileTransferManager::outgoingFileOfferRejected, this, &FileTransfer::onOutgoingFileOfferRejected, Qt::QueuedConnection);
    connect(m_ftm, &FileTransferManager::sendProgress, this, &FileTransfer::onSendProgress, Qt::QueuedConnection);
    connect(m_ftm, &FileTransferManager::sendComplete, this, &FileTransfer::onSendComplete, Qt::QueuedConnection);
    connect(m_ftm, &FileTransferManager::sendError, this, &FileTransfer::onSendError, Qt::QueuedConnection);
}

// ---------------------------------------------------------------------------
// SignalingClient handlers
// ---------------------------------------------------------------------------
void FileTransfer::onRoomCreated(const QString& roomId, const QString& /*password*/)
{
    m_genRoomLabel->setText(roomId);
    m_roomIdCopyLabel->setText(roomId);
    qInfo() << "[Stage1] \U0001F3E0 Room created \u2013 ID:" << roomId;
    QMessageBox::information(this, QStringLiteral("Room Created"),
        QStringLiteral("Your room has been created.\n\nRoom ID:  %1\n\n"
            "Share this ID with the peers you want to invite.\nConnecting now\u2026")
        .arg(roomId));
}

void FileTransfer::onStage1Complete(const QString& roomId,
    const QString& peerId, const QJsonArray& peers)
{
    m_ftm = m_client->ftm();
    connectFtmSignals();
    m_peerCombo->setEnabled(true);
    m_browseBtn->setEnabled(true);
    populatePeerCombo(peers);
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
// Live peer presence
// ---------------------------------------------------------------------------
void FileTransfer::onPeerJoined(const QString& peerId, const QString& appType)
{
    addPeerToCombo(peerId);
    qInfo() << "[UI] \U0001F7E2 Peer joined:" << peerId << "appType:" << appType;
}

void FileTransfer::onPeerLeft(const QString& peerId)
{
    removePeerFromCombo(peerId);
    qInfo() << "[UI] \U0001F534 Peer left:" << peerId;
}

// ---------------------------------------------------------------------------
// FTM handlers – receive side
// ---------------------------------------------------------------------------
void FileTransfer::onIncomingFileOffer(const FileTransferManager::TransferInfo& info)
{
    qInfo() << "[FT] \U0001F4E5 File offer from peer:"
        << info.name
        << QString::number(info.size / 1024.0, 'f', 1) + QStringLiteral(" KB")
        << "| chunks:" << info.totalChunks << "| from:" << info.peerId;
    upsertTransferRow(info);
    raise(); activateWindow();

    const int btn = QMessageBox::question(this,
        QStringLiteral("Incoming File Transfer"),
        QStringLiteral("A peer wants to send you a file:\n\n"
            "  Name:  %1\n  Size:  %2 KB\n  From:  %3\n\nAccept the transfer?")
        .arg(info.name, QString::number(info.size / 1024.0, 'f', 1), info.peerId),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

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
    qInfo() << "[FT] Accepted \u2013 save to:" << savePath;
    if (m_ftm) m_ftm->acceptTransfer(info.transferId, savePath);
}

void FileTransfer::onTransferAccepted(const QString& transferId)
{
    updateTransferRow(transferId, QStringLiteral("waiting-webrtc"));
    qInfo() << "[FT] \u2705 Transfer accepted \u2013 waiting for WebRTC:" << transferId;
}

void FileTransfer::onTransferRejected(const QString& transferId)
{
    updateTransferRow(transferId, QStringLiteral("rejected"));
    qInfo() << "[FT] Transfer rejected \u2013" << transferId;
}

void FileTransfer::onTransferCancelled(const QString& transferId)
{
    updateTransferRow(transferId, QStringLiteral("cancelled"));
    qWarning() << "[FT] Transfer cancelled \u2013" << transferId;
}

void FileTransfer::onTransferProgress(const QString& transferId,
    double progress, int chunksReceived, int totalChunks)
{
    updateTransferRow(transferId, QStringLiteral("active"), progress);
    qDebug() << "[FT] Progress:" << transferId
        << QString::number(progress, 'f', 1) + QStringLiteral("%")
        << chunksReceived << "/" << totalChunks;
}

void FileTransfer::onTransferComplete(const QString& transferId, const QString& filePath)
{
    updateTransferRow(transferId, QStringLiteral("completed"), 100.0);
    qInfo() << "[FT] \u2705 Receive complete \u2013" << transferId << "saved to:" << filePath;
    QMessageBox::information(this, QStringLiteral("Transfer Complete"),
        QStringLiteral("File received successfully.\n\nSaved to:\n%1").arg(filePath));
}

void FileTransfer::onTransferError(const QString& transferId, const QString& reason)
{
    updateTransferRow(transferId, QStringLiteral("error"));
    qCritical() << "[FT] \u274C Transfer error \u2013" << transferId << reason;
}

void FileTransfer::onWebRtcOfferReceived(const QString& transferId,
    const QString& fromPeerId, const QJsonObject& offer)
{
    qInfo() << "[WebRTC] Offer received \u2013 transfer:" << transferId << "from:" << fromPeerId;
    qDebug() << "[WebRTC] SDP type:" << offer.value(QStringLiteral("type")).toString();
    updateTransferRow(transferId, QStringLiteral("negotiating"));
}

// ---------------------------------------------------------------------------
// FTM handlers – send side
// ---------------------------------------------------------------------------
void FileTransfer::onOutgoingFileOfferSent(const QString& transferId)
{
    if (m_ftm) {
        const auto map = m_ftm->transfers();
        if (map.contains(transferId))
            upsertTransferRow(map[transferId]);
    }
    updateTransferRow(transferId, QStringLiteral("offering"));
    qInfo() << "[FT] \U0001F4E4 Outgoing offer sent \u2013 waiting for peer:" << transferId;
}

void FileTransfer::onOutgoingFileOfferAccepted(const QString& transferId)
{
    updateTransferRow(transferId, QStringLiteral("webrtc-connecting"));
    qInfo() << "[FT] \u2705 Peer accepted our offer \u2013 WebRTC starting:" << transferId;
}

void FileTransfer::onOutgoingFileOfferRejected(const QString& transferId)
{
    updateTransferRow(transferId, QStringLiteral("rejected"));
    qWarning() << "[FT] Peer rejected our file offer \u2013" << transferId;
    QMessageBox::information(this, QStringLiteral("Offer Rejected"),
        QStringLiteral("The peer declined your file transfer request.\n\nID: %1\u2026")
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
    qInfo() << "[FT] \u2705 Send complete \u2013" << transferId;
    QMessageBox::information(this, QStringLiteral("File Sent"),
        QStringLiteral("File sent successfully.\n\nTransfer ID:\n%1").arg(transferId));
}

void FileTransfer::onSendError(const QString& transferId, const QString& reason)
{
    updateTransferRow(transferId, QStringLiteral("error"));
    qCritical() << "[FT] \u274C Send error \u2013" << transferId << reason;
    QMessageBox::critical(this, QStringLiteral("Send Error"),
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
        QStringLiteral("FileTransfer-log-%1.txt")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))),
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
    // Colors from design spec
    const char* color = kColorDebug;
    const char* badge = "DBG";
    const char* bgHex = "#1C1C1C";
    switch (level) {
    case LogHandler::Level::Debug:
        color = kColorDebug;   badge = "DEBUG"; bgHex = "#1A1A1A"; break;
    case LogHandler::Level::Info:
        color = kColorInfo;    badge = "INFO";  bgHex = "#0B1F27"; break;
    case LogHandler::Level::Warning:
        color = kColorWarning; badge = "WARN";  bgHex = "#271B05"; break;
    case LogHandler::Level::Critical:
        color = kColorCritical; badge = "ERROR"; bgHex = "#270808"; break;
    }

    // Format: [timestamp] BADGE message
    const QString html = QStringLiteral(
        "<span style='color:#484F58;font-size:11px;font-family:JetBrains Mono,monospace;'>"
        "[%1]</span> "
        "<span style='color:%2;font-weight:700;font-size:11px;"
        "background-color:%3;border-radius:3px;padding:1px 5px;"
        "font-family:JetBrains Mono,monospace;'>%4</span>"
        "<span style='color:%2;font-size:12px;font-family:JetBrains Mono,monospace;'>"
        " %5</span>")
        .arg(timestamp.toHtmlEscaped(),
            QLatin1String(color),
            QLatin1String(bgHex),
            QLatin1String(badge),
            message.toHtmlEscaped());

    m_logView->append(html);
    m_logView->verticalScrollBar()->setValue(
        m_logView->verticalScrollBar()->maximum());
}