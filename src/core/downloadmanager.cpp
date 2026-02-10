module;
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
#include <QJsonValue>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTime>
#include <QtGlobal>
#include <QClipboard>
#include <QGuiApplication>
#include <QPointer>
#include <QTextStream>
#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QByteArrayView>
#include <QtConcurrent>

module raad.core.downloadmanager;

import raad.utils.download_utils;
import raad.utils.category_utils;

namespace utils = raad::utils;

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

    const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!baseDir.isEmpty()) {
        QDir().mkpath(baseDir);
        m_sessionPath = baseDir + "/downloads.json";
    }

    ensureDefaultQueue();
    loadSession();
    schedulerTick();
    updatePowerState();
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
    QString resolvedCategory = category.isEmpty() || category == "Auto"
        ? (normalizedPath.isEmpty() ? QStringLiteral("Auto") : utils::detectCategory(normalizedPath))
        : category;

    if (normalizedPath.isEmpty() || QFileInfo(normalizedPath).isDir()) {
        const QString fallback = normalizedPath;
        normalizedPath = resolveDownloadPath(urlStr, resolvedCategory, fallback);
    }

    if (resolvedCategory == "Auto" && !normalizedPath.isEmpty()) {
        resolvedCategory = utils::detectCategory(normalizedPath);
    }

    if (!normalizedPath.isEmpty()) {
        QFileInfo info(normalizedPath);
        const QString maybeName = utils::fileNameFromUrl(url);
        if (!maybeName.isEmpty() && utils::looksLikeGuidName(info.fileName())) {
            normalizedPath = info.dir().filePath(maybeName);
        }
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

    DownloaderTask* task = createTask(url, normalizedPath, resolvedQueue, resolvedCategory, 8);
    if (task && options) {
        applyTaskOptions(task, *options);
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
    const QString cookieHeader = options.value("cookieHeader").toString();
    if (!cookieHeader.isEmpty()) task->setCookieHeader(cookieHeader);
    const QString authUser = options.value("authUser").toString();
    const QString authPassword = options.value("authPassword").toString();
    if (!authUser.isEmpty()) task->setAuthUser(authUser);
    if (!authPassword.isEmpty()) task->setAuthPassword(authPassword);
    const QString proxyHost = options.value("proxyHost").toString();
    const int proxyPort = options.value("proxyPort").toInt(0);
    const QString proxyUser = options.value("proxyUser").toString();
    const QString proxyPassword = options.value("proxyPassword").toString();
    if (!proxyHost.isEmpty()) task->setProxyHost(proxyHost);
    if (proxyPort > 0) task->setProxyPort(proxyPort);
    if (!proxyUser.isEmpty()) task->setProxyUser(proxyUser);
    if (!proxyPassword.isEmpty()) task->setProxyPassword(proxyPassword);

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
    if (state == "Done") {
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
            t->restart();
            startQueued();
        } else {
            const int maxRetries = t->retryMax() >= 0 ? t->retryMax() : m_autoRetryMax;
            const int delaySec = t->retryDelaySec() >= 0 ? t->retryDelaySec() : m_autoRetryDelaySec;
            int attempts = m_taskRetryCount.value(t, 0);
            if (attempts < maxRetries) {
                m_taskRetryCount[t] = attempts + 1;
                QPointer<DownloaderTask> taskPtr(t);
                emit toastRequested(QStringLiteral("Retrying in %1s: %2").arg(delaySec).arg(name), QStringLiteral("warning"));
                QTimer::singleShot(delaySec * 1000, this, [this, taskPtr]() {
                    if (!taskPtr) return;
                    if (taskPtr->stateString() == "Error") {
                        taskPtr->restart();
                        startQueued();
                    }
                });
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
    int running = 0;
    for (DownloaderTask* t : m_queue) {
        if (t && t->isRunning()) {
            running++;
            const QString qname = m_taskQueue.value(t, defaultQueueName());
            runningPerQueue[qname] = runningPerQueue.value(qname, 0) + 1;
        }
    }

    const QTime now = QTime::currentTime();
    for (DownloaderTask* candidate : m_queue) {
        if (running >= m_maxConcurrent) break;
        if (!candidate || !candidate->isIdle()) continue;
        if (m_pauseOnBattery && m_onBattery) continue;

        const QString qname = m_taskQueue.value(candidate, defaultQueueName());
        if (!m_queues.contains(qname)) createQueue(qname);
        const QueueInfo* info = queueInfo(qname);
        if (!info) continue;
        if (!isQueueAllowed(*info, now)) continue;

        const int queueLimit = info->maxConcurrent > 0 ? info->maxConcurrent : m_maxConcurrent;
        if (runningPerQueue.value(qname, 0) >= queueLimit) continue;

        applyTaskSpeed(candidate);
        candidate->start();
        running++;
        runningPerQueue[qname] = runningPerQueue.value(qname, 0) + 1;
    }
    emit countsChanged();
}

void DownloadManager::removeDownload(int index)
{
    DownloaderTask* task = m_model.taskAt(index);
    if (!task) return;
    m_queue.removeAll(task);
    m_taskSpeed.remove(task);
    m_taskReceived.remove(task);
    m_taskTotal.remove(task);
    m_taskLastReceived.remove(task);
    m_taskMaxSpeed.remove(task);
    m_taskCompletedAt.remove(task);
    m_taskRetryCount.remove(task);
    m_taskQueue.remove(task);
    m_taskCategory.remove(task);
    m_taskPausedBySchedule.remove(task);
    m_taskPausedByQuota.remove(task);
    m_taskPausedByBattery.remove(task);
    if (m_checksumWatchers.contains(task)) {
        if (QPointer<QFutureWatcher<QString>> watcher = m_checksumWatchers.take(task)) {
            watcher->cancel();
            watcher->deleteLater();
        }
    }
    task->cancel();
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
                m_taskQueue.remove(task);
                m_taskCategory.remove(task);
                m_taskPausedBySchedule.remove(task);
                m_taskPausedByQuota.remove(task);
                m_taskPausedByBattery.remove(task);
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
    m_taskQueue.clear();
    m_taskCategory.clear();
    m_taskPausedBySchedule.clear();
    m_taskPausedByQuota.clear();
    updateTotals();
    emit countsChanged();
    scheduleSave();
}

void DownloadManager::retryFailed()
{
    for (DownloaderTask* t : m_queue) {
        if (t && t->stateString() == "Error") {
            t->restart();
        }
    }
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

qint64 DownloadManager::taskCompletedAt(int index) const
{
    DownloaderTask* task = m_model.taskAt(index);
    if (!task) return 0;
    return m_taskCompletedAt.value(task, 0);
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

void DownloadManager::pauseTask(int index)
{
    DownloaderTask* task = m_model.taskAt(index);
    if (!task) return;
    task->pause();
    scheduleSave();
}

void DownloadManager::resumeTask(int index)
{
    DownloaderTask* task = m_model.taskAt(index);
    if (!task) return;
    task->resume();
    startQueued();
    scheduleSave();
}

void DownloadManager::togglePause(int index)
{
    DownloaderTask* task = m_model.taskAt(index);
    if (!task) return;
    const QString state = task->stateString();
    if (state == "Active") {
        task->pause();
    } else if (state == "Paused") {
        task->resume();
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

    const QString fallbackFolder = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
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
        obj.insert("category", m_taskCategory.value(task, utils::detectCategory(task->fileName())));
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
    QUrl url(urlStr);
    if (!url.isValid()) {
        emit toastRequested(QStringLiteral("Invalid URL"), QStringLiteral("danger"));
        return;
    }

    QNetworkAccessManager* net = new QNetworkAccessManager(this);
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", "raad/1.0");
    QNetworkReply* reply = net->head(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, net]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString err = reply->errorString();
        const QNetworkReply::NetworkError errCode = reply->error();
        const QVariant len = reply->header(QNetworkRequest::ContentLengthHeader);
        const QByteArray acceptRanges = reply->rawHeader("Accept-Ranges");
        reply->deleteLater();
        net->deleteLater();

        if (status <= 0 || errCode != QNetworkReply::NoError) {
            emit toastRequested(QStringLiteral("Test failed: %1").arg(err), QStringLiteral("danger"));
            return;
        }
        QString msg = QStringLiteral("HTTP %1").arg(status);
        if (len.isValid() && len.toLongLong() > 0) {
            msg += QStringLiteral(" • Size %1").arg(len.toLongLong());
        }
        if (!acceptRanges.isEmpty()) {
            msg += QStringLiteral(" • Ranges %1").arg(QString::fromUtf8(acceptRanges));
        }
        emit toastRequested(msg, QStringLiteral("info"));
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

    m_queues.remove(name);
    m_queueOrder.removeAll(name);
    emit queuesChanged();
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

    emit queuesChanged();
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
    const QString resolved = category.isEmpty() ? utils::detectCategory(task->fileName()) : category;
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
    return utils::detectCategory(name);
}

QString DownloadManager::resolveDownloadPath(const QString& urlStr, const QString& category, const QString& fallbackFolder) const
{
    QUrl url(urlStr);
    QString fileName = utils::fileNameFromUrl(url);
    if (fileName.isEmpty()) fileName = QStringLiteral("download.bin");

    QString effectiveCategory = category;
    if (effectiveCategory.isEmpty() || effectiveCategory == "Auto") {
        effectiveCategory = utils::detectCategory(fileName);
    }
    QString folder = categoryFolderForName(effectiveCategory);
    if (folder.isEmpty()) {
        folder = utils::normalizeFilePath(fallbackFolder);
    }
    if (folder.isEmpty()) {
        folder = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
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
    updateTotals();
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
    m_taskQueue[task] = queueName;
    m_taskCategory[task] = category;
    m_taskLastReceived[task] = 0;
    m_taskMaxSpeed[task] = 0;
    m_taskCompletedAt[task] = 0;
    m_taskRetryCount[task] = 0;
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

    return task;
}

void DownloadManager::loadSession()
{
    if (m_sessionPath.isEmpty()) return;
    QFile file(m_sessionPath);
    if (!file.exists()) return;
    if (!file.open(QIODevice::ReadOnly)) return;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return;
    const QJsonObject root = doc.object();

    m_restoreInProgress = true;

    if (root.contains("maxConcurrent")) setMaxConcurrent(root.value("maxConcurrent").toInt(m_maxConcurrent));
    if (root.contains("globalMaxSpeed")) setGlobalMaxSpeed(static_cast<qint64>(root.value("globalMaxSpeed").toDouble(m_globalMaxSpeed)));
    if (root.contains("pauseOnBattery")) setPauseOnBattery(root.value("pauseOnBattery").toBool(false));
    if (root.contains("resumeOnAC")) setResumeOnAC(root.value("resumeOnAC").toBool(true));

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
        const int segments = obj.value("segments").toInt(8);
        const QString queueName = obj.value("queueName").toString(defaultQueueName());
        const QString category = obj.value("category").toString(utils::detectCategory(filePath));
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
        const QJsonObject proxyObj = obj.value("proxy").toObject();
        const QString proxyHost = proxyObj.value("host").toString();
        const int proxyPort = proxyObj.value("port").toInt(0);
        const QString proxyUser = proxyObj.value("user").toString();
        const QString proxyPassword = proxyObj.value("password").toString();

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
        if (!proxyHost.isEmpty()) task->setProxyHost(proxyHost);
        if (proxyPort > 0) task->setProxyPort(proxyPort);
        if (!proxyUser.isEmpty()) task->setProxyUser(proxyUser);
        if (!proxyPassword.isEmpty()) task->setProxyPassword(proxyPassword);
        if (retryMax >= 0) task->setRetryMax(retryMax);
        if (retryDelay >= 0) task->setRetryDelaySec(retryDelay);
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

        const qint64 receivedFromDisk = utils::bytesReceivedOnDisk(filePath, segments);
        const qint64 received = bytesReceived > 0 ? bytesReceived : receivedFromDisk;
        const qint64 total = bytesTotal > 0 ? bytesTotal : 0;
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
    root.insert("version", 4);
    root.insert("maxConcurrent", m_maxConcurrent);
    root.insert("globalMaxSpeed", static_cast<double>(m_globalMaxSpeed));
    root.insert("pauseOnBattery", m_pauseOnBattery);
    root.insert("resumeOnAC", m_resumeOnAC);

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
        obj.insert("category", m_taskCategory.value(task, utils::detectCategory(task->fileName())));
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
        QJsonArray headersArray;
        for (const QString& header : task->customHeaders()) headersArray.append(header);
        obj.insert("headers", headersArray);
        obj.insert("cookieHeader", task->cookieHeader());
        obj.insert("authUser", task->authUser());
        obj.insert("authPassword", task->authPassword());
        QJsonObject proxyObj;
        proxyObj.insert("host", task->proxyHost());
        proxyObj.insert("port", task->proxyPort());
        proxyObj.insert("user", task->proxyUser());
        proxyObj.insert("password", task->proxyPassword());
        obj.insert("proxy", proxyObj);
        items.append(obj);
    }
    root.insert("items", items);

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
