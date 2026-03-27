module;
#include <algorithm>
#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDebug>
#include <QTimer>
#include <QThread>
#include <QDateTime>
#include <QFileInfo>
#include <QPointer>
#include <QSslError>
#include <QNetworkProxy>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QElapsedTimer>
#include <ctime>

module raad.core.downloadertask;

import raad.utils.download_utils;

namespace utils = raad::utils;

DownloaderTask::DownloaderTask(const QUrl& url,
                               const QString& filePath,
                               int segments,
                               QObject* parent)
    : QObject(parent),
    m_url(url),
    m_filePath(filePath),
    m_parallelTarget(qBound(1, segments, 32)),
    m_segments(qBound(1, segments, 32)),
    m_effectiveSegments(qBound(1, segments, 32))
{
    m_filePath = utils::normalizeFilePath(m_filePath);
    m_singleTempPath = m_filePath + ".part";
    m_checksumState = QStringLiteral("None");
    m_adaptiveTarget = qBound(1, m_segments, 32);
    clearErrorState();
    resetAdaptiveStats();
    resetNetworkManager();
}

void DownloaderTask::resetNetworkManager()
{
    if (m_manager) {
        m_manager->deleteLater();
        m_manager = nullptr;
    }
    m_manager = new QNetworkAccessManager(this);
    if (!m_proxyHost.isEmpty() && m_proxyPort > 0) {
        QNetworkProxy proxy(QNetworkProxy::HttpProxy, m_proxyHost, m_proxyPort, m_proxyUser, m_proxyPassword);
        m_manager->setProxy(proxy);
    }
}

QUrl DownloaderTask::currentUrl() const
{
    if (!m_mirrorUrls.isEmpty() && m_mirrorIndex >= 0 && m_mirrorIndex < m_mirrorUrls.size()) {
        return QUrl(m_mirrorUrls.at(m_mirrorIndex));
    }
    return m_url;
}

void DownloaderTask::applyNetworkOptions(QNetworkRequest& req) const
{
    const QString ua = m_userAgent.trimmed();
    if (!ua.isEmpty() && !req.hasRawHeader("User-Agent")) {
        req.setRawHeader("User-Agent", ua.toUtf8());
    }
    if (!m_cookieHeader.isEmpty()) {
        req.setRawHeader("Cookie", m_cookieHeader.toUtf8());
    }
    if (!m_authUser.isEmpty()) {
        const QByteArray auth = (m_authUser + ":" + m_authPassword).toUtf8().toBase64();
        req.setRawHeader("Authorization", QByteArray("Basic ") + auth);
    }
    for (const QString& headerLine : m_customHeaders) {
        const int sep = headerLine.indexOf(':');
        if (sep <= 0) continue;
        const QString key = headerLine.left(sep).trimmed();
        const QString value = headerLine.mid(sep + 1).trimmed();
        if (key.isEmpty()) continue;
        const QString lower = key.toLower();
        if (lower == "range" || lower == "if-range") continue;
        req.setRawHeader(key.toUtf8(), value.toUtf8());
    }
}

void DownloaderTask::setMirrorUrls(const QStringList& urls)
{
    if (m_mirrorUrls == urls) return;
    m_mirrorUrls = urls;
    if (!m_mirrorUrls.isEmpty()) {
        m_mirrorIndex = 0;
        const QUrl nextUrl(m_mirrorUrls.first());
        if (nextUrl.isValid()) m_url = nextUrl;
        emit mirrorIndexChanged();
    }
    emit mirrorUrlsChanged();
}

void DownloaderTask::setMirrorIndex(int index)
{
    if (index < 0) index = 0;
    if (m_mirrorIndex == index) return;
    m_mirrorIndex = index;
    if (!m_mirrorUrls.isEmpty() && m_mirrorIndex < m_mirrorUrls.size()) {
        const QUrl nextUrl(m_mirrorUrls.at(m_mirrorIndex));
        if (nextUrl.isValid()) m_url = nextUrl;
    }
    emit mirrorIndexChanged();
}

bool DownloaderTask::advanceMirror()
{
    if (m_mirrorUrls.isEmpty()) return false;
    if (m_mirrorIndex + 1 >= m_mirrorUrls.size()) return false;
    m_mirrorIndex += 1;
    const QUrl nextUrl(m_mirrorUrls.at(m_mirrorIndex));
    if (nextUrl.isValid()) {
        m_url = nextUrl;
        m_etag.clear();
        m_lastModified.clear();
    }
    emit mirrorIndexChanged();
    return true;
}

void DownloaderTask::setChecksumAlgorithm(const QString& algo)
{
    if (m_checksumAlgorithm == algo) return;
    m_checksumAlgorithm = algo;
    emit checksumChanged();
}

void DownloaderTask::setChecksumExpected(const QString& value)
{
    if (m_checksumExpected == value) return;
    m_checksumExpected = value;
    if (m_checksumExpected.isEmpty()) {
        m_checksumState = QStringLiteral("None");
        m_checksumActual.clear();
    } else if (m_checksumState == QStringLiteral("None")) {
        m_checksumState = QStringLiteral("Pending");
    }
    emit checksumChanged();
}

void DownloaderTask::setChecksumActual(const QString& value)
{
    if (m_checksumActual == value) return;
    m_checksumActual = value;
    emit checksumChanged();
}

void DownloaderTask::setChecksumState(const QString& value)
{
    if (m_checksumState == value) return;
    m_checksumState = value;
    emit checksumChanged();
}

void DownloaderTask::setVerifyOnComplete(bool enabled)
{
    if (m_verifyOnComplete == enabled) return;
    m_verifyOnComplete = enabled;
    emit verifyOnCompleteChanged();
}

void DownloaderTask::setResumeWarning(const QString& warning)
{
    if (m_resumeWarning == warning) return;
    m_resumeWarning = warning;
    emit resumeWarningChanged();
}

void DownloaderTask::appendLog(const QString& line)
{
    if (line.trimmed().isEmpty()) return;
    m_logLines.append(line);
    while (m_logLines.size() > m_logLimit) {
        m_logLines.removeFirst();
    }
    emit logLinesChanged();
}

void DownloaderTask::appendSpeedSample(qint64 bytesPerSecond)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_lastSpeedSampleMs > 0 && nowMs - m_lastSpeedSampleMs < 900) return;
    m_lastSpeedSampleMs = nowMs;
    m_speedHistory.append(static_cast<qreal>(bytesPerSecond));
    while (m_speedHistory.size() > m_speedHistoryLimit) {
        m_speedHistory.removeFirst();
    }
    emit speedHistoryChanged();
}

void DownloaderTask::setPostOpenFile(bool value)
{
    if (m_postOpenFile == value) return;
    m_postOpenFile = value;
    emit postActionsChanged();
}

void DownloaderTask::setPostRevealFolder(bool value)
{
    if (m_postRevealFolder == value) return;
    m_postRevealFolder = value;
    emit postActionsChanged();
}

void DownloaderTask::setPostExtract(bool value)
{
    if (m_postExtract == value) return;
    m_postExtract = value;
    emit postActionsChanged();
}

void DownloaderTask::setPostScript(const QString& script)
{
    if (m_postScript == script) return;
    m_postScript = script;
    emit postActionsChanged();
}

void DownloaderTask::setRetryMax(int value)
{
    if (m_retryMax == value) return;
    m_retryMax = value;
    emit retryPolicyChanged();
}

void DownloaderTask::setRetryDelaySec(int value)
{
    if (m_retryDelaySec == value) return;
    m_retryDelaySec = value;
    emit retryPolicyChanged();
}

void DownloaderTask::setCustomHeaders(const QStringList& headers)
{
    if (m_customHeaders == headers) return;
    m_customHeaders = headers;
    emit networkOptionsChanged();
}

void DownloaderTask::setCookieHeader(const QString& value)
{
    if (m_cookieHeader == value) return;
    m_cookieHeader = value;
    emit networkOptionsChanged();
}

void DownloaderTask::setAuthUser(const QString& value)
{
    if (m_authUser == value) return;
    m_authUser = value;
    emit networkOptionsChanged();
}

void DownloaderTask::setAuthPassword(const QString& value)
{
    if (m_authPassword == value) return;
    m_authPassword = value;
    emit networkOptionsChanged();
}

void DownloaderTask::setProxyHost(const QString& value)
{
    if (m_proxyHost == value) return;
    m_proxyHost = value;
    if (m_state != State::Downloading) resetNetworkManager();
    emit networkOptionsChanged();
}

void DownloaderTask::setProxyPort(int value)
{
    if (m_proxyPort == value) return;
    m_proxyPort = value;
    if (m_state != State::Downloading) resetNetworkManager();
    emit networkOptionsChanged();
}

void DownloaderTask::setProxyUser(const QString& value)
{
    if (m_proxyUser == value) return;
    m_proxyUser = value;
    if (m_state != State::Downloading) resetNetworkManager();
    emit networkOptionsChanged();
}

void DownloaderTask::setProxyPassword(const QString& value)
{
    if (m_proxyPassword == value) return;
    m_proxyPassword = value;
    if (m_state != State::Downloading) resetNetworkManager();
    emit networkOptionsChanged();
}

void DownloaderTask::setUserAgent(const QString& value)
{
    const QString next = value.trimmed().isEmpty()
        ? QStringLiteral("raad/1.0")
        : value.trimmed();
    if (m_userAgent == next) return;
    m_userAgent = next;
    emit networkOptionsChanged();
}

void DownloaderTask::setAllowInsecureSsl(bool enabled)
{
    if (m_allowInsecureSsl == enabled) return;
    m_allowInsecureSsl = enabled;
    emit networkOptionsChanged();
}

void DownloaderTask::setPriority(int value)
{
    const int next = qBound(0, value, 1000);
    if (m_priority == next) return;
    m_priority = next;
    emit priorityChanged();
}

void DownloaderTask::setAdaptiveSegmentsEnabled(bool enabled)
{
    if (m_adaptiveSegmentsEnabled == enabled) return;
    m_adaptiveSegmentsEnabled = enabled;
    if (!enabled) {
        m_parallelTarget = qBound(1, m_segments, 32);
        m_adaptiveTarget = m_parallelTarget;
        resetAdaptiveStats();
    }
    emit adaptiveSegmentsChanged();
}

void DownloaderTask::sampleWriteLatency(qint64 elapsedMs)
{
    if (elapsedMs <= 0) return;
    const qreal previous = m_adaptiveWriteLatencyMs;
    if (m_adaptiveWriteSamples == 0) {
        m_adaptiveWriteLatencyMs = static_cast<qreal>(elapsedMs);
    } else {
        constexpr qreal kAlpha = 0.2;
        m_adaptiveWriteLatencyMs =
            (1.0 - kAlpha) * m_adaptiveWriteLatencyMs + kAlpha * static_cast<qreal>(elapsedMs);
    }
    m_adaptiveWriteSamples = qMin(m_adaptiveWriteSamples + 1, 5000);
    if (qAbs(m_adaptiveWriteLatencyMs - previous) >= 0.25) {
        emit adaptiveMetricsChanged();
    }
}

void DownloaderTask::sampleCpuLoad()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 nowTicks = static_cast<qint64>(std::clock());
    if (nowTicks <= 0) {
        m_adaptiveCpuLastWallMs = nowMs;
        m_adaptiveCpuLastClockTicks = 0;
        return;
    }

    if (m_adaptiveCpuLastWallMs <= 0 || m_adaptiveCpuLastClockTicks <= 0) {
        m_adaptiveCpuLastWallMs = nowMs;
        m_adaptiveCpuLastClockTicks = nowTicks;
        return;
    }

    const qint64 wallDeltaMs = nowMs - m_adaptiveCpuLastWallMs;
    const qint64 tickDelta = nowTicks - m_adaptiveCpuLastClockTicks;
    m_adaptiveCpuLastWallMs = nowMs;
    m_adaptiveCpuLastClockTicks = nowTicks;
    if (wallDeltaMs < 300 || tickDelta <= 0) return;

    const qreal cpuSeconds = static_cast<qreal>(tickDelta) / static_cast<qreal>(CLOCKS_PER_SEC);
    const qreal wallSeconds = static_cast<qreal>(wallDeltaMs) / 1000.0;
    const qreal cores = static_cast<qreal>(qMax(1, QThread::idealThreadCount()));
    qreal usagePct = 0.0;
    if (wallSeconds > 0.0 && cores > 0.0) {
        usagePct = (cpuSeconds / (wallSeconds * cores)) * 100.0;
    }
    usagePct = qBound<qreal>(0.0, usagePct, 100.0);

    const qreal previous = m_adaptiveCpuLoadPct;
    constexpr qreal kAlpha = 0.18;
    if (m_adaptiveCpuSamples == 0) {
        m_adaptiveCpuLoadPct = usagePct;
    } else {
        m_adaptiveCpuLoadPct = (1.0 - kAlpha) * m_adaptiveCpuLoadPct + kAlpha * usagePct;
    }
    m_adaptiveCpuSamples = qMin(m_adaptiveCpuSamples + 1, 5000);
    if (qAbs(m_adaptiveCpuLoadPct - previous) >= 1.0) {
        emit adaptiveMetricsChanged();
    }
}

void DownloaderTask::sampleNetworkRead(qint64 bytes)
{
    if (bytes <= 0) return;
    m_adaptiveReadEvents = qMin<qint64>(1000000, m_adaptiveReadEvents + 1);
    const qint64 total = m_adaptiveReadEvents + m_adaptiveErrorEvents;
    if (total > 0) {
        const qreal prev = m_adaptivePacketLossRate;
        m_adaptivePacketLossRate = static_cast<qreal>(m_adaptiveErrorEvents) / static_cast<qreal>(total);
        if (qAbs(m_adaptivePacketLossRate - prev) >= 0.005) {
            emit adaptiveMetricsChanged();
        }
    }
}

void DownloaderTask::sampleNetworkError(QNetworkReply::NetworkError err)
{
    if (err == QNetworkReply::NoError || err == QNetworkReply::OperationCanceledError) {
        return;
    }

    m_adaptiveErrorEvents = qMin<qint64>(1000000, m_adaptiveErrorEvents + 1);
    switch (err) {
    case QNetworkReply::TimeoutError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::ProxyConnectionRefusedError:
    case QNetworkReply::ProxyTimeoutError:
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::SslHandshakeFailedError:
        m_adaptivePacketLossHints = qMin(m_adaptivePacketLossHints + 1, 100);
        break;
    default:
        break;
    }

    const qint64 total = m_adaptiveReadEvents + m_adaptiveErrorEvents;
    if (total > 0) {
        const qreal prev = m_adaptivePacketLossRate;
        m_adaptivePacketLossRate = static_cast<qreal>(m_adaptiveErrorEvents) / static_cast<qreal>(total);
        if (qAbs(m_adaptivePacketLossRate - prev) >= 0.005) {
            emit adaptiveMetricsChanged();
        }
    }
}

void DownloaderTask::resetAdaptiveStats()
{
    m_adaptiveErrors = 0;
    m_adaptiveThrottleHits = 0;
    m_adaptiveServerThrottleHints = 0;
    m_adaptiveWriteLatencyMs = 0.0;
    m_adaptiveCpuLoadPct = 0.0;
    m_adaptivePacketLossRate = 0.0;
    m_adaptiveWriteSamples = 0;
    m_adaptiveCpuSamples = 0;
    m_adaptivePacketLossHints = 0;
    m_adaptiveReadEvents = 0;
    m_adaptiveErrorEvents = 0;
    m_adaptiveCpuLastClockTicks = static_cast<qint64>(std::clock());
    m_adaptiveCpuLastWallMs = QDateTime::currentMSecsSinceEpoch();
    m_adaptiveLastEvalMs = QDateTime::currentMSecsSinceEpoch();
    emit adaptiveMetricsChanged();
}

int DownloaderTask::recommendedAdaptiveTarget() const
{
    if (!m_adaptiveSegmentsEnabled) {
        return qBound(1, m_segments, 32);
    }
    if (!m_serverSupportsRange || !m_useRange) {
        return 1;
    }

    int target = 6;
    const qint64 speedBps = qMax<qint64>(0, m_speed);
    if (speedBps >= 8 * 1024 * 1024) target = 10;
    if (speedBps >= 20 * 1024 * 1024) target = 14;
    if (speedBps >= 50 * 1024 * 1024) target = 20;
    if (speedBps >= 120 * 1024 * 1024) target = 28;

    if (m_totalSize > 0 && m_totalSize < (64ll * 1024 * 1024)) {
        target = qMin(target, 8);
    }

    if (m_adaptiveCpuSamples > 4) {
        if (m_adaptiveCpuLoadPct > 80.0) {
            target -= 8;
        } else if (m_adaptiveCpuLoadPct > 60.0) {
            target -= 4;
        }
    }

    if (m_adaptiveWriteSamples > 8) {
        if (m_adaptiveWriteLatencyMs > 18.0) {
            target -= 8;
        } else if (m_adaptiveWriteLatencyMs > 10.0) {
            target -= 4;
        }
    }
    if (m_adaptiveThrottleHits > 6) {
        target -= 2;
    }
    if (m_adaptiveErrors > 1) {
        target -= 4;
    }
    if (m_adaptiveServerThrottleHints > 0) {
        target -= 4;
    }
    if (m_adaptivePacketLossHints > 2 || m_adaptivePacketLossRate > 0.08) {
        target -= 6;
    } else if (m_adaptivePacketLossRate > 0.03) {
        target -= 3;
    }

    target = qBound(4, target, 32);
    return target;
}

void DownloaderTask::evaluateAdaptiveSegments()
{
    if (!m_adaptiveSegmentsEnabled || m_state != State::Downloading) return;
    if (!m_serverSupportsRange || !m_useRange) return;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_adaptiveLastEvalMs > 0 && nowMs - m_adaptiveLastEvalMs < 3000) return;
    m_adaptiveLastEvalMs = nowMs;
    sampleCpuLoad();

    const int nextTarget = recommendedAdaptiveTarget();
    if (nextTarget != m_adaptiveTarget) {
        m_adaptiveTarget = nextTarget;
        emit adaptiveSegmentsChanged();
    }
    if (nextTarget != m_parallelTarget) {
        m_parallelTarget = qBound(1, nextTarget, 32);
    }

    // Decay counters to react quickly but avoid oscillation.
    m_adaptiveErrors = qMax(0, m_adaptiveErrors - 1);
    m_adaptiveThrottleHits = qMax(0, m_adaptiveThrottleHits - 2);
    m_adaptiveServerThrottleHints = qMax(0, m_adaptiveServerThrottleHints - 1);
    m_adaptivePacketLossHints = qMax(0, m_adaptivePacketLossHints - 1);
}

bool DownloaderTask::ensureOutputWritable(QString* why) const
{
    const QString outPath = utils::normalizeFilePath(m_filePath);
    if (outPath.isEmpty()) {
        if (why) *why = QStringLiteral("Empty output path");
        return false;
    }

    const QFileInfo info(outPath);
    const QString dirPath = info.absolutePath();
    if (dirPath.isEmpty()) {
        if (why) *why = QStringLiteral("Invalid output directory");
        return false;
    }
    QDir dir(dirPath);
    if (!dir.exists() && !QDir().mkpath(dirPath)) {
        if (why) *why = QStringLiteral("Cannot create output directory");
        return false;
    }
    const QFileInfo dirInfo(dirPath);
    if (!dirInfo.isWritable()) {
        if (why) *why = QStringLiteral("Output directory is not writable");
        return false;
    }
    return true;
}

bool DownloaderTask::ensureDiskCapacity(qint64 totalSizeBytes, qint64 alreadyHaveBytes, QString* why) const
{
    if (totalSizeBytes <= 0) return true;
    const qint64 remaining = qMax<qint64>(0, totalSizeBytes - qMax<qint64>(0, alreadyHaveBytes));
    if (remaining <= 0) return true;

    const QString outPath = utils::normalizeFilePath(m_filePath);
    QStorageInfo storage(QFileInfo(outPath).absolutePath());
    if (!storage.isValid() || !storage.isReady()) {
        if (why) *why = QStringLiteral("Storage info unavailable");
        return false;
    }
    const qint64 reserve = 64ll * 1024 * 1024;
    const qint64 needed = remaining + reserve;
    if (storage.bytesAvailable() < needed) {
        if (why) {
            *why = QStringLiteral("Insufficient free space (%1 MB required)")
                       .arg(static_cast<qlonglong>(needed / (1024 * 1024)));
        }
        return false;
    }
    return true;
}

qint64 DownloaderTask::parseContentRangeStart(const QByteArray& contentRange) const
{
    const QString value = QString::fromUtf8(contentRange).trimmed();
    if (value.isEmpty()) return -1;
    QRegularExpression re(QStringLiteral("^bytes\\s+(\\d+)-\\d+/\\d+|^bytes\\s+(\\d+)-\\d+/\\*$"),
                          QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = re.match(value);
    if (!match.hasMatch()) return -1;
    QString cap = match.captured(1);
    if (cap.isEmpty()) cap = match.captured(2);
    bool ok = false;
    const qint64 start = cap.toLongLong(&ok);
    return ok ? start : -1;
}

void DownloaderTask::recordError(const QString& category,
                                 const QString& code,
                                 const QString& message,
                                 int httpStatus,
                                 int networkError)
{
    bool changed = false;
    if (m_errorCategory != category) {
        m_errorCategory = category;
        changed = true;
    }
    if (m_errorCode != code) {
        m_errorCode = code;
        changed = true;
    }
    if (m_errorMessage != message) {
        m_errorMessage = message;
        changed = true;
    }
    if (httpStatus > 0 && m_lastHttpStatus != httpStatus) {
        m_lastHttpStatus = httpStatus;
        changed = true;
    }
    if (networkError >= 0 && m_lastNetworkError != networkError) {
        m_lastNetworkError = networkError;
        changed = true;
    }
    if (changed) emit errorStateChanged();
}

void DownloaderTask::clearErrorState()
{
    bool changed = false;
    if (!m_errorCategory.isEmpty()) {
        m_errorCategory.clear();
        changed = true;
    }
    if (!m_errorCode.isEmpty()) {
        m_errorCode.clear();
        changed = true;
    }
    if (!m_errorMessage.isEmpty()) {
        m_errorMessage.clear();
        changed = true;
    }
    if (m_lastHttpStatus != 0) {
        m_lastHttpStatus = 0;
        changed = true;
    }
    if (m_lastNetworkError != -1) {
        m_lastNetworkError = -1;
        changed = true;
    }
    if (changed) emit errorStateChanged();
}

void DownloaderTask::start()
{
    if (m_state != State::Idle)
        return;

    if (!m_pauseReason.isEmpty()) {
        m_pauseReason.clear();
        emit pauseReasonChanged();
    }
    if (m_pausedAt != 0) {
        m_pausedAt = 0;
        emit pausedAtChanged();
    }
    clearErrorState();
    resetAdaptiveStats();

    QString safetyError;
    if (!ensureOutputWritable(&safetyError)) {
        m_anyError = true;
        recordError(QStringLiteral("disk"),
                    QStringLiteral("output_not_writable"),
                    safetyError);
        m_state = State::Finished;
        emit stateChanged();
        emit finished(false);
        return;
    }

    const bool hasExistingFile = QFile::exists(m_filePath) && QFileInfo(m_filePath).size() > 0;
    bool hasPartialSegments = false;
    if (m_segments > 1) {
        for (int i = 0; i < m_segments; ++i) {
            QString partPath = QString("%1.part%2").arg(m_filePath).arg(i);
            if (QFile::exists(partPath)) {
                hasPartialSegments = true;
                break;
            }
        }
    }

    m_speedTimer.start();
    m_lastBytes = 0;
    m_throttleTimer.start();
    m_throttleBytes = 0;

    const QUrl activeUrl = currentUrl();
    if (!activeUrl.isValid()) {
        m_anyError = true;
        recordError(QStringLiteral("network"),
                    QStringLiteral("invalid_url"),
                    QStringLiteral("Download URL is invalid"));
        m_state = State::Finished;
        emit stateChanged();
        emit finished(false);
        return;
    }
    qDebug() << "DownloaderTask::start for" << activeUrl;
    appendLog(QStringLiteral("Start: %1").arg(activeUrl.toString()));
    m_anyError = false;
    m_state = State::Downloading;
    emit stateChanged();

    QNetworkRequest headReq(activeUrl);
    headReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    applyNetworkOptions(headReq);
    if (m_headReply) {
        QPointer<QNetworkReply> oldHead = m_headReply;
        m_headReply = nullptr;
        if (oldHead) {
            QObject::disconnect(oldHead, nullptr, this, nullptr);
            oldHead->abort();
            if (oldHead) oldHead->deleteLater();
        }
    }

    QNetworkReply* headReply = m_manager->head(headReq);
    m_headReply = headReply;

    connect(headReply, &QNetworkReply::errorOccurred, this, [this, headReply](QNetworkReply::NetworkError err) {
        if (err == QNetworkReply::OperationCanceledError || m_state == State::Paused || m_state == State::Canceled)
            return;
        m_adaptiveErrors = qMin(m_adaptiveErrors + 1, 100);
        sampleNetworkError(err);
        m_lastNetworkError = static_cast<int>(err);
        emit errorStateChanged();
        qWarning() << "HEAD error:" << headReply->errorString();
        appendLog(QStringLiteral("HEAD error: %1").arg(headReply->errorString()));
    });
#if QT_CONFIG(ssl)
    connect(headReply, &QNetworkReply::sslErrors, this, [this, headReply](const QList<QSslError>& errors) {
        qWarning() << "HEAD SSL errors:" << errors;
        if (m_allowInsecureSsl && headReply) {
            headReply->ignoreSslErrors(errors);
            appendLog(QStringLiteral("HEAD SSL errors ignored by policy"));
        }
    });
#endif

		    connect(headReply, &QNetworkReply::finished, this, [this, headReply, hasExistingFile, hasPartialSegments]() {
		        m_headReply = nullptr;
		        if (m_state != State::Downloading) {
		            headReply->deleteLater();
		            return;
        }
        if (headReply->error() != QNetworkReply::NoError) {
            qDebug() << "HEAD failed, fallback to single stream:" << headReply->errorString();
            appendLog(QStringLiteral("HEAD failed, fallback to single stream"));
            m_adaptiveErrors = qMin(m_adaptiveErrors + 1, 100);
            sampleNetworkError(headReply->error());
            headReply->deleteLater();
            m_totalSize = 0;
            m_useRange = true;
            m_effectiveSegments = 1;
            startSingleStream(hasExistingFile);
            return;
        }
        const QByteArray etag = headReply->rawHeader("ETag");
        if (!etag.isEmpty()) {
            m_etag = QString::fromUtf8(etag);
        }
        const QByteArray lastMod = headReply->rawHeader("Last-Modified");
        if (!lastMod.isEmpty()) {
            m_lastModified = QString::fromUtf8(lastMod);
        }
        QVariant cl = headReply->header(QNetworkRequest::ContentLengthHeader);
        QByteArray acceptRanges = headReply->rawHeader("Accept-Ranges");
        const int statusCode = headReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (statusCode > 0) {
            m_lastHttpStatus = statusCode;
            emit errorStateChanged();
            if (statusCode == 429 || statusCode == 503 || statusCode == 504) {
                m_adaptiveServerThrottleHints = qMin(m_adaptiveServerThrottleHints + 1, 100);
            }
        }
        headReply->deleteLater();

        if (!cl.isValid() || cl.toLongLong() <= 0) {
            qDebug() << "No Content-Length → single stream";
            m_totalSize = 0;
            m_useRange = false;
            m_effectiveSegments = 1;
            startSingleStream(false);
            return;
        }

        m_totalSize = cl.toLongLong();
        QString localSafetyError;
        if (!ensureDiskCapacity(m_totalSize, utils::bytesReceivedOnDisk(m_filePath, m_segments), &localSafetyError)) {
            m_anyError = true;
            recordError(QStringLiteral("disk"),
                        QStringLiteral("insufficient_space"),
                        localSafetyError);
            m_state = State::Finished;
            emit stateChanged();
            emit finished(false);
            return;
        }
        if (acceptRanges.toLower() != "bytes") {
            qDebug() << "Server does not support ranges";
            m_useRange = false;
            m_serverSupportsRange = false;
        } else {
            m_serverSupportsRange = true;
        }

        if (m_adaptiveSegmentsEnabled) {
            m_parallelTarget = qBound(4, m_segments, 32);
            m_adaptiveTarget = m_parallelTarget;
            emit adaptiveSegmentsChanged();
        }

        if (!m_useRange || m_segments == 1) {
            m_effectiveSegments = 1;
            startSingleStream(hasExistingFile);
            return;
        }

        int segCount = qMax(1, m_segments);
        if (m_totalSize > 0) {
            segCount = static_cast<int>(qMin<qint64>(segCount, m_totalSize));
        }
        if (segCount <= 1) {
            m_effectiveSegments = 1;
            startSingleStream(hasExistingFile);
            return;
        }
        m_effectiveSegments = segCount;

        // Prepare segments
        m_segmentsInfo.clear();
        m_segmentsInfo.reserve(32);
        qint64 segSize = m_totalSize / segCount;

        for (int i = 0; i < segCount; ++i) {
            Segment s;
            s.start = i * segSize;
            s.end = (i == segCount - 1)
                        ? (m_totalSize - 1)
                        : ((i + 1) * segSize - 1);
            s.tempFilePath = QString("%1.part%2").arg(m_filePath).arg(i);
            if (hasPartialSegments && QFile::exists(s.tempFilePath)) {
                QFileInfo info(s.tempFilePath);
                qint64 segLen = s.end - s.start + 1;
                s.downloaded = qMin(info.size(), segLen);
            } else {
                QFile::remove(s.tempFilePath);
                s.downloaded = 0;
            }
            s.file = nullptr;
            s.processing = false;
            s.buffer.clear();
            m_segmentsInfo.push_back(s);
        }

	        for (int i = segCount; i < m_segments; ++i) {
	            QFile::remove(QString("%1.part%2").arg(m_filePath).arg(i));
	        }

	        bool anyStarted = false;
	        for (Segment& s : m_segmentsInfo) {
	            const qint64 segLen = s.end - s.start + 1;
	            if (s.downloaded >= segLen) continue;
	            startSegment(&s);
	            anyStarted = true;
	        }
	        if (!anyStarted) {
	            // All segments already on disk; just merge and finish.
	            onSegmentFinished();
	        }
	    });
}

void DownloaderTask::startSingleStream(bool resume)
{
    m_effectiveSegments = 1;

    QNetworkRequest req(currentUrl());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    if (m_singleReply) {
        QPointer<QNetworkReply> oldReply = m_singleReply;
        m_singleReply = nullptr;
        if (oldReply) {
            QObject::disconnect(oldReply, nullptr, this, nullptr);
            oldReply->abort();
            if (oldReply) oldReply->deleteLater();
        }
    }

    if (m_singleFile) {
        m_singleFile->close();
        m_singleFile->deleteLater();
        m_singleFile = nullptr;
    }

    m_singleBuffer.clear();
    m_singleProcessing = false;
    m_resumeSingle = resume && m_useRange;

    const QString tempPath = m_filePath + ".part";
    bool hasTemp = QFile::exists(tempPath);
    bool hasMain = QFile::exists(m_filePath);
    m_useSingleTemp = hasTemp || !hasMain;
    m_singleTempPath = m_useSingleTemp ? tempPath : m_filePath;

    qint64 existingSize = 0;
    if (m_resumeSingle && QFile::exists(m_singleTempPath)) {
        QFileInfo info(m_singleTempPath);
        existingSize = info.size();
        if (existingSize > 0) {
            req.setRawHeader("Range", QByteArray("bytes=") + QByteArray::number(existingSize) + "-");
            if (!m_etag.isEmpty()) {
                req.setRawHeader("If-Range", m_etag.toUtf8());
            } else if (!m_lastModified.isEmpty()) {
                req.setRawHeader("If-Range", m_lastModified.toUtf8());
            }
        } else {
            m_resumeSingle = false;
        }
    }
    QString spaceError;
    if (!ensureDiskCapacity(m_totalSize, existingSize, &spaceError)) {
        m_anyError = true;
        recordError(QStringLiteral("disk"),
                    QStringLiteral("insufficient_space"),
                    spaceError);
        m_state = State::Finished;
        emit stateChanged();
        emit finished(false);
        return;
    }

    m_singleFile = new QFile(m_singleTempPath);
    QIODevice::OpenMode mode = QIODevice::WriteOnly | (m_resumeSingle ? QIODevice::Append : QIODevice::Truncate);
    if (!m_singleFile->open(mode)) {
        qWarning() << "Cannot open output file" << m_singleTempPath;
        delete m_singleFile; m_singleFile = nullptr;
        m_anyError = true;
        recordError(QStringLiteral("disk"),
                    QStringLiteral("open_failed"),
                    QStringLiteral("Cannot open output file: %1").arg(m_singleTempPath));
        m_state = State::Finished;
        emit stateChanged();
        emit finished(false);
        return;
    }

    m_singleWritten = m_resumeSingle ? existingSize : 0;

    applyNetworkOptions(req);
    QNetworkReply* reply = m_manager->get(req);
    m_singleReply = reply;
    QPointer<QNetworkReply> replyPtr(reply);

    connect(reply, &QNetworkReply::metaDataChanged, this, [this, replyPtr, existingSize]() {
        if (!replyPtr || replyPtr != m_singleReply) return;
        int status = replyPtr->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status > 0 && status != m_lastHttpStatus) {
            m_lastHttpStatus = status;
            emit errorStateChanged();
        }
        if (status == 429 || status == 503 || status == 504) {
            m_adaptiveServerThrottleHints = qMin(m_adaptiveServerThrottleHints + 1, 100);
        }
        const QByteArray etag = replyPtr->rawHeader("ETag");
        if (!etag.isEmpty()) {
            m_etag = QString::fromUtf8(etag);
        }
        const QByteArray lastMod = replyPtr->rawHeader("Last-Modified");
        if (!lastMod.isEmpty()) {
            m_lastModified = QString::fromUtf8(lastMod);
        }
        if (status == 206) {
            m_serverSupportsRange = true;
            if (m_resumeSingle && existingSize > 0) {
                const qint64 start = parseContentRangeStart(replyPtr->rawHeader("Content-Range"));
                if (start >= 0 && start != existingSize) {
                    if (m_singleFile) {
                        m_singleFile->close();
                        if (!m_singleFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                            qWarning() << "Cannot reopen output file for resume reset" << m_singleTempPath;
                        }
                    }
                    m_resumeSingle = false;
                    m_singleWritten = 0;
                    setResumeWarning(QStringLiteral("Resume mismatch; restarted"));
                    appendLog(QStringLiteral("Resume mismatch (Content-Range start mismatch); restarted"));
                }
            }
        } else if (status == 200) {
            m_serverSupportsRange = false;
        }
        if (!m_resumeSingle) return;
        if (status >= 400) {
            QPointer<QNetworkReply> activeReply = m_singleReply;
            m_singleReply = nullptr;
            if (activeReply) {
                QObject::disconnect(activeReply, nullptr, this, nullptr);
                activeReply->abort();
                if (activeReply) activeReply->deleteLater();
            }
            if (m_singleFile) {
                m_singleFile->close();
                m_singleFile->deleteLater();
                m_singleFile = nullptr;
            }
            m_resumeSingle = false;
            m_singleWritten = 0;
            setResumeWarning(QStringLiteral("Resume rejected; restarting"));
            appendLog(QStringLiteral("Resume rejected; restarting from 0"));
            recordError(QStringLiteral("network"),
                        QStringLiteral("resume_rejected"),
                        QStringLiteral("Resume request rejected by server"),
                        status);
            QTimer::singleShot(0, this, [this] {
                if (m_state == State::Downloading) startSingleStream(false);
            });
            return;
        }
        if (status != 206) {
            if (m_singleFile) {
                m_singleFile->close();
                if (!m_singleFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    qWarning() << "Cannot reopen output file for restart" << m_singleTempPath;
                }
            }
            m_resumeSingle = false;
            m_singleWritten = 0;
            if (existingSize > 0) {
                setResumeWarning(QStringLiteral("Resume not supported; restarted"));
                appendLog(QStringLiteral("Resume not supported; restarted"));
            }
        }
        if (status >= 400) {
            recordError(QStringLiteral("network"),
                        QStringLiteral("http_status"),
                        QStringLiteral("HTTP status %1").arg(status),
                        status);
        }
    });

    connect(reply, &QNetworkReply::errorOccurred, this, [this, replyPtr](QNetworkReply::NetworkError err) {
        if (err == QNetworkReply::OperationCanceledError || m_state == State::Paused || m_state == State::Canceled)
            return;
        if (!replyPtr) return;
        m_adaptiveErrors = qMin(m_adaptiveErrors + 1, 100);
        sampleNetworkError(err);
        recordError(QStringLiteral("network"),
                    QStringLiteral("network_error"),
                    replyPtr->errorString(),
                    0,
                    static_cast<int>(err));
        qWarning() << "GET error:" << replyPtr->errorString();
        appendLog(QStringLiteral("GET error: %1").arg(replyPtr->errorString()));
    });
#if QT_CONFIG(ssl)
    connect(reply, &QNetworkReply::sslErrors, this, [this, reply](const QList<QSslError>& errors) {
        qWarning() << "GET SSL errors:" << errors;
        if (m_allowInsecureSsl && reply) {
            reply->ignoreSslErrors(errors);
            appendLog(QStringLiteral("GET SSL errors ignored by policy"));
        }
    });
#endif

    connect(reply, &QNetworkReply::readyRead, this, [this, replyPtr]() mutable {
        if (!replyPtr || replyPtr != m_singleReply) return;
        QByteArray data = replyPtr->readAll();
        sampleNetworkRead(data.size());
        // append to single buffer
        m_singleBuffer.append(data);

        // try to process buffer (non-blocking)
        if (!m_singleProcessing) processSingleBuffer();
    });

    connect(reply, &QNetworkReply::finished, this, [this, replyPtr]() mutable {
        if (!replyPtr) return;
        if (replyPtr != m_singleReply) {
            replyPtr->deleteLater();
            return;
        }
        if (m_state == State::Downloading && replyPtr->error() != QNetworkReply::NoError) {
            m_anyError = true;
            recordError(QStringLiteral("network"),
                        QStringLiteral("download_failed"),
                        replyPtr->errorString(),
                        replyPtr->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
                        static_cast<int>(replyPtr->error()));
        }

        // ensure buffer fully processed
        if (!m_singleProcessing && m_singleBuffer.size() > 0) processSingleBuffer();

        // Final drain after network close: flush any buffered bytes regardless of throttle.
        if (m_singleFile && !m_singleBuffer.isEmpty()) {
            while (!m_singleBuffer.isEmpty()) {
                QElapsedTimer writeTimer;
                writeTimer.start();
                const qint64 written = m_singleFile->write(m_singleBuffer.constData(), m_singleBuffer.size());
                sampleWriteLatency(writeTimer.elapsed());
                if (written <= 0) {
                    m_anyError = true;
                    recordError(QStringLiteral("disk"),
                                QStringLiteral("write_failed"),
                                QStringLiteral("Failed writing output buffer"));
                    break;
                }
                m_singleBuffer.remove(0, written);
                m_singleWritten += written;
            }
            emit progress(totalDownloaded(), m_totalSize);
            updateSpeedAndETA();
        }

        if (m_singleFile) {
            m_singleFile->close();
            m_singleFile->deleteLater();
            m_singleFile = nullptr;
        }

        replyPtr->deleteLater();
        m_singleReply = nullptr;

        if (m_state == State::Paused || m_state == State::Canceled)
            return;

        if (m_useSingleTemp && !m_singleTempPath.isEmpty() && m_singleTempPath != m_filePath) {
            if (QFile::exists(m_filePath)) {
                QFile::remove(m_filePath);
            }
            if (!QFile::rename(m_singleTempPath, m_filePath)) {
                m_anyError = true;
            }
        }

        if (!m_anyError && m_totalSize > 0) {
            const QFileInfo info(m_filePath);
            if (info.exists() && info.size() != m_totalSize) {
                m_anyError = true;
                recordError(QStringLiteral("disk"),
                            QStringLiteral("final_size_mismatch"),
                            QStringLiteral("Final file size mismatch (%1 != %2)")
                                .arg(info.size())
                                .arg(m_totalSize));
            }
        }

        m_state = State::Finished;
        emit stateChanged();
        emit finished(!m_anyError);
    });
}

void DownloaderTask::processSingleBuffer()
{
    if (!m_singleFile) return;
    if (m_singleProcessing) return;
    if (m_singleBuffer.isEmpty()) return;
    if (m_state != State::Downloading) return;

    m_singleProcessing = true;

    // how many bytes allowed to write now based on throttle window
    qint64 elapsedMs = m_throttleTimer.elapsed();
    if (elapsedMs == 0) elapsedMs = 1;
    qint64 allowed = (m_maxSpeed > 0) ? (m_maxSpeed * elapsedMs / 1000 - m_throttleBytes) : m_singleBuffer.size();

    if (allowed <= 0) {
        m_adaptiveThrottleHits = qMin(m_adaptiveThrottleHits + 1, 100);
        // schedule later
        QTimer::singleShot(50, this, [this]{ m_singleProcessing = false; processSingleBuffer(); });
        return;
    }

    qint64 toWrite = qMin<qint64>(allowed, m_singleBuffer.size());
    QElapsedTimer writeTimer;
    writeTimer.start();
    qint64 written = m_singleFile->write(m_singleBuffer.constData(), toWrite);
    sampleWriteLatency(writeTimer.elapsed());
    if (written > 0) {
        m_singleBuffer.remove(0, written);
        m_throttleBytes += written;
        m_singleWritten += written;
    } else if (written < 0) {
        recordError(QStringLiteral("disk"),
                    QStringLiteral("write_failed"),
                    QStringLiteral("Failed writing output buffer"));
    }

    // reset throttle window if > 1000 ms
    if (m_throttleTimer.elapsed() >= 1000) {
        m_throttleTimer.restart();
        m_throttleBytes = 0;
    }

    // update progress and speed/eta
    qint64 totalDownloadedBytes = totalDownloaded();
    emit progress(totalDownloadedBytes, m_totalSize);
    updateSpeedAndETA();
    evaluateAdaptiveSegments();

    m_singleProcessing = false;

    if (!m_singleBuffer.isEmpty()) {
        QTimer::singleShot(10, this, [this]{ processSingleBuffer(); });
    }
}

void DownloaderTask::startSegment(Segment* segment)
{
    if (m_state != State::Downloading)
        return;

    // open file for append if not opened
    if (!segment->file) {
        segment->file = new QFile(segment->tempFilePath);
        if (!segment->file->open(QIODevice::WriteOnly | QIODevice::Append)) {
            qWarning() << "Cannot open temp file" << segment->tempFilePath;
            delete segment->file;
            segment->file = nullptr;
            m_anyError = true;
            recordError(QStringLiteral("disk"),
                        QStringLiteral("open_failed"),
                        QStringLiteral("Cannot open temporary segment file"));
            m_state = State::Finished;
            emit stateChanged();
            cleanup(false);
            emit finished(false);
            return;
        }
    }

    QNetworkRequest req(currentUrl());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader(
        "Range",
        QString("bytes=%1-%2")
            .arg(segment->start + segment->downloaded)
            .arg(segment->end)
            .toUtf8());
    if (segment->downloaded > 0) {
        if (!m_etag.isEmpty()) {
            req.setRawHeader("If-Range", m_etag.toUtf8());
        } else if (!m_lastModified.isEmpty()) {
            req.setRawHeader("If-Range", m_lastModified.toUtf8());
        }
    }

    applyNetworkOptions(req);
    QNetworkReply* reply = m_manager->get(req);
    segment->reply = reply;
    QPointer<QNetworkReply> replyPtr(reply);

    connect(reply, &QNetworkReply::metaDataChanged, this, [this, segment, replyPtr]() {
        if (!replyPtr || replyPtr != segment->reply) return;

        const int status = replyPtr->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status > 0 && status != m_lastHttpStatus) {
            m_lastHttpStatus = status;
            emit errorStateChanged();
        }
        if (status == 429 || status == 503 || status == 504) {
            m_adaptiveServerThrottleHints = qMin(m_adaptiveServerThrottleHints + 1, 100);
        }
        const QByteArray etag = replyPtr->rawHeader("ETag");
        if (!etag.isEmpty()) {
            m_etag = QString::fromUtf8(etag);
        }
        const QByteArray lastMod = replyPtr->rawHeader("Last-Modified");
        if (!lastMod.isEmpty()) {
            m_lastModified = QString::fromUtf8(lastMod);
        }
        if (status == 0) return; // not available yet
        if (status == 206) {
            if (segment->downloaded > 0) {
                const qint64 expectedStart = segment->start + segment->downloaded;
                const qint64 actualStart = parseContentRangeStart(replyPtr->rawHeader("Content-Range"));
                if (actualStart >= 0 && actualStart != expectedStart) {
                    qWarning() << "Content-Range mismatch, fallback to single stream";
                    m_useRange = false;
                    m_serverSupportsRange = false;
                    m_adaptiveErrors = qMin(m_adaptiveErrors + 1, 100);
                    setResumeWarning(QStringLiteral("Resume mismatch; switched to single stream"));
                    appendLog(QStringLiteral("Segment resume mismatch; switched to single stream"));
                    recordError(QStringLiteral("network"),
                                QStringLiteral("resume_mismatch"),
                                QStringLiteral("Segment Content-Range mismatch"),
                                status);
                    QTimer::singleShot(0, this, [this] {
                        if (m_state != State::Downloading) return;
                        m_effectiveSegments = 1;
                        cleanup(false);
                        startSingleStream(false);
                    });
                    return;
                }
            }
            return;
        }

        // If we're doing multi-segment, a non-206 usually means the server ignored Range.
        const bool isWholeFileRange = (segment->start == 0 && m_totalSize > 0 && segment->end == m_totalSize - 1);
        if (status == 200 && (m_segmentsInfo.size() <= 1 || isWholeFileRange)) {
            return; // treat as a valid single-range download
        }

        if (status == 200) {
            qWarning() << "SEGMENT GET returned 200 (Range ignored), falling back to single stream";
            m_useRange = false;
            m_serverSupportsRange = false;
            m_adaptiveServerThrottleHints = qMin(m_adaptiveServerThrottleHints + 1, 100);
            setResumeWarning(QStringLiteral("Range ignored; switched to single stream"));
            appendLog(QStringLiteral("Range ignored; switched to single stream"));
            recordError(QStringLiteral("network"),
                        QStringLiteral("range_ignored"),
                        QStringLiteral("Server ignored Range header"),
                        status);
            QTimer::singleShot(0, this, [this] {
                if (m_state != State::Downloading) return;
                m_effectiveSegments = 1;
                cleanup(false);
                startSingleStream(false);
            });
            return;
        }

        if (status >= 400) {
            qWarning() << "SEGMENT GET HTTP error status" << status;
            recordError(QStringLiteral("network"),
                        QStringLiteral("http_status"),
                        QStringLiteral("HTTP status %1").arg(status),
                        status);
        }
    });

    connect(reply, &QNetworkReply::errorOccurred, this, [this, reply](QNetworkReply::NetworkError err) {
        if (err == QNetworkReply::OperationCanceledError || m_state == State::Paused || m_state == State::Canceled)
            return;
        m_adaptiveErrors = qMin(m_adaptiveErrors + 1, 100);
        sampleNetworkError(err);
        recordError(QStringLiteral("network"),
                    QStringLiteral("network_error"),
                    reply->errorString(),
                    0,
                    static_cast<int>(err));
        qWarning() << "SEGMENT GET error:" << reply->errorString();
        appendLog(QStringLiteral("SEGMENT error: %1").arg(reply->errorString()));
    });
#if QT_CONFIG(ssl)
    connect(reply, &QNetworkReply::sslErrors, this, [this, reply](const QList<QSslError>& errors) {
        qWarning() << "SEGMENT GET SSL errors:" << errors;
        if (m_allowInsecureSsl && reply) {
            reply->ignoreSslErrors(errors);
            appendLog(QStringLiteral("SEGMENT SSL errors ignored by policy"));
        }
    });
#endif

    connect(reply, &QNetworkReply::readyRead, this, [this, segment, replyPtr]() mutable {
        if (!replyPtr || replyPtr != segment->reply) return;
        QByteArray data = replyPtr->readAll();
        sampleNetworkRead(data.size());
        // append to segment buffer
        segment->buffer.append(data);

        // try to process buffer (non-blocking)
        if (!segment->processing) processSegmentBuffer(segment);
    });

    connect(reply, &QNetworkReply::finished, this, [this, segment, replyPtr]() mutable {
        if (!replyPtr) return;
        if (replyPtr != segment->reply) {
            replyPtr->deleteLater();
            return;
        }
        if (m_state == State::Downloading && segment->reply && segment->reply->error() != QNetworkReply::NoError) {
            m_anyError = true;
            recordError(QStringLiteral("network"),
                        QStringLiteral("download_failed"),
                        segment->reply->errorString(),
                        segment->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
                        static_cast<int>(segment->reply->error()));
        }
        // ensure buffer fully processed later
        if (!segment->processing && segment->buffer.size() > 0) processSegmentBuffer(segment);

        // Final drain after network close: flush any buffered bytes regardless of throttle.
        if (segment->file && !segment->buffer.isEmpty()) {
            while (!segment->buffer.isEmpty()) {
                QElapsedTimer writeTimer;
                writeTimer.start();
                const qint64 written = segment->file->write(segment->buffer.constData(), segment->buffer.size());
                sampleWriteLatency(writeTimer.elapsed());
                if (written <= 0) {
                    m_anyError = true;
                    recordError(QStringLiteral("disk"),
                                QStringLiteral("write_failed"),
                                QStringLiteral("Failed writing segment buffer"));
                    break;
                }
                segment->buffer.remove(0, written);
                segment->downloaded += written;
            }
            emit progress(totalDownloaded(), m_totalSize);
            updateSpeedAndETA();
        }

        if (segment->file) {
            segment->file->close();
            segment->file->deleteLater();
            segment->file = nullptr;
        }

        if (segment->reply) {
            replyPtr->deleteLater();
            segment->reply = nullptr;
        }

        if (m_state == State::Paused || m_state == State::Canceled)
            return;

        onSegmentFinished();
    });
}

void DownloaderTask::processSegmentBuffer(Segment* s)
{
    if (!s->file) return;
    if (s->processing) return;
    if (s->buffer.isEmpty()) return;
    if (m_state != State::Downloading) return;

    s->processing = true;

    qint64 elapsedMs = m_throttleTimer.elapsed();
    if (elapsedMs == 0) elapsedMs = 1;
    qint64 allowed = (m_maxSpeed > 0) ? (m_maxSpeed * elapsedMs / 1000 - m_throttleBytes) : s->buffer.size();

    if (allowed <= 0) {
        m_adaptiveThrottleHits = qMin(m_adaptiveThrottleHits + 1, 100);
        // schedule for later
        s->processing = false;
        QTimer::singleShot(50, this, [this, s]{ processSegmentBuffer(s); });
        return;
    }

    qint64 toWrite = qMin<qint64>(allowed, s->buffer.size());
    QElapsedTimer writeTimer;
    writeTimer.start();
    qint64 written = s->file->write(s->buffer.constData(), toWrite);
    sampleWriteLatency(writeTimer.elapsed());
    if (written > 0) {
        s->buffer.remove(0, written);
        s->downloaded += written;
        m_throttleBytes += written;
    } else if (written < 0) {
        recordError(QStringLiteral("disk"),
                    QStringLiteral("write_failed"),
                    QStringLiteral("Failed writing segment buffer"));
    }

    // reset throttle window if >= 1000 ms
    if (m_throttleTimer.elapsed() >= 1000) {
        m_throttleTimer.restart();
        m_throttleBytes = 0;
    }

    // update progress and speed/eta
    qint64 totalDownloadedBytes = totalDownloaded();
    emit progress(totalDownloadedBytes, m_totalSize);
    updateSpeedAndETA();
    evaluateAdaptiveSegments();

    s->processing = false;
    const bool hasPending = !s->buffer.isEmpty();
    if (hasPending) {
        QTimer::singleShot(10, this, [this, s]{ processSegmentBuffer(s); });
    }

    // Re-run dynamic balancing once buffered bytes are committed.
    rebalanceSegments();
}

void DownloaderTask::rebalanceSegments()
{
    if (m_state != State::Downloading) return;
    if (!m_useRange || !m_serverSupportsRange) return;
    if (!m_adaptiveSegmentsEnabled) return;
    if (m_anyError) return;
    if (m_segmentsInfo.size() < 2) return;

    constexpr int kMaxSegments = 32;
    const int desiredConnections = qBound(1, m_parallelTarget, kMaxSegments);

    int activeConnections = 0;
    for (const Segment& s : m_segmentsInfo) {
        if (s.reply) {
            ++activeConnections;
        }
    }
    int freeSlots = desiredConnections - activeConnections;
    if (freeSlots <= 0) return;

    while (freeSlots > 0) {
        if (!splitLargestRemainingSegment()) break;
        --freeSlots;
    }
}

bool DownloaderTask::splitLargestRemainingSegment()
{
    if (!m_adaptiveSegmentsEnabled) return false;
    constexpr qint64 kMinChunkBytes = 256 * 1024;
    constexpr int kMaxSegments = 32;
    if (m_segmentsInfo.size() >= kMaxSegments) return false;

    int donorIndex = -1;
    qint64 donorRemaining = 0;
    for (int i = 0; i < m_segmentsInfo.size(); ++i) {
        const Segment& s = m_segmentsInfo.at(i);
        if (!s.reply) continue;
        if (s.processing || !s.buffer.isEmpty()) continue;

        const qint64 total = qMax<qint64>(0, s.end - s.start + 1);
        const qint64 remaining = qMax<qint64>(0, total - s.downloaded);
        if (remaining < (kMinChunkBytes * 2)) continue;
        if (remaining > donorRemaining) {
            donorRemaining = remaining;
            donorIndex = i;
        }
    }

    if (donorIndex < 0) return false;

    Segment& donor = m_segmentsInfo[donorIndex];
    const qint64 nextOffset = donor.start + donor.downloaded;
    const qint64 oldEnd = donor.end;
    const qint64 remaining = oldEnd - nextOffset + 1;
    if (remaining < (kMinChunkBytes * 2)) return false;

    const qint64 firstHalf = remaining / 2;
    const qint64 donorNewEnd = nextOffset + firstHalf - 1;
    const qint64 splitStart = donorNewEnd + 1;
    if (splitStart > oldEnd || donorNewEnd < nextOffset) return false;

    if (donor.reply) {
        QPointer<QNetworkReply> donorReply = donor.reply;
        donor.reply = nullptr;
        if (donorReply) {
            QObject::disconnect(donorReply, nullptr, this, nullptr);
            donorReply->abort();
            if (donorReply) donorReply->deleteLater();
        }
    }
    if (donor.file) {
        donor.file->flush();
        donor.file->close();
        donor.file->deleteLater();
        donor.file = nullptr;
    }
    donor.end = donorNewEnd;

    Segment splitSegment;
    splitSegment.start = splitStart;
    splitSegment.end = oldEnd;
    splitSegment.downloaded = 0;
    splitSegment.processing = false;
    splitSegment.reply = nullptr;
    splitSegment.file = nullptr;
    splitSegment.buffer.clear();
    splitSegment.tempFilePath = QString("%1.part%2").arg(m_filePath).arg(m_segmentsInfo.size());
    QFile::remove(splitSegment.tempFilePath);

    m_segmentsInfo.push_back(splitSegment);
    m_effectiveSegments = m_segmentsInfo.size();

    appendLog(QStringLiteral("Dynamic split [%1-%2] + [%3-%4]")
                  .arg(donor.start)
                  .arg(donor.end)
                  .arg(splitSegment.start)
                  .arg(splitSegment.end));

    startSegment(&m_segmentsInfo[donorIndex]);
    startSegment(&m_segmentsInfo.last());
    return true;
}

void DownloaderTask::onSegmentFinished()
{
    if (m_state != State::Downloading)
        return;

    rebalanceSegments();

    for (const Segment& s : m_segmentsInfo) {
        if (s.downloaded < (s.end - s.start + 1))
            return;
    }

    if (m_anyError) {
        if (m_errorCode.isEmpty()) {
            recordError(QStringLiteral("download"),
                        QStringLiteral("segment_failed"),
                        QStringLiteral("Segment download failed"));
        }
        m_state = State::Finished;
        emit stateChanged();
        emit finished(false);
        return;
    }

    if (!mergeSegments()) {
        m_anyError = true;
        recordError(QStringLiteral("disk"),
                    QStringLiteral("merge_failed"),
                    QStringLiteral("Failed to merge segment files"));
        m_state = State::Finished;
        emit stateChanged();
        emit finished(false);
        return;
    }

    m_state = State::Finished;
    emit stateChanged();
    emit finished(true);
}

bool DownloaderTask::mergeSegments()
{
    QFile out(m_filePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        recordError(QStringLiteral("disk"),
                    QStringLiteral("merge_open_failed"),
                    QStringLiteral("Cannot open output file for merge"));
        return false;
    }

    QVector<const Segment*> ordered;
    ordered.reserve(m_segmentsInfo.size());
    for (const Segment& s : m_segmentsInfo) {
        ordered.push_back(&s);
    }
    std::sort(ordered.begin(), ordered.end(), [](const Segment* a, const Segment* b) {
        return a->start < b->start;
    });

    for (const Segment* seg : ordered) {
        if (!seg) continue;
        QFile part(seg->tempFilePath);
        if (!part.open(QIODevice::ReadOnly)) {
            recordError(QStringLiteral("disk"),
                        QStringLiteral("merge_part_missing"),
                        QStringLiteral("Cannot open segment part: %1").arg(seg->tempFilePath));
            out.close();
            return false;
        }
        QByteArray buffer;
        buffer.resize(1024 * 1024);
        while (!part.atEnd()) {
            qint64 readBytes = part.read(buffer.data(), buffer.size());
            if (readBytes <= 0) break;
            qint64 written = out.write(buffer.constData(), readBytes);
            if (written != readBytes) {
                recordError(QStringLiteral("disk"),
                            QStringLiteral("merge_write_failed"),
                            QStringLiteral("Failed while merging segments"));
                part.close();
                out.close();
                return false;
            }
        }
        part.close();
        part.remove();
    }
    out.close();
    const QFileInfo mergedInfo(m_filePath);
    if (m_totalSize > 0 && mergedInfo.exists() && mergedInfo.size() != m_totalSize) {
        recordError(QStringLiteral("disk"),
                    QStringLiteral("merge_size_mismatch"),
                    QStringLiteral("Merged file size mismatch (%1 != %2)")
                        .arg(mergedInfo.size())
                        .arg(m_totalSize));
        return false;
    }
    return true;
}

void DownloaderTask::pause()
{
    if (m_state != State::Downloading)
        return;

    qDebug() << "Pause requested for" << m_filePath;
    appendLog(QStringLiteral("Paused"));
    if (m_pauseReason.isEmpty()) {
        m_pauseReason = QStringLiteral("User");
        emit pauseReasonChanged();
    }
    m_state = State::Paused;
    emit stateChanged();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_pausedAt != nowMs) {
        m_pausedAt = nowMs;
        emit pausedAtChanged();
    }
    m_speed = 0;
    m_eta = -1;
    emit speedChanged(0);
    emit etaChanged(-1);

    for (Segment& s : m_segmentsInfo) {
        if (s.reply) {
            QPointer<QNetworkReply> segReply = s.reply;
            s.reply = nullptr;
            if (segReply) {
                QObject::disconnect(segReply, nullptr, this, nullptr);
                segReply->abort();
                if (segReply) segReply->deleteLater();
            }
        }
        if (s.file) {
            s.file->flush();
            s.file->close();
            s.file->deleteLater();
            s.file = nullptr;
        }
        s.buffer.clear();
        s.processing = false;
    }
    if (m_headReply) {
        QPointer<QNetworkReply> oldHead = m_headReply;
        m_headReply = nullptr;
        if (oldHead) {
            QObject::disconnect(oldHead, nullptr, this, nullptr);
            oldHead->abort();
            if (oldHead) oldHead->deleteLater();
        }
    }
    // For single-stream
    if (m_singleReply) {
        QPointer<QNetworkReply> oldReply = m_singleReply;
        m_singleReply = nullptr;
        if (oldReply) {
            QObject::disconnect(oldReply, nullptr, this, nullptr);
            oldReply->abort();
            if (oldReply) oldReply->deleteLater();
        }
    }
    if (m_singleFile) {
        m_singleFile->flush();
        m_singleFile->close();
        m_singleFile->deleteLater();
        m_singleFile = nullptr;
    }
    m_singleBuffer.clear();
    m_singleProcessing = false;

    // On some platforms, reusing a QNetworkAccessManager after aborting can cause subsequent
    // requests to stall. Resetting here makes resume/start reliable.
    resetNetworkManager();
}

void DownloaderTask::pauseWithReason(const QString& reason)
{
    if (m_state != State::Downloading)
        return;
    if (m_pauseReason != reason) {
        m_pauseReason = reason;
        emit pauseReasonChanged();
    }
    pause();
}

void DownloaderTask::markPaused()
{
    if (m_state == State::Paused)
        return;
    if (m_state == State::Downloading) {
        pause();
        return;
    }
    if (m_state == State::Finished || m_state == State::Canceled)
        return;

    m_state = State::Paused;
    emit stateChanged();
    if (m_pauseReason.isEmpty()) {
        m_pauseReason = QStringLiteral("User");
        emit pauseReasonChanged();
    }
    if (m_pausedAt == 0) {
        m_pausedAt = QDateTime::currentMSecsSinceEpoch();
        emit pausedAtChanged();
    }
    m_speed = 0;
    m_eta = -1;
    emit speedChanged(0);
    emit etaChanged(-1);
}

void DownloaderTask::pauseAfterFailure(const QString& reason)
{
    if (m_state == State::Canceled)
        return;
    if (m_state == State::Downloading) {
        pauseWithReason(reason);
        return;
    }

    if (m_pauseReason != reason) {
        m_pauseReason = reason;
        emit pauseReasonChanged();
    }
    if (m_pausedAt == 0) {
        m_pausedAt = QDateTime::currentMSecsSinceEpoch();
        emit pausedAtChanged();
    }
    m_speed = 0;
    m_eta = -1;
    emit speedChanged(0);
    emit etaChanged(-1);

    if (m_state != State::Paused) {
        m_state = State::Paused;
        emit stateChanged();
    }
}

void DownloaderTask::markError()
{
    if (m_state == State::Finished && m_anyError)
        return;
    if (m_state == State::Canceled)
        return;

    // This is used for session restore; we don't emit finished() here.
    m_anyError = true;
    if (m_errorCode.isEmpty()) {
        recordError(QStringLiteral("download"),
                    QStringLiteral("unknown"),
                    QStringLiteral("Download failed"));
    }
    m_state = State::Finished;
    emit stateChanged();
    m_speed = 0;
    m_eta = -1;
    emit speedChanged(0);
    emit etaChanged(-1);
    if (!m_pauseReason.isEmpty()) {
        m_pauseReason.clear();
        emit pauseReasonChanged();
    }
}

void DownloaderTask::markDone()
{
    if (m_state == State::Finished && !m_anyError)
        return;
    if (m_state == State::Canceled)
        return;
    m_anyError = false;
    clearErrorState();
    m_state = State::Finished;
    emit stateChanged();
    m_speed = 0;
    m_eta = -1;
    emit speedChanged(0);
    emit etaChanged(-1);
    if (!m_pauseReason.isEmpty()) {
        m_pauseReason.clear();
        emit pauseReasonChanged();
    }
}

void DownloaderTask::markCanceled()
{
    if (m_state == State::Canceled)
        return;
    m_state = State::Canceled;
    emit stateChanged();
    m_speed = 0;
    m_eta = -1;
    emit speedChanged(0);
    emit etaChanged(-1);
    if (!m_pauseReason.isEmpty()) {
        m_pauseReason.clear();
        emit pauseReasonChanged();
    }
}

void DownloaderTask::resume()
{
    if (m_state != State::Paused)
        return;

    qDebug() << "Resume requested for" << m_filePath;
    appendLog(QStringLiteral("Resumed"));
    // Resume should behave like a cold start (like app relaunch) but keep partial files.
    // This avoids subtle in-memory state getting out of sync with disk/network after pause.
    if (!m_pauseReason.isEmpty()) {
        m_pauseReason.clear();
        emit pauseReasonChanged();
    }
    m_state = State::Idle;
    start();
}

void DownloaderTask::recover()
{
    if (m_state == State::Canceled)
        return;
    if (m_state != State::Paused && !(m_state == State::Finished && m_anyError))
        return;

    appendLog(QStringLiteral("Recover requested"));

    for (Segment& s : m_segmentsInfo) {
        if (s.reply) {
            QPointer<QNetworkReply> segReply = s.reply;
            s.reply = nullptr;
            if (segReply) {
                QObject::disconnect(segReply, nullptr, this, nullptr);
                segReply->abort();
                if (segReply) segReply->deleteLater();
            }
        }
        if (s.file) {
            s.file->flush();
            s.file->close();
            s.file->deleteLater();
            s.file = nullptr;
        }
        s.buffer.clear();
        s.processing = false;
    }

    if (m_headReply) {
        QPointer<QNetworkReply> oldHead = m_headReply;
        m_headReply = nullptr;
        if (oldHead) {
            QObject::disconnect(oldHead, nullptr, this, nullptr);
            oldHead->abort();
            if (oldHead) oldHead->deleteLater();
        }
    }

    if (m_singleReply) {
        QPointer<QNetworkReply> oldReply = m_singleReply;
        m_singleReply = nullptr;
        if (oldReply) {
            QObject::disconnect(oldReply, nullptr, this, nullptr);
            oldReply->abort();
            if (oldReply) oldReply->deleteLater();
        }
    }

    if (m_singleFile) {
        m_singleFile->flush();
        m_singleFile->close();
        m_singleFile->deleteLater();
        m_singleFile = nullptr;
    }

    m_singleBuffer.clear();
    m_singleProcessing = false;
    m_singleWritten = 0;

    if (!m_pauseReason.isEmpty()) {
        m_pauseReason.clear();
        emit pauseReasonChanged();
    }
    if (m_pausedAt != 0) {
        m_pausedAt = 0;
        emit pausedAtChanged();
    }

    m_anyError = false;
    clearErrorState();
    resetNetworkManager();

    if (m_state != State::Idle) {
        m_state = State::Idle;
        emit stateChanged();
    }
    start();
}

void DownloaderTask::seedPersistedStats(qint64 lastSpeed, int lastEta, qint64 pausedAtMs, const QString& pauseReason)
{
    if (lastSpeed < 0) lastSpeed = 0;
    if (lastEta < -1) lastEta = -1;
    if (pausedAtMs < 0) pausedAtMs = 0;

    if (m_lastSpeed != lastSpeed) {
        m_lastSpeed = lastSpeed;
        emit lastSpeedChanged();
    }
    if (m_lastEta != lastEta) {
        m_lastEta = lastEta;
        emit lastEtaChanged();
    }
    if (m_pausedAt != pausedAtMs) {
        m_pausedAt = pausedAtMs;
        emit pausedAtChanged();
    }
    if (m_pauseReason != pauseReason) {
        m_pauseReason = pauseReason;
        emit pauseReasonChanged();
    }
}

void DownloaderTask::setResumeInfo(const QString& etag, const QString& lastModified)
{
    m_etag = etag;
    m_lastModified = lastModified;
}

void DownloaderTask::setFilePath(const QString& path)
{
    m_filePath = utils::normalizeFilePath(path);
    m_singleTempPath = m_filePath + ".part";
}

void DownloaderTask::setUrl(const QUrl& url)
{
    if (url.isValid()) {
        m_url = url;
    }
}

void DownloaderTask::cancel()
{
    if (m_state == State::Finished || m_state == State::Canceled)
        return;

    qDebug() << "Cancel requested for" << m_filePath;
    appendLog(QStringLiteral("Canceled"));
    m_state = State::Canceled;
    emit stateChanged();
    if (!m_pauseReason.isEmpty()) {
        m_pauseReason.clear();
        emit pauseReasonChanged();
    }
    if (m_pausedAt != 0) {
        m_pausedAt = 0;
        emit pausedAtChanged();
    }
    cleanup(true);
}

void DownloaderTask::updateSpeedAndETA() {
    if (!m_speedTimer.isValid()) {
        m_speedTimer.start();
        m_lastBytes = 0;
        m_speed = 0;
        m_eta = -1;
        emit speedChanged(0);
        emit etaChanged(-1);
        return;
    }

    qint64 elapsed = m_speedTimer.elapsed(); // ms
    if (elapsed < 500) return; // update twice/sec
    m_speedTimer.restart();

    qint64 totalDownloadedBytes = totalDownloaded();

    qint64 bytesDelta = totalDownloadedBytes - m_lastBytes;
    m_lastBytes = totalDownloadedBytes;

    qint64 speed = (bytesDelta * 1000) / (elapsed == 0 ? 1 : elapsed); // bytes/sec
    m_speed = speed;
    emit speedChanged(m_speed);
    appendSpeedSample(m_speed);
    if (speed > 0 && m_lastSpeed != speed) {
        m_lastSpeed = speed;
        emit lastSpeedChanged();
    }

    if (m_totalSize > 0 && speed > 0) {
        int etaSec = static_cast<int>((m_totalSize - totalDownloadedBytes) / speed);
        m_eta = etaSec;
        emit etaChanged(m_eta);
        if (m_lastEta != etaSec) {
            m_lastEta = etaSec;
            emit lastEtaChanged();
        }
    } else {
        m_eta = -1;
        emit etaChanged(m_eta);
    }
    evaluateAdaptiveSegments();
}

void DownloaderTask::restart()
{
    appendLog(QStringLiteral("Restart requested"));
    cleanup(false);
    m_state = State::Idle;
    emit stateChanged();
    start();
}

qint64 DownloaderTask::totalDownloaded() const
{
    qint64 total = 0;
    for (const Segment& s : m_segmentsInfo) total += s.downloaded;
    total += m_singleWritten;
    return total;
}

qint64 DownloaderTask::segmentTotal(int index) const
{
    if (index < 0) return 0;

    if (!m_segmentsInfo.isEmpty()) {
        if (index >= m_segmentsInfo.size()) return 0;
        const Segment& s = m_segmentsInfo.at(index);
        return qMax<qint64>(0, s.end - s.start + 1);
    }

    if (m_effectiveSegments <= 1 && index == 0) {
        return qMax<qint64>(0, m_totalSize);
    }
    return 0;
}

qint64 DownloaderTask::segmentDownloaded(int index) const
{
    if (index < 0) return 0;

    if (!m_segmentsInfo.isEmpty()) {
        if (index >= m_segmentsInfo.size()) return 0;
        const qint64 total = segmentTotal(index);
        return qMax<qint64>(0, qMin(total, m_segmentsInfo.at(index).downloaded));
    }

    if (m_effectiveSegments <= 1 && index == 0) {
        const qint64 total = segmentTotal(index);
        return qMax<qint64>(0, qMin(total > 0 ? total : totalDownloaded(), totalDownloaded()));
    }
    return 0;
}

bool DownloaderTask::segmentActive(int index) const
{
    if (index < 0 || m_state != State::Downloading) return false;

    if (!m_segmentsInfo.isEmpty()) {
        if (index >= m_segmentsInfo.size()) return false;
        const Segment& s = m_segmentsInfo.at(index);
        return s.reply || s.processing || !s.buffer.isEmpty();
    }

    if (m_effectiveSegments <= 1 && index == 0) {
        return m_singleReply || m_singleProcessing || !m_singleBuffer.isEmpty();
    }
    return false;
}

QString DownloaderTask::segmentState(int index) const
{
    if (index < 0) return QStringLiteral("Waiting");
    if (m_state == State::Canceled) return QStringLiteral("Canceled");
    if (m_anyError && m_state == State::Finished) return QStringLiteral("Error");

    const qint64 total = segmentTotal(index);
    const qint64 downloaded = segmentDownloaded(index);
    if (total > 0 && downloaded >= total) return QStringLiteral("Complete");

    if (m_state == State::Paused) {
        return downloaded > 0 ? QStringLiteral("Paused") : QStringLiteral("Waiting");
    }
    if (m_state == State::Idle) return QStringLiteral("Queued");
    if (m_state == State::Finished) return QStringLiteral("Complete");
    if (segmentActive(index)) return QStringLiteral("Receiving Data");
    return QStringLiteral("Waiting");
}

QString DownloaderTask::stateString() const
{
    if (m_anyError && m_state == State::Finished)
        return "Error";
    switch (m_state) {
    case State::Idle: return "Queued";
    case State::Downloading: return "Active";
    case State::Paused: return "Paused";
    case State::Finished: return "Done";
    case State::Canceled: return "Canceled";
    }
    return "Unknown";
}

void DownloaderTask::cleanup(bool emitFinished)
{
    for (Segment& s : m_segmentsInfo) {
        if (s.reply) {
            QPointer<QNetworkReply> segReply = s.reply;
            s.reply = nullptr;
            if (segReply) {
                QObject::disconnect(segReply, nullptr, this, nullptr);
                segReply->abort();
                if (segReply) segReply->deleteLater();
            }
        }
        if (s.file) {
            s.file->flush();
            s.file->close();
            s.file->deleteLater();
            s.file = nullptr;
        }
        s.buffer.clear();
        s.processing = false;
        s.downloaded = 0;
        QFile::remove(s.tempFilePath);
    }

    if (m_singleFile) {
        m_singleFile->flush();
        m_singleFile->close();
        m_singleFile->deleteLater();
        m_singleFile = nullptr;
    }
    if (m_singleReply) {
        QPointer<QNetworkReply> oldReply = m_singleReply;
        m_singleReply = nullptr;
        if (oldReply) {
            QObject::disconnect(oldReply, nullptr, this, nullptr);
            oldReply->abort();
            if (oldReply) oldReply->deleteLater();
        }
    }
    if (m_headReply) {
        QPointer<QNetworkReply> oldHead = m_headReply;
        m_headReply = nullptr;
        if (oldHead) {
            QObject::disconnect(oldHead, nullptr, this, nullptr);
            oldHead->abort();
            if (oldHead) oldHead->deleteLater();
        }
    }

    m_singleBuffer.clear();
    m_singleProcessing = false;
    m_singleWritten = 0;
    if (m_useSingleTemp && !m_singleTempPath.isEmpty()) {
        QFile::remove(m_singleTempPath);
    } else {
        QFile::remove(m_filePath);
    }

    resetNetworkManager();

    if (emitFinished) emit finished(false);
}
