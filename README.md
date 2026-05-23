
# FileMitra – Secure Peer-to-Peer File Transfer Client

A modern, lightweight desktop application for secure file transfer between peers over WebRTC. FileMitra enables users to create or join transfer rooms and exchange files with end-to-end encryption in a user-friendly interface.

---

## ✨ Features

- 🔐 **Room-Based Transfer System** – Create or join transfer rooms with password protection
- 🌐 **WebRTC-Powered P2P** – Direct peer-to-peer file transfer with automatic signaling
- 👥 **Real-Time Peer Discovery** – Live presence updates showing connected peers
- 📁 **Drag & Drop File Selection** – Intuitive file selection with multiple input methods
- 📊 **Progress Tracking** – Real-time upload/download progress with detailed transfer history
- 📝 **Activity Logging** – Comprehensive logs with color-coded severity levels (Debug, Info, Warning, Error)
- 🎨 **Modern Dark UI** – Professional dark theme with responsive design and accessibility
- ⚡ **File Transfer Management** – Accept, reject, or cancel transfers with action buttons

---

# 🚀 Quick Start

## Prerequisites

- **Qt 6.x** (Core, GUI, Network)
- **C++17** or later
- **WebRTC** library (libdatachannel or similar)
- **CMake 3.16+** (if using CMake build system)

## Installation

### 1. Clone the repository

```bash
git clone https://github.com/sushrut007/FileTransfer.git
cd FileTransfer
```

### 2. Install dependencies (Qt)

```bash
# macOS
brew install qt

# Ubuntu/Debian
sudo apt-get install qt6-base-dev qt6-tools-dev

# Windows
# Download from https://www.qt.io/download-open-source
```

### 3. Build the project

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### 4. Run the application

```bash
./FileTransfer   # Linux/macOS
FileTransfer.exe # Windows
```

---

# 📖 Usage

## Basic Workflow

### 1. Connect to Server

- Enter your signaling server URL (default: `http://localhost:3000`)
- Choose to **Create** a new room or **Join** an existing one
- Enter the room password
- Click **Connect**

### 2. Create Room Mode

- A unique Room ID will be generated after connection
- Share the Room ID with peers who want to join
- Copy button allows quick clipboard access

### 3. Join Room Mode

- Enter an existing Room ID
- Use the same password as the room creator
- Connect to gain access to the room

### 4. Send a File

- Select a target peer from the **Target Peer** dropdown
- Drag and drop a file or click **Browse** to select
- Click **Send File**
- Monitor progress in the **File Transfers** table

### 5. Receive a File

- When a peer sends a file, a notification appears
- Choose to **Accept** or **Reject** the transfer
- Specify the save location
- Monitor download progress in real-time

---

## Status Indicators

| Status | Meaning |
|--------|---------|
| 🔴 Disconnected | Not connected to server |
| 🟡 Connecting... | Connection in progress |
| 🟢 Connected | Ready to transfer files |
| 🟡 Pending | Transfer awaiting action |
| 🔵 Active | Transfer in progress |
| 🟢 Completed | Transfer finished successfully |
| 🔴 Error | Transfer failed |
| 🟡 Cancelled | Transfer was cancelled |

---

# 🏗️ Architecture

## Components

- **FileTransfer (UI)** – Main window and UI logic
- **SignalingClient** – WebSocket connection to signaling server for room/peer management
- **FileTransferManager (FTM)** – Handles peer connection negotiation and transfer orchestration
- **WebRTC Data Channel** – P2P file transmission over encrypted connection
- **LogHandler** – Centralized logging with level filtering

## Data Flow

```text
User Action
    ↓
UI Layer (FileTransfer)
    ↓
SignalingClient (Server Connection)
    ↓
FileTransferManager (WebRTC Setup)
    ↓
Data Channel (P2P Transfer)
    ↓
File System
```

---

# 🎨 UI Design

- **Dark Theme** – `#0D1117` background with accent color `#3882F6`
- **Color-Coded Status** – Green (success), Red (error), Amber (warning), Cyan (info)
- **Responsive Layout** – Adjustable splitter panels for file transfers and logs
- **Accessible Components** – High contrast ratios and keyboard navigation support

---

# 📋 File Structure

```text
FileTransfer/
├── CMakeLists.txt
├── FileTransfer.h
├── FileTransfer.cpp          # Main UI implementation
├── SignalingClient.h/cpp     # Server connection
├── FileTransferManager.h/cpp # WebRTC & transfer logic
├── LogHandler.h/cpp          # Logging system
├── assets/
│   └── FileMitraLogo.png     # Application logo
└── README.md
```

---

# ⚙️ Configuration

## Server URL

Set the signaling server endpoint. Example formats:

- WebSocket: `wss://your-server:port`
- HTTP: `http://localhost:3000`

## Room ID Format

Randomly generated alphanumeric identifiers.

Format:

```text
[A-Za-z0-9]{8,}
```

## Password Requirements

- Required for both create and join modes
- Case-sensitive
- No length restrictions enforced client-side

---

# 🔍 Logging & Debugging

- **Activity Log Tab** – View real-time application logs

## Log Levels

- 🔵 DEBUG (Grey) – Detailed technical information
- 🔷 INFO (Cyan) – General operational info
- 🟠 WARN (Amber) – Warning conditions
- 🔴 ERROR (Red) – Error conditions

## Export Logs

Save logs as timestamped `.txt` file.

---

# 🐛 Troubleshooting

| Issue | Solution |
|------|----------|
| Connection failed | Verify server URL and network connectivity |
| Room ID not generating | Check server is running and password is correct |
| Peer not visible | Ensure both peers are connected to same room |
| Transfer hangs | Check network stability; try cancelling and retrying |
| File not saved | Verify save location has write permissions |

---

# 🤝 Contributing

Contributions are welcome! Please follow these steps:

1. Fork the repository
2. Create a feature branch

```bash
git checkout -b feature/AmazingFeature
```

3. Commit changes

```bash
git commit -m 'Add AmazingFeature'
```

4. Push to branch

```bash
git push origin feature/AmazingFeature
```

5. Open a Pull Request

---

# 📄 License

This project is licensed under the **MIT License** – see LICENSE file for details.

---

# 👤 Author

**Sushrut**

- GitHub: [@sushrut007](https://github.com/sushrut007)
- Repository: [FileTransfer](https://github.com/sushrut007/FileTransfer)

---

# 🙏 Acknowledgments

- Built with [Qt](https://www.qt.io)
- P2P connectivity via WebRTC
- Modern UI inspired by GitHub's dark theme

---

# 📞 Support

For issues, questions, or suggestions:

- Open an [Issue](https://github.com/sushrut007/FileTransfer/issues)
- Check existing documentation
- Review the Activity Log for diagnostic information

---

## **Version:** v1.0

## **Last Updated:** 2026-05-23

---

# 💡 Customization Tips

Before deploying, consider updating:

1. **Author/License Info** – Replace with your actual details
2. **Server Details** – Add your actual signaling server address
3. **Screenshots** – Add:


<img width="1920" height="1080" alt="Screenshot 2026-05-21 232809" src="https://github.com/user-attachments/assets/e0be1af3-9ae2-410b-bcac-a2938aaff47d" />

4. **Additional Sections** – Add roadmap, changelog, or FAQ as needed
5. **Badges** – Add build status, version, or license badges at the top
