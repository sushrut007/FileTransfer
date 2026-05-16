#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include "WebRTCEngine.h"
#include <QDebug>
#include <QJsonDocument>
#include <QMetaObject>

#ifdef HAVE_LIBDATACHANNEL
#  include <rtc/rtc.hpp>
#endif

WebRTCEngine::WebRTCEngine(QObject* parent) : QObject(parent)
{
#ifdef HAVE_LIBDATACHANNEL
    rtc::InitLogger(rtc::LogLevel::Warning);
#endif
}

WebRTCEngine::~WebRTCEngine()
{
#ifdef HAVE_LIBDATACHANNEL
    QMutexLocker lock(&m_mutex);
    for (auto& s : m_sessions) {
        if (s->dc) s->dc->close();
        if (s->pc) s->pc->close();
    }
    m_sessions.clear();
#endif
}

void WebRTCEngine::processOffer(const QString& transferId,
    const QString& fromPeerId,
    const QJsonObject& offer,
    const QString& savePath)
{
#ifdef HAVE_LIBDATACHANNEL
    const QString sdp = offer.value(QStringLiteral("sdp")).toString();
    if (sdp.isEmpty()) {
        emit transferError(transferId, QStringLiteral("Empty SDP"));
        return;
    }

    auto session = std::make_shared<Session>();
    session->transferId = transferId;
    session->targetPeerId = fromPeerId;
    session->savePath = savePath;

    {
        QMutexLocker lock(&m_mutex);
        if (m_sessions.contains(transferId)) {
            auto& old = m_sessions[transferId];
            if (old->dc) old->dc->close();
            if (old->pc) old->pc->close();
        }
        m_sessions.insert(transferId, session);
    }

    auto pc = std::make_shared<rtc::PeerConnection>(buildConfig());
    session->pc = pc;

    pc->onLocalCandidate([this, transferId, fromPeerId](rtc::Candidate c) {
        QJsonObject cand;
        cand[QStringLiteral("candidate")] = QString::fromStdString(c.candidate());
        cand[QStringLiteral("sdpMid")] = QString::fromStdString(c.mid());
        cand[QStringLiteral("sdpMLineIndex")] = 0;
        QMetaObject::invokeMethod(this, [this, transferId, fromPeerId, cand]() {
            emit localIceCandidate(transferId, fromPeerId, cand);
            }, Qt::QueuedConnection);
        });

    pc->onLocalDescription([this, transferId, fromPeerId](rtc::Description desc) {
        if (desc.type() != rtc::Description::Type::Answer) return;
        QJsonObject answer;
        answer[QStringLiteral("type")] = QStringLiteral("answer");
        answer[QStringLiteral("sdp")] = QString::fromStdString(std::string(desc));
        QMetaObject::invokeMethod(this, [this, transferId, fromPeerId, answer]() {
            emit answerReady(transferId, fromPeerId, answer);
            }, Qt::QueuedConnection);
        });

    pc->onStateChange([this, transferId](rtc::PeerConnection::State state) {
        if (state == rtc::PeerConnection::State::Failed)
            QMetaObject::invokeMethod(this, [this, transferId]() {
            emit transferError(transferId,
                QStringLiteral("PeerConnection failed"));
                }, Qt::QueuedConnection);
        });

    pc->onDataChannel([this, transferId, savePath, session]
    (std::shared_ptr<rtc::DataChannel> dc) {
            session->dc = dc;
            QMetaObject::invokeMethod(this, [this, transferId]() {
                emit dataChannelOpen(transferId);
                }, Qt::QueuedConnection);

            session->outFile.setFileName(savePath);
            if (!session->outFile.open(QIODevice::WriteOnly)) {
                const QString err = QStringLiteral("Cannot open: ") + savePath;
                QMetaObject::invokeMethod(this, [this, transferId, err]() {
                    emit transferError(transferId, err);
                    }, Qt::QueuedConnection);
                return;
            }

            // Single-emission finalise helper
            auto finalise = [this, session, transferId](const QString& trigger) {
                if (session->completed) return;
                session->completed = true;
                if (session->outFile.isOpen()) {
                    session->outFile.flush();
                    session->outFile.close();
                }
                const QString path = session->savePath;
                QMetaObject::invokeMethod(this, [this, transferId, path]() {
                    emit transferComplete(transferId, path);
                    }, Qt::QueuedConnection);
                };

            dc->onMessage([this, transferId, session, finalise]
            (rtc::message_variant data) {

                    if (std::holds_alternative<rtc::string>(data)) {
                        const QString text = QString::fromStdString(
                            std::get<rtc::string>(data));
                        const QJsonObject msg =
                            QJsonDocument::fromJson(text.toUtf8()).object();
                        const QString type = msg.value(QStringLiteral("type")).toString();

                        if (type == QStringLiteral("file-start")) {
                            session->totalBytes = static_cast<qint64>(
                                msg.value(QStringLiteral("size")).toDouble());
                        }
                        else if (type == QStringLiteral("file-end")) {
                            finalise(QStringLiteral("file-end"));
                        }
                        return;
                    }

                    if (std::holds_alternative<rtc::binary>(data)) {
                        if (session->completed) return;
                        const auto& bin = std::get<rtc::binary>(data);
                        qint64 writeSize = static_cast<qint64>(bin.size());

                        if (session->totalBytes > 0) {
                            const qint64 remaining =
                                session->totalBytes - session->bytesReceived;
                            if (remaining <= 0) return;
                            writeSize = std::min(writeSize, remaining);
                        }

                        session->outFile.write(
                            reinterpret_cast<const char*>(bin.data()),
                            static_cast<qsizetype>(writeSize));
                        session->bytesReceived += writeSize;

                        const qint64 recv = session->bytesReceived;
                        const qint64 total = session->totalBytes;

                        QMetaObject::invokeMethod(this, [this, transferId, recv, total]() {
                            emit progressUpdate(transferId, recv, total);
                            }, Qt::QueuedConnection);

                        if (total > 0 && recv >= total)
                            finalise(QStringLiteral("byte-count"));
                    }
                });

            dc->onError([this, transferId](const std::string& err) {
                const QString msg = QString::fromStdString(err);
                QMetaObject::invokeMethod(this, [this, transferId, msg]() {
                    emit transferError(transferId, msg);
                    }, Qt::QueuedConnection);
                });
        });

    try {
        pc->setRemoteDescription(
            rtc::Description(sdp.toStdString(),
                rtc::Description::Type::Offer));
    }
    catch (const std::exception& e) {
        const QString msg = QString::fromLatin1(e.what());
        emit transferError(transferId,
            QStringLiteral("SDP set failed: ") + msg);
    }
#else
    Q_UNUSED(fromPeerId) Q_UNUSED(offer) Q_UNUSED(savePath)
        emit transferError(transferId,
            QStringLiteral("WebRTC not available – build with HAVE_LIBDATACHANNEL"));
#endif
}

void WebRTCEngine::addIceCandidate(const QString& transferId,
    const QJsonObject& candidate)
{
#ifdef HAVE_LIBDATACHANNEL
    QMutexLocker lock(&m_mutex);
    if (!m_sessions.contains(transferId)) return;
    auto& s = m_sessions[transferId];
    if (!s->pc) return;
    try {
        s->pc->addRemoteCandidate(rtc::Candidate(
            candidate.value(QStringLiteral("candidate")).toString().toStdString(),
            candidate.value(QStringLiteral("sdpMid")).toString().toStdString()));
    }
    catch (const std::exception& e) {
        qWarning() << "[WebRTC] addRemoteCandidate failed:" << e.what();
    }
#else
    Q_UNUSED(transferId) Q_UNUSED(candidate)
#endif
}

void WebRTCEngine::closeTransfer(const QString& transferId)
{
#ifdef HAVE_LIBDATACHANNEL
    QMutexLocker lock(&m_mutex);
    if (!m_sessions.contains(transferId)) return;
    auto& s = m_sessions[transferId];
    if (s->dc) { s->dc->close(); s->dc.reset(); }
    if (s->pc) { s->pc->close(); s->pc.reset(); }
    if (s->outFile.isOpen()) s->outFile.close();
    m_sessions.remove(transferId);
#else
    Q_UNUSED(transferId)
#endif
}

#ifdef HAVE_LIBDATACHANNEL
rtc::Configuration WebRTCEngine::buildConfig() const
{
    rtc::Configuration cfg;
    cfg.iceServers.emplace_back("stun:stun.l.google.com:19302");
    cfg.iceServers.emplace_back("stun:stun1.l.google.com:19302");
    cfg.maxMessageSize = 65536;
    return cfg;
}
#endif