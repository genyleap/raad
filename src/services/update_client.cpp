module;
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
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
#include <QTemporaryFile>
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
    if (m_signatureVerified) {
        m_signatureVerified = false;
        emit signatureVerificationChanged();
    }
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
        QString verifyError;
        if (!verifyDownloadedPayload(targetPath, &verifyError)) {
            QFile::remove(targetPath);
            setStatus(QStringLiteral("Verification failed"));
            setError(verifyError.isEmpty() ? QStringLiteral("Downloaded payload verification failed") : verifyError);
            return;
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
    if (m_requireSignature && !m_signatureVerified) {
        setError(QStringLiteral("Signature verification is required before install"));
        setStatus(QStringLiteral("Verification required"));
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
    m_requireSignature = settings.value(QStringLiteral("requireSignature"), m_requireSignature).toBool();
    m_publicKeyPath = settings.value(QStringLiteral("publicKeyPath"), m_publicKeyPath).toString();
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
    settings.setValue(QStringLiteral("requireSignature"), m_requireSignature);
    settings.setValue(QStringLiteral("publicKeyPath"), m_publicKeyPath);
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
        const QJsonObject assetObj = assetByUrl(assets, assetUrl);
        QString expectedSha = assetObj.value(QStringLiteral("sha256")).toString().trimmed();
        if (expectedSha.isEmpty()) expectedSha = assetObj.value(QStringLiteral("checksum")).toString().trimmed();
        if (expectedSha.isEmpty()) expectedSha = assetObj.value(QStringLiteral("hash")).toString().trimmed();
        if (expectedSha.startsWith(QStringLiteral("sha256:"), Qt::CaseInsensitive)) {
            expectedSha = expectedSha.mid(QStringLiteral("sha256:").size()).trimmed();
        }
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
    const QJsonObject assetObj = assetByUrl(assets, assetUrl);
    QString expectedSha = assetObj.value(QStringLiteral("sha256")).toString().trimmed();
    if (expectedSha.isEmpty()) expectedSha = assetObj.value(QStringLiteral("checksum")).toString().trimmed();
    if (expectedSha.isEmpty()) expectedSha = assetObj.value(QStringLiteral("hash")).toString().trimmed();
    if (expectedSha.startsWith(QStringLiteral("sha256:"), Qt::CaseInsensitive)) {
        expectedSha = expectedSha.mid(QStringLiteral("sha256:").size()).trimmed();
    }
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

        QNetworkRequest sigReq{QUrl(m_signatureUrl)};
        sigReq.setRawHeader("User-Agent", "raad/1.0");
        QNetworkReply* sigReply = m_net.get(sigReq);
        QEventLoop loop;
        connect(sigReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        if (!sigReply || sigReply->error() != QNetworkReply::NoError) {
            if (errorOut) *errorOut = QStringLiteral("Failed to fetch signature file");
            if (sigReply) sigReply->deleteLater();
            return false;
        }

        const QByteArray sigData = sigReply->readAll();
        sigReply->deleteLater();
        if (sigData.isEmpty()) {
            if (errorOut) *errorOut = QStringLiteral("Signature file is empty");
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
