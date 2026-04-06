module;
#include <QByteArrayView>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>
#include <QSettings>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

module raad.services.update_client;

import raad.utils.download_utils;
import raad.utils.version_utils;

namespace utils = raad::utils;

static QString settingsGroup()
{
    return QStringLiteral("updates");
}

static QString normalizeUpdateMode(const QString& mode)
{
    const QString normalized = mode.trimmed().toLower();
    if (normalized == QStringLiteral("automatic")) return normalized;
    return QStringLiteral("custom");
}

static QString updatesDirPath()
{
    const QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataDir.trimmed().isEmpty()) return QString();
    return QDir(appDataDir).filePath(QStringLiteral("updates"));
}

static QString appUpdaterHelperPath()
{
#if defined(Q_OS_WIN)
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("RaadUpdaterHelper.exe"));
#else
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("RaadUpdaterHelper"));
#endif
}

static bool copyWithOverwrite(const QString& fromPath, const QString& toPath)
{
    if (QFileInfo::exists(toPath) && !QFile::remove(toPath)) {
        return false;
    }
    return QFile::copy(fromPath, toPath);
}

static bool looksLikeManualInstaller(const QString& path)
{
    const QString lower = QFileInfo(path).fileName().toLower();
    return lower.endsWith(QStringLiteral(".dmg"))
        || lower.endsWith(QStringLiteral(".pkg"))
        || lower.endsWith(QStringLiteral(".msi"))
        || lower.endsWith(QStringLiteral(".zip"))
        || lower.endsWith(QStringLiteral(".tar.gz"))
        || lower.endsWith(QStringLiteral(".tar.xz"))
        || lower.endsWith(QStringLiteral(".deb"))
        || lower.endsWith(QStringLiteral(".rpm"))
        || lower.endsWith(QStringLiteral(".exe"));
}

static bool isSelfInstallSupportedAsset(const QString& currentExecutablePath, const QString& downloadedPath)
{
    const QFileInfo currentInfo(currentExecutablePath);
    const QFileInfo downloadedInfo(downloadedPath);
    const QString currentName = currentInfo.fileName().toLower();
    const QString downloadedName = downloadedInfo.fileName().toLower();

    if (!currentName.isEmpty() && downloadedName == currentName) {
        return true;
    }
    if (downloadedName.endsWith(QStringLiteral(".appimage"))) {
        return true;
    }
    if (looksLikeManualInstaller(downloadedPath)) {
        return false;
    }
#if defined(Q_OS_WIN)
    return downloadedName.endsWith(QStringLiteral(".exe"))
        && !downloadedName.contains(QStringLiteral("setup"))
        && !downloadedName.contains(QStringLiteral("installer"));
#else
    return downloadedInfo.suffix().isEmpty();
#endif
}

static bool startUpdaterHelperDetached(const QString& helperPath, const QString& jobPath, QString* errorOut)
{
#if defined(Q_OS_WIN)
    const QString nativeHelper = QDir::toNativeSeparators(helperPath);
    const QString nativeJob = QDir::toNativeSeparators(jobPath);
    if (QProcess::startDetached(helperPath, {QStringLiteral("--job"), jobPath})) {
        return true;
    }

    const QString args = QStringLiteral("--job \"%1\"").arg(nativeJob);
    const int rc = static_cast<int>(reinterpret_cast<qintptr>(
        ShellExecuteW(nullptr,
                      L"runas",
                      reinterpret_cast<LPCWSTR>(nativeHelper.utf16()),
                      reinterpret_cast<LPCWSTR>(args.utf16()),
                      nullptr,
                      SW_SHOWNORMAL)));
    if (rc <= 32) {
        if (errorOut) {
            *errorOut = (rc == 1223)
                ? QStringLiteral("Administrator permission was denied.")
                : QStringLiteral("Failed to launch updater helper (code %1).").arg(rc);
        }
        return false;
    }
    return true;
#else
    const bool launched = QProcess::startDetached(helperPath, {QStringLiteral("--job"), jobPath});
    if (!launched && errorOut) {
        *errorOut = QStringLiteral("Failed to launch updater helper.");
    }
    return launched;
#endif
}

UpdateClient::UpdateClient(QObject* parent)
    : QObject(parent)
{
    m_currentVersion = QCoreApplication::applicationVersion();
    if (m_currentVersion.trimmed().isEmpty()) {
        m_currentVersion = QStringLiteral("0.0.0");
    }
    loadSettings();
    consumePendingUpdateStatus();

    connect(&m_autoTimer, &QTimer::timeout, this, &UpdateClient::maybeAutoCheck);
    m_autoTimer.setSingleShot(true);
    m_autoTimer.start(1500);
}

void UpdateClient::setChannel(const QString& channel)
{
    const QString next = channel.trimmed().isEmpty() ? QStringLiteral("stable") : channel.trimmed();
    if (m_channel == next) return;
    m_channel = next;
    saveSettings();
    emit channelChanged();
}

void UpdateClient::setAutoCheck(bool enabled)
{
    if (m_autoCheck == enabled) return;
    m_autoCheck = enabled;
    saveSettings();
    emit autoCheckChanged();
}

void UpdateClient::setAutoDownload(bool enabled)
{
    const QString nextMode = enabled ? QStringLiteral("automatic") : QStringLiteral("custom");
    const bool modeChanged = (m_updateMode != nextMode);
    if (m_autoDownload == enabled && !modeChanged) return;
    m_autoDownload = enabled;
    m_updateMode = nextMode;
    saveSettings();
    emit autoDownloadChanged();
    if (modeChanged) {
        emit updateModeChanged();
    }
}

void UpdateClient::setUpdateMode(const QString& mode)
{
    const QString next = normalizeUpdateMode(mode);
    const bool nextAutoDownload = (next == QStringLiteral("automatic"));
    if (m_updateMode == next && m_autoDownload == nextAutoDownload) return;

    const bool autoDownloadToggled = (m_autoDownload != nextAutoDownload);
    m_updateMode = next;
    m_autoDownload = nextAutoDownload;
    saveSettings();
    emit updateModeChanged();
    if (autoDownloadToggled) {
        emit autoDownloadChanged();
    }
}

void UpdateClient::setSourcePreference(const QString& source)
{
    QString next = source.trimmed().toLower();
    if (next.isEmpty()) next = QStringLiteral("auto");
    if (next != QStringLiteral("auto") && next != QStringLiteral("website") && next != QStringLiteral("github")) {
        next = QStringLiteral("auto");
    }
    if (m_sourcePreference == next) return;
    m_sourcePreference = next;
    saveSettings();
    emit sourcePreferenceChanged();
}

void UpdateClient::setGithubRepo(const QString& repo)
{
    const QString next = repo.trimmed();
    if (m_githubRepo == next) return;
    m_githubRepo = next;
    saveSettings();
    emit githubRepoChanged();
}

void UpdateClient::setManifestUrl(const QString& url)
{
    const QString next = url.trimmed();
    if (m_manifestUrl == next) return;
    m_manifestUrl = next;
    saveSettings();
    emit manifestUrlChanged();
}

void UpdateClient::setRequireSignature(bool enabled)
{
    if (m_requireSignature == enabled) return;
    m_requireSignature = enabled;
    saveSettings();
    emit signaturePolicyChanged();
}

void UpdateClient::setPublicKeyPath(const QString& path)
{
    const QString next = path.trimmed();
    if (m_publicKeyPath == next) return;
    m_publicKeyPath = next;
    saveSettings();
    emit publicKeyPathChanged();
}

void UpdateClient::checkNow()
{
    if (m_downloadReply) {
        setStatus(QStringLiteral("Updater busy"));
        setError(QStringLiteral("A download is already in progress"));
        return;
    }
    if (m_activeReply) {
        m_activeReply->disconnect(this);
        m_activeReply->abort();
        m_activeReply->deleteLater();
        m_activeReply = nullptr;
    }
    resetUpdateInfo();
    setError(QString());
    setStatus(QStringLiteral("Checking for updates..."));

    const QString pref = m_sourcePreference.toLower();
    if (pref == QStringLiteral("github")) {
        if (!m_githubRepo.isEmpty()) {
            checkGitHubReleases();
            return;
        }
        setStatus(QStringLiteral("Configure update sources"));
        setError(QStringLiteral("GitHub repo not configured"));
        return;
    }
    if (pref == QStringLiteral("website")) {
        if (!m_manifestUrl.isEmpty()) {
            checkWebsiteManifest();
            return;
        }
        setStatus(QStringLiteral("Configure update sources"));
        setError(QStringLiteral("Manifest URL not configured"));
        return;
    }

    if (!m_manifestUrl.isEmpty()) {
        checkWebsiteManifest();
        return;
    }
    if (!m_githubRepo.isEmpty()) {
        checkGitHubReleases();
        return;
    }

    setStatus(QStringLiteral("Configure update sources"));
    setError(QStringLiteral("No update source configured"));
}

void UpdateClient::downloadUpdate()
{
    if (m_downloadUrl.isEmpty()) {
        setError(QStringLiteral("No download URL"));
        return;
    }
    if (m_downloadReply) {
        m_downloadReply->disconnect(this);
        m_downloadReply->abort();
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;
    }

    setError(QString());
    m_downloadProgress = 0.0;
    emit downloadProgressChanged();
    m_downloadedPath.clear();
    emit downloadReadyChanged();
    if (m_signatureVerified) {
        m_signatureVerified = false;
        emit signatureVerificationChanged();
    }
    setStatus(QStringLiteral("Downloading update..."));

    const QString fileName = pickFileNameFromUrl(m_downloadUrl);
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (baseDir.isEmpty()) baseDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (baseDir.isEmpty()) baseDir = QDir::tempPath();

    const QString targetPath = utils::uniqueFilePath(QDir(baseDir).filePath(fileName));
    if (m_downloadFile) {
        delete m_downloadFile;
        m_downloadFile = nullptr;
    }
    m_downloadFile = new QFile(targetPath, this);
    if (!m_downloadFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setStatus(QStringLiteral("Download failed"));
        setError(QStringLiteral("Failed to open file for download"));
        m_downloadFile->deleteLater();
        m_downloadFile = nullptr;
        return;
    }

    QNetworkRequest req{QUrl(m_downloadUrl)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(30000);
    req.setRawHeader("User-Agent", "raad/1.0");
    QNetworkReply* reply = m_net.get(req);
    m_downloadReply = reply;

    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        if (reply != m_downloadReply) return;
        if (!m_downloadFile) return;
        if (!reply->isOpen() || reply->isFinished()) return;
        const QByteArray chunk = reply->readAll();
        if (!chunk.isEmpty()) {
            m_downloadFile->write(chunk);
        }
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](qint64 received, qint64 total) {
        if (reply != m_downloadReply) return;
        if (total <= 0) return;
        m_downloadProgress = static_cast<qreal>(received) / static_cast<qreal>(total);
        emit downloadProgressChanged();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, targetPath]() {
        if (reply != m_downloadReply) {
            reply->deleteLater();
            return;
        }
        if (m_downloadFile && reply->isOpen()) {
            const QByteArray chunk = reply->readAll();
            if (!chunk.isEmpty()) {
                m_downloadFile->write(chunk);
            }
        }
        const bool ok = (reply->error() == QNetworkReply::NoError);
        const QString err = reply->errorString();
        reply->deleteLater();
        m_downloadReply = nullptr;
        if (m_downloadFile) {
            m_downloadFile->flush();
            m_downloadFile->close();
            m_downloadFile->deleteLater();
            m_downloadFile = nullptr;
        }
        if (!ok) {
            QFile::remove(targetPath);
            setStatus(QStringLiteral("Download failed"));
            setError(err);
            return;
        }
        if (m_downloadProgress < 1.0) {
            m_downloadProgress = 1.0;
            emit downloadProgressChanged();
        }
        QString verifyError;
        if (!verifyDownloadedPayload(targetPath, &verifyError)) {
            QFile::remove(targetPath);
            setStatus(QStringLiteral("Verification failed"));
            setError(verifyError.isEmpty() ? QStringLiteral("Downloaded payload verification failed") : verifyError);
            return;
        }
        m_downloadedPath = targetPath;
        emit downloadReadyChanged();
        if (m_expectedSha256.trimmed().isEmpty()) {
            setStatus(QStringLiteral("Update downloaded, but release checksum is missing"));
        } else if (m_requireSignature && m_signatureVerified) {
            setStatus(QStringLiteral("Update downloaded and verified"));
        } else {
            setStatus(QStringLiteral("Update downloaded"));
        }
    });
}

void UpdateClient::installUpdate()
{
    const QString path = m_downloadedPath.trimmed();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        setError(QStringLiteral("Downloaded update file was not found"));
        setStatus(QStringLiteral("Install failed"));
        return;
    }
    if (m_requireSignature && !m_signatureVerified) {
        setError(QStringLiteral("Signature verification is required before install"));
        setStatus(QStringLiteral("Verification required"));
        return;
    }

    const QString currentExe = QCoreApplication::applicationFilePath();
    if (!isSelfInstallSupportedAsset(currentExe, path)) {
        setError(QString());
        setStatus(QStringLiteral("Opening installer..."));
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
            setError(QStringLiteral("Failed to open installer"));
            setStatus(QStringLiteral("Install failed"));
        }
        return;
    }

    const QString expectedSha256 = utils::normalizeChecksum(m_expectedSha256);
    if (utils::detectChecksumAlgo(expectedSha256) != QStringLiteral("SHA256")) {
        setError(QStringLiteral("Release does not provide trusted SHA-256 metadata for this asset"));
        setStatus(QStringLiteral("Install blocked"));
        return;
    }

    const QString downloadedHash = utils::normalizeChecksum(sha256ForFile(path));
    if (downloadedHash.isEmpty() || downloadedHash != expectedSha256) {
        setError(QStringLiteral("Downloaded update hash verification failed"));
        setStatus(QStringLiteral("Install failed"));
        return;
    }

    const QString helperPath = appUpdaterHelperPath();
    if (!QFileInfo::exists(helperPath)) {
        setError(QStringLiteral("Updater helper executable not found"));
        setStatus(QStringLiteral("Install failed"));
        return;
    }

    const QString updatesDir = updatesDirPath();
    if (updatesDir.trimmed().isEmpty()) {
        setError(QStringLiteral("Could not resolve update workspace"));
        setStatus(QStringLiteral("Install failed"));
        return;
    }
    QDir().mkpath(updatesDir);

    const QString stagedPath = QDir(updatesDir).filePath(QStringLiteral("staged-%1").arg(QFileInfo(path).fileName()));
    if (!copyWithOverwrite(path, stagedPath)) {
        setError(QStringLiteral("Failed to stage update file"));
        setStatus(QStringLiteral("Install failed"));
        return;
    }

    const QString backupPath = currentExe + QStringLiteral(".backup.old");
    const QString jobPath = QDir(updatesDir).filePath(
        QStringLiteral("update-job-%1.json").arg(QString::number(QDateTime::currentMSecsSinceEpoch())));

    QJsonObject job;
    job.insert(QStringLiteral("pid"), static_cast<qint64>(QCoreApplication::applicationPid()));
    job.insert(QStringLiteral("current_executable"), currentExe);
    job.insert(QStringLiteral("staged_executable"), stagedPath);
    job.insert(QStringLiteral("backup_executable"), backupPath);
    job.insert(QStringLiteral("working_directory"), QCoreApplication::applicationDirPath());
    job.insert(QStringLiteral("expected_sha256"), expectedSha256);
    job.insert(QStringLiteral("cleanup_source_on_success"), true);
    job.insert(QStringLiteral("timeout_ms"), 45000);
    job.insert(QStringLiteral("args"), QJsonArray());

    QFile jobFile(jobPath);
    if (!jobFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QFile::remove(stagedPath);
        setError(QStringLiteral("Failed to write update job file"));
        setStatus(QStringLiteral("Install failed"));
        return;
    }
    jobFile.write(QJsonDocument(job).toJson(QJsonDocument::Indented));
    jobFile.close();

    QString launchError;
    if (!startUpdaterHelperDetached(helperPath, jobPath, &launchError)) {
        QFile::remove(jobPath);
        QFile::remove(stagedPath);
        setError(launchError.isEmpty() ? QStringLiteral("Failed to launch updater helper") : launchError);
        setStatus(QStringLiteral("Install failed"));
        return;
    }

    setError(QString());
    setStatus(QStringLiteral("Installing update and restarting..."));
    QTimer::singleShot(250, []() { QCoreApplication::quit(); });
}

void UpdateClient::resetSettingsToDefaults()
{
    if (m_activeReply) {
        m_activeReply->disconnect(this);
        m_activeReply->abort();
        m_activeReply->deleteLater();
        m_activeReply = nullptr;
    }
    if (m_downloadReply) {
        m_downloadReply->disconnect(this);
        m_downloadReply->abort();
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;
    }
    if (m_downloadFile) {
        if (m_downloadFile->isOpen()) {
            m_downloadFile->close();
        }
        m_downloadFile->deleteLater();
        m_downloadFile = nullptr;
    }

    QSettings settings;
    settings.beginGroup(settingsGroup());
    settings.remove(QString());
    settings.endGroup();

    const QString updatesDirValue = updatesDirPath();
    if (!updatesDirValue.trimmed().isEmpty()) {
        QDir updatesDir(updatesDirValue);
        if (updatesDir.exists()) {
            updatesDir.removeRecursively();
        }
    }

    m_channel = QStringLiteral("stable");
    m_autoCheck = true;
    m_autoDownload = false;
    m_updateMode = QStringLiteral("custom");
    m_sourcePreference = QStringLiteral("auto");
    m_githubRepo = QStringLiteral("genyleap/raad");
    m_manifestUrl.clear();
    m_requireSignature = false;
    m_publicKeyPath.clear();

    resetUpdateInfo();
    setError(QString());
    setStatus(QStringLiteral("Update settings restored to defaults"));

    emit channelChanged();
    emit autoCheckChanged();
    emit autoDownloadChanged();
    emit updateModeChanged();
    emit sourcePreferenceChanged();
    emit githubRepoChanged();
    emit manifestUrlChanged();
    emit signaturePolicyChanged();
    emit publicKeyPathChanged();
}

void UpdateClient::loadSettings()
{
    QSettings settings;
    settings.beginGroup(settingsGroup());
    m_channel = settings.value(QStringLiteral("channel"), m_channel).toString();
    if (settings.contains(QStringLiteral("updateMode"))) {
        m_updateMode = normalizeUpdateMode(settings.value(QStringLiteral("updateMode")).toString());
    } else {
        m_updateMode = settings.value(QStringLiteral("autoDownload"), m_autoDownload).toBool()
            ? QStringLiteral("automatic")
            : QStringLiteral("custom");
    }
    m_autoCheck = true;
    m_autoDownload = (m_updateMode == QStringLiteral("automatic"));
    m_sourcePreference = settings.value(QStringLiteral("sourcePreference"), m_sourcePreference).toString().trimmed().toLower();
    if (m_sourcePreference.isEmpty()) m_sourcePreference = QStringLiteral("auto");
    m_githubRepo = settings.value(QStringLiteral("githubRepo"), m_githubRepo).toString();
    m_manifestUrl = settings.value(QStringLiteral("manifestUrl"), m_manifestUrl).toString();
    m_requireSignature = settings.value(QStringLiteral("requireSignature"), m_requireSignature).toBool();
    m_publicKeyPath = settings.value(QStringLiteral("publicKeyPath"), m_publicKeyPath).toString();
    settings.endGroup();
}

void UpdateClient::saveSettings()
{
    QSettings settings;
    settings.beginGroup(settingsGroup());
    settings.setValue(QStringLiteral("channel"), m_channel);
    settings.remove(QStringLiteral("autoCheck"));
    settings.setValue(QStringLiteral("autoDownload"), m_autoDownload);
    settings.setValue(QStringLiteral("updateMode"), m_updateMode);
    settings.setValue(QStringLiteral("sourcePreference"), m_sourcePreference);
    settings.setValue(QStringLiteral("githubRepo"), m_githubRepo);
    settings.setValue(QStringLiteral("manifestUrl"), m_manifestUrl);
    settings.setValue(QStringLiteral("requireSignature"), m_requireSignature);
    settings.setValue(QStringLiteral("publicKeyPath"), m_publicKeyPath);
    settings.endGroup();
}

void UpdateClient::consumePendingUpdateStatus()
{
    const QString updatesDirValue = updatesDirPath();
    if (updatesDirValue.trimmed().isEmpty()) return;

    QDir updatesDir(updatesDirValue);
    if (!updatesDir.exists()) return;

    const QFileInfoList statusFiles = updatesDir.entryInfoList(
        QStringList() << QStringLiteral("update-job-*.json.status.json"),
        QDir::Files,
        QDir::Time | QDir::Reversed);
    if (statusFiles.isEmpty()) return;

    const QFileInfo latest = statusFiles.last();
    QFile statusFile(latest.absoluteFilePath());
    if (!statusFile.open(QIODevice::ReadOnly)) {
        QFile::remove(latest.absoluteFilePath());
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(statusFile.readAll(), &parseError);
    statusFile.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        QFile::remove(latest.absoluteFilePath());
        return;
    }

    const QJsonObject root = doc.object();
    const bool ok = root.value(QStringLiteral("ok")).toBool(false);
    const QString message = root.value(QStringLiteral("message")).toString().trimmed();
    if (ok) {
        setError(QString());
        setStatus(message.isEmpty()
                      ? QStringLiteral("Update applied successfully")
                      : QStringLiteral("Update: %1").arg(message));
    } else {
        setError(message.isEmpty() ? QStringLiteral("Updater helper failed") : message);
        setStatus(QStringLiteral("Install failed"));
    }

    for (const QFileInfo& fileInfo : statusFiles) {
        QFile::remove(fileInfo.absoluteFilePath());
    }
}

void UpdateClient::setStatus(const QString& status)
{
    if (m_status == status) return;
    m_status = status;
    emit statusChanged();
}

void UpdateClient::setError(const QString& error)
{
    if (m_lastError == error) return;
    m_lastError = error;
    emit lastErrorChanged();
}

void UpdateClient::resetUpdateInfo()
{
    m_updateAvailable = false;
    m_latestVersion.clear();
    m_releaseNotes.clear();
    m_downloadUrl.clear();
    m_expectedSha256.clear();
    m_signatureUrl.clear();
    m_downloadedPath.clear();
    m_downloadProgress = 0.0;
    if (m_signatureVerified) {
        m_signatureVerified = false;
        emit signatureVerificationChanged();
    }
    emit updateAvailableChanged();
    emit updateInfoChanged();
    emit downloadReadyChanged();
    emit downloadProgressChanged();
}

void UpdateClient::maybeAutoCheck()
{
    checkNow();
}

void UpdateClient::checkWebsiteManifest()
{
    setStatus(QStringLiteral("Checking website manifest..."));
    QNetworkRequest req{QUrl(m_manifestUrl)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(12000);
    req.setRawHeader("User-Agent", "raad/1.0");
    QNetworkReply* reply = m_net.get(req);
    m_activeReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply != m_activeReply) {
            reply->deleteLater();
            return;
        }
        const QByteArray data = reply->readAll();
        const bool ok = (reply->error() == QNetworkReply::NoError);
        reply->deleteLater();
        m_activeReply = nullptr;
        if (!ok) {
            if (m_sourcePreference == QStringLiteral("auto") && !m_githubRepo.isEmpty()) {
                checkGitHubReleases();
                return;
            }
            setStatus(QStringLiteral("Failed to fetch manifest"));
            setError(QStringLiteral("Manifest request failed"));
            return;
        }
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(data, &err);
        if (err.error != QJsonParseError::NoError) {
            if (m_sourcePreference == QStringLiteral("auto") && !m_githubRepo.isEmpty()) {
                checkGitHubReleases();
                return;
            }
            setStatus(QStringLiteral("Invalid manifest"));
            setError(QStringLiteral("Manifest parse error"));
            return;
        }
        handleManifestJson(doc);
    });
}

void UpdateClient::checkGitHubReleases()
{
    setStatus(QStringLiteral("Checking GitHub releases..."));
    const bool allowPrerelease = (m_channel.toLower() == QStringLiteral("beta"));
    QString url;
    if (allowPrerelease) {
        url = QStringLiteral("https://api.github.com/repos/%1/releases").arg(m_githubRepo);
    } else {
        url = QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(m_githubRepo);
    }

    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(12000);
    req.setRawHeader("User-Agent", "raad/1.0");
    QNetworkReply* reply = m_net.get(req);
    m_activeReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, allowPrerelease]() {
        if (reply != m_activeReply) {
            reply->deleteLater();
            return;
        }
        const QByteArray data = reply->readAll();
        const bool ok = (reply->error() == QNetworkReply::NoError);
        reply->deleteLater();
        m_activeReply = nullptr;
        if (!ok) {
            setStatus(QStringLiteral("Failed to fetch GitHub releases"));
            setError(QStringLiteral("GitHub request failed"));
            return;
        }
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(data, &err);
        if (err.error != QJsonParseError::NoError) {
            setStatus(QStringLiteral("Invalid GitHub response"));
            setError(QStringLiteral("GitHub parse error"));
            return;
        }
        handleGitHubJson(doc, allowPrerelease);
    });
}

void UpdateClient::handleManifestJson(const QJsonDocument& doc)
{
    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        if (obj.contains(QStringLiteral("releases")) && obj.value(QStringLiteral("releases")).isArray()) {
            QJsonArray releases = obj.value(QStringLiteral("releases")).toArray();
            QJsonObject best;
            QString bestVersion;
            const QString desiredChannel = m_channel.toLower();
            for (const QJsonValue& v : releases) {
                if (!v.isObject()) continue;
                QJsonObject rel = v.toObject();
                const QString channel = rel.value(QStringLiteral("channel")).toString().toLower();
                if (!desiredChannel.isEmpty() && !channel.isEmpty() && channel != desiredChannel) continue;
                const QString version = rel.value(QStringLiteral("version")).toString();
                if (version.isEmpty()) continue;
                if (best.isEmpty() || utils::compareVersions(bestVersion, version) < 0) {
                    best = rel;
                    bestVersion = version;
                }
            }
            if (!best.isEmpty()) {
                obj = best;
            } else if (!releases.isEmpty() && releases.first().isObject()) {
                obj = releases.first().toObject();
            }
        }
        const QString version = obj.value(QStringLiteral("version")).toString();
        const QString notes = obj.value(QStringLiteral("notes")).toString();
        QJsonArray assets = obj.value(QStringLiteral("assets")).toArray();
        const QString assetUrl = selectAssetUrl(assets);
        const QJsonObject assetObj = assetByUrl(assets, assetUrl);
        const QString expectedSha = resolveExpectedSha256(assets, assetObj, assetUrl);
        QString signatureUrl = assetObj.value(QStringLiteral("signature")).toString().trimmed();
        if (signatureUrl.isEmpty()) signatureUrl = assetObj.value(QStringLiteral("sig")).toString().trimmed();
        if (signatureUrl.isEmpty()) signatureUrl = assetObj.value(QStringLiteral("signatureUrl")).toString().trimmed();
        const QString baseName = !assetObj.value(QStringLiteral("name")).toString().isEmpty()
            ? assetObj.value(QStringLiteral("name")).toString()
            : utils::fileNameFromUrl(QUrl(assetUrl));
        if (signatureUrl.isEmpty()) {
            signatureUrl = sidecarAssetUrl(assets, baseName, {QStringLiteral(".sig"), QStringLiteral(".minisig"), QStringLiteral(".asc")});
        }

        m_latestVersion = version;
        m_releaseNotes = notes;
        m_downloadUrl = assetUrl;
        m_expectedSha256 = expectedSha;
        m_signatureUrl = signatureUrl;
        emit updateInfoChanged();

        if (version.isEmpty() || assetUrl.isEmpty()) {
            setStatus(QStringLiteral("No update available"));
            return;
        }

        const int cmp = utils::compareVersions(m_currentVersion, version);
        m_updateAvailable = (cmp < 0);
        emit updateAvailableChanged();
        if (m_updateAvailable && m_expectedSha256.trimmed().isEmpty()) {
            setStatus(QStringLiteral("Update available, but checksum metadata is missing"));
        } else {
            setStatus(m_updateAvailable ? QStringLiteral("Update available") : QStringLiteral("Up to date"));
        }
        if (m_updateAvailable && m_updateMode == QStringLiteral("automatic")) {
            downloadUpdate();
        }
        return;
    }
    setStatus(QStringLiteral("Invalid manifest format"));
    setError(QStringLiteral("Manifest format error"));
}

void UpdateClient::handleGitHubJson(const QJsonDocument& doc, bool allowPrerelease)
{
    QJsonObject rel;
    if (doc.isObject()) {
        rel = doc.object();
    } else if (doc.isArray()) {
        const QJsonArray releases = doc.array();
        for (const QJsonValue& v : releases) {
            if (!v.isObject()) continue;
            const QJsonObject obj = v.toObject();
            const bool prerelease = obj.value(QStringLiteral("prerelease")).toBool(false);
            if (allowPrerelease && prerelease) { rel = obj; break; }
            if (!allowPrerelease && !prerelease) { rel = obj; break; }
        }
        if (rel.isEmpty() && !releases.isEmpty() && releases.first().isObject()) {
            rel = releases.first().toObject();
        }
    }

    if (rel.isEmpty()) {
        setStatus(QStringLiteral("No releases found"));
        return;
    }

    QString version = rel.value(QStringLiteral("tag_name")).toString();
    if (version.isEmpty()) version = rel.value(QStringLiteral("name")).toString();
    const QString notes = rel.value(QStringLiteral("body")).toString();
    const QJsonArray assets = rel.value(QStringLiteral("assets")).toArray();
    const QString assetUrl = selectAssetUrl(assets);
    const QJsonObject assetObj = assetByUrl(assets, assetUrl);
    const QString expectedSha = resolveExpectedSha256(assets, assetObj, assetUrl);
    QString signatureUrl = assetObj.value(QStringLiteral("signature")).toString().trimmed();
    if (signatureUrl.isEmpty()) signatureUrl = assetObj.value(QStringLiteral("sig")).toString().trimmed();
    if (signatureUrl.isEmpty()) signatureUrl = assetObj.value(QStringLiteral("signatureUrl")).toString().trimmed();
    const QString baseName = !assetObj.value(QStringLiteral("name")).toString().isEmpty()
        ? assetObj.value(QStringLiteral("name")).toString()
        : utils::fileNameFromUrl(QUrl(assetUrl));
    if (signatureUrl.isEmpty()) {
        signatureUrl = sidecarAssetUrl(assets, baseName, {QStringLiteral(".sig"), QStringLiteral(".minisig"), QStringLiteral(".asc")});
    }

    m_latestVersion = version;
    m_releaseNotes = notes;
    m_downloadUrl = assetUrl;
    m_expectedSha256 = expectedSha;
    m_signatureUrl = signatureUrl;
    emit updateInfoChanged();

    if (version.isEmpty() || assetUrl.isEmpty()) {
        setStatus(QStringLiteral("No compatible assets"));
        return;
    }

    const int cmp = utils::compareVersions(m_currentVersion, version);
    m_updateAvailable = (cmp < 0);
    emit updateAvailableChanged();
    if (m_updateAvailable && m_expectedSha256.trimmed().isEmpty()) {
        setStatus(QStringLiteral("Update available, but checksum metadata is missing"));
    } else {
        setStatus(m_updateAvailable ? QStringLiteral("Update available") : QStringLiteral("Up to date"));
    }
    if (m_updateAvailable && m_updateMode == QStringLiteral("automatic")) {
        downloadUpdate();
    }
}

QString UpdateClient::platformKey() const
{
#if defined(Q_OS_MAC)
    return QStringLiteral("macos");
#elif defined(Q_OS_WIN)
    return QStringLiteral("windows");
#else
    return QStringLiteral("linux");
#endif
}

QString UpdateClient::selectAssetUrl(const QJsonArray& assets) const
{
    if (assets.isEmpty()) return QString();
    const QString arch = QSysInfo::currentCpuArchitecture().toLower();
    const QString platform = platformKey();

    auto platformMatches = [&](const QString& value) -> bool {
        const QString v = value.toLower();
        if (v.isEmpty()) return true;
        if (platform == "macos") return v == "mac" || v == "macos" || v == "osx" || v == "darwin";
        if (platform == "windows") return v == "windows" || v == "win" || v == "win32" || v == "win64";
        return v == "linux" || v == "gnu/linux" || v == "ubuntu" || v == "debian" || v == "fedora";
    };

    auto archMatches = [&](const QString& value) -> bool {
        const QString v = value.toLower();
        if (v.isEmpty()) return true;
        if (v == "x64" || v == "amd64" || v == "x86_64") {
            return arch.contains("x86_64") || arch.contains("amd64") || arch.contains("x64");
        }
        if (v == "arm64" || v == "aarch64") {
            return arch.contains("arm64") || arch.contains("aarch64") || arch.contains("arm");
        }
        return arch.contains(v);
    };

    auto scoreFor = [&](const QString& name) -> int {
        const QString n = name.toLower();
        int score = 0;
        if (platform == "macos") {
            if (n.endsWith(".dmg")) score += 30;
            else if (n.endsWith(".pkg")) score += 20;
            else if (n.endsWith(".zip")) score += 10;
        } else if (platform == "windows") {
            if (n.endsWith(".msi")) score += 30;
            else if (n.endsWith(".exe")) score += 20;
            else if (n.endsWith(".zip")) score += 10;
        } else {
            if (n.endsWith(".appimage")) score += 30;
            else if (n.endsWith(".deb")) score += 20;
            else if (n.endsWith(".rpm")) score += 15;
            else if (n.endsWith(".tar.gz") || n.endsWith(".tgz")) score += 10;
        }
        if (arch.contains("arm") && n.contains("arm")) score += 5;
        if ((arch.contains("x86_64") || arch.contains("amd64")) && (n.contains("x86_64") || n.contains("amd64") || n.contains("x64"))) score += 5;
        if (n.contains(platform)) score += 2;
        return score;
    };

    QString bestUrl;
    int bestScore = -1;
    for (const QJsonValue& v : assets) {
        if (!v.isObject()) continue;
        const QJsonObject obj = v.toObject();
        const QString explicitPlatform = obj.value(QStringLiteral("platform")).toString();
        if (!platformMatches(explicitPlatform)) continue;
        const QString explicitArch = obj.value(QStringLiteral("arch")).toString();
        if (!archMatches(explicitArch)) continue;

        QString url = obj.value(QStringLiteral("browser_download_url")).toString();
        if (url.isEmpty()) url = obj.value(QStringLiteral("url")).toString();
        if (url.isEmpty()) url = obj.value(QStringLiteral("downloadUrl")).toString();
        if (url.isEmpty()) url = obj.value(QStringLiteral("href")).toString();
        QString name = obj.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) name = obj.value(QStringLiteral("file")).toString();
        if (name.isEmpty() && !url.isEmpty()) name = utils::fileNameFromUrl(QUrl(url));
        if (url.isEmpty() || name.isEmpty()) continue;
        const QString lowerName = name.toLower();
        if (lowerName.endsWith(QStringLiteral(".sig")) ||
            lowerName.endsWith(QStringLiteral(".asc")) ||
            lowerName.endsWith(QStringLiteral(".minisig")) ||
            lowerName.endsWith(QStringLiteral(".sha256")) ||
            lowerName.endsWith(QStringLiteral(".sha512"))) {
            continue;
        }
        int score = scoreFor(name);
        if (score > bestScore) {
            bestScore = score;
            bestUrl = url;
        }
    }
    return bestUrl;
}

QString UpdateClient::pickFileNameFromUrl(const QString& url) const
{
    const QString name = utils::fileNameFromUrl(QUrl(url));
    if (!name.isEmpty()) return name;
    return QStringLiteral("raad-update.bin");
}

QJsonObject UpdateClient::assetByUrl(const QJsonArray& assets, const QString& url) const
{
    if (url.isEmpty()) return QJsonObject();
    for (const QJsonValue& v : assets) {
        if (!v.isObject()) continue;
        const QJsonObject obj = v.toObject();
        QString candidate = obj.value(QStringLiteral("browser_download_url")).toString();
        if (candidate.isEmpty()) candidate = obj.value(QStringLiteral("url")).toString();
        if (candidate.isEmpty()) candidate = obj.value(QStringLiteral("downloadUrl")).toString();
        if (candidate.isEmpty()) candidate = obj.value(QStringLiteral("href")).toString();
        if (!candidate.isEmpty() && candidate == url) return obj;
    }
    return QJsonObject();
}

QString UpdateClient::sidecarAssetUrl(const QJsonArray& assets, const QString& baseName, const QStringList& suffixes) const
{
    const QString base = baseName.trimmed().toLower();
    if (base.isEmpty()) return QString();
    for (const QJsonValue& v : assets) {
        if (!v.isObject()) continue;
        const QJsonObject obj = v.toObject();
        QString name = obj.value(QStringLiteral("name")).toString();
        QString url = obj.value(QStringLiteral("browser_download_url")).toString();
        if (url.isEmpty()) url = obj.value(QStringLiteral("url")).toString();
        if (url.isEmpty()) url = obj.value(QStringLiteral("downloadUrl")).toString();
        if (url.isEmpty()) url = obj.value(QStringLiteral("href")).toString();
        if (name.isEmpty() && !url.isEmpty()) {
            name = utils::fileNameFromUrl(QUrl(url));
        }
        if (name.isEmpty() || url.isEmpty()) continue;
        const QString lower = name.toLower();
        for (const QString& suffix : suffixes) {
            const QString target = (base + suffix.toLower());
            if (lower == target || lower.endsWith(target)) {
                return url;
            }
        }
    }
    return QString();
}

QString UpdateClient::namedAssetUrl(const QJsonArray& assets, const QStringList& candidateNames) const
{
    if (candidateNames.isEmpty()) return QString();

    QStringList loweredNames;
    loweredNames.reserve(candidateNames.size());
    for (const QString& candidate : candidateNames) {
        const QString lowered = candidate.trimmed().toLower();
        if (!lowered.isEmpty()) loweredNames.push_back(lowered);
    }
    if (loweredNames.isEmpty()) return QString();

    for (const QJsonValue& v : assets) {
        if (!v.isObject()) continue;
        const QJsonObject obj = v.toObject();
        QString name = obj.value(QStringLiteral("name")).toString();
        QString url = obj.value(QStringLiteral("browser_download_url")).toString();
        if (url.isEmpty()) url = obj.value(QStringLiteral("url")).toString();
        if (url.isEmpty()) url = obj.value(QStringLiteral("downloadUrl")).toString();
        if (url.isEmpty()) url = obj.value(QStringLiteral("href")).toString();
        if (name.isEmpty() && !url.isEmpty()) {
            name = utils::fileNameFromUrl(QUrl(url));
        }
        const QString lowerName = name.trimmed().toLower();
        if (!lowerName.isEmpty() && loweredNames.contains(lowerName) && !url.isEmpty()) {
            return url;
        }
    }
    return QString();
}

QString UpdateClient::fetchRemoteText(const QString& url, QString* errorOut)
{
    return QString::fromUtf8(fetchRemoteBytes(url, errorOut));
}

QByteArray UpdateClient::fetchRemoteBytes(const QString& url, QString* errorOut)
{
    const QString trimmedUrl = url.trimmed();
    if (trimmedUrl.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("Remote URL is empty");
        return {};
    }

    QNetworkRequest req{QUrl(trimmedUrl)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(15000);
    req.setRawHeader("User-Agent", "raad/1.0");
    QNetworkReply* reply = m_net.get(req);

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(15000);
    loop.exec();

    if (!timeout.isActive() && reply && !reply->isFinished()) {
        reply->abort();
        if (errorOut) *errorOut = QStringLiteral("Remote request timed out");
        reply->deleteLater();
        return {};
    }

    if (!reply) {
        if (errorOut) *errorOut = QStringLiteral("Remote request failed");
        return {};
    }

    const bool ok = (reply->error() == QNetworkReply::NoError);
    const QString err = reply->errorString();
    const QByteArray data = reply->readAll();
    reply->deleteLater();
    if (!ok) {
        if (errorOut) *errorOut = err.isEmpty() ? QStringLiteral("Remote request failed") : err;
        return {};
    }
    return data;
}

QString UpdateClient::resolveExpectedSha256(const QJsonArray& assets, const QJsonObject& assetObj, const QString& assetUrl)
{
    QString expectedSha = assetObj.value(QStringLiteral("sha256")).toString().trimmed();
    if (expectedSha.isEmpty()) expectedSha = assetObj.value(QStringLiteral("checksum")).toString().trimmed();
    if (expectedSha.isEmpty()) expectedSha = assetObj.value(QStringLiteral("hash")).toString().trimmed();
    if (expectedSha.isEmpty()) expectedSha = assetObj.value(QStringLiteral("digest")).toString().trimmed();
    expectedSha = utils::normalizeChecksum(expectedSha);
    if (utils::detectChecksumAlgo(expectedSha) == QStringLiteral("SHA256")) {
        return expectedSha;
    }

    const QString baseName = !assetObj.value(QStringLiteral("name")).toString().isEmpty()
        ? assetObj.value(QStringLiteral("name")).toString()
        : utils::fileNameFromUrl(QUrl(assetUrl));
    if (baseName.isEmpty()) return QString();

    QString checksumUrl = assetObj.value(QStringLiteral("sha256Url")).toString().trimmed();
    if (checksumUrl.isEmpty()) checksumUrl = assetObj.value(QStringLiteral("checksumUrl")).toString().trimmed();
    if (checksumUrl.isEmpty()) checksumUrl = assetObj.value(QStringLiteral("digestUrl")).toString().trimmed();
    if (checksumUrl.isEmpty()) checksumUrl = sidecarAssetUrl(assets,
                                                             baseName,
                                                             {QStringLiteral(".sha256"),
                                                              QStringLiteral(".sha256sum"),
                                                              QStringLiteral(".sha256.txt")});
    if (checksumUrl.isEmpty()) {
        checksumUrl = namedAssetUrl(assets,
                                    {QStringLiteral("sha256sums"),
                                     QStringLiteral("sha256sums.txt"),
                                     QStringLiteral("checksums.txt"),
                                     QStringLiteral("checksums.sha256")});
    }
    if (checksumUrl.isEmpty()) return QString();

    QString fetchError;
    const QString checksumText = fetchRemoteText(checksumUrl, &fetchError);
    if (checksumText.isEmpty()) return QString();

    const QString resolved = utils::extractChecksumFromText(checksumText,
                                                            QFileInfo(baseName).fileName(),
                                                            QStringLiteral("SHA256"));
    if (utils::detectChecksumAlgo(resolved) == QStringLiteral("SHA256")) {
        return resolved;
    }
    return QString();
}

QString UpdateClient::sha256ForFile(const QString& path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray buffer;
    buffer.resize(1024 * 1024);
    while (!file.atEnd()) {
        const qint64 n = file.read(buffer.data(), buffer.size());
        if (n <= 0) break;
        hash.addData(QByteArrayView(buffer.constData(), static_cast<qsizetype>(n)));
    }
    file.close();
    return QString::fromUtf8(hash.result().toHex()).toLower();
}

bool UpdateClient::verifySignatureWithOpenSsl(const QString& payloadPath,
                                              const QString& signaturePath,
                                              const QString& publicKeyPath,
                                              QString* errorOut) const
{
    QProcess proc;
    proc.start(QStringLiteral("openssl"),
               QStringList()
                   << QStringLiteral("dgst")
                   << QStringLiteral("-sha256")
                   << QStringLiteral("-verify")
                   << publicKeyPath
                   << QStringLiteral("-signature")
                   << signaturePath
                   << payloadPath);
    if (!proc.waitForFinished(20000)) {
        if (errorOut) *errorOut = QStringLiteral("OpenSSL verify timed out");
        proc.kill();
        return false;
    }
    const QString stdOut = QString::fromUtf8(proc.readAllStandardOutput());
    const QString stdErr = QString::fromUtf8(proc.readAllStandardError());
    if (proc.exitCode() != 0) {
        if (errorOut) *errorOut = stdErr.isEmpty() ? QStringLiteral("OpenSSL verification failed") : stdErr.trimmed();
        return false;
    }
    if (!stdOut.contains(QStringLiteral("Verified OK"), Qt::CaseInsensitive) &&
        !stdErr.contains(QStringLiteral("Verified OK"), Qt::CaseInsensitive)) {
        if (errorOut) *errorOut = QStringLiteral("Signature verification did not return success");
        return false;
    }
    return true;
}

bool UpdateClient::verifyDownloadedPayload(const QString& payloadPath, QString* errorOut)
{
    const QString expected = utils::normalizeChecksum(m_expectedSha256);
    if (!expected.isEmpty()) {
        const QString actual = utils::normalizeChecksum(sha256ForFile(payloadPath));
        if (actual.isEmpty()) {
            if (errorOut) *errorOut = QStringLiteral("Failed to compute SHA-256");
            return false;
        }
        if (actual != expected) {
            if (errorOut) {
                *errorOut = QStringLiteral("SHA-256 mismatch (expected %1, got %2)")
                                .arg(expected, actual);
            }
            return false;
        }
    }

    bool nextSignatureVerified = false;
    if (m_requireSignature) {
        if (m_publicKeyPath.trimmed().isEmpty()) {
            if (errorOut) *errorOut = QStringLiteral("Public key path is required for signature verification");
            return false;
        }
        if (m_signatureUrl.trimmed().isEmpty()) {
            if (errorOut) *errorOut = QStringLiteral("Detached signature URL is required");
            return false;
        }
        if (!QFile::exists(m_publicKeyPath)) {
            if (errorOut) *errorOut = QStringLiteral("Public key file not found");
            return false;
        }

        QString fetchError;
        const QByteArray sigData = fetchRemoteBytes(m_signatureUrl, &fetchError);
        if (sigData.isEmpty()) {
            if (errorOut) {
                *errorOut = fetchError.isEmpty()
                    ? QStringLiteral("Signature file is empty")
                    : fetchError;
            }
            return false;
        }

        QTemporaryFile sigTmp(QDir::tempPath() + QStringLiteral("/raad-signature-XXXXXX.sig"));
        sigTmp.setAutoRemove(true);
        if (!sigTmp.open()) {
            if (errorOut) *errorOut = QStringLiteral("Cannot create temporary signature file");
            return false;
        }
        sigTmp.write(sigData);
        sigTmp.flush();

        QString verifyError;
        if (!verifySignatureWithOpenSsl(payloadPath, sigTmp.fileName(), m_publicKeyPath, &verifyError)) {
            if (errorOut) *errorOut = verifyError;
            return false;
        }
        nextSignatureVerified = true;
    }

    if (m_signatureVerified != nextSignatureVerified) {
        m_signatureVerified = nextSignatureVerified;
        emit signatureVerificationChanged();
    }
    return true;
}
