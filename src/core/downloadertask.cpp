module;
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
    m_segments(qMax(1, segments))
{
    m_filePath = utils::normalizeFilePath(m_filePath);
    m_singleTempPath = m_filePath + ".part";
    m_checksumState = QStringLiteral("None");
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
    headReq.setRawHeader("User-Agent", "raad/1.0");
    applyNetworkOptions(headReq);
    if (m_headReply) {
        m_headReply->abort();
        m_headReply->deleteLater();
        m_headReply = nullptr;
    }

    QNetworkReply* headReply = m_manager->head(headReq);
    m_headReply = headReply;

    connect(headReply, &QNetworkReply::errorOccurred, this, [this, headReply](QNetworkReply::NetworkError err) {
        if (err == QNetworkReply::OperationCanceledError || m_state == State::Paused || m_state == State::Canceled)
            return;
        qWarning() << "HEAD error:" << headReply->errorString();
        appendLog(QStringLiteral("HEAD error: %1").arg(headReply->errorString()));
    });
#if QT_CONFIG(ssl)
    connect(headReply, &QNetworkReply::sslErrors, this, [](const QList<QSslError>& errors) {
        qWarning() << "HEAD SSL errors:" << errors;
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
            headReply->deleteLater();
            m_totalSize = 0;
            m_useRange = true;
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
        headReply->deleteLater();

        if (!cl.isValid() || cl.toLongLong() <= 0) {
            qDebug() << "No Content-Length → single stream";
            m_totalSize = 0;
            m_useRange = false;
            startSingleStream(false);
            return;
        }

        m_totalSize = cl.toLongLong();
        if (acceptRanges.toLower() != "bytes") {
            qDebug() << "Server does not support ranges";
            m_useRange = false;
            m_serverSupportsRange = false;
        } else {
            m_serverSupportsRange = true;
        }

        if (!m_useRange || m_segments == 1) {
            startSingleStream(hasExistingFile);
            return;
        }

        int segCount = m_segments;
        if (m_totalSize > 0) {
            if (m_totalSize < 4 * 1024 * 1024) segCount = 1;
            else if (m_totalSize < 32 * 1024 * 1024) segCount = qMin(2, m_segments);
            else if (m_totalSize < 128 * 1024 * 1024) segCount = qMin(4, m_segments);
        }

        // Prepare segments
        m_segmentsInfo.clear();
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
    QNetworkRequest req(currentUrl());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", "raad/1.0");

    if (m_singleReply) {
        m_singleReply->abort();
        m_singleReply->deleteLater();
        m_singleReply = nullptr;
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

    m_singleFile = new QFile(m_singleTempPath);
    QIODevice::OpenMode mode = QIODevice::WriteOnly | (m_resumeSingle ? QIODevice::Append : QIODevice::Truncate);
    if (!m_singleFile->open(mode)) {
        qWarning() << "Cannot open output file" << m_singleTempPath;
        delete m_singleFile; m_singleFile = nullptr;
        m_anyError = true;
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
        } else if (status == 200) {
            m_serverSupportsRange = false;
        }
        if (!m_resumeSingle) return;
        if (status >= 400) {
            if (m_singleReply) {
                m_singleReply->abort();
                m_singleReply->deleteLater();
                m_singleReply = nullptr;
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
    });

    connect(reply, &QNetworkReply::errorOccurred, this, [this, reply](QNetworkReply::NetworkError err) {
        if (err == QNetworkReply::OperationCanceledError || m_state == State::Paused || m_state == State::Canceled)
            return;
        qWarning() << "GET error:" << reply->errorString();
        appendLog(QStringLiteral("GET error: %1").arg(reply->errorString()));
    });
#if QT_CONFIG(ssl)
    connect(reply, &QNetworkReply::sslErrors, this, [](const QList<QSslError>& errors) {
        qWarning() << "GET SSL errors:" << errors;
    });
#endif

    connect(reply, &QNetworkReply::readyRead, this, [this, replyPtr]() mutable {
        if (!replyPtr || replyPtr != m_singleReply) return;
        QByteArray data = replyPtr->readAll();
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
        }

        // ensure buffer fully processed
        if (!m_singleProcessing && m_singleBuffer.size() > 0) processSingleBuffer();

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
        // schedule later
        QTimer::singleShot(50, this, [this]{ m_singleProcessing = false; processSingleBuffer(); });
        return;
    }

    qint64 toWrite = qMin<qint64>(allowed, m_singleBuffer.size());
    qint64 written = m_singleFile->write(m_singleBuffer.constData(), toWrite);
    if (written > 0) {
        m_singleBuffer.remove(0, written);
        m_throttleBytes += written;
        m_singleWritten += written;
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
    req.setRawHeader("User-Agent", "raad/1.0");
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
        const QByteArray etag = replyPtr->rawHeader("ETag");
        if (!etag.isEmpty()) {
            m_etag = QString::fromUtf8(etag);
        }
        const QByteArray lastMod = replyPtr->rawHeader("Last-Modified");
        if (!lastMod.isEmpty()) {
            m_lastModified = QString::fromUtf8(lastMod);
        }
        if (status == 0) return; // not available yet
        if (status == 206) return;

        // If we're doing multi-segment, a non-206 usually means the server ignored Range.
        const bool isWholeFileRange = (segment->start == 0 && m_totalSize > 0 && segment->end == m_totalSize - 1);
        if (status == 200 && (m_segmentsInfo.size() <= 1 || isWholeFileRange)) {
            return; // treat as a valid single-range download
        }

        if (status == 200) {
            qWarning() << "SEGMENT GET returned 200 (Range ignored), falling back to single stream";
            m_useRange = false;
            m_serverSupportsRange = false;
            setResumeWarning(QStringLiteral("Range ignored; switched to single stream"));
            appendLog(QStringLiteral("Range ignored; switched to single stream"));
            QTimer::singleShot(0, this, [this] {
                if (m_state != State::Downloading) return;
                cleanup(false);
                startSingleStream(false);
            });
            return;
        }

        if (status >= 400) {
            qWarning() << "SEGMENT GET HTTP error status" << status;
        }
    });

    connect(reply, &QNetworkReply::errorOccurred, this, [this, reply](QNetworkReply::NetworkError err) {
        if (err == QNetworkReply::OperationCanceledError || m_state == State::Paused || m_state == State::Canceled)
            return;
        qWarning() << "SEGMENT GET error:" << reply->errorString();
        appendLog(QStringLiteral("SEGMENT error: %1").arg(reply->errorString()));
    });
#if QT_CONFIG(ssl)
    connect(reply, &QNetworkReply::sslErrors, this, [](const QList<QSslError>& errors) {
        qWarning() << "SEGMENT GET SSL errors:" << errors;
    });
#endif

    connect(reply, &QNetworkReply::readyRead, this, [this, segment, replyPtr]() mutable {
        if (!replyPtr || replyPtr != segment->reply) return;
        QByteArray data = replyPtr->readAll();
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
        }
        // ensure buffer fully processed later
        if (!segment->processing && segment->buffer.size() > 0) processSegmentBuffer(segment);

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
        // schedule for later
        s->processing = false;
        QTimer::singleShot(50, this, [this, s]{ processSegmentBuffer(s); });
        return;
    }

    qint64 toWrite = qMin<qint64>(allowed, s->buffer.size());
    qint64 written = s->file->write(s->buffer.constData(), toWrite);
    if (written > 0) {
        s->buffer.remove(0, written);
        s->downloaded += written;
        m_throttleBytes += written;
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

    s->processing = false;

    if (!s->buffer.isEmpty()) {
        QTimer::singleShot(10, this, [this, s]{ processSegmentBuffer(s); });
    }
}

void DownloaderTask::onSegmentFinished()
{
    if (m_state != State::Downloading)
        return;

    for (const Segment& s : m_segmentsInfo) {
        if (s.downloaded < (s.end - s.start + 1))
            return;
    }

    if (m_anyError) {
        m_state = State::Finished;
        emit stateChanged();
        emit finished(false);
        return;
    }

    if (!mergeSegments()) {
        m_anyError = true;
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
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    for (const Segment& s : m_segmentsInfo) {
        QFile part(s.tempFilePath);
        if (!part.open(QIODevice::ReadOnly)) {
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
                part.close();
                out.close();
                return false;
            }
        }
        part.close();
        part.remove();
    }
    out.close();
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
            QObject::disconnect(s.reply, nullptr, this, nullptr);
            s.reply->abort();
            s.reply->deleteLater();
            s.reply = nullptr;
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
        QObject::disconnect(m_headReply, nullptr, this, nullptr);
        m_headReply->abort();
        m_headReply->deleteLater();
        m_headReply = nullptr;
    }
    // For single-stream
    if (m_singleReply) {
        QObject::disconnect(m_singleReply, nullptr, this, nullptr);
        m_singleReply->abort();
        m_singleReply->deleteLater();
        m_singleReply = nullptr;
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

void DownloaderTask::markError()
{
    if (m_state == State::Finished && m_anyError)
        return;
    if (m_state == State::Canceled)
        return;

    // This is used for session restore; we don't emit finished() here.
    m_anyError = true;
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
            QObject::disconnect(s.reply, nullptr, this, nullptr);
            s.reply->abort();
            s.reply->deleteLater();
            s.reply = nullptr;
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
        QObject::disconnect(m_singleReply, nullptr, this, nullptr);
        m_singleReply->abort();
        m_singleReply->deleteLater();
        m_singleReply = nullptr;
    }
    if (m_headReply) {
        QObject::disconnect(m_headReply, nullptr, this, nullptr);
        m_headReply->abort();
        m_headReply->deleteLater();
        m_headReply = nullptr;
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
