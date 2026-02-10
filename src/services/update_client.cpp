module;
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QSettings>

module raad.services.update_client;

import raad.utils.download_utils;
import raad.utils.version_utils;

namespace utils = raad::utils;

static QString settingsGroup()
{
    return QStringLiteral("updates");
}

UpdateClient::UpdateClient(QObject* parent)
    : QObject(parent)
{
    m_currentVersion = QCoreApplication::applicationVersion();
    if (m_currentVersion.trimmed().isEmpty()) {
        m_currentVersion = QStringLiteral("0.0.0");
    }
    loadSettings();

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
    if (m_autoDownload == enabled) return;
    m_autoDownload = enabled;
    saveSettings();
    emit autoDownloadChanged();
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

void UpdateClient::checkNow()
{
    if (m_activeReply) {
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
        m_downloadReply->abort();
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;
    }

    setError(QString());
    m_downloadProgress = 0.0;
    emit downloadProgressChanged();
    m_downloadedPath.clear();
    emit downloadReadyChanged();
    setStatus(QStringLiteral("Downloading update..."));

    const QString fileName = pickFileNameFromUrl(m_downloadUrl);
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (baseDir.isEmpty()) baseDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (baseDir.isEmpty()) baseDir = QDir::tempPath();

    const QString targetPath = QDir(baseDir).filePath(fileName);
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
    req.setRawHeader("User-Agent", "raad/1.0");
    m_downloadReply = m_net.get(req);

    connect(m_downloadReply, &QNetworkReply::readyRead, this, [this]() {
        if (!m_downloadFile || !m_downloadReply) return;
        if (!m_downloadReply->isOpen() || m_downloadReply->isFinished()) return;
        m_downloadFile->write(m_downloadReply->readAll());
    });
    connect(m_downloadReply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (total <= 0) return;
        m_downloadProgress = static_cast<qreal>(received) / static_cast<qreal>(total);
        emit downloadProgressChanged();
    });
    connect(m_downloadReply, &QNetworkReply::finished, this, [this, targetPath]() {
        if (!m_downloadReply) return;
        const bool ok = (m_downloadReply->error() == QNetworkReply::NoError);
        const QString err = m_downloadReply->errorString();
        m_downloadReply->deleteLater();
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
        m_downloadedPath = targetPath;
        emit downloadReadyChanged();
        setStatus(QStringLiteral("Update downloaded"));
    });
}

void UpdateClient::installUpdate()
{
    if (m_downloadedPath.isEmpty()) {
        setError(QStringLiteral("No downloaded update"));
        return;
    }

    const QString path = m_downloadedPath;
    setStatus(QStringLiteral("Launching installer..."));

#if defined(Q_OS_WIN)
    QProcess::startDetached(path, {});
#elif defined(Q_OS_MAC)
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
#endif
}

void UpdateClient::loadSettings()
{
    QSettings settings;
    settings.beginGroup(settingsGroup());
    m_channel = settings.value(QStringLiteral("channel"), m_channel).toString();
    m_autoCheck = settings.value(QStringLiteral("autoCheck"), m_autoCheck).toBool();
    m_autoDownload = settings.value(QStringLiteral("autoDownload"), m_autoDownload).toBool();
    m_sourcePreference = settings.value(QStringLiteral("sourcePreference"), m_sourcePreference).toString().trimmed().toLower();
    if (m_sourcePreference.isEmpty()) m_sourcePreference = QStringLiteral("auto");
    m_githubRepo = settings.value(QStringLiteral("githubRepo"), m_githubRepo).toString();
    m_manifestUrl = settings.value(QStringLiteral("manifestUrl"), m_manifestUrl).toString();
    settings.endGroup();
}

void UpdateClient::saveSettings()
{
    QSettings settings;
    settings.beginGroup(settingsGroup());
    settings.setValue(QStringLiteral("channel"), m_channel);
    settings.setValue(QStringLiteral("autoCheck"), m_autoCheck);
    settings.setValue(QStringLiteral("autoDownload"), m_autoDownload);
    settings.setValue(QStringLiteral("sourcePreference"), m_sourcePreference);
    settings.setValue(QStringLiteral("githubRepo"), m_githubRepo);
    settings.setValue(QStringLiteral("manifestUrl"), m_manifestUrl);
    settings.endGroup();
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
    m_downloadedPath.clear();
    m_downloadProgress = 0.0;
    emit updateAvailableChanged();
    emit updateInfoChanged();
    emit downloadReadyChanged();
    emit downloadProgressChanged();
}

void UpdateClient::maybeAutoCheck()
{
    if (!m_autoCheck) return;
    checkNow();
}

void UpdateClient::checkWebsiteManifest()
{
    setStatus(QStringLiteral("Checking website manifest..."));
    QNetworkRequest req{QUrl(m_manifestUrl)};
    req.setRawHeader("User-Agent", "raad/1.0");
    m_activeReply = m_net.get(req);
    connect(m_activeReply, &QNetworkReply::finished, this, [this]() {
        if (!m_activeReply) return;
        const QByteArray data = m_activeReply->readAll();
        const bool ok = (m_activeReply->error() == QNetworkReply::NoError);
        m_activeReply->deleteLater();
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
    req.setRawHeader("User-Agent", "raad/1.0");
    m_activeReply = m_net.get(req);
    connect(m_activeReply, &QNetworkReply::finished, this, [this, allowPrerelease]() {
        if (!m_activeReply) return;
        const QByteArray data = m_activeReply->readAll();
        const bool ok = (m_activeReply->error() == QNetworkReply::NoError);
        m_activeReply->deleteLater();
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

        m_latestVersion = version;
        m_releaseNotes = notes;
        m_downloadUrl = assetUrl;
        emit updateInfoChanged();

        if (version.isEmpty() || assetUrl.isEmpty()) {
            setStatus(QStringLiteral("No update available"));
            return;
        }

        const int cmp = utils::compareVersions(m_currentVersion, version);
        m_updateAvailable = (cmp < 0);
        emit updateAvailableChanged();
        setStatus(m_updateAvailable ? QStringLiteral("Update available") : QStringLiteral("Up to date"));
        if (m_updateAvailable && m_autoDownload) {
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

    m_latestVersion = version;
    m_releaseNotes = notes;
    m_downloadUrl = assetUrl;
    emit updateInfoChanged();

    if (version.isEmpty() || assetUrl.isEmpty()) {
        setStatus(QStringLiteral("No compatible assets"));
        return;
    }

    const int cmp = utils::compareVersions(m_currentVersion, version);
    m_updateAvailable = (cmp < 0);
    emit updateAvailableChanged();
    setStatus(m_updateAvailable ? QStringLiteral("Update available") : QStringLiteral("Up to date"));
    if (m_updateAvailable && m_autoDownload) {
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
