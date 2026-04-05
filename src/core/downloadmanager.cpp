module;
#include <limits>
#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTime>
#include <QtGlobal>
#include <QThread>
#include <QClipboard>
#include <QGuiApplication>
#include <QPointer>
#include <QTextStream>
#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QNetworkInformation>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QSslError>
#include <QByteArrayView>
#include <QRandomGenerator>
#include <QtConcurrent>
#include <QStorageInfo>

#if defined(Q_OS_MACOS)
#include <mach/mach.h>
#include <sys/resource.h>
#elif defined(Q_OS_LINUX)
#include <sys/resource.h>
#endif

module raad.core.downloadmanager;

import raad.utils.download_utils;
import raad.utils.category_utils;

namespace utils = raad::utils;

namespace {

QString defaultDownloadsFolderPath()
{
    QString root = utils::normalizeFilePath(
        QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
            .filePath(QStringLiteral("Raad")));
    if (root.isEmpty()) {
        root = utils::normalizeFilePath(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    }
    if (!root.isEmpty()) {
        QDir().mkpath(root);
    }
    return root;
}

int normalizedSegmentCount(int value)
{
    constexpr int kAllowedSegments[] = {1, 2, 4, 8, 16, 32};

    int best = kAllowedSegments[0];
    int bestDiff = qAbs(value - best);
    for (int candidate : kAllowedSegments) {
        const int diff = qAbs(value - candidate);
        if (diff < bestDiff) {
            bestDiff = diff;
            best = candidate;
        }
    }
    return best;
}

int segmentCountFromOptions(const QVariantMap* options)
{
    constexpr int kDefaultSegments = 8;
    if (!options || !options->contains(QStringLiteral("segments"))) {
        return kDefaultSegments;
    }

    bool ok = false;
    const int requested = options->value(QStringLiteral("segments")).toInt(&ok);
    return ok ? normalizedSegmentCount(requested) : kDefaultSegments;
}

qint64 currentProcessCpuTimeNs()
{
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    struct rusage usage;
    if (::getrusage(RUSAGE_SELF, &usage) == 0) {
        const qint64 userNs = static_cast<qint64>(usage.ru_utime.tv_sec) * 1000000000LL
            + static_cast<qint64>(usage.ru_utime.tv_usec) * 1000LL;
        const qint64 sysNs = static_cast<qint64>(usage.ru_stime.tv_sec) * 1000000000LL
            + static_cast<qint64>(usage.ru_stime.tv_usec) * 1000LL;
        return userNs + sysNs;
    }
#endif
    return 0;
}

qint64 currentProcessResidentBytes()
{
#if defined(Q_OS_MACOS)
    mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        return static_cast<qint64>(info.resident_size);
    }
    return 0;
#elif defined(Q_OS_LINUX)
    QFile status(QStringLiteral("/proc/self/status"));
    if (status.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!status.atEnd()) {
            const QByteArray line = status.readLine();
            if (line.startsWith("VmRSS:")) {
                const QList<QByteArray> parts = line.simplified().split(' ');
                if (parts.size() >= 2) {
                    bool ok = false;
                    const qint64 kb = parts.at(1).toLongLong(&ok);
                    if (ok) return kb * 1024LL;
                }
                break;
            }
        }
    }
    return 0;
#else
    return 0;
#endif
}

} // namespace

DownloadManager::DownloadManager(QObject* parent) : QObject(parent) {
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &DownloadManager::saveSession);
    if (auto* app = QCoreApplication::instance()) {
        connect(app, &QCoreApplication::aboutToQuit, this, &DownloadManager::saveSession);
    }

    m_schedulerTimer.setInterval(60000);
    connect(&m_schedulerTimer, &QTimer::timeout, this, &DownloadManager::schedulerTick);
    m_schedulerTimer.start();

    m_powerTimer.setInterval(60000);
    connect(&m_powerTimer, &QTimer::timeout, this, &DownloadManager::updatePowerState);
    m_powerTimer.start();

    m_runtimeStatsClock.start();
    m_lastProcessCpuTimeNs = currentProcessCpuTimeNs();
    m_runtimeStatsTimer.setInterval(1500);
    connect(&m_runtimeStatsTimer, &QTimer::timeout, this, &DownloadManager::updateRuntimeStats);
    m_runtimeStatsTimer.start();

    const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!baseDir.isEmpty()) {
        QDir().mkpath(baseDir);
        m_sessionPath = baseDir + "/downloads.json";
        m_sessionBackupPath = baseDir + "/downloads.json.bak";
        m_telemetryPath = baseDir + "/telemetry.ndjson";
    }

    ensureDefaultQueue();
    loadSession();
    schedulerTick();
    updatePowerState();
    updateRuntimeStats();

    QNetworkInformation::loadDefaultBackend();
    if (QNetworkInformation* info = QNetworkInformation::instance()) {
        connect(info, &QNetworkInformation::reachabilityChanged, this, [this]() {
            handleNetworkReachabilityChanged();
        });
        handleNetworkReachabilityChanged();
    }
}

void DownloadManager::updateRuntimeStats()
{
    const qint64 currentCpuNs = currentProcessCpuTimeNs();
    const qint64 wallNs = m_runtimeStatsClock.isValid() ? m_runtimeStatsClock.restart() * 1000000LL : 0LL;

    qreal nextCpuLoad = m_processCpuLoad;
    if (wallNs > 0 && currentCpuNs > 0 && m_lastProcessCpuTimeNs > 0) {
        const qint64 cpuDeltaNs = qMax<qint64>(0, currentCpuNs - m_lastProcessCpuTimeNs);
        const int cores = qMax(1, QThread::idealThreadCount());
        nextCpuLoad = qBound<qreal>(0.0,
                                    (static_cast<qreal>(cpuDeltaNs) / static_cast<qreal>(wallNs * cores)) * 100.0,
                                    100.0);
    }
    m_lastProcessCpuTimeNs = currentCpuNs;

    const qint64 nextMemoryBytes = currentProcessResidentBytes();
    QString nextReachability = QStringLiteral("Unknown");
    if (QNetworkInformation* info = QNetworkInformation::instance()) {
        switch (info->reachability()) {
        case QNetworkInformation::Reachability::Online:
            nextReachability = QStringLiteral("Online");
            break;
        case QNetworkInformation::Reachability::Local:
            nextReachability = QStringLiteral("Local");
            break;
        case QNetworkInformation::Reachability::Site:
            nextReachability = QStringLiteral("Limited");
            break;
        case QNetworkInformation::Reachability::Disconnected:
            nextReachability = QStringLiteral("Offline");
            break;
        case QNetworkInformation::Reachability::Unknown:
        default:
            nextReachability = QStringLiteral("Unknown");
            break;
        }
    }

    qint64 nextDiskFreeBytes = 0;
    QString storagePath = defaultDownloadsFolderPath();
    if (storagePath.isEmpty()) {
        storagePath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    }
    if (!storagePath.isEmpty()) {
        QStorageInfo storage(storagePath);
        if (storage.isValid() && storage.isReady()) {
            nextDiskFreeBytes = storage.bytesAvailable();
        }
    }

    qreal nextAverageSegments = 0.0;
    int activeSegmentTasks = 0;
    qreal segmentSum = 0.0;
    for (DownloaderTask* task : m_queue) {
        if (!task || task->stateString() != QStringLiteral("Active")) {
            continue;
        }
        segmentSum += task->effectiveSegments();
        ++activeSegmentTasks;
    }
    if (activeSegmentTasks > 0) {
        nextAverageSegments = segmentSum / static_cast<qreal>(activeSegmentTasks);
    }

    const bool cpuChanged = qAbs(nextCpuLoad - m_processCpuLoad) >= 0.5;
    const bool memoryChanged = qAbs(nextMemoryBytes - m_processMemoryBytes) >= (256 * 1024);
    const bool diskChanged = qAbs(nextDiskFreeBytes - m_diskFreeBytes) >= (16 * 1024 * 1024);
    const bool reachabilityChanged = nextReachability != m_networkReachability;
    const bool segmentsChanged = qAbs(nextAverageSegments - m_averageActiveSegments) >= 0.1;

    if (!cpuChanged && !memoryChanged && !diskChanged && !reachabilityChanged && !segmentsChanged) {
        return;
    }

    m_processCpuLoad = nextCpuLoad;
    m_processMemoryBytes = nextMemoryBytes;
    m_diskFreeBytes = nextDiskFreeBytes;
    m_networkReachability = nextReachability;
    m_averageActiveSegments = nextAverageSegments;
    emit runtimeStatsChanged();
}

void DownloadManager::setNetworkTestState(bool running, const QString& message, const QString& kind)
{
    bool changed = false;
    if (m_networkTestRunning != running) {
        m_networkTestRunning = running;
        changed = true;
    }
    if (m_networkTestMessage != message) {
        m_networkTestMessage = message;
        changed = true;
    }
    if (m_networkTestKind != kind) {
        m_networkTestKind = kind;
        changed = true;
    }
    if (changed) {
        emit networkTestStateChanged();
    }
}

QString DownloadManager::taskHost(DownloaderTask* task) const
{
    if (!task) return QString();
    const QUrl url(task->url());
    return utils::normalizeHost(url.host());
}

bool DownloadManager::isRetryableFailure(DownloaderTask* task) const
{
    if (!task) return false;
    const int status = task->lastHttpStatus();
    const int netErr = task->lastNetworkError();

    if (status == 408 || status == 409 || status == 425 || status == 429) return true;
    if (status >= 500 && status <= 599) return true;
    if (status >= 400 && status <= 499) return false;

    if (netErr < 0) return true;
    const auto err = static_cast<QNetworkReply::NetworkError>(netErr);
    switch (err) {
    case QNetworkReply::AuthenticationRequiredError:
    case QNetworkReply::ContentAccessDenied:
    case QNetworkReply::ContentNotFoundError:
    case QNetworkReply::ProtocolFailure:
    case QNetworkReply::ProtocolInvalidOperationError:
        return false;
    default:
        return true;
    }
}

int DownloadManager::nextRetryDelayMs(DownloaderTask* task, int attempt) const
{
    const int baseSec = (task && task->retryDelaySec() >= 0) ? task->retryDelaySec() : m_autoRetryDelaySec;
    const int safeBaseSec = qBound(1, baseSec, 120);
    const int cappedAttempt = qBound(0, attempt, 8);
    qint64 delayMs = static_cast<qint64>(safeBaseSec) * 1000;
    delayMs *= (1ll << cappedAttempt);
    const int jitterMs = QRandomGenerator::global()->bounded(safeBaseSec * 350 + 1);
    delayMs += jitterMs;
    return static_cast<int>(qBound<qint64>(500, delayMs, 10ll * 60 * 1000));
}

bool DownloadManager::hostCooldownAllowsStart(const QString& host, qint64 nowMs) const
{
    if (host.isEmpty()) return true;
    return m_hostCooldownUntilMs.value(host, 0) <= nowMs;
}

bool DownloadManager::isConnectivityFailure(DownloaderTask* task) const
{
    if (!task) return false;
    const auto err = static_cast<QNetworkReply::NetworkError>(task->lastNetworkError());
    switch (err) {
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::ProxyConnectionRefusedError:
    case QNetworkReply::ProxyTimeoutError:
    case QNetworkReply::UnknownNetworkError:
        return true;
    default:
        break;
    }

    const int status = task->lastHttpStatus();
    return status == 408 || status == 429 || status == 503 || status == 504;
}

QString DownloadManager::pauseReasonForFailure(DownloaderTask* task) const
{
    if (!task) return QStringLiteral("Network");

    const auto err = static_cast<QNetworkReply::NetworkError>(task->lastNetworkError());
    switch (err) {
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
        return QStringLiteral("Network changed");
    case QNetworkReply::TimeoutError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::ProxyConnectionRefusedError:
    case QNetworkReply::ProxyTimeoutError:
    case QNetworkReply::UnknownNetworkError:
        return QStringLiteral("Network offline");
    default:
        break;
    }

    const int status = task->lastHttpStatus();
    if (status == 429 || status == 503 || status == 504) {
        return QStringLiteral("Server busy");
    }
    return QStringLiteral("Network");
}

void DownloadManager::handleNetworkReachabilityChanged()
{
    QNetworkInformation* info = QNetworkInformation::instance();
    if (!info) return;

    const auto reachability = info->reachability();
    const bool online = reachability == QNetworkInformation::Reachability::Online;

    for (DownloaderTask* task : m_queue) {
        if (!task) continue;

        if (!online && task->isRunning()) {
            task->pauseWithReason(QStringLiteral("Network offline"));
            m_taskPausedByNetwork[task] = true;
            continue;
        }

        if (online && m_taskPausedByNetwork.value(task, false) && task->stateString() == "Paused") {
            m_taskPausedByNetwork[task] = false;
            task->recover();
        }
    }

    if (online) {
        startQueued();
    }
    scheduleSave();
}

void DownloadManager::writeTelemetryEvent(const QString& name, const QVariantMap& payload)
{
    QVariantMap event = payload;
    event.insert(QStringLiteral("name"), name);
    event.insert(QStringLiteral("ts"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    emit backendEvent(name, event);
    if (!m_telemetryEnabled || m_telemetryPath.isEmpty()) return;

    QFile file(m_telemetryPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
    QJsonObject obj = QJsonObject::fromVariantMap(event);
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    file.write("\n");
    file.close();
}

QJsonDocument DownloadManager::loadSessionDocument() const
{
    auto parsePath = [](const QString& path) -> QJsonDocument {
        if (path.isEmpty()) return QJsonDocument();
        QFile file(path);
        if (!file.exists()) return QJsonDocument();
        if (!file.open(QIODevice::ReadOnly)) return QJsonDocument();
        const QByteArray raw = file.readAll();
        file.close();
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
        if (err.error != QJsonParseError::NoError) return QJsonDocument();
        return doc;
    };

    QJsonDocument doc = parsePath(m_sessionPath);
    if (doc.isObject()) return doc;
    doc = parsePath(m_sessionBackupPath);
    return doc;
}

void DownloadManager::addDownload(const QString &urlStr, const QString &filePath) {
    addDownloadAdvancedWithOptions(urlStr, filePath, defaultQueueName(), QString(), false);
}

void DownloadManager::addDownloadAdvanced(const QString &urlStr,
                                          const QString &filePath,
                                          const QString &queueName,
                                          const QString &category)
{
    addDownloadAdvancedWithOptions(urlStr, filePath, queueName, category, false);
}

void DownloadManager::addDownloadAdvancedWithOptions(const QString &urlStr,
                                                     const QString &filePath,
                                                     const QString &queueName,
                                                     const QString &category,
                                                     bool startPaused)
{
    addDownloadInternal(urlStr, filePath, queueName, category, startPaused, nullptr);
}

void DownloadManager::addDownloadAdvancedWithExtras(const QString &urlStr,
                                                    const QString &filePath,
                                                    const QString &queueName,
                                                    const QString &category,
                                                    bool startPaused,
                                                    const QVariantMap& options)
{
    addDownloadInternal(urlStr, filePath, queueName, category, startPaused, &options);
}

DownloaderTask* DownloadManager::addDownloadInternal(const QString &urlStr,
                                                     const QString &filePath,
                                                     const QString &queueName,
                                                     const QString &category,
                                                     bool startPaused,
                                                     const QVariantMap* options)
{
    QUrl url(urlStr);
    if (!url.isValid()) {
        qWarning() << "Invalid URL:" << urlStr;
        return nullptr;
    }

    QString resolvedQueue = queueName.isEmpty() ? defaultQueueName() : queueName;
    const QString host = utils::normalizeHost(url.host());
    if (!host.isEmpty() && (queueName.isEmpty() || resolvedQueue == defaultQueueName())) {
        const QString ruleQueue = m_domainRules.value(host);
        if (!ruleQueue.isEmpty()) {
            resolvedQueue = ruleQueue;
        }
    }
    if (!m_queues.contains(resolvedQueue)) {
        createQueue(resolvedQueue);
    }

    QString normalizedPath = utils::normalizeFilePath(filePath);
    const QString inferredUrlName = utils::fileNameFromUrl(url);
    QString resolvedCategory = category;
    if (resolvedCategory.isEmpty() || resolvedCategory == "Auto") {
        if (!inferredUrlName.isEmpty()) {
            resolvedCategory = utils::toString(utils::detectCategory(inferredUrlName));
        }
        if (resolvedCategory.isEmpty() || resolvedCategory == "Other") {
            resolvedCategory = normalizedPath.isEmpty()
                ? QStringLiteral("Auto")
                : utils::toString(utils::detectCategory(normalizedPath));
        }
    }

    if (normalizedPath.isEmpty() || QFileInfo(normalizedPath).isDir()) {
        const QString fallback = normalizedPath;
        normalizedPath = resolveDownloadPath(urlStr, resolvedCategory, fallback);
    }

    if (resolvedCategory == "Auto" && !normalizedPath.isEmpty()) {
        resolvedCategory = utils::toString(utils::detectCategory(normalizedPath));
    }

    if (!normalizedPath.isEmpty()) {
        QFileInfo info(normalizedPath);
        if (!inferredUrlName.isEmpty() && utils::looksLikeGuidName(info.fileName())) {
            normalizedPath = info.dir().filePath(inferredUrlName);
        }
    }
    if ((resolvedCategory.isEmpty() || resolvedCategory == "Auto" || resolvedCategory == "Other") && !normalizedPath.isEmpty()) {
        const QString inferredPathCategory = utils::toString(utils::detectCategory(normalizedPath));
        if (!inferredPathCategory.isEmpty() && inferredPathCategory != "Other") {
            resolvedCategory = inferredPathCategory;
        }
    }
    if (resolvedCategory.isEmpty() || resolvedCategory == "Auto") {
        resolvedCategory = QStringLiteral("Other");
    }
    if (!resolvedCategory.isEmpty() && resolvedCategory != "Auto") {
        const QString folder = categoryFolderForName(resolvedCategory);
        if (!folder.isEmpty()) {
            QFileInfo info(normalizedPath);
            normalizedPath = QDir(folder).filePath(info.fileName());
        }
    }
    normalizedPath = utils::uniqueFilePath(normalizedPath);
    if (!normalizedPath.isEmpty()) {
        QDir().mkpath(QFileInfo(normalizedPath).absolutePath());
    }

    const int segments = segmentCountFromOptions(options);
    DownloaderTask* task = createTask(url, normalizedPath, resolvedQueue, resolvedCategory, segments);
    if (task && options) {
        applyTaskOptions(task, *options);
        m_taskPriority[task] = task->priority();
    }
    if (startPaused && task) {
        task->markPaused();
    }
    startQueued();
    scheduleSave();
    return task;
}

void DownloadManager::applyTaskOptions(DownloaderTask* task, const QVariantMap& options)
{
    if (!task) return;

    QStringList mirrors = options.value("mirrors").toStringList();
    if (mirrors.isEmpty()) {
        const QString mirrorText = options.value("mirrors").toString().trimmed();
        if (!mirrorText.isEmpty()) {
            mirrors = mirrorText.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        }
    }
    if (!mirrors.contains(task->url())) {
        mirrors.prepend(task->url());
    }
    if (!mirrors.isEmpty()) task->setMirrorUrls(mirrors);

    const QString checksumAlgo = options.value("checksumAlgo").toString();
    if (!checksumAlgo.isEmpty()) task->setChecksumAlgorithm(checksumAlgo);
    const QString checksumExpected = options.value("checksumExpected").toString();
    if (!checksumExpected.isEmpty()) task->setChecksumExpected(checksumExpected);
    if (options.contains("verifyOnComplete")) task->setVerifyOnComplete(options.value("verifyOnComplete").toBool());

    QStringList headers = options.value("headers").toStringList();
    if (headers.isEmpty()) {
        const QString headerText = options.value("headers").toString().trimmed();
        if (!headerText.isEmpty()) {
            headers = headerText.split("\n", Qt::SkipEmptyParts);
        }
    }
    if (!headers.isEmpty()) task->setCustomHeaders(headers);
    const QString userAgent = options.contains("userAgent")
        ? options.value("userAgent").toString().trimmed()
        : m_defaultUserAgent;
    task->setUserAgent(userAgent);
    bool allowInsecureSsl = m_defaultAllowInsecureSsl;
    if (options.contains("allowInsecureSsl")) {
        allowInsecureSsl = options.value("allowInsecureSsl").toBool();
    } else if (options.contains("ignoreSslErrors")) {
        allowInsecureSsl = options.value("ignoreSslErrors").toBool();
    }
    task->setAllowInsecureSsl(allowInsecureSsl);
    const QString cookieHeader = options.value("cookieHeader").toString();
    if (!cookieHeader.isEmpty()) task->setCookieHeader(cookieHeader);
    const QString authUser = options.value("authUser").toString();
    const QString authPassword = options.value("authPassword").toString();
    if (!authUser.isEmpty()) task->setAuthUser(authUser);
    if (!authPassword.isEmpty()) task->setAuthPassword(authPassword);
    const QString proxyHost = options.contains("proxyHost")
        ? options.value("proxyHost").toString().trimmed()
        : m_defaultProxyHost;
    const int proxyPortRaw = options.contains("proxyPort")
        ? options.value("proxyPort").toInt(0)
        : m_defaultProxyPort;
    const int proxyPort = qBound(0, proxyPortRaw, 65535);
    const QString proxyUser = options.contains("proxyUser")
        ? options.value("proxyUser").toString()
        : m_defaultProxyUser;
    const QString proxyPassword = options.contains("proxyPassword")
        ? options.value("proxyPassword").toString()
        : m_defaultProxyPassword;
    task->setProxyHost(proxyHost);
    task->setProxyPort(proxyPort);
    task->setProxyUser(proxyUser);
    task->setProxyPassword(proxyPassword);

    if (options.contains("retryMax")) {
        bool ok = false;
        const int value = options.value("retryMax").toInt(&ok);
        task->setRetryMax(ok ? value : -1);
    }
    if (options.contains("retryDelaySec")) {
        bool ok = false;
        const int value = options.value("retryDelaySec").toInt(&ok);
        task->setRetryDelaySec(ok ? value : -1);
    }

    if (options.contains("priority")) {
        bool ok = false;
        const int value = options.value("priority").toInt(&ok);
        if (ok) task->setPriority(qBound(0, value, 1000));
    }
    if (options.contains("adaptiveSegments")) {
        task->setAdaptiveSegmentsEnabled(options.value("adaptiveSegments").toBool());
    }

    if (options.contains("postOpenFile")) task->setPostOpenFile(options.value("postOpenFile").toBool());
    if (options.contains("postRevealFolder")) task->setPostRevealFolder(options.value("postRevealFolder").toBool());
    if (options.contains("postExtract")) task->setPostExtract(options.value("postExtract").toBool());
    const QString postScript = options.value("postScript").toString();
    if (!postScript.isEmpty()) task->setPostScript(postScript);
}

void DownloadManager::onTaskFinishedWrapper(bool success) {
    Q_UNUSED(success)
    // find which task finished
    DownloaderTask* t = qobject_cast<DownloaderTask*>(sender());
    if (!t) return;

    if (m_bulkCancelInProgress) {
        // `cancelAll()` handles container cleanup in one shot; avoid re-entrancy crashes.
        return;
    }

    m_taskSpeed[t] = 0;
    m_taskCompletedAt[t] = QDateTime::currentMSecsSinceEpoch();

    const QString state = t->stateString();
    const QString name = QFileInfo(t->fileName()).fileName();
    writeTelemetryEvent(QStringLiteral("task_finished"),
                        {
                            {QStringLiteral("name"), name},
                            {QStringLiteral("state"), state},
                            {QStringLiteral("url"), t->url()},
                            {QStringLiteral("errorCode"), t->errorCode()},
                            {QStringLiteral("errorCategory"), t->errorCategory()},
                            {QStringLiteral("httpStatus"), t->lastHttpStatus()},
                            {QStringLiteral("networkError"), t->lastNetworkError()}
                        });

    if (state == "Done") {
        // Ensure final progress metadata is consistent for completed tasks.
        qint64 finalReceived = qMax(m_taskReceived.value(t, 0), m_taskTotal.value(t, 0));
        qint64 finalTotal = qMax(m_taskTotal.value(t, 0), finalReceived);
        const QString normalized = utils::normalizeFilePath(t->fileName());
        const QFileInfo info(normalized);
        if (info.exists() && info.isFile()) {
            const qint64 actualSize = qMax<qint64>(0, info.size());
            if (actualSize > 0) {
                finalReceived = actualSize;
                finalTotal = actualSize;
            }
        } else if (finalReceived <= 0) {
            finalReceived = 0;
        }
        m_taskReceived[t] = finalReceived;
        m_taskLastReceived[t] = finalReceived;
        m_taskTotal[t] = finalTotal;
        m_model.seedProgress(t, finalReceived, finalTotal);

        m_taskRetryCount[t] = 0;
        emit toastRequested(QStringLiteral("Download finished: %1").arg(name), QStringLiteral("success"));
        applyPostActions(t);
        if (t->verifyOnComplete() || !t->checksumExpected().isEmpty()) {
            verifyChecksumAsync(t);
        }
    } else if (state == "Error") {
        emit toastRequested(QStringLiteral("Download failed: %1").arg(name), QStringLiteral("danger"));
    } else if (state == "Canceled") {
        emit toastRequested(QStringLiteral("Download canceled: %1").arg(name), QStringLiteral("muted"));
    }

    if (state == "Error") {
        if (t->advanceMirror()) {
            const QString newUrl = t->url();
            emit toastRequested(QStringLiteral("Switching mirror: %1").arg(newUrl), QStringLiteral("warning"));
            t->recover();
            startQueued();
        } else {
            const int maxRetries = t->retryMax() >= 0 ? t->retryMax() : m_autoRetryMax;
            int attempts = m_taskRetryCount.value(t, 0);
            const bool retryable = isRetryableFailure(t);
            if (retryable && attempts < maxRetries) {
                m_taskRetryCount[t] = attempts + 1;
                QPointer<DownloaderTask> taskPtr(t);
                const int delayMs = nextRetryDelayMs(t, attempts);
                const int delaySecUi = qMax(1, delayMs / 1000);
                const QString host = taskHost(t);
                if (!host.isEmpty() && (t->lastHttpStatus() == 429 || t->lastHttpStatus() == 503 || t->lastHttpStatus() == 504)) {
                    m_hostCooldownUntilMs[host] = QDateTime::currentMSecsSinceEpoch() + delayMs;
                }
                emit toastRequested(QStringLiteral("Retrying in %1s: %2").arg(delaySecUi).arg(name), QStringLiteral("warning"));
                QTimer::singleShot(delayMs, this, [this, taskPtr, host]() {
                    if (!taskPtr) return;
                    if (taskPtr->stateString() == "Error") {
                        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                        if (!host.isEmpty() && !hostCooldownAllowsStart(host, nowMs)) {
                            const int remaining = static_cast<int>(qMax<qint64>(100, m_hostCooldownUntilMs.value(host, 0) - nowMs));
                            QTimer::singleShot(remaining, this, [this, taskPtr]() {
                                if (!taskPtr) return;
                                if (taskPtr->stateString() == "Error") {
                                    taskPtr->recover();
                                    startQueued();
                                }
                            });
                            return;
                        }
                        taskPtr->recover();
                        startQueued();
                    }
                });
            } else if (retryable) {
                const QString reason = pauseReasonForFailure(t);
                t->pauseAfterFailure(reason);
                m_taskPausedByNetwork[t] = isConnectivityFailure(t);
                emit toastRequested(QStringLiteral("Paused: %1 (%2)").arg(name, reason), QStringLiteral("warning"));
            } else if (!retryable) {
                emit toastRequested(QStringLiteral("Not retryable: %1").arg(name), QStringLiteral("warning"));
            }
        }
    }

    updateTotals();
    scheduleSave();
    startQueued();
    emit countsChanged();
}

void DownloadManager::setMaxConcurrent(int v)
{
    if (v < 1) v = 1;
    if (m_maxConcurrent == v) return;
    m_maxConcurrent = v;
    emit maxConcurrentChanged();
    startQueued();
    scheduleSave();
}

void DownloadManager::setPerHostMaxConcurrent(int value)
{
    if (value < 1) value = 1;
    if (m_perHostMaxConcurrent == value) return;
    m_perHostMaxConcurrent = value;
    emit schedulingPolicyChanged();
    startQueued();
    scheduleSave();
}

void DownloadManager::setPersistSensitiveOptions(bool enabled)
{
    if (m_persistSensitiveOptions == enabled) return;
    m_persistSensitiveOptions = enabled;
    emit persistencePolicyChanged();
    scheduleSave();
}

void DownloadManager::setTelemetryEnabled(bool enabled)
{
    if (m_telemetryEnabled == enabled) return;
    m_telemetryEnabled = enabled;
    emit telemetryPolicyChanged();
    scheduleSave();
}

void DownloadManager::setDefaultUserAgent(const QString& value)
{
    const QString next = value.trimmed().isEmpty()
        ? QStringLiteral("raad/1.0")
        : value.trimmed();
    if (m_defaultUserAgent == next) return;
    m_defaultUserAgent = next;
    emit networkDefaultsChanged();
    scheduleSave();
}

void DownloadManager::setDefaultAllowInsecureSsl(bool enabled)
{
    if (m_defaultAllowInsecureSsl == enabled) return;
    m_defaultAllowInsecureSsl = enabled;
    emit networkDefaultsChanged();
    scheduleSave();
}

void DownloadManager::setDefaultProxyHost(const QString& value)
{
    const QString next = value.trimmed();
    if (m_defaultProxyHost == next) return;
    m_defaultProxyHost = next;
    emit networkDefaultsChanged();
    scheduleSave();
}

void DownloadManager::setDefaultProxyPort(int value)
{
    const int next = qBound(0, value, 65535);
    if (m_defaultProxyPort == next) return;
    m_defaultProxyPort = next;
    emit networkDefaultsChanged();
    scheduleSave();
}

void DownloadManager::setDefaultProxyUser(const QString& value)
{
    if (m_defaultProxyUser == value) return;
    m_defaultProxyUser = value;
    emit networkDefaultsChanged();
    scheduleSave();
}

void DownloadManager::setDefaultProxyPassword(const QString& value)
{
    if (m_defaultProxyPassword == value) return;
    m_defaultProxyPassword = value;
    emit networkDefaultsChanged();
    scheduleSave();
}

int DownloadManager::activeCount() const
{
    int count = 0;
    for (DownloaderTask* t : m_queue) {
        if (t && t->isRunning()) count++;
    }
    return count;
}

int DownloadManager::queuedCount() const
{
    int count = 0;
    for (DownloaderTask* t : m_queue) {
        if (t && t->isIdle()) count++;
    }
    return count;
}

int DownloadManager::completedCount() const
{
    int count = 0;
    for (int i = 0; i < m_model.rowCount(); ++i) {
        if (m_model.isFinishedAt(i)) count++;
    }
    return count;
}

void DownloadManager::setGlobalMaxSpeed(qint64 v)
{
    if (v < 0) v = 0;
    if (m_globalMaxSpeed == v) return;
    m_globalMaxSpeed = v;
    emit globalMaxSpeedChanged();
    for (DownloaderTask* t : m_queue) {
        if (t) applyTaskSpeed(t);
    }
    scheduleSave();
}

void DownloadManager::setPauseOnBattery(bool enabled)
{
    if (m_pauseOnBattery == enabled) return;
    m_pauseOnBattery = enabled;
    emit powerPolicyChanged();
    updatePowerState();
    scheduleSave();
    schedulerTick();
}

void DownloadManager::setResumeOnAC(bool enabled)
{
    if (m_resumeOnAC == enabled) return;
    m_resumeOnAC = enabled;
    emit powerPolicyChanged();
    scheduleSave();
    schedulerTick();
}

void DownloadManager::updatePowerState()
{
    const bool next = m_powerMonitor.isOnBattery(m_onBattery);
    if (m_onBattery == next) return;
    m_onBattery = next;
    emit powerStateChanged();
    schedulerTick();
}

void DownloadManager::startQueued()
{
    QHash<QString, int> runningPerQueue;
    QHash<QString, int> runningPerHost;
    int running = 0;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    for (DownloaderTask* t : m_queue) {
        if (t && t->isRunning()) {
            running++;
            const QString qname = m_taskQueue.value(t, defaultQueueName());
            runningPerQueue[qname] = runningPerQueue.value(qname, 0) + 1;
            const QString host = taskHost(t);
            if (!host.isEmpty()) {
                runningPerHost[host] = runningPerHost.value(host, 0) + 1;
            }
        }
    }

    const QTime now = QTime::currentTime();
    if (m_pauseOnBattery && m_onBattery) {
        emit countsChanged();
        return;
    }

    QVector<DownloaderTask*> candidates;
    candidates.reserve(m_queue.size());
    for (DownloaderTask* candidate : m_queue) {
        if (!candidate || !candidate->isIdle()) continue;
        candidates.append(candidate);
    }

    while (running < m_maxConcurrent) {
        DownloaderTask* best = nullptr;
        QString bestQueue;
        QString bestHost;
        int bestPriority = std::numeric_limits<int>::min();
        int bestQueuePressure = std::numeric_limits<int>::max();
        int bestHostPressure = std::numeric_limits<int>::max();
        qint64 bestOrder = std::numeric_limits<qint64>::max();

        for (DownloaderTask* candidate : candidates) {
            if (!candidate || !candidate->isIdle()) continue;

            const QString qname = m_taskQueue.value(candidate, defaultQueueName());
            if (!m_queues.contains(qname)) createQueue(qname);
            const QueueInfo* info = queueInfo(qname);
            if (!info) continue;
            if (!isQueueAllowed(*info, now)) continue;

            const int queueLimit = info->maxConcurrent > 0 ? info->maxConcurrent : m_maxConcurrent;
            const int queuePressure = runningPerQueue.value(qname, 0);
            if (queuePressure >= queueLimit) continue;

            const QString host = taskHost(candidate);
            if (!host.isEmpty()) {
                if (!hostCooldownAllowsStart(host, nowMs)) continue;
                if (m_perHostMaxConcurrent > 0 && runningPerHost.value(host, 0) >= m_perHostMaxConcurrent) continue;
            }

            const int priority = m_taskPriority.value(candidate, candidate->priority());
            const int hostPressure = host.isEmpty() ? 0 : runningPerHost.value(host, 0);
            const qint64 createdOrder = m_taskCreatedOrder.value(candidate, std::numeric_limits<qint64>::max());

            const bool better =
                (priority > bestPriority) ||
                (priority == bestPriority && queuePressure < bestQueuePressure) ||
                (priority == bestPriority && queuePressure == bestQueuePressure && hostPressure < bestHostPressure) ||
                (priority == bestPriority && queuePressure == bestQueuePressure && hostPressure == bestHostPressure && createdOrder < bestOrder);

            if (better) {
                best = candidate;
                bestQueue = qname;
                bestHost = host;
                bestPriority = priority;
                bestQueuePressure = queuePressure;
                bestHostPressure = hostPressure;
                bestOrder = createdOrder;
            }
        }

        if (!best) break;
        applyTaskSpeed(best);
        best->start();
        running++;
        runningPerQueue[bestQueue] = runningPerQueue.value(bestQueue, 0) + 1;
        if (!bestHost.isEmpty()) {
            runningPerHost[bestHost] = runningPerHost.value(bestHost, 0) + 1;
        }
    }
    emit countsChanged();
}

void DownloadManager::removeDownload(int index)
{
    removeDownloadWithOptions(index, false);
}

void DownloadManager::removeDownloadWithOptions(int index, bool deleteFromDisk)
{
    DownloaderTask* task = m_model.taskAt(index);
    if (!task) return;
    const QString filePath = utils::normalizeFilePath(task->fileName());
    const int configuredSegments = task->segments();
    const int effectiveSegments = task->effectiveSegments();
    m_queue.removeAll(task);
    m_taskSpeed.remove(task);
    m_taskReceived.remove(task);
    m_taskTotal.remove(task);
    m_taskLastReceived.remove(task);
    m_taskMaxSpeed.remove(task);
    m_taskCompletedAt.remove(task);
    m_taskRetryCount.remove(task);
    m_taskPriority.remove(task);
    m_taskCreatedOrder.remove(task);
    m_taskQueue.remove(task);
    m_taskCategory.remove(task);
    m_taskPausedBySchedule.remove(task);
    m_taskPausedByQuota.remove(task);
    m_taskPausedByBattery.remove(task);
    m_taskPausedByNetwork.remove(task);
    if (m_checksumWatchers.contains(task)) {
        if (QPointer<QFutureWatcher<QString>> watcher = m_checksumWatchers.take(task)) {
            watcher->cancel();
            watcher->deleteLater();
        }
    }
    task->cancel();
    if (deleteFromDisk) {
        deleteTaskFilesOnDisk(filePath, configuredSegments, effectiveSegments);
    }
    m_model.removeAt(index);
    updateTotals();
    scheduleSave();
    startQueued();
}

void DownloadManager::clearCompleted()
{
    for (int i = m_model.rowCount() - 1; i >= 0; --i) {
        if (m_model.isFinishedAt(i)) {
            DownloaderTask* task = m_model.taskAt(i);
            if (task) {
                m_queue.removeAll(task);
                m_taskSpeed.remove(task);
                m_taskReceived.remove(task);
                m_taskTotal.remove(task);
                m_taskLastReceived.remove(task);
                m_taskMaxSpeed.remove(task);
                m_taskCompletedAt.remove(task);
                m_taskRetryCount.remove(task);
                m_taskPriority.remove(task);
                m_taskCreatedOrder.remove(task);
                m_taskQueue.remove(task);
                m_taskCategory.remove(task);
                m_taskPausedBySchedule.remove(task);
                m_taskPausedByQuota.remove(task);
                m_taskPausedByBattery.remove(task);
                m_taskPausedByNetwork.remove(task);
                if (m_checksumWatchers.contains(task)) {
                    if (QPointer<QFutureWatcher<QString>> watcher = m_checksumWatchers.take(task)) {
                        watcher->cancel();
                        watcher->deleteLater();
                    }
                }
            }
            m_model.removeAt(i);
        }
    }
    updateTotals();
    scheduleSave();
    startQueued();
}

void DownloadManager::pauseAll()
{
    for (DownloaderTask* t : m_queue) {
        if (t && t->isRunning()) t->pause();
    }
    emit countsChanged();
    scheduleSave();
}

void DownloadManager::resumeAll()
{
    for (DownloaderTask* t : m_queue) {
        if (t && t->stateString() == "Paused") t->resume();
    }
    startQueued();
    scheduleSave();
}

void DownloadManager::cancelAll()
{
    const QVector<DownloaderTask*> tasks = m_queue;
    m_bulkCancelInProgress = true;
    for (DownloaderTask* t : tasks) {
        if (t) t->cancel();
    }
    m_bulkCancelInProgress = false;
    m_queue.clear();
    m_taskSpeed.clear();
    m_taskReceived.clear();
    m_taskTotal.clear();
    m_taskLastReceived.clear();
    m_taskMaxSpeed.clear();
    m_taskCompletedAt.clear();
    m_taskRetryCount.clear();
    m_taskPriority.clear();
    m_taskCreatedOrder.clear();
    m_taskQueue.clear();
    m_taskCategory.clear();
    m_taskPausedBySchedule.clear();
    m_taskPausedByQuota.clear();
    m_taskPausedByBattery.clear();
    m_taskPausedByNetwork.clear();
    updateTotals();
    emit countsChanged();
    scheduleSave();
}

void DownloadManager::retryFailed()
{
    for (DownloaderTask* t : m_queue) {
        if (t && t->stateString() == "Error") {
            t->recover();
        }
    }
    startQueued();
    scheduleSave();
}

void DownloadManager::retryTask(int index)
{
    DownloaderTask* task = m_model.taskAt(index);
    if (!task) return;
    task->recover();
    startQueued();
    scheduleSave();
}

void DownloadManager::openFile(int index)
{
    DownloaderTask* task = m_model.taskAt(index);
    if (!task) return;
    const QString path = utils::normalizeFilePath(task->fileName());
    QFileInfo info(path);
    if (info.exists()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()));
    } else if (!info.absolutePath().isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
    }
}

void DownloadManager::revealInFolder(int index)
{
    DownloaderTask* task = m_model.taskAt(index);
    if (!task) return;
    const QString path = utils::normalizeFilePath(task->fileName());
    QFileInfo info(path);
    const QString absPath = info.absoluteFilePath();
#if defined(Q_OS_MAC)
    if (info.exists() && !absPath.isEmpty()) {
        QProcess::startDetached("open", QStringList() << "-R" << absPath);
        return;
    }
#elif defined(Q_OS_WIN)
    if (info.exists() && !absPath.isEmpty()) {
        const QString nativePath = QDir::toNativeSeparators(absPath);
        QProcess::startDetached("explorer", QStringList() << "/select," + nativePath);
        return;
    }
#endif
    if (!info.absolutePath().isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
    }
}

bool DownloadManager::fileExists(int index) const
{
    DownloaderTask* task = m_model.taskAt(index);
    if (!task) return false;
    const QString path = utils::normalizeFilePath(task->fileName());
    if (path.isEmpty()) return false;
    QFileInfo info(path);
    return info.exists() && info.isFile();
}

void DownloadManager::applyPostActions(DownloaderTask* task)
{
    if (!task) return;
    const QString path = utils::normalizeFilePath(task->fileName());
    if (path.isEmpty() || !utils::fileExistsPath(path)) return;

    const QFileInfo info(path);
    const QString dirPath = info.absolutePath();
    const QString lower = path.toLower();

    if (task->postRevealFolder()) {
        revealPath(path);
        task->appendLog(QStringLiteral("Post action: Reveal in folder"));
    }
    if (task->postOpenFile()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()));
        task->appendLog(QStringLiteral("Post action: Open file"));
    }
    if (task->postExtract()) {
        bool launched = false;
#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
        if (lower.endsWith(".zip")) {
            launched = QProcess::startDetached(QStringLiteral("unzip"), QStringList() << QStringLiteral("-o") << path << QStringLiteral("-d") << dirPath);
        } else if (lower.endsWith(".tar.gz") || lower.endsWith(".tgz") || lower.endsWith(".tar.xz") || lower.endsWith(".tar.bz2") || lower.endsWith(".tar")) {
            launched = QProcess::startDetached(QStringLiteral("tar"), QStringList() << QStringLiteral("-xf") << path << QStringLiteral("-C") << dirPath);
        }
#endif
        if (launched) {
            emit toastRequested(QStringLiteral("Extracting: %1").arg(info.fileName()), QStringLiteral("info"));
            task->appendLog(QStringLiteral("Post action: Extract"));
        } else {
            emit toastRequested(QStringLiteral("Extract failed (tool missing?)"), QStringLiteral("warning"));
        }
    }

    const QString script = task->postScript().trimmed();
    if (!script.isEmpty()) {
        QString resolved = script;
        resolved.replace(QStringLiteral("{file}"), path);
        resolved.replace(QStringLiteral("{dir}"), dirPath);
#if defined(Q_OS_WIN)
        QProcess::startDetached(QStringLiteral("cmd"), QStringList() << QStringLiteral("/C") << resolved);
#else
        QProcess::startDetached(QStringLiteral("/bin/sh"), QStringList() << QStringLiteral("-c") << resolved);
#endif
        task->appendLog(QStringLiteral("Post action: Script"));
    }
}

void DownloadManager::verifyChecksumAsync(DownloaderTask* task)
{
    if (!task) return;
    const QString path = utils::normalizeFilePath(task->fileName());
    if (!utils::fileExistsPath(path)) {
        emit toastRequested(QStringLiteral("File not found for checksum"), QStringLiteral("danger"));
        return;
    }

    QString algo = task->checksumAlgorithm().trimmed();
    const QString expectedRaw = task->checksumExpected().trimmed();
    if (algo.isEmpty()) {
        if (!expectedRaw.isEmpty()) {
            algo = utils::detectChecksumAlgo(expectedRaw);
        }
        if (algo.isEmpty()) {
            algo = QStringLiteral("SHA256");
        }
        task->setChecksumAlgorithm(algo);
    }

    const QString algoUpper = algo.toUpper();
    QCryptographicHash::Algorithm hashAlgo = QCryptographicHash::Sha256;
    if (algoUpper == "MD5") hashAlgo = QCryptographicHash::Md5;
    else if (algoUpper == "SHA1") hashAlgo = QCryptographicHash::Sha1;
    else if (algoUpper == "SHA256") hashAlgo = QCryptographicHash::Sha256;
    else if (algoUpper == "SHA512") hashAlgo = QCryptographicHash::Sha512;
    else {
        task->setChecksumState(QStringLiteral("Unknown"));
        emit toastRequested(QStringLiteral("Unknown checksum algorithm"), QStringLiteral("warning"));
        return;
    }

    if (m_checksumWatchers.contains(task) && m_checksumWatchers.value(task)) {
        emit toastRequested(QStringLiteral("Checksum already running"), QStringLiteral("warning"));
        return;
    }

    task->setChecksumState(QStringLiteral("Verifying"));
    task->appendLog(QStringLiteral("Checksum verify started (%1)").arg(algoUpper));

    QPointer<DownloaderTask> taskPtr(task);
    QPointer<QFutureWatcher<QString>> watcher = new QFutureWatcher<QString>(this);
    m_checksumWatchers.insert(task, watcher);

    QFuture<QString> future = QtConcurrent::run([path, hashAlgo]() -> QString {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return QString();
        QCryptographicHash hash(hashAlgo);
        QByteArray buffer;
        buffer.resize(1024 * 1024);
        while (!file.atEnd()) {
            const qint64 readBytes = file.read(buffer.data(), buffer.size());
            if (readBytes <= 0) break;
            hash.addData(QByteArrayView(buffer.constData(), static_cast<qsizetype>(readBytes)));
        }
        file.close();
        return QString::fromUtf8(hash.result().toHex());
    });

    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, taskPtr, watcher, expectedRaw]() {
        if (!taskPtr) {
            if (watcher) watcher->deleteLater();
            return;
        }
        const QString actual = watcher ? watcher->result() : QString();
        if (watcher) watcher->deleteLater();
        m_checksumWatchers.remove(taskPtr);

        if (actual.isEmpty()) {
            taskPtr->setChecksumState(QStringLiteral("Failed"));
            taskPtr->appendLog(QStringLiteral("Checksum failed"));
            emit toastRequested(QStringLiteral("Checksum failed"), QStringLiteral("danger"));
            return;
        }
        taskPtr->setChecksumActual(actual);
        if (expectedRaw.isEmpty()) {
            taskPtr->setChecksumState(QStringLiteral("Computed"));
            taskPtr->appendLog(QStringLiteral("Checksum computed"));
            emit toastRequested(QStringLiteral("Checksum computed"), QStringLiteral("info"));
            return;
        }
        const QString expected = utils::normalizeChecksum(expectedRaw);
        const QString actualNorm = utils::normalizeChecksum(actual);
        if (expected == actualNorm) {
            taskPtr->setChecksumState(QStringLiteral("OK"));
            taskPtr->appendLog(QStringLiteral("Checksum OK"));
            emit toastRequested(QStringLiteral("Checksum OK"), QStringLiteral("success"));
        } else {
            taskPtr->setChecksumState(QStringLiteral("Mismatch"));
            taskPtr->appendLog(QStringLiteral("Checksum mismatch"));
            emit toastRequested(QStringLiteral("Checksum mismatch"), QStringLiteral("danger"));
        }
    });

    watcher->setFuture(future);
}

qint64 DownloadManager::taskMaxSpeed(int index) const
{
    DownloaderTask* task = m_model.taskAt(index);
    if (!task) return 0;
    return m_taskMaxSpeed.value(task, 0);
}

qint64 DownloadManager::taskBytesReceived(int index) const
{
    DownloaderTask* task = m_model.taskAt(index);
    if (!task) return 0;
    return m_taskReceived.value(task, 0);
}

qint64 DownloadManager::taskBytesTotal(int index) const
{
    DownloaderTask* task = m_model.taskAt(index);
    if (!task) return 0;
    return m_taskTotal.value(task, 0);
}

qint64 DownloadManager::taskCompletedAt(int index) const
{
    DownloaderTask* task = m_model.taskAt(index);
    if (!task) return 0;
    return m_taskCompletedAt.value(task, 0);
}

int DownloadManager::taskPriority(int index) const
{
    DownloaderTask* task = m_model.taskAt(index);
    if (!task) return 100;
    return m_taskPriority.value(task, task->priority());
}

void DownloadManager::setTaskMaxSpeed(int index, qint64 bytesPerSecond)
{
    DownloaderTask* task = m_model.taskAt(index);
    if (!task) return;
    if (bytesPerSecond < 0) bytesPerSecond = 0;
    if (m_taskMaxSpeed.value(task, 0) == bytesPerSecond) return;
    m_taskMaxSpeed[task] = bytesPerSecond;
    applyTaskSpeed(task);
    scheduleSave();
}

void DownloadManager::setTaskPriority(int index, int priority)
{
    DownloaderTask* task = m_model.taskAt(index);
    if (!task) return;
    const int normalized = qBound(0, priority, 1000);
    if (m_taskPriority.value(task, task->priority()) == normalized) return;
    m_taskPriority[task] = normalized;
    task->setPriority(normalized);
    scheduleSave();
    startQueued();
}

void DownloadManager::pauseTask(int index)
{
    DownloaderTask* task = m_model.taskAt(index);
    if (!task) return;
    m_taskPausedByNetwork[task] = false;
    task->pause();
    scheduleSave();
}

int DownloadManager::indexOfTask(QObject* taskObject) const
{
    DownloaderTask* task = qobject_cast<DownloaderTask*>(taskObject);
    return task ? m_model.indexOfTask(task) : -1;
}

int DownloadManager::taskCount() const
{
    return m_model.rowCount();
}

QObject* DownloadManager::taskObjectAt(int index) const
{
    return m_model.taskAt(index);
}

QString DownloadManager::taskQueueName(int index) const
{
    DownloaderTask* task = m_model.taskAt(index);
    return task ? m_taskQueue.value(task, defaultQueueName()) : defaultQueueName();
}

QString DownloadManager::taskCategoryName(int index) const
{
    DownloaderTask* task = m_model.taskAt(index);
    return task ? m_taskCategory.value(task, utils::toString(utils::detectCategory(task->fileName())))
                : QStringLiteral("Other");
}

void DownloadManager::resumeTask(int index)
{
    DownloaderTask* task = m_model.taskAt(index);
    if (!task) return;
    const QString pauseReason = task->pauseReason().trimmed();
    const bool needsRecoveryResume = task->stateString() == "Error"
        || (task->stateString() == "Paused"
            && !pauseReason.isEmpty()
            && pauseReason != QStringLiteral("User"));
    if (needsRecoveryResume) {
        m_taskPausedByNetwork[task] = false;
        task->recover();
    } else {
        task->resume();
    }
    startQueued();
    scheduleSave();
}

void DownloadManager::togglePause(int index)
{
    DownloaderTask* task = m_model.taskAt(index);
    if (!task) return;
    const QString state = task->stateString();
    if (state == "Active") {
        m_taskPausedByNetwork[task] = false;
        task->pause();
    } else if (state == "Paused") {
        const QString pauseReason = task->pauseReason().trimmed();
        if (!pauseReason.isEmpty() && pauseReason != QStringLiteral("User")) {
            m_taskPausedByNetwork[task] = false;
            task->recover();
        } else {
            task->resume();
        }
        startQueued();
    }
    scheduleSave();
}

void DownloadManager::importList(const QString& path)
{
    const QString filePath = utils::normalizeFilePath(path);
    if (filePath.isEmpty()) return;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return;

    const QByteArray raw = file.readAll();
    file.close();

    const QString fallbackFolder = defaultDownloadsFolderPath();
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (doc.isArray() || doc.isObject()) {
        QJsonArray items;
        if (doc.isArray()) {
            items = doc.array();
        } else {
            const QJsonObject root = doc.object();
            items = root.value("items").toArray();
        }
        for (const QJsonValue& v : items) {
            QString urlStr;
            QString filePathEntry;
            QString queue;
            QString category;
            bool startPaused = false;
            if (v.isString()) {
                urlStr = v.toString();
            } else if (v.isObject()) {
                QJsonObject obj = v.toObject();
                urlStr = obj.value("url").toString();
                filePathEntry = obj.value("filePath").toString();
                queue = obj.value("queueName").toString();
                category = obj.value("category").toString();
                startPaused = obj.value("startPaused").toBool(false);
            }
            if (urlStr.isEmpty()) continue;
            if (filePathEntry.isEmpty()) {
                filePathEntry = resolveDownloadPath(urlStr, category, fallbackFolder);
            }
            addDownloadAdvancedWithOptions(urlStr, filePathEntry, queue, category, startPaused);
        }
        emit toastRequested(QStringLiteral("Imported downloads"), QStringLiteral("success"));
        return;
    }

    const QString text = QString::fromUtf8(raw);
    const QStringList lines = text.split('\n');
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        if (trimmed.startsWith("#") || trimmed.startsWith("//")) continue;
        QStringList parts;
        if (trimmed.contains("|")) {
            parts = trimmed.split("|");
        } else {
            parts = trimmed.split(QRegularExpression("\\s+"));
        }
        if (parts.isEmpty()) continue;
        QString urlStr = parts.value(0).trimmed();
        QString filePathEntry = parts.value(1).trimmed();
        QString queue = parts.value(2).trimmed();
        QString category = parts.value(3).trimmed();
        if (urlStr.isEmpty()) continue;
        if (filePathEntry.isEmpty()) {
            filePathEntry = resolveDownloadPath(urlStr, category, fallbackFolder);
        }
        addDownloadAdvancedWithOptions(urlStr, filePathEntry, queue, category, false);
    }
    emit toastRequested(QStringLiteral("Imported downloads"), QStringLiteral("success"));
}

void DownloadManager::exportList(const QString& path)
{
    const QString filePath = utils::normalizeFilePath(path);
    if (filePath.isEmpty()) return;
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;

    if (filePath.endsWith(".txt", Qt::CaseInsensitive)) {
        QTextStream out(&file);
        for (int i = 0; i < m_model.rowCount(); ++i) {
            DownloaderTask* task = m_model.taskAt(i);
            if (!task) continue;
            out << task->url() << "\n";
        }
        file.close();
        emit toastRequested(QStringLiteral("Exported list"), QStringLiteral("success"));
        return;
    }

    QJsonObject root;
    root.insert("version", 1);
    QJsonArray items;
    for (int i = 0; i < m_model.rowCount(); ++i) {
        DownloaderTask* task = m_model.taskAt(i);
        if (!task) continue;
        QJsonObject obj;
        obj.insert("url", task->url());
        obj.insert("filePath", task->fileName());
        obj.insert("queueName", m_taskQueue.value(task, defaultQueueName()));
        obj.insert("category", m_taskCategory.value(task, utils::toString(utils::detectCategory(task->fileName()))));
        obj.insert("state", task->stateString());
        obj.insert("bytesReceived", static_cast<double>(m_taskReceived.value(task, 0)));
        obj.insert("bytesTotal", static_cast<double>(m_taskTotal.value(task, 0)));
        items.append(obj);
    }
    root.insert("items", items);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    emit toastRequested(QStringLiteral("Exported list"), QStringLiteral("success"));
}

void DownloadManager::verifyTask(int index)
{
    DownloaderTask* task = m_model.taskAt(index);
    if (!task) return;
    verifyChecksumAsync(task);
}

void DownloadManager::testUrl(const QString& urlStr)
{
    const QString trimmed = urlStr.trimmed();
    if (trimmed.isEmpty()) {
        setNetworkTestState(false, QStringLiteral("Enter a URL to test."), QStringLiteral("warning"));
        emit toastRequested(QStringLiteral("Enter a URL to test"), QStringLiteral("warning"));
        return;
    }

    if (m_networkTestRunning) {
        emit toastRequested(QStringLiteral("Network test already running"), QStringLiteral("warning"));
        return;
    }

    QUrl url = QUrl::fromUserInput(trimmed);
    if (!url.isValid() || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))) {
        setNetworkTestState(false, QStringLiteral("Invalid URL"), QStringLiteral("danger"));
        emit toastRequested(QStringLiteral("Invalid URL"), QStringLiteral("danger"));
        return;
    }

    const qint64 startedMs = QDateTime::currentMSecsSinceEpoch();
    const QString hostLabel = url.host().isEmpty() ? url.toString(QUrl::RemovePath) : url.host();
    setNetworkTestState(true, QStringLiteral("Testing %1 ...").arg(hostLabel), QStringLiteral("info"));

    QNetworkAccessManager* net = new QNetworkAccessManager(this);
    if (!m_defaultProxyHost.isEmpty() && m_defaultProxyPort > 0) {
        QNetworkProxy proxy(QNetworkProxy::HttpProxy,
                            m_defaultProxyHost,
                            m_defaultProxyPort,
                            m_defaultProxyUser,
                            m_defaultProxyPassword);
        net->setProxy(proxy);
    }
    auto finishResult = [this, net](bool success, const QString& message, const QString& kind) {
        const QString resolvedKind = success ? kind : QStringLiteral("danger");
        setNetworkTestState(false, message, resolvedKind);
        emit toastRequested(message, resolvedKind);
        net->deleteLater();
    };

    QNetworkRequest headReq(url);
    headReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    headReq.setRawHeader("User-Agent", m_defaultUserAgent.toUtf8());
    QNetworkReply* headReply = net->head(headReq);
#if QT_CONFIG(ssl)
    connect(headReply, &QNetworkReply::sslErrors, this, [this, headReply](const QList<QSslError>& errors) {
        if (m_defaultAllowInsecureSsl && headReply) {
            headReply->ignoreSslErrors(errors);
        }
    });
#endif

    connect(headReply, &QNetworkReply::finished, this, [this, headReply, url, startedMs, finishResult, net]() mutable {
        const int status = headReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError errCode = headReply->error();
        const QString errText = headReply->errorString();
        const QVariant len = headReply->header(QNetworkRequest::ContentLengthHeader);
        const QByteArray acceptRanges = headReply->rawHeader("Accept-Ranges");
        headReply->deleteLater();

        const qint64 elapsedMs = qMax<qint64>(1, QDateTime::currentMSecsSinceEpoch() - startedMs);
        const bool headSucceeded = (errCode == QNetworkReply::NoError) && (status >= 200 && status < 400);
        if (headSucceeded) {
            QString message = QStringLiteral("HEAD %1").arg(status);
            if (len.isValid() && len.toLongLong() > 0) {
                message += QStringLiteral(" • Size %1 B").arg(len.toLongLong());
            }
            if (!acceptRanges.isEmpty()) {
                message += QStringLiteral(" • Ranges %1").arg(QString::fromUtf8(acceptRanges));
            }
            message += QStringLiteral(" • %1 ms").arg(elapsedMs);
            finishResult(true, message, QStringLiteral("success"));
            return;
        }

        const bool shouldFallbackGet = (status == 0 || status == 403 || status == 405 || status == 429 || status == 500 || status == 501)
                                       || (errCode != QNetworkReply::NoError);
        if (!shouldFallbackGet) {
            finishResult(false, QStringLiteral("Test failed: HTTP %1 • %2").arg(status).arg(errText), QStringLiteral("danger"));
            return;
        }

        QNetworkRequest getReq(url);
        getReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        getReq.setRawHeader("User-Agent", m_defaultUserAgent.toUtf8());
        getReq.setRawHeader("Range", "bytes=0-262143");
        QNetworkReply* getReply = net->get(getReq);
#if QT_CONFIG(ssl)
        connect(getReply, &QNetworkReply::sslErrors, this, [this, getReply](const QList<QSslError>& errors) {
            if (m_defaultAllowInsecureSsl && getReply) {
                getReply->ignoreSslErrors(errors);
            }
        });
#endif
        qint64* probeBytes = new qint64(0);

        connect(getReply, &QNetworkReply::readyRead, this, [getReply, probeBytes]() {
            if (!getReply) return;
            *probeBytes += getReply->readAll().size();
        });

        connect(getReply, &QNetworkReply::finished, this, [getReply, probeBytes, startedMs, finishResult]() mutable {
            const int statusGet = getReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QNetworkReply::NetworkError getErrCode = getReply->error();
            const QString getErr = getReply->errorString();
            if (getReply) {
                *probeBytes += getReply->readAll().size();
                getReply->deleteLater();
            }

            if (getErrCode != QNetworkReply::NoError || (statusGet <= 0 || statusGet >= 500)) {
                finishResult(false, QStringLiteral("Test failed: HTTP %1 • %2").arg(statusGet).arg(getErr), QStringLiteral("danger"));
                delete probeBytes;
                return;
            }

            const qint64 elapsedGetMs = qMax<qint64>(1, QDateTime::currentMSecsSinceEpoch() - startedMs);
            const qint64 bytesPerSec = (*probeBytes * 1000) / elapsedGetMs;
            QString message = QStringLiteral("Probe %1 • %2 KB/s • %3 ms")
                                  .arg(statusGet)
                                  .arg(qMax<qint64>(0, bytesPerSec / 1024))
                                  .arg(elapsedGetMs);
            finishResult(true, message, QStringLiteral("success"));
            delete probeBytes;
        });
    });
}

bool DownloadManager::renameTaskFile(int index, const QString& newName)
{
    if (newName.trimmed().isEmpty()) return false;
    DownloaderTask* task = m_model.taskAt(index);
    if (!task) return false;
    QFileInfo info(task->fileName());
    const QString newPath = info.dir().filePath(newName.trimmed());
    return moveTaskFile(index, newPath);
}

bool DownloadManager::moveTaskFile(int index, const QString& newPath)
{
    DownloaderTask* task = m_model.taskAt(index);
    if (!task) return false;
    if (task->stateString() == "Active") return false;

    const QString oldPath = utils::normalizeFilePath(task->fileName());
    const QString normalizedNew = utils::normalizeFilePath(newPath);
    if (normalizedNew.isEmpty()) return false;

    const QString finalNew = utils::uniqueFilePath(normalizedNew);
    QDir().mkpath(QFileInfo(finalNew).absolutePath());
    const bool ok = renameTaskFilesOnDisk(oldPath, finalNew, task->segments());
    if (!ok) return false;
    task->setFilePath(finalNew);
    m_model.updateFileName(task, finalNew);
    scheduleSave();
    emit toastRequested(QStringLiteral("Moved to: %1").arg(QFileInfo(finalNew).fileName()), QStringLiteral("info"));
    return true;
}

void DownloadManager::createQueue(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return;
    if (m_queues.contains(trimmed)) return;

    QueueInfo info;
    info.name = trimmed;
    info.maxConcurrent = m_maxConcurrent;
    info.maxSpeed = 0;
    info.lastResetDate = QDate::currentDate();
    m_queues.insert(trimmed, info);
    m_queueOrder.append(trimmed);
    emit queuesChanged();
    scheduleSave();
}

void DownloadManager::removeQueue(const QString& name)
{
    if (!m_queues.contains(name)) return;
    if (name == defaultQueueName()) return;

    const QString fallback = defaultQueueName();
    for (auto it = m_taskQueue.begin(); it != m_taskQueue.end(); ++it) {
        if (it.value() == name) {
            it.value() = fallback;
            m_model.updateMetadata(it.key(), fallback, m_taskCategory.value(it.key()));
            applyTaskSpeed(it.key());
        }
    }

    bool domainRulesWereChanged = false;
    for (auto it = m_domainRules.begin(); it != m_domainRules.end(); ++it) {
        if (it.value() == name) {
            it.value() = fallback;
            domainRulesWereChanged = true;
        }
    }

    m_queues.remove(name);
    m_queueOrder.removeAll(name);
    emit queuesChanged();
    if (domainRulesWereChanged) {
        emit domainRulesChanged();
    }
    scheduleSave();
    startQueued();
}

void DownloadManager::renameQueue(const QString& oldName, const QString& newName)
{
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty()) return;
    if (!m_queues.contains(oldName)) return;
    if (m_queues.contains(trimmed)) return;

    QueueInfo info = m_queues.take(oldName);
    info.name = trimmed;
    m_queues.insert(trimmed, info);

    for (int i = 0; i < m_queueOrder.size(); ++i) {
        if (m_queueOrder[i] == oldName) {
            m_queueOrder[i] = trimmed;
        }
    }

    for (auto it = m_taskQueue.begin(); it != m_taskQueue.end(); ++it) {
        if (it.value() == oldName) {
            it.value() = trimmed;
            m_model.updateMetadata(it.key(), trimmed, m_taskCategory.value(it.key()));
        }
    }

    bool domainRulesWereChanged = false;
    for (auto it = m_domainRules.begin(); it != m_domainRules.end(); ++it) {
        if (it.value() == oldName) {
            it.value() = trimmed;
            domainRulesWereChanged = true;
        }
    }

    emit queuesChanged();
    if (domainRulesWereChanged) {
        emit domainRulesChanged();
    }
    scheduleSave();
}

void DownloadManager::setTaskQueue(int index, const QString& name)
{
    DownloaderTask* task = m_model.taskAt(index);
    if (!task) return;
    const QString resolved = name.isEmpty() ? defaultQueueName() : name;
    if (!m_queues.contains(resolved)) createQueue(resolved);
    m_taskQueue[task] = resolved;
    m_model.updateMetadata(task, resolved, m_taskCategory.value(task));
    applyTaskSpeed(task);
    scheduleSave();
    startQueued();
}

void DownloadManager::setTaskCategory(int index, const QString& category)
{
    DownloaderTask* task = m_model.taskAt(index);
    if (!task) return;
    const QString resolved = category.isEmpty() ? utils::toString(utils::detectCategory(task->fileName())) : category;
    if (m_taskCategory.value(task) == resolved) return;
    m_taskCategory[task] = resolved;
    m_model.updateMetadata(task, m_taskQueue.value(task, defaultQueueName()), resolved);
    scheduleSave();
}

QStringList DownloadManager::queueNames() const
{
    return m_queueOrder;
}

int DownloadManager::queueMaxConcurrent(const QString& name) const
{
    const QueueInfo* info = queueInfo(name);
    return info ? info->maxConcurrent : m_maxConcurrent;
}

void DownloadManager::setQueueMaxConcurrent(const QString& name, int value)
{
    QueueInfo* info = queueInfo(name);
    if (!info) return;
    if (value < 1) value = 1;
    if (info->maxConcurrent == value) return;
    info->maxConcurrent = value;
    scheduleSave();
    startQueued();
}

qint64 DownloadManager::queueMaxSpeed(const QString& name) const
{
    const QueueInfo* info = queueInfo(name);
    return info ? info->maxSpeed : 0;
}

void DownloadManager::setQueueMaxSpeed(const QString& name, qint64 value)
{
    QueueInfo* info = queueInfo(name);
    if (!info) return;
    if (value < 0) value = 0;
    if (info->maxSpeed == value) return;
    info->maxSpeed = value;
    for (auto it = m_taskQueue.constBegin(); it != m_taskQueue.constEnd(); ++it) {
        if (it.value() == name) {
            applyTaskSpeed(it.key());
        }
    }
    scheduleSave();
}

bool DownloadManager::queueScheduleEnabled(const QString& name) const
{
    const QueueInfo* info = queueInfo(name);
    return info ? info->scheduleEnabled : false;
}

void DownloadManager::setQueueScheduleEnabled(const QString& name, bool enabled)
{
    QueueInfo* info = queueInfo(name);
    if (!info) return;
    if (info->scheduleEnabled == enabled) return;
    info->scheduleEnabled = enabled;
    scheduleSave();
    enforceQueuePolicies();
    startQueued();
}

int DownloadManager::queueScheduleStartMinutes(const QString& name) const
{
    const QueueInfo* info = queueInfo(name);
    return info ? info->startMinutes : 0;
}

void DownloadManager::setQueueScheduleStartMinutes(const QString& name, int minutes)
{
    QueueInfo* info = queueInfo(name);
    if (!info) return;
    minutes = qBound(0, minutes, 23 * 60 + 59);
    if (info->startMinutes == minutes) return;
    info->startMinutes = minutes;
    scheduleSave();
    enforceQueuePolicies();
}

int DownloadManager::queueScheduleEndMinutes(const QString& name) const
{
    const QueueInfo* info = queueInfo(name);
    return info ? info->endMinutes : 0;
}

void DownloadManager::setQueueScheduleEndMinutes(const QString& name, int minutes)
{
    QueueInfo* info = queueInfo(name);
    if (!info) return;
    minutes = qBound(0, minutes, 23 * 60 + 59);
    if (info->endMinutes == minutes) return;
    info->endMinutes = minutes;
    scheduleSave();
    enforceQueuePolicies();
}

bool DownloadManager::queueQuotaEnabled(const QString& name) const
{
    const QueueInfo* info = queueInfo(name);
    return info ? info->quotaEnabled : false;
}

void DownloadManager::setQueueQuotaEnabled(const QString& name, bool enabled)
{
    QueueInfo* info = queueInfo(name);
    if (!info) return;
    if (info->quotaEnabled == enabled) return;
    info->quotaEnabled = enabled;
    scheduleSave();
    enforceQueuePolicies();
}

qint64 DownloadManager::queueQuotaBytes(const QString& name) const
{
    const QueueInfo* info = queueInfo(name);
    return info ? info->quotaBytes : 0;
}

void DownloadManager::setQueueQuotaBytes(const QString& name, qint64 bytes)
{
    QueueInfo* info = queueInfo(name);
    if (!info) return;
    if (bytes < 0) bytes = 0;
    if (info->quotaBytes == bytes) return;
    info->quotaBytes = bytes;
    scheduleSave();
    enforceQueuePolicies();
}

qint64 DownloadManager::queueDownloadedToday(const QString& name) const
{
    const QueueInfo* info = queueInfo(name);
    return info ? info->downloadedToday : 0;
}

QString DownloadManager::defaultQueueName() const
{
    return m_queueOrder.isEmpty() ? QStringLiteral("General") : m_queueOrder.first();
}

QStringList DownloadManager::categoryNames() const
{
    return utils::categoryNames();
}

QString DownloadManager::categoryFolder(const QString& category) const
{
    return categoryFolderForName(category);
}

void DownloadManager::setCategoryFolder(const QString& category, const QString& folder)
{
    if (category.isEmpty() || category == "Auto") return;
    QString normalized = utils::normalizeFilePath(folder.trimmed());
    if (normalized.endsWith("/")) normalized.chop(1);
    if (normalized.isEmpty()) {
        if (m_categoryFolders.contains(category)) {
            m_categoryFolders.remove(category);
            scheduleSave();
            emit categoryFoldersChanged();
        }
        return;
    }
    if (m_categoryFolders.value(category) == normalized) return;
    m_categoryFolders[category] = normalized;
    scheduleSave();
    emit categoryFoldersChanged();
}

QStringList DownloadManager::domainRuleHosts() const
{
    return m_domainRules.keys();
}

QString DownloadManager::domainRuleQueue(const QString& host) const
{
    const QString key = utils::normalizeHost(host);
    return m_domainRules.value(key);
}

void DownloadManager::setDomainRule(const QString& host, const QString& queue)
{
    const QString key = utils::normalizeHost(host);
    if (key.isEmpty()) return;
    const QString resolvedQueue = queue.isEmpty() ? defaultQueueName() : queue;
    if (!m_queues.contains(resolvedQueue)) createQueue(resolvedQueue);
    if (m_domainRules.value(key) == resolvedQueue) return;
    m_domainRules[key] = resolvedQueue;
    scheduleSave();
    emit domainRulesChanged();
}

void DownloadManager::removeDomainRule(const QString& host)
{
    const QString key = utils::normalizeHost(host);
    if (key.isEmpty()) return;
    if (!m_domainRules.contains(key)) return;
    m_domainRules.remove(key);
    scheduleSave();
    emit domainRulesChanged();
}

QString DownloadManager::detectCategoryForName(const QString& name) const
{
    if (name.isEmpty()) return QStringLiteral("Other");
    return utils::toString(utils::detectCategory(name));
}

QString DownloadManager::resolveDownloadPath(const QString& urlStr, const QString& category, const QString& fallbackFolder) const
{
    QUrl url(urlStr);
    QString fileName = utils::fileNameFromUrl(url);
    if (fileName.isEmpty()) fileName = QStringLiteral("download.bin");

    QString effectiveCategory = category;
    if (effectiveCategory.isEmpty() || effectiveCategory == "Auto") {
        effectiveCategory = utils::toString(utils::detectCategory(fileName));
    }
    QString folder = categoryFolderForName(effectiveCategory);
    if (folder.isEmpty()) {
        folder = utils::normalizeFilePath(fallbackFolder);
    }
    if (folder.isEmpty()) {
        folder = defaultDownloadsFolderPath();
    }
    QDir dir(folder);
    return dir.filePath(fileName);
}

QString DownloadManager::clipboardText() const
{
    if (const QClipboard* cb = QGuiApplication::clipboard()) {
        return cb->text();
    }
    return QString();
}

void DownloadManager::copyText(const QString& text) const
{
    if (QClipboard* cb = QGuiApplication::clipboard()) {
        cb->setText(text);
    }
}

QString DownloadManager::processApiCommand(const QString& commandJson)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(commandJson.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return QString::fromUtf8(
            QJsonDocument(QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("invalid_json")}
            }).toJson(QJsonDocument::Compact));
    }

    const QJsonObject req = doc.object();
    const QString cmd = req.value(QStringLiteral("cmd")).toString().trimmed();
    if (cmd.isEmpty()) {
        return QString::fromUtf8(
            QJsonDocument(QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("missing_cmd")}
            }).toJson(QJsonDocument::Compact));
    }

    QJsonObject res{
        {QStringLiteral("ok"), true},
        {QStringLiteral("cmd"), cmd}
    };

    if (cmd == QStringLiteral("stats")) {
        res.insert(QStringLiteral("active"), activeCount());
        res.insert(QStringLiteral("queued"), queuedCount());
        res.insert(QStringLiteral("completed"), completedCount());
        res.insert(QStringLiteral("speed"), static_cast<double>(totalSpeed()));
    } else if (cmd == QStringLiteral("pauseAll")) {
        pauseAll();
    } else if (cmd == QStringLiteral("resumeAll")) {
        resumeAll();
    } else if (cmd == QStringLiteral("cancelAll")) {
        cancelAll();
    } else if (cmd == QStringLiteral("add")) {
        const QString url = req.value(QStringLiteral("url")).toString();
        const QString filePath = req.value(QStringLiteral("filePath")).toString();
        const QString queueName = req.value(QStringLiteral("queueName")).toString();
        const QString category = req.value(QStringLiteral("category")).toString();
        const bool startPaused = req.value(QStringLiteral("startPaused")).toBool(false);
        QVariantMap options;
        if (req.contains(QStringLiteral("options")) && req.value(QStringLiteral("options")).isObject()) {
            options = req.value(QStringLiteral("options")).toObject().toVariantMap();
        }
        if (url.trimmed().isEmpty()) {
            res[QStringLiteral("ok")] = false;
            res.insert(QStringLiteral("error"), QStringLiteral("missing_url"));
        } else {
            addDownloadAdvancedWithExtras(url, filePath, queueName, category, startPaused, options);
        }
    } else if (cmd == QStringLiteral("setNetworkDefaults")) {
        if (req.contains(QStringLiteral("userAgent"))) {
            setDefaultUserAgent(req.value(QStringLiteral("userAgent")).toString());
        }
        if (req.contains(QStringLiteral("allowInsecureSsl"))) {
            setDefaultAllowInsecureSsl(req.value(QStringLiteral("allowInsecureSsl")).toBool(m_defaultAllowInsecureSsl));
        }
        if (req.contains(QStringLiteral("proxyHost"))) {
            setDefaultProxyHost(req.value(QStringLiteral("proxyHost")).toString());
        }
        if (req.contains(QStringLiteral("proxyPort"))) {
            setDefaultProxyPort(req.value(QStringLiteral("proxyPort")).toInt(0));
        }
        if (req.contains(QStringLiteral("proxyUser"))) {
            setDefaultProxyUser(req.value(QStringLiteral("proxyUser")).toString());
        }
        if (req.contains(QStringLiteral("proxyPassword"))) {
            setDefaultProxyPassword(req.value(QStringLiteral("proxyPassword")).toString());
        }
    } else if (cmd == QStringLiteral("retryFailed")) {
        retryFailed();
    } else {
        res[QStringLiteral("ok")] = false;
        res.insert(QStringLiteral("error"), QStringLiteral("unknown_cmd"));
    }

    return QString::fromUtf8(QJsonDocument(res).toJson(QJsonDocument::Compact));
}

void DownloadManager::onTaskProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    auto* task = qobject_cast<DownloaderTask*>(sender());
    if (!task) return;
    qint64 previous = m_taskLastReceived.value(task, 0);
    qint64 delta = bytesReceived - previous;
    if (delta < 0) delta = 0;
    m_taskLastReceived[task] = bytesReceived;

    const QString queueName = m_taskQueue.value(task, defaultQueueName());
    if (QueueInfo* info = queueInfo(queueName)) {
        info->downloadedToday += delta;
        if (info->quotaEnabled && info->quotaBytes > 0 && info->downloadedToday >= info->quotaBytes) {
            enforceQueuePolicies();
        }
    }
    m_taskReceived[task] = bytesReceived;
    m_taskTotal[task] = bytesTotal;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool forceUpdate = (bytesTotal > 0 && bytesReceived >= bytesTotal);
    if (forceUpdate || m_lastTotalsUpdateMs <= 0 || (nowMs - m_lastTotalsUpdateMs) >= 120) {
        m_lastTotalsUpdateMs = nowMs;
        updateTotals();
    }
}

void DownloadManager::onTaskSpeedChanged(qint64 bytesPerSecond)
{
    auto* task = qobject_cast<DownloaderTask*>(sender());
    if (!task) return;
    m_taskSpeed[task] = bytesPerSecond;
    updateTotals();
}

void DownloadManager::updateTotals()
{
    qint64 speed = 0;
    qint64 received = 0;
    qint64 total = 0;

    for (auto it = m_taskSpeed.constBegin(); it != m_taskSpeed.constEnd(); ++it) {
        speed += it.value();
    }
    for (auto it = m_taskReceived.constBegin(); it != m_taskReceived.constEnd(); ++it) {
        received += it.value();
    }
    for (auto it = m_taskTotal.constBegin(); it != m_taskTotal.constEnd(); ++it) {
        total += it.value();
    }

    if (speed != m_totalSpeed || received != m_totalReceived || total != m_totalSize) {
        m_totalSpeed = speed;
        m_totalReceived = received;
        m_totalSize = total;
        emit totalsChanged();
    }
}

DownloaderTask* DownloadManager::createTask(const QUrl& url,
                                            const QString& filePath,
                                            const QString& queueName,
                                            const QString& category,
                                            int segments)
{
    DownloaderTask* task = new DownloaderTask(url, filePath, segments, this);
    task->setUserAgent(m_defaultUserAgent);
    task->setAllowInsecureSsl(m_defaultAllowInsecureSsl);
    task->setProxyHost(m_defaultProxyHost);
    task->setProxyPort(qBound(0, m_defaultProxyPort, 65535));
    task->setProxyUser(m_defaultProxyUser);
    task->setProxyPassword(m_defaultProxyPassword);
    m_taskQueue[task] = queueName;
    m_taskCategory[task] = category;
    m_taskLastReceived[task] = 0;
    m_taskMaxSpeed[task] = 0;
    m_taskCompletedAt[task] = 0;
    m_taskRetryCount[task] = 0;
    m_taskPriority[task] = task->priority();
    m_taskCreatedOrder[task] = ++m_taskOrderCounter;
    applyTaskSpeed(task);

    m_model.addDownload(task, queueName, category);
    m_queue.append(task);

    connect(task, &DownloaderTask::finished, this, &DownloadManager::onTaskFinishedWrapper);
    connect(task, &DownloaderTask::stateChanged, this, &DownloadManager::countsChanged);
    connect(task, &DownloaderTask::stateChanged, this, &DownloadManager::scheduleSave);
    connect(task, &DownloaderTask::progress, this, &DownloadManager::onTaskProgress);
    connect(task, &DownloaderTask::speedChanged, this, &DownloadManager::onTaskSpeedChanged);
    connect(task, &DownloaderTask::mirrorUrlsChanged, this, &DownloadManager::scheduleSave);
    connect(task, &DownloaderTask::mirrorIndexChanged, this, &DownloadManager::scheduleSave);
    connect(task, &DownloaderTask::checksumChanged, this, &DownloadManager::scheduleSave);
    connect(task, &DownloaderTask::verifyOnCompleteChanged, this, &DownloadManager::scheduleSave);
    connect(task, &DownloaderTask::resumeWarningChanged, this, &DownloadManager::scheduleSave);
    connect(task, &DownloaderTask::logLinesChanged, this, &DownloadManager::scheduleSave);
    connect(task, &DownloaderTask::speedHistoryChanged, this, &DownloadManager::scheduleSave);
    connect(task, &DownloaderTask::postActionsChanged, this, &DownloadManager::scheduleSave);
    connect(task, &DownloaderTask::retryPolicyChanged, this, &DownloadManager::scheduleSave);
    connect(task, &DownloaderTask::networkOptionsChanged, this, &DownloadManager::scheduleSave);
    connect(task, &DownloaderTask::priorityChanged, this, [this, task]() {
        if (!task) return;
        m_taskPriority[task] = task->priority();
        scheduleSave();
        startQueued();
    });
    connect(task, &DownloaderTask::errorStateChanged, this, [this, task]() {
        writeTelemetryEvent(QStringLiteral("task_error_state"),
                            {
                                {QStringLiteral("url"), task ? task->url() : QString()},
                                {QStringLiteral("errorCode"), task ? task->errorCode() : QString()},
                                {QStringLiteral("errorCategory"), task ? task->errorCategory() : QString()},
                                {QStringLiteral("errorMessage"), task ? task->errorMessage() : QString()},
                                {QStringLiteral("httpStatus"), task ? task->lastHttpStatus() : 0},
                                {QStringLiteral("networkError"), task ? task->lastNetworkError() : -1}
                            });
    });

    return task;
}

void DownloadManager::loadSession()
{
    if (m_sessionPath.isEmpty()) return;
    const QJsonDocument doc = loadSessionDocument();
    if (!doc.isObject()) return;
    const QJsonObject root = doc.object();

    m_restoreInProgress = true;

    if (root.contains("maxConcurrent")) setMaxConcurrent(root.value("maxConcurrent").toInt(m_maxConcurrent));
    if (root.contains("globalMaxSpeed")) setGlobalMaxSpeed(static_cast<qint64>(root.value("globalMaxSpeed").toDouble(m_globalMaxSpeed)));
    if (root.contains("pauseOnBattery")) setPauseOnBattery(root.value("pauseOnBattery").toBool(false));
    if (root.contains("resumeOnAC")) setResumeOnAC(root.value("resumeOnAC").toBool(true));
    if (root.contains("perHostMaxConcurrent")) setPerHostMaxConcurrent(root.value("perHostMaxConcurrent").toInt(m_perHostMaxConcurrent));
    if (root.contains("persistSensitiveOptions")) setPersistSensitiveOptions(root.value("persistSensitiveOptions").toBool(false));
    if (root.contains("telemetryEnabled")) setTelemetryEnabled(root.value("telemetryEnabled").toBool(true));
    if (root.contains("defaultUserAgent")) setDefaultUserAgent(root.value("defaultUserAgent").toString(m_defaultUserAgent));
    if (root.contains("defaultAllowInsecureSsl")) setDefaultAllowInsecureSsl(root.value("defaultAllowInsecureSsl").toBool(m_defaultAllowInsecureSsl));
    const QJsonObject defaultProxyObj = root.value("defaultProxy").toObject();
    if (defaultProxyObj.contains("host")) setDefaultProxyHost(defaultProxyObj.value("host").toString());
    if (defaultProxyObj.contains("port")) setDefaultProxyPort(defaultProxyObj.value("port").toInt(0));
    if (defaultProxyObj.contains("user")) setDefaultProxyUser(defaultProxyObj.value("user").toString());
    if (defaultProxyObj.contains("password")) setDefaultProxyPassword(defaultProxyObj.value("password").toString());

    m_queues.clear();
    m_queueOrder.clear();
    const QJsonArray queues = root.value("queues").toArray();
    for (const QJsonValue& v : queues) {
        if (!v.isObject()) continue;
        const QJsonObject obj = v.toObject();
        const QString name = obj.value("name").toString();
        if (name.isEmpty()) continue;
        QueueInfo info;
        info.name = name;
        info.maxConcurrent = obj.value("maxConcurrent").toInt(m_maxConcurrent);
        info.maxSpeed = static_cast<qint64>(obj.value("maxSpeed").toDouble(0));
        info.scheduleEnabled = obj.value("scheduleEnabled").toBool(false);
        info.startMinutes = obj.value("startMinutes").toInt(0);
        info.endMinutes = obj.value("endMinutes").toInt(0);
        info.quotaEnabled = obj.value("quotaEnabled").toBool(false);
        info.quotaBytes = static_cast<qint64>(obj.value("quotaBytes").toDouble(0));
        info.downloadedToday = static_cast<qint64>(obj.value("downloadedToday").toDouble(0));
        const QString dateStr = obj.value("lastResetDate").toString();
        info.lastResetDate = QDate::fromString(dateStr, Qt::ISODate);
        if (!info.lastResetDate.isValid()) info.lastResetDate = QDate::currentDate();
        m_queues.insert(name, info);
        m_queueOrder.append(name);
    }
    ensureDefaultQueue();

    m_categoryFolders.clear();
    const QJsonObject catFolders = root.value("categoryFolders").toObject();
    for (auto it = catFolders.begin(); it != catFolders.end(); ++it) {
        const QString key = it.key();
        const QString value = utils::normalizeFilePath(it.value().toString());
        if (!key.isEmpty() && !value.isEmpty()) {
            m_categoryFolders.insert(key, value);
        }
    }

    m_domainRules.clear();
    const QJsonObject domainRules = root.value("domainRules").toObject();
    for (auto it = domainRules.begin(); it != domainRules.end(); ++it) {
        const QString key = utils::normalizeHost(it.key());
        const QString value = it.value().toString();
        if (!key.isEmpty() && !value.isEmpty()) {
            m_domainRules.insert(key, value);
        }
    }

    const QJsonArray items = root.value("items").toArray();
    for (const QJsonValue& v : items) {
        if (!v.isObject()) continue;
        const QJsonObject obj = v.toObject();
        const QString urlStr = obj.value("url").toString();
        QString filePath = obj.value("filePath").toString();
        if (urlStr.isEmpty() || filePath.isEmpty()) continue;
        const int segments = normalizedSegmentCount(obj.value("segments").toInt(8));
        const QString queueName = obj.value("queueName").toString(defaultQueueName());
        const QString category = obj.value("category").toString(
            utils::toString(utils::detectCategory(filePath)));
        const QString state = obj.value("state").toString();
        const qint64 taskMaxSpeed = static_cast<qint64>(obj.value("taskMaxSpeed").toDouble(0));
        const qint64 bytesTotal = static_cast<qint64>(obj.value("bytesTotal").toDouble(0));
        const qint64 bytesReceived = static_cast<qint64>(obj.value("bytesReceived").toDouble(0));
        const qint64 lastSpeed = static_cast<qint64>(obj.value("lastSpeed").toDouble(0));
        const int lastEta = obj.value("lastEta").toInt(-1);
        const qint64 pausedAt = static_cast<qint64>(obj.value("pausedAt").toDouble(0));
        const QString pauseReason = obj.value("pauseReason").toString();
        const qint64 completedAt = static_cast<qint64>(obj.value("completedAt").toDouble(0));
        const QString etag = obj.value("etag").toString();
        const QString lastModified = obj.value("lastModified").toString();
        const QString resumeWarning = obj.value("resumeWarning").toString();
        const int priority = obj.value("priority").toInt(100);
        const bool adaptiveSegments = obj.contains("adaptiveSegments")
            ? obj.value("adaptiveSegments").toBool(true)
            : true;
        const QJsonArray mirrorsArray = obj.value("mirrors").toArray();
        QStringList mirrorUrls;
        for (const QJsonValue& mv : mirrorsArray) {
            const QString mirror = mv.toString();
            if (!mirror.isEmpty()) mirrorUrls.append(mirror);
        }
        if (mirrorUrls.isEmpty()) mirrorUrls.append(urlStr);
        const int mirrorIndex = obj.value("mirrorIndex").toInt(0);
        const QString checksumAlgo = obj.value("checksumAlgo").toString();
        const QString checksumExpected = obj.value("checksumExpected").toString();
        const QString checksumActual = obj.value("checksumActual").toString();
        const QString checksumState = obj.value("checksumState").toString();
        const bool verifyOnComplete = obj.value("verifyOnComplete").toBool(false);
        const bool postOpenFile = obj.value("postOpenFile").toBool(false);
        const bool postRevealFolder = obj.value("postRevealFolder").toBool(false);
        const bool postExtract = obj.value("postExtract").toBool(false);
        const QString postScript = obj.value("postScript").toString();
        const int retryMax = obj.value("retryMax").toInt(-1);
        const int retryDelay = obj.value("retryDelaySec").toInt(-1);
        const QString cookieHeader = obj.value("cookieHeader").toString();
        const QJsonArray headersArray = obj.value("headers").toArray();
        QStringList customHeaders;
        for (const QJsonValue& hv : headersArray) {
            const QString header = hv.toString();
            if (!header.isEmpty()) customHeaders.append(header);
        }
        const QString authUser = obj.value("authUser").toString();
        const QString authPassword = obj.value("authPassword").toString();
        const QString userAgent = obj.value("userAgent").toString(m_defaultUserAgent);
        const bool allowInsecureSsl = obj.contains("allowInsecureSsl")
            ? obj.value("allowInsecureSsl").toBool(m_defaultAllowInsecureSsl)
            : m_defaultAllowInsecureSsl;
        const QJsonObject proxyObj = obj.value("proxy").toObject();
        const QString proxyHost = proxyObj.contains("host")
            ? proxyObj.value("host").toString()
            : m_defaultProxyHost;
        const int proxyPort = proxyObj.contains("port")
            ? proxyObj.value("port").toInt(0)
            : m_defaultProxyPort;
        const QString proxyUser = proxyObj.contains("user")
            ? proxyObj.value("user").toString()
            : m_defaultProxyUser;
        const QString proxyPassword = proxyObj.contains("password")
            ? proxyObj.value("password").toString()
            : m_defaultProxyPassword;

        const QUrl url(urlStr);
        if (!filePath.isEmpty()) {
            const QString oldLocalPath = utils::normalizeFilePath(filePath);
            QFileInfo info(oldLocalPath);
            const QString maybeName = utils::fileNameFromUrl(url);
            if (!maybeName.isEmpty() && utils::looksLikeGuidName(info.fileName())) {
                const QString newLocalPath = info.dir().filePath(maybeName);

                // If an old GUID-based name exists on disk, try to rename it (and any segment part files).
                bool switchedToNew = false;

                const QFileInfo oldMainInfo(oldLocalPath);
                const QFileInfo newMainInfo(newLocalPath);
                if (oldMainInfo.exists() && !newMainInfo.exists()) {
                    if (QFile::rename(oldLocalPath, newLocalPath)) {
                        switchedToNew = true;
                    }
                }

                for (int i = 0; i < segments; ++i) {
                    const QString oldPart = QString("%1.part%2").arg(oldLocalPath).arg(i);
                    if (!QFile::exists(oldPart)) continue;

                    const QString newPart = QString("%1.part%2").arg(newLocalPath).arg(i);
                    if (QFile::exists(newPart)) continue;

                    if (QFile::rename(oldPart, newPart)) {
                        switchedToNew = true;
                    }
                }

                // If nothing exists yet, prefer the nicer name for future writes.
                if (!switchedToNew) {
                    const bool oldExists = oldMainInfo.exists();
                    bool anyOldParts = false;
                    for (int i = 0; i < segments; ++i) {
                        if (QFile::exists(QString("%1.part%2").arg(oldLocalPath).arg(i))) {
                            anyOldParts = true;
                            break;
                        }
                    }
                    if (!oldExists && !anyOldParts) {
                        switchedToNew = true;
                    }
                }

                if (switchedToNew) {
                    filePath = newLocalPath;
                } else {
                    filePath = oldLocalPath;
                }
            } else {
                filePath = oldLocalPath;
            }
        }

        DownloaderTask* task = createTask(url, filePath, queueName, category, segments);
        task->setMirrorUrls(mirrorUrls);
        task->setMirrorIndex(mirrorIndex);
        task->setChecksumAlgorithm(checksumAlgo);
        task->setChecksumExpected(checksumExpected);
        if (!checksumActual.isEmpty()) task->setChecksumActual(checksumActual);
        if (!checksumState.isEmpty()) task->setChecksumState(checksumState);
        task->setVerifyOnComplete(verifyOnComplete);
        task->setPostOpenFile(postOpenFile);
        task->setPostRevealFolder(postRevealFolder);
        task->setPostExtract(postExtract);
        if (!postScript.isEmpty()) task->setPostScript(postScript);
        if (!customHeaders.isEmpty()) task->setCustomHeaders(customHeaders);
        if (!cookieHeader.isEmpty()) task->setCookieHeader(cookieHeader);
        if (!authUser.isEmpty()) task->setAuthUser(authUser);
        if (!authPassword.isEmpty()) task->setAuthPassword(authPassword);
        task->setUserAgent(userAgent);
        task->setAllowInsecureSsl(allowInsecureSsl);
        task->setProxyHost(proxyHost);
        task->setProxyPort(qBound(0, proxyPort, 65535));
        task->setProxyUser(proxyUser);
        task->setProxyPassword(proxyPassword);
        if (retryMax >= 0) task->setRetryMax(retryMax);
        if (retryDelay >= 0) task->setRetryDelaySec(retryDelay);
        task->setPriority(qBound(0, priority, 1000));
        task->setAdaptiveSegmentsEnabled(adaptiveSegments);
        m_taskPriority[task] = task->priority();
        if (taskMaxSpeed > 0) {
            m_taskMaxSpeed[task] = taskMaxSpeed;
            applyTaskSpeed(task);
        }
        if (state == "Paused") {
            task->markPaused();
        } else if (state == "Error") {
            task->markError();
        } else if (state == "Done") {
            task->markDone();
        } else if (state == "Canceled") {
            task->markCanceled();
        }

        const QFileInfo restoredInfo(filePath);
        const qint64 actualCompletedSize = (state == "Done" && restoredInfo.exists() && restoredInfo.isFile())
            ? qMax<qint64>(0, restoredInfo.size())
            : 0;
        const qint64 receivedFromDisk = utils::bytesReceivedOnDisk(filePath, segments);
        const qint64 received = actualCompletedSize > 0
            ? actualCompletedSize
            : (bytesReceived > 0 ? bytesReceived : receivedFromDisk);
        const qint64 total = actualCompletedSize > 0
            ? actualCompletedSize
            : (bytesTotal > 0 ? bytesTotal : 0);
        m_model.seedProgress(task, received, total);
        m_taskReceived[task] = received;
        m_taskTotal[task] = total;
        m_taskLastReceived[task] = received;
        if (completedAt > 0) {
            m_taskCompletedAt[task] = completedAt;
        }

        qint64 pausedAtSeed = 0;
        if (state == "Paused") {
            pausedAtSeed = pausedAt > 0 ? pausedAt : task->pausedAt();
        }
        task->seedPersistedStats(lastSpeed, lastEta, pausedAtSeed, pauseReason);
        task->setResumeInfo(etag, lastModified);
        if (!resumeWarning.isEmpty()) task->setResumeWarning(resumeWarning);
        if (state == "Done" || state == "Canceled" || state == "Error") {
            m_model.seedFinished(task, true);
        }
    }

    m_restoreInProgress = false;
    emit queuesChanged();
    emit categoryFoldersChanged();
    emit domainRulesChanged();
    updateTotals();
    startQueued();
}

void DownloadManager::scheduleSave()
{
    if (m_restoreInProgress || m_sessionPath.isEmpty()) return;
    if (!m_saveTimer.isActive()) {
        m_saveTimer.start();
    }
}

void DownloadManager::saveSession()
{
    if (m_restoreInProgress || m_sessionPath.isEmpty()) return;

    QJsonObject root;
    root.insert("version", 6);
    root.insert("maxConcurrent", m_maxConcurrent);
    root.insert("globalMaxSpeed", static_cast<double>(m_globalMaxSpeed));
    root.insert("pauseOnBattery", m_pauseOnBattery);
    root.insert("resumeOnAC", m_resumeOnAC);
    root.insert("perHostMaxConcurrent", m_perHostMaxConcurrent);
    root.insert("persistSensitiveOptions", m_persistSensitiveOptions);
    root.insert("telemetryEnabled", m_telemetryEnabled);
    root.insert("defaultUserAgent", m_defaultUserAgent);
    root.insert("defaultAllowInsecureSsl", m_defaultAllowInsecureSsl);
    QJsonObject defaultProxyObj;
    defaultProxyObj.insert("host", m_defaultProxyHost);
    defaultProxyObj.insert("port", m_defaultProxyPort);
    if (m_persistSensitiveOptions) {
        defaultProxyObj.insert("user", m_defaultProxyUser);
        defaultProxyObj.insert("password", m_defaultProxyPassword);
    }
    root.insert("defaultProxy", defaultProxyObj);

    QJsonArray queues;
    for (const QString& name : m_queueOrder) {
        if (!m_queues.contains(name)) continue;
        const QueueInfo& info = m_queues.value(name);
        QJsonObject obj;
        obj.insert("name", info.name);
        obj.insert("maxConcurrent", info.maxConcurrent);
        obj.insert("maxSpeed", static_cast<double>(info.maxSpeed));
        obj.insert("scheduleEnabled", info.scheduleEnabled);
        obj.insert("startMinutes", info.startMinutes);
        obj.insert("endMinutes", info.endMinutes);
        obj.insert("quotaEnabled", info.quotaEnabled);
        obj.insert("quotaBytes", static_cast<double>(info.quotaBytes));
        obj.insert("downloadedToday", static_cast<double>(info.downloadedToday));
        obj.insert("lastResetDate", info.lastResetDate.toString(Qt::ISODate));
        queues.append(obj);
    }
    root.insert("queues", queues);

    QJsonObject catFolders;
    for (auto it = m_categoryFolders.begin(); it != m_categoryFolders.end(); ++it) {
        catFolders.insert(it.key(), it.value());
    }
    root.insert("categoryFolders", catFolders);

    QJsonObject domainRules;
    for (auto it = m_domainRules.begin(); it != m_domainRules.end(); ++it) {
        domainRules.insert(it.key(), it.value());
    }
    root.insert("domainRules", domainRules);

    QJsonArray items;
    for (int i = 0; i < m_model.rowCount(); ++i) {
        DownloaderTask* task = m_model.taskAt(i);
        if (!task) continue;
        const QString state = task->stateString();

        QJsonObject obj;
        obj.insert("url", task->url());
        obj.insert("filePath", task->fileName());
        obj.insert("segments", task->segments());
        obj.insert("queueName", m_taskQueue.value(task, defaultQueueName()));
        obj.insert("category", m_taskCategory.value(task, utils::toString(utils::detectCategory(task->fileName()))));
        obj.insert("state", state);
        obj.insert("taskMaxSpeed", static_cast<double>(m_taskMaxSpeed.value(task, 0)));
        obj.insert("bytesReceived", static_cast<double>(m_taskReceived.value(task, 0)));
        obj.insert("bytesTotal", static_cast<double>(m_taskTotal.value(task, 0)));
        obj.insert("lastSpeed", static_cast<double>(task->lastSpeed()));
        obj.insert("lastEta", task->lastEta());
        obj.insert("pausedAt", static_cast<double>(task->pausedAt()));
        obj.insert("pauseReason", task->pauseReason());
        obj.insert("completedAt", static_cast<double>(m_taskCompletedAt.value(task, 0)));
        obj.insert("etag", task->etag());
        obj.insert("lastModified", task->lastModified());
        obj.insert("resumeWarning", task->resumeWarning());
        QJsonArray mirrorArray;
        const QStringList mirrorUrls = task->mirrorUrls();
        for (const QString& m : mirrorUrls) mirrorArray.append(m);
        obj.insert("mirrors", mirrorArray);
        obj.insert("mirrorIndex", task->mirrorIndex());
        obj.insert("checksumAlgo", task->checksumAlgorithm());
        obj.insert("checksumExpected", task->checksumExpected());
        obj.insert("checksumActual", task->checksumActual());
        obj.insert("checksumState", task->checksumState());
        obj.insert("verifyOnComplete", task->verifyOnComplete());
        obj.insert("postOpenFile", task->postOpenFile());
        obj.insert("postRevealFolder", task->postRevealFolder());
        obj.insert("postExtract", task->postExtract());
        obj.insert("postScript", task->postScript());
        obj.insert("retryMax", task->retryMax());
        obj.insert("retryDelaySec", task->retryDelaySec());
        obj.insert("priority", m_taskPriority.value(task, task->priority()));
        obj.insert("adaptiveSegments", task->adaptiveSegmentsEnabled());
        obj.insert("userAgent", task->userAgent());
        obj.insert("allowInsecureSsl", task->allowInsecureSsl());
        obj.insert("errorCategory", task->errorCategory());
        obj.insert("errorCode", task->errorCode());
        obj.insert("errorMessage", task->errorMessage());
        obj.insert("lastHttpStatus", task->lastHttpStatus());
        obj.insert("lastNetworkError", task->lastNetworkError());
        if (m_persistSensitiveOptions) {
            QJsonArray headersArray;
            for (const QString& header : task->customHeaders()) headersArray.append(header);
            obj.insert("headers", headersArray);
            obj.insert("cookieHeader", task->cookieHeader());
            obj.insert("authUser", task->authUser());
            obj.insert("authPassword", task->authPassword());
        }
        QJsonObject proxyObj;
        proxyObj.insert("host", task->proxyHost());
        proxyObj.insert("port", task->proxyPort());
        if (m_persistSensitiveOptions) {
            proxyObj.insert("user", task->proxyUser());
            proxyObj.insert("password", task->proxyPassword());
        }
        obj.insert("proxy", proxyObj);
        items.append(obj);
    }
    root.insert("items", items);

    if (!m_sessionBackupPath.isEmpty() && QFile::exists(m_sessionPath)) {
        QFile::remove(m_sessionBackupPath);
        QFile::copy(m_sessionPath, m_sessionBackupPath);
    }

    QSaveFile file(m_sessionPath);
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.commit();
}

DownloadManager::QueueInfo* DownloadManager::queueInfo(const QString& name)
{
    auto it = m_queues.find(name);
    if (it == m_queues.end()) return nullptr;
    return &it.value();
}

const DownloadManager::QueueInfo* DownloadManager::queueInfo(const QString& name) const
{
    auto it = m_queues.constFind(name);
    if (it == m_queues.constEnd()) return nullptr;
    return &it.value();
}

void DownloadManager::ensureDefaultQueue()
{
    if (m_queueOrder.isEmpty()) {
        QueueInfo info;
        info.name = QStringLiteral("General");
        info.maxConcurrent = m_maxConcurrent;
        info.lastResetDate = QDate::currentDate();
        m_queues.insert(info.name, info);
        m_queueOrder.append(info.name);
        emit queuesChanged();
    }
}

QString DownloadManager::categoryFolderForName(const QString& category) const
{
    const QString key = category.trimmed();
    if (key.isEmpty() || key == "Auto") return QString();
    return m_categoryFolders.value(key);
}

void DownloadManager::revealPath(const QString& path) const
{
    const QString localPath = utils::normalizeFilePath(path);
    if (localPath.isEmpty()) return;
    QFileInfo info(localPath);
    const QString absPath = info.absoluteFilePath();
#if defined(Q_OS_MAC)
    if (info.exists() && !absPath.isEmpty()) {
        QProcess::startDetached(QStringLiteral("open"), QStringList() << QStringLiteral("-R") << absPath);
        return;
    }
#elif defined(Q_OS_WIN)
    if (info.exists() && !absPath.isEmpty()) {
        const QString nativePath = QDir::toNativeSeparators(absPath);
        QProcess::startDetached(QStringLiteral("explorer"), QStringList() << "/select," + nativePath);
        return;
    }
#endif
    if (!info.absolutePath().isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
    }
}


bool DownloadManager::renameTaskFilesOnDisk(const QString& oldPath, const QString& newPath, int segments) const
{
    if (oldPath.isEmpty() || newPath.isEmpty()) return false;
    if (oldPath == newPath) return true;

    bool ok = true;
    if (QFile::exists(newPath) && QFile::exists(oldPath)) {
        return false;
    }

    if (QFile::exists(oldPath)) {
        ok = ok && QFile::rename(oldPath, newPath);
    }

    const QString oldSingle = oldPath + ".part";
    const QString newSingle = newPath + ".part";
    if (QFile::exists(oldSingle)) {
        ok = ok && QFile::rename(oldSingle, newSingle);
    }

    const int maxParts = qMax(1, segments);
    for (int i = 0; i < maxParts; ++i) {
        const QString oldPart = QString("%1.part%2").arg(oldPath).arg(i);
        const QString newPart = QString("%1.part%2").arg(newPath).arg(i);
        if (QFile::exists(oldPart)) {
            ok = ok && QFile::rename(oldPart, newPart);
        }
    }
    return ok;
}

bool DownloadManager::deleteTaskFilesOnDisk(const QString& filePath, int segments, int effectiveSegments) const
{
    if (filePath.isEmpty()) return false;

    bool removedAnything = false;
    bool ok = true;

    const auto removeIfExists = [&](const QString& path) {
        if (!QFile::exists(path)) {
            return;
        }
        removedAnything = true;
        if (!QFile::remove(path)) {
            ok = false;
        }
    };

    removeIfExists(filePath);
    removeIfExists(filePath + ".part");

    const int maxParts = qMax(1, qMax(segments, effectiveSegments));
    for (int i = 0; i < maxParts; ++i) {
        removeIfExists(QString("%1.part%2").arg(filePath).arg(i));
    }

    return removedAnything && ok;
}

void DownloadManager::applyTaskSpeed(DownloaderTask* task)
{
    if (!task) return;
    const QString qname = m_taskQueue.value(task, defaultQueueName());
    const QueueInfo* info = queueInfo(qname);
    qint64 effective = m_globalMaxSpeed;
    if (info && info->maxSpeed > 0) {
        if (effective == 0 || info->maxSpeed < effective) {
            effective = info->maxSpeed;
        }
    }
    const qint64 taskLimit = m_taskMaxSpeed.value(task, 0);
    if (taskLimit > 0) {
        if (effective == 0 || taskLimit < effective) {
            effective = taskLimit;
        }
    }
    task->setMaxSpeed(effective);
}

bool DownloadManager::isWithinSchedule(const QueueInfo& info, const QTime& now) const
{
    if (!info.scheduleEnabled) return true;
    const int start = info.startMinutes;
    const int end = info.endMinutes;
    const int current = now.hour() * 60 + now.minute();
    if (start == end) return true;
    if (start < end) {
        return current >= start && current < end;
    }
    return current >= start || current < end;
}

bool DownloadManager::isQueueAllowed(const QueueInfo& info, const QTime& now) const
{
    if (info.scheduleEnabled && !isWithinSchedule(info, now)) return false;
    if (info.quotaEnabled && info.quotaBytes > 0 && info.downloadedToday >= info.quotaBytes) return false;
    return true;
}

void DownloadManager::enforceQueuePolicies()
{
    const QDate today = QDate::currentDate();
    const QTime now = QTime::currentTime();
    const bool blockByBattery = m_pauseOnBattery && m_onBattery;

    for (auto it = m_queues.begin(); it != m_queues.end(); ++it) {
        QueueInfo& info = it.value();
        if (!info.lastResetDate.isValid() || info.lastResetDate != today) {
            info.lastResetDate = today;
            info.downloadedToday = 0;
        }

        const bool allowed = isQueueAllowed(info, now);
        for (DownloaderTask* task : m_queue) {
            if (!task) continue;
            const QString qname = m_taskQueue.value(task, defaultQueueName());
            if (qname != info.name) continue;

            if (task->isRunning()) {
                if (blockByBattery) {
                    task->pauseWithReason(QStringLiteral("Battery"));
                    m_taskPausedByBattery[task] = true;
                } else if (!allowed) {
                    if (info.scheduleEnabled && !isWithinSchedule(info, now)) {
                        task->pauseWithReason(QStringLiteral("Schedule"));
                    } else if (info.quotaEnabled && info.quotaBytes > 0 && info.downloadedToday >= info.quotaBytes) {
                        task->pauseWithReason(QStringLiteral("Quota"));
                    } else {
                        task->pause();
                    }
                    if (info.scheduleEnabled && !isWithinSchedule(info, now)) {
                        m_taskPausedBySchedule[task] = true;
                    }
                    if (info.quotaEnabled && info.quotaBytes > 0 && info.downloadedToday >= info.quotaBytes) {
                        m_taskPausedByQuota[task] = true;
                    }
                }
            }

            if (task->stateString() == "Paused") {
                const bool pausedBySchedule = m_taskPausedBySchedule.value(task, false);
                const bool pausedByQuota = m_taskPausedByQuota.value(task, false);
                const bool pausedByBattery = m_taskPausedByBattery.value(task, false);
                const bool canResume = allowed && (!blockByBattery) && (resumeOnAC() || !pausedByBattery);
                if (canResume && (pausedBySchedule || pausedByQuota || pausedByBattery)) {
                    m_taskPausedBySchedule[task] = false;
                    m_taskPausedByQuota[task] = false;
                    m_taskPausedByBattery[task] = false;
                    task->resume();
                }
            }
        }
    }
}

void DownloadManager::schedulerTick()
{
    enforceQueuePolicies();
    startQueued();
}
