/*!
 * @file        downloadertask.cppm
 * @brief       Download task execution and lifecycle management.
 * @details     Implements a single download task with support for segmented
 *              and single-stream HTTP downloads, resume handling, throttling,
 *              mirror failover, retry policies, integrity verification,
 *              and post-download actions.
 *
 *              This class represents the lowest-level active unit of work
 *              in the download system and encapsulates all networking,
 *              buffering, throttling, and state transitions required to
 *              reliably fetch a remote resource.
 *
 *              DownloaderTask is designed to be:
 *              - UI-observable (via Qt properties and signals)
 *              - Policy-driven (speed limits, retries, mirrors)
 *              - Resilient across pauses, resumes, and restarts
 *
 * @author      <a href='https://github.com/thecompez'>Kambiz Asadzadeh</a>
 * @since       09 Feb 2026
 * @copyright   Copyright (c) 2026 Genyleap. All rights reserved.
 * @license     https://github.com/genyleap/raad/blob/main/LICENSE.md
 */

module;
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QVector>
#include <QFile>
#include <QUrl>
#include <QElapsedTimer>
#include <QStringList>
#include <QVariantList>

#ifndef Q_MOC_RUN
export module raad.core.downloadertask;
#endif

#ifdef Q_MOC_RUN
#define RAAD_MODULE_EXPORT
#else
#define RAAD_MODULE_EXPORT export
#endif

/**
 * @brief Represents a single downloadable resource.
 *
 * DownloaderTask manages the complete lifecycle of a download, including:
 * - Network request setup and execution
 * - Segmented or single-stream transfer strategies
 * - Resume detection and validation (ETag / Last-Modified)
 * - Bandwidth throttling and speed estimation
 * - Retry and mirror failover logic
 * - Integrity verification via checksums
 * - Emission of UI-facing progress, speed, and state signals
 *
 * This class is intentionally self-contained and does not enforce
 * global or queue-level policies; those are handled by higher-level
 * orchestrators such as DownloadManager.
 */
RAAD_MODULE_EXPORT class DownloaderTask : public QObject {

    Q_OBJECT

    //!< @brief Max speed limit in bytes/sec (0 = unlimited).
    Q_PROPERTY(qint64 maxSpeed READ maxSpeed WRITE setMaxSpeed NOTIFY maxSpeedChanged)

    //!< @brief Current task state string.
    Q_PROPERTY(QString stateString READ stateString NOTIFY stateChanged)

    //!< @brief Current download speed in bytes/sec.
    Q_PROPERTY(qint64 speed READ speed NOTIFY speedChanged)

    //!< @brief Estimated time remaining in seconds.
    Q_PROPERTY(int eta READ eta NOTIFY etaChanged)

    //!< @brief Last recorded speed in bytes/sec.
    Q_PROPERTY(qint64 lastSpeed READ lastSpeed NOTIFY lastSpeedChanged)

    //!< @brief Last recorded ETA in seconds.
    Q_PROPERTY(int lastEta READ lastEta NOTIFY lastEtaChanged)

    //!< @brief Time the task was paused (epoch ms).
    Q_PROPERTY(qint64 pausedAt READ pausedAt NOTIFY pausedAtChanged)

    //!< @brief Reason for pause, if any.
    Q_PROPERTY(QString pauseReason READ pauseReason NOTIFY pauseReasonChanged)

    //!< @brief Mirror URLs for failover.
    Q_PROPERTY(QStringList mirrorUrls READ mirrorUrls WRITE setMirrorUrls NOTIFY mirrorUrlsChanged)

    //!< @brief Index of active mirror URL.
    Q_PROPERTY(int mirrorIndex READ mirrorIndex WRITE setMirrorIndex NOTIFY mirrorIndexChanged)

    //!< @brief Checksum algorithm name.
    Q_PROPERTY(QString checksumAlgorithm READ checksumAlgorithm WRITE setChecksumAlgorithm NOTIFY checksumChanged)

    //!< @brief Expected checksum value.
    Q_PROPERTY(QString checksumExpected READ checksumExpected WRITE setChecksumExpected NOTIFY checksumChanged)

    //!< @brief Actual computed checksum value.
    Q_PROPERTY(QString checksumActual READ checksumActual NOTIFY checksumChanged)

    //!< @brief Checksum verification state.
    Q_PROPERTY(QString checksumState READ checksumState NOTIFY checksumChanged)

    //!< @brief Verify checksum on completion.
    Q_PROPERTY(bool verifyOnComplete READ verifyOnComplete WRITE setVerifyOnComplete NOTIFY verifyOnCompleteChanged)

    //!< @brief Warning when resume is not possible.
    Q_PROPERTY(QString resumeWarning READ resumeWarning NOTIFY resumeWarningChanged)

    //!< @brief Log lines for this task.
    Q_PROPERTY(QStringList logLines READ logLines NOTIFY logLinesChanged)

    //!< @brief Speed history samples for charting.
    Q_PROPERTY(QVariantList speedHistory READ speedHistory NOTIFY speedHistoryChanged)

    //!< @brief Post-action to open file.
    Q_PROPERTY(bool postOpenFile READ postOpenFile WRITE setPostOpenFile NOTIFY postActionsChanged)

    //!< @brief Post-action to reveal folder.
    Q_PROPERTY(bool postRevealFolder READ postRevealFolder WRITE setPostRevealFolder NOTIFY postActionsChanged)

    //!< @brief Post-action to extract archives.
    Q_PROPERTY(bool postExtract READ postExtract WRITE setPostExtract NOTIFY postActionsChanged)

    //!< @brief Post-action script.
    Q_PROPERTY(QString postScript READ postScript WRITE setPostScript NOTIFY postActionsChanged)

    //!< @brief Retry attempts maximum.
    Q_PROPERTY(int retryMax READ retryMax WRITE setRetryMax NOTIFY retryPolicyChanged)

    //!< @brief Retry delay in seconds.
    Q_PROPERTY(int retryDelaySec READ retryDelaySec WRITE setRetryDelaySec NOTIFY retryPolicyChanged)

    //!< @brief Custom request headers.
    Q_PROPERTY(QStringList customHeaders READ customHeaders WRITE setCustomHeaders NOTIFY networkOptionsChanged)

    //!< @brief Cookie header value.
    Q_PROPERTY(QString cookieHeader READ cookieHeader WRITE setCookieHeader NOTIFY networkOptionsChanged)

    //!< @brief HTTP basic auth user.
    Q_PROPERTY(QString authUser READ authUser WRITE setAuthUser NOTIFY networkOptionsChanged)

    //!< @brief HTTP basic auth password.
    Q_PROPERTY(QString authPassword READ authPassword WRITE setAuthPassword NOTIFY networkOptionsChanged)

    //!< @brief Proxy host.
    Q_PROPERTY(QString proxyHost READ proxyHost WRITE setProxyHost NOTIFY networkOptionsChanged)

    //!< @brief Proxy port.
    Q_PROPERTY(int proxyPort READ proxyPort WRITE setProxyPort NOTIFY networkOptionsChanged)

    //!< @brief Proxy username.
    Q_PROPERTY(QString proxyUser READ proxyUser WRITE setProxyUser NOTIFY networkOptionsChanged)

    //!< @brief Proxy password.
    Q_PROPERTY(QString proxyPassword READ proxyPassword WRITE setProxyPassword NOTIFY networkOptionsChanged)

public:
    /**
     * @brief Construct a new download task.
     * @param url Source URL.
     * @param filePath Target file path.
     * @param segments Preferred segment count.
     * @param parent Optional parent QObject.
     */
    explicit DownloaderTask(const QUrl& url,
                            const QString& filePath,
                            int segments = 4,
                            QObject* parent = nullptr);

    //!< @brief Start the download.
    Q_INVOKABLE void start();

    //!< @brief Pause the download.
    Q_INVOKABLE void pause();

    /**
     * @brief Pause the download with a reason.
     * @param reason Pause reason.
     */
    void pauseWithReason(const QString& reason);

    //!< @brief Resume a paused download.
    Q_INVOKABLE void resume();

    //!< @brief Cancel the download.
    Q_INVOKABLE void cancel();

    //!< @brief Restart the download from scratch.
    Q_INVOKABLE void restart();

    //!< @brief Mark the task as paused without network state.
    void markPaused();

    //!< @brief Mark the task as failed.
    void markError();

    //!< @brief Mark the task as completed.
    void markDone();

    //!< @brief Mark the task as canceled.
    void markCanceled();

    //!< @brief Return the output file path.
    Q_INVOKABLE QString fileName() const { return m_filePath; }

    //!< @brief Return the task URL string.
    Q_INVOKABLE QString url() const { return m_url.toString(); }

    //!< @brief Return the segment count.
    Q_INVOKABLE int segments() const { return m_segments; }

    /**
     * @brief Set max speed limit.
     * @param v Speed limit in bytes/sec (0 = unlimited).
     */
    Q_INVOKABLE void setMaxSpeed(qint64 v) {
        if (m_maxSpeed != v) {
            m_maxSpeed = v;
            emit maxSpeedChanged();
            // reset throttle window so limit applies immediately
            m_throttleTimer.restart();
            m_throttleBytes = 0;
        }
    }
    //!< @brief Return max speed limit.
    qint64 maxSpeed() const { return m_maxSpeed; }

    //!< @brief Return the human-readable state string.
    QString stateString() const;

    //!< @brief Return current speed in bytes/sec.
    qint64 speed() const { return m_speed; }

    //!< @brief Return ETA in seconds.
    int eta() const { return m_eta; }

    //!< @brief Return last recorded speed in bytes/sec.
    qint64 lastSpeed() const { return m_lastSpeed; }

    //!< @brief Return last recorded ETA in seconds.
    int lastEta() const { return m_lastEta; }

    //!< @brief Return paused time (epoch ms).
    qint64 pausedAt() const { return m_pausedAt; }

    //!< @brief Return current pause reason.
    QString pauseReason() const { return m_pauseReason; }

    //!< @brief Check if the task is currently running.
    bool isRunning() const { return m_state == State::Downloading; }

    //!< @brief Check if the task is idle/queued.
    bool isIdle() const { return m_state == State::Idle; }


    //!< @brief Return mirror URL list.
    QStringList mirrorUrls() const { return m_mirrorUrls; }

    /**
     * @brief Set mirror URL list.
     * @param urls Mirror URL list.
     */
    void setMirrorUrls(const QStringList& urls);

    //!< @brief Return current mirror index.
    int mirrorIndex() const { return m_mirrorIndex; }

    /**
     * @brief Set current mirror index.
     * @param index Mirror index.
     */
    void setMirrorIndex(int index);

    /**
     * @brief Advance to the next mirror.
     * @return True if moved to a new mirror.
     */
    bool advanceMirror();

    //!< @brief Return checksum algorithm name.
    QString checksumAlgorithm() const { return m_checksumAlgorithm; }

    /**
     * @brief Set checksum algorithm name.
     * @param algo Algorithm name.
     */
    void setChecksumAlgorithm(const QString& algo);

    //!< @brief Return expected checksum string.
    QString checksumExpected() const { return m_checksumExpected; }

    /**
     * @brief Set expected checksum string.
     * @param value Checksum string.
     */
    void setChecksumExpected(const QString& value);

    //!< @brief Return actual checksum string.
    QString checksumActual() const { return m_checksumActual; }

    //!< @brief Return checksum state string.
    QString checksumState() const { return m_checksumState; }

    /**
     * @brief Set actual checksum string.
     * @param value Checksum string.
     */
    void setChecksumActual(const QString& value);

    /**
     * @brief Set checksum state string.
     * @param value State string.
     */
    void setChecksumState(const QString& value);

    //!< @brief Return verify-on-complete flag.
    bool verifyOnComplete() const { return m_verifyOnComplete; }

    /**
     * @brief Set verify-on-complete flag.
     * @param enabled Whether to verify on completion.
     */
    void setVerifyOnComplete(bool enabled);

    //!< @brief Return resume warning message.
    QString resumeWarning() const { return m_resumeWarning; }

    /**
     * @brief Set resume warning message.
     * @param warning Warning message.
     */
    void setResumeWarning(const QString& warning);

    //!< @brief Return log lines.
    QStringList logLines() const { return m_logLines; }

    /**
     * @brief Append a log line.
     * @param line Log line.
     */
    void appendLog(const QString& line);

    //!< @brief Return speed history samples.
    QVariantList speedHistory() const { return m_speedHistory; }

    /**
     * @brief Append a speed sample.
     * @param bytesPerSecond Speed sample.
     */
    void appendSpeedSample(qint64 bytesPerSecond);


    //!< @brief Return post-action open file flag.
    bool postOpenFile() const { return m_postOpenFile; }

    /**
     * @brief Set post-action open file flag.
     * @param value Whether to open file.
     */
    void setPostOpenFile(bool value);

    //!< @brief Return post-action reveal folder flag.
    bool postRevealFolder() const { return m_postRevealFolder; }

    /**
     * @brief Set post-action reveal folder flag.
     * @param value Whether to reveal folder.
     */
    void setPostRevealFolder(bool value);

    //!< @brief Return post-action extract flag.
    bool postExtract() const { return m_postExtract; }

    /**
     * @brief Set post-action extract flag.
     * @param value Whether to extract archives.
     */
    void setPostExtract(bool value);

    //!< @brief Return post-action script.
    QString postScript() const { return m_postScript; }

    /**
     * @brief Set post-action script.
     * @param script Script string.
     */
    void setPostScript(const QString& script);

    //!< @brief Return max retry attempts.
    int retryMax() const { return m_retryMax; }

    /**
     * @brief Set max retry attempts.
     * @param value Retry attempts.
     */
    void setRetryMax(int value);

    //!< @brief Return retry delay in seconds.
    int retryDelaySec() const { return m_retryDelaySec; }

    /**
     * @brief Set retry delay in seconds.
     * @param value Retry delay.
     */
    void setRetryDelaySec(int value);

    //!< @brief Return custom headers list.
    QStringList customHeaders() const { return m_customHeaders; }

    /**
     * @brief Set custom headers list.
     * @param headers Header list.
     */
    void setCustomHeaders(const QStringList& headers);

    //!< @brief Return cookie header.
    QString cookieHeader() const { return m_cookieHeader; }

    /**
     * @brief Set cookie header.
     * @param value Cookie header value.
     */
    void setCookieHeader(const QString& value);

    //!< @brief Return auth user.
    QString authUser() const { return m_authUser; }

    /**
     * @brief Set auth user.
     * @param value Username.
     */
    void setAuthUser(const QString& value);

    //!< @brief Return auth password.
    QString authPassword() const { return m_authPassword; }

    /**
     * @brief Set auth password.
     * @param value Password.
     */
    void setAuthPassword(const QString& value);

    //!< @brief Return proxy host.
    QString proxyHost() const { return m_proxyHost; }

    /**
     * @brief Set proxy host.
     * @param value Hostname.
     */
    void setProxyHost(const QString& value);

    //!< @brief Return proxy port.
    int proxyPort() const { return m_proxyPort; }

    /**
     * @brief Set proxy port.
     * @param value Port number.
     */
    void setProxyPort(int value);

    //!< @brief Return proxy user.
    QString proxyUser() const { return m_proxyUser; }

    /**
     * @brief Set proxy user.
     * @param value Username.
     */
    void setProxyUser(const QString& value);

    //!< @brief Return proxy password.
    QString proxyPassword() const { return m_proxyPassword; }

    /**
     * @brief Set proxy password.
     * @param value Password.
     */
    void setProxyPassword(const QString& value);

    /**
     * @brief Seed persisted stats on restore.
     * @param lastSpeed Last speed value.
     * @param lastEta Last ETA value.
     * @param pausedAtMs Paused timestamp in ms.
     * @param pauseReason Pause reason string.
     */
    void seedPersistedStats(qint64 lastSpeed, int lastEta, qint64 pausedAtMs, const QString& pauseReason);

    /**
     * @brief Set resume headers info.
     * @param etag ETag string.
     * @param lastModified Last-Modified string.
     */
    void setResumeInfo(const QString& etag, const QString& lastModified);

    //!< @brief Return cached ETag.
    QString etag() const { return m_etag; }

    //!< @brief Return cached Last-Modified.
    QString lastModified() const { return m_lastModified; }

    /**
     * @brief Set output file path.
     * @param path File path.
     */
    void setFilePath(const QString& path);

    /**
     * @brief Set the download URL.
     * @param url URL value.
     */
    void setUrl(const QUrl& url);

signals:
    /**
     * @brief Emitted on progress updates.
     * @param bytesReceived Received bytes.
     * @param bytesTotal Total bytes.
     */
    void progress(qint64 bytesReceived, qint64 bytesTotal);

    /**
     * @brief Emitted when task finishes.
     * @param success Whether task succeeded.
     */
    void finished(bool success);

    /**
     * @brief Emitted when current speed changes.
     * @param bytesPerSecond Speed value.
     */
    void speedChanged(qint64 bytesPerSecond);

    /**
     * @brief Emitted when ETA changes.
     * @param seconds ETA in seconds.
     */
    void etaChanged(int seconds);

    //!< @brief Emitted when last speed changes.
    void lastSpeedChanged();

    //!< @brief Emitted when last ETA changes.
    void lastEtaChanged();

    //!< @brief Emitted when pause reason changes.
    void pauseReasonChanged();

    //!< @brief Emitted when max speed changes.
    void maxSpeedChanged();

    //!< @brief Emitted when task state changes.
    void stateChanged();

    //!< @brief Emitted when paused timestamp changes.
    void pausedAtChanged();

    //!< @brief Emitted when mirror URL list changes.
    void mirrorUrlsChanged();

    //!< @brief Emitted when mirror index changes.
    void mirrorIndexChanged();

    //!< @brief Emitted when checksum data changes.
    void checksumChanged();

    //!< @brief Emitted when verify-on-complete flag changes.
    void verifyOnCompleteChanged();

    //!< @brief Emitted when resume warning changes.
    void resumeWarningChanged();

    //!< @brief Emitted when log lines change.
    void logLinesChanged();

    //!< @brief Emitted when speed history changes.
    void speedHistoryChanged();

    //!< @brief Emitted when post-actions change.
    void postActionsChanged();

    //!< @brief Emitted when retry policy changes.
    void retryPolicyChanged();

    //!< @brief Emitted when network options change.
    void networkOptionsChanged();

private slots:
    //!< @brief Handle completion of a segment.
    void onSegmentFinished();

private:

    /**
     * @brief Internal task state machine.
     *
     * State transitions are strictly controlled and drive:
     * - Network activity
     * - Signal emission
     * - UI-visible state strings
     */
    enum class State {
        Idle,               //!< Task is created or queued, not yet started.
        Downloading,        //!< Task is actively transferring data.
        Paused,             //!< Task is paused by user or policy.
        Finished,           //!< Task has completed (success or failure).
        Canceled            //!< Task was explicitly canceled.
    };


    /**
     * @brief Metadata and runtime state for a download segment.
     *
     * Each segment represents a byte range request with its own temporary
     * storage and buffering state. Segments are merged on successful completion.
     */
    struct Segment {
        qint64 start = 0;                   //!< Byte range start offset.
        qint64 end = 0;                     //!< Byte range end offset.
        qint64 downloaded = 0;              //!< Bytes downloaded so far.
        QNetworkReply* reply = nullptr;     //!< Active network reply.
        QString tempFilePath;               //!< Temporary file path.

        // Throttling and buffering
        QByteArray buffer;                  //!< Buffered incoming data.
        QFile* file = nullptr;              //!< Output file handle.
        bool processing = false;            //!< Buffer processing flag.
    };


    QUrl m_url;                                     //!< Source URL.
    QString m_filePath;                             //!< Target file path.
    int m_segments = 1;                             //!< Requested segment count.
    qint64 m_totalSize = 0;                         //!< Total content size.

    QVector<Segment> m_segmentsInfo;                //!< Segment list.
    QNetworkAccessManager* m_manager = nullptr;     //!< Network manager.
    QNetworkReply* m_headReply = nullptr;           //!< HEAD request reply.

    State m_state = State::Idle;            //!< Current state.
    bool m_useRange = true;                 //!< Whether range requests are enabled.
    bool m_anyError = false;                //!< Error flag.
    bool m_serverSupportsRange = true;      //!< Server range support flag.

    QElapsedTimer m_speedTimer;             //!< Speed/ETA update timer.
    qint64 m_lastBytes = 0;                 //!< Bytes at last speed sample.
    qint64 m_speed = 0;                     //!< Current speed in bytes/sec.
    qint64 m_lastSpeed = 0;                 //!< Last speed in bytes/sec.
    int m_eta = -1;                         //!< Current ETA in seconds.
    int m_lastEta = -1;                     //!< Last ETA in seconds.
    qint64 m_pausedAt = 0;                  //!< Pause timestamp in ms.
    QString m_pauseReason;                  //!< Pause reason string.
    QString m_etag;                         //!< Cached ETag.
    QString m_lastModified;                 //!< Cached Last-Modified.
    QStringList m_mirrorUrls;               //!< Mirror URL list.
    int m_mirrorIndex = 0;                  //!< Active mirror index.
    QString m_checksumAlgorithm;            //!< Checksum algorithm.
    QString m_checksumExpected;             //!< Expected checksum.
    QString m_checksumActual;               //!< Actual checksum.
    QString m_checksumState;                //!< Checksum state string.
    bool m_verifyOnComplete = false;        //!< Verify-on-complete flag.
    QString m_resumeWarning;                //!< Resume warning string.
    QStringList m_logLines;                 //!< Log line list.
    QVariantList m_speedHistory;            //!< Speed history samples.
    qint64 m_lastSpeedSampleMs = 0;         //!< Last speed sample time.
    int m_logLimit = 200;                   //!< Log line limit.
    int m_speedHistoryLimit = 90;           //!< Speed history limit.
    bool m_postOpenFile = false;            //!< Post-action open file flag.
    bool m_postRevealFolder = false;        //!< Post-action reveal folder flag.
    bool m_postExtract = false;             //!< Post-action extract flag.
    QString m_postScript;                   //!< Post-action script.
    int m_retryMax = -1;                    //!< Retry attempt limit.
    int m_retryDelaySec = -1;               //!< Retry delay in seconds.
    QStringList m_customHeaders;            //!< Custom headers list.
    QString m_cookieHeader;                 //!< Cookie header value.
    QString m_authUser;                     //!< Auth user.
    QString m_authPassword;                 //!< Auth password.
    QString m_proxyHost;                    //!< Proxy host.
    int m_proxyPort = 0;                    //!< Proxy port.
    QString m_proxyUser;                    //!< Proxy user.
    QString m_proxyPassword;                //!< Proxy password.

    // throttle window (non-blocking)
    QElapsedTimer m_throttleTimer;          //!< Throttle timer.
    qint64 m_throttleBytes = 0;             //!< Bytes written in current throttle window.
    qint64 m_maxSpeed = 0;                  //!< Max speed in bytes/sec.

    // single-stream helpers
    QByteArray m_singleBuffer;              //!< Single-stream buffer.
    QFile* m_singleFile = nullptr;          //!< Single-stream file handle.
    QNetworkReply* m_singleReply = nullptr; //!< Single-stream reply.
    bool m_singleProcessing = false;        //!< Single-stream processing flag.
    qint64 m_singleWritten = 0;             //!< Single-stream bytes written.
    bool m_resumeSingle = false;            //!< Resume single-stream flag.
    QString m_singleTempPath;               //!< Single-stream temp path.
    bool m_useSingleTemp = true;            //!< Use temp file for single stream.

    /**
     * @brief Start a network request for a specific segment.
     *
     * Applies range headers, authentication, proxy settings,
     * and custom headers before dispatching the request.
     */
    void startSegment(Segment* segment);

    /**
     * @brief Start or resume a single-stream download.
     *
     * Used when segmented downloads are unavailable or disabled.
     */
    void startSingleStream(bool resume);

    /**
     * @brief Merge completed segment files into the final output file.
     *
     * @return true if merge completed successfully.
     */
    bool mergeSegments();

    /**
     * @brief Sum total downloaded bytes.
     * @return Total bytes downloaded.
     */
    qint64 totalDownloaded() const;

    //!< @brief Update speed and ETA estimates.
    void updateSpeedAndETA();

    /**
     * @brief Cleanup internal state and temp files.
     * @param emitFinished Whether to emit finished.
     */
    void cleanup(bool emitFinished);

    /**
     * @brief Process buffered segment data.
     * @param s Segment pointer.
     */
    void processSegmentBuffer(Segment* s);

    //!< @brief Process buffered single-stream data.
    void processSingleBuffer();

    //!< @brief Reset the network manager with current proxy settings.
    void resetNetworkManager();

    //!< @brief Return the active URL (mirror-aware).
    QUrl currentUrl() const;

    /**
     * @brief Apply headers/auth options to a request.
     * @param req Request to modify.
     */
    void applyNetworkOptions(QNetworkRequest& req) const;
};

#include "downloadertask.moc"
