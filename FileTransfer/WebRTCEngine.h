#pragma once
#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QMap>
#include <QMutex>
#include <QFile>
#include <memory>

#ifdef HAVE_LIBDATACHANNEL
#  include <rtc/rtc.hpp>
#endif

class WebRTCEngine : public QObject
{
    Q_OBJECT
public:
    explicit WebRTCEngine(QObject* parent = nullptr);
    ~WebRTCEngine();

    void processOffer(const QString& transferId,
        const QString& fromPeerId,
        const QJsonObject& offer,
        const QString& savePath);
    void addIceCandidate(const QString& transferId,
        const QJsonObject& candidate);
    void closeTransfer(const QString& transferId);

signals:
    void answerReady(const QString& transferId,
        const QString& targetPeerId,
        const QJsonObject& answer);
    void localIceCandidate(const QString& transferId,
        const QString& targetPeerId,
        const QJsonObject& candidate);
    void dataChannelOpen(const QString& transferId);
    void progressUpdate(const QString& transferId,
        qint64 bytesReceived, qint64 totalBytes);
    void transferComplete(const QString& transferId, const QString& savePath);
    void transferError(const QString& transferId, const QString& reason);

private:
#ifdef HAVE_LIBDATACHANNEL
    struct Session {
        QString                              transferId;
        QString                              targetPeerId;
        QString                              savePath;
        std::shared_ptr<rtc::PeerConnection> pc;
        std::shared_ptr<rtc::DataChannel>    dc;
        QFile                                outFile;
        qint64                               bytesReceived = 0;
        qint64                               totalBytes = 0;
        bool                                 fileStartReceived = false;
        bool                                 completed = false;
    };
    QMap<QString, std::shared_ptr<Session>> m_sessions;
    QMutex                                  m_mutex;
    rtc::Configuration buildConfig() const;
#endif
};