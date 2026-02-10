/*!
 * @file        update_client.cppm
 * @brief       Application update discovery and delivery client.
 * @details     Provides a self-contained update client capable of checking,
 *              retrieving, and installing application updates from multiple
 *              sources, including GitHub Releases and an optional website-
 *              hosted JSON manifest.
 *
 *              The UpdateClient is designed to be UI-friendly and QML-aware,
 *              exposing all relevant state, progress, and configuration
 *              through Qt properties and signals.
 *
 *              Key responsibilities include:
 *              - Periodic and manual update checks
 *              - Channel-based filtering (stable / beta / prerelease)
 *              - Source selection and prioritization (auto / website / GitHub)
 *              - Download management with progress reporting
 *              - Best-effort installation handoff
 *
 *              This component intentionally avoids application-specific
 *              install logic and instead focuses on update discovery,
 *              validation, and delivery.
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
#include <QString>
#include <QTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

export module raad.services.update_client;

#ifdef Q_MOC_RUN
#define RAAD_MODULE_EXPORT
#else
#define RAAD_MODULE_EXPORT export
#endif

/**
 * @brief Update client responsible for discovering and downloading new releases.
 *
 * UpdateClient encapsulates all logic required to determine whether a newer
 * version of the application is available, retrieve metadata such as release
 * notes, and download the appropriate update artifact for the current platform.
 *
 * The client supports:
 * - Multiple update sources with configurable preference
 * - Automatic and manual update checks
 * - Optional auto-download behavior
 * - Channel-aware filtering (stable, beta, prerelease)
 * - UI-observable progress and status reporting
 *
 * UpdateClient does not perform privileged installation steps; instead,
 * it prepares the update payload and exposes it to the host application
 * for final installation.
 */
RAAD_MODULE_EXPORT class UpdateClient : public QObject {
    Q_OBJECT
    //!< @brief Current application version.
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)

    //!< @brief Update channel (stable/beta).
    Q_PROPERTY(QString channel READ channel WRITE setChannel NOTIFY channelChanged)

    //!< @brief Auto check for updates.
    Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY autoCheckChanged)

    //!< @brief Auto download updates.
    Q_PROPERTY(bool autoDownload READ autoDownload WRITE setAutoDownload NOTIFY autoDownloadChanged)

    //!< @brief Preferred update source (auto/website/github).
    Q_PROPERTY(QString sourcePreference READ sourcePreference WRITE setSourcePreference NOTIFY sourcePreferenceChanged)

    //!< @brief GitHub repo in owner/repo form.
    Q_PROPERTY(QString githubRepo READ githubRepo WRITE setGithubRepo NOTIFY githubRepoChanged)

    //!< @brief Optional website manifest URL.
    Q_PROPERTY(QString manifestUrl READ manifestUrl WRITE setManifestUrl NOTIFY manifestUrlChanged)

    //!< @brief Whether an update is available.
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY updateAvailableChanged)

    //!< @brief Latest version string.
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY updateInfoChanged)

    //!< @brief Release notes text.
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY updateInfoChanged)

    //!< @brief Download URL for the update.
    Q_PROPERTY(QString downloadUrl READ downloadUrl NOTIFY updateInfoChanged)

    //!< @brief Download progress (0-1).
    Q_PROPERTY(qreal downloadProgress READ downloadProgress NOTIFY downloadProgressChanged)

    //!< @brief Downloaded file path.
    Q_PROPERTY(QString downloadedPath READ downloadedPath NOTIFY downloadReadyChanged)

    //!< @brief Status text.
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

    //!< @brief Last error text.
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

    //!< @brief Whether a download is ready to install.
    Q_PROPERTY(bool downloadReady READ downloadReady NOTIFY downloadReadyChanged)

public:
    /**
     * @brief Construct a new UpdateClient.
     * @param parent Optional parent QObject.
     */
    explicit UpdateClient(QObject* parent = nullptr);

    //!< @brief Return current version.
    QString currentVersion() const { return m_currentVersion; }

    //!< @brief Return channel.
    QString channel() const { return m_channel; }

    /**
     * @brief Set channel.
     * @param channel Channel name.
     */
    void setChannel(const QString& channel);

    //!< @brief Return auto-check setting.
    bool autoCheck() const { return m_autoCheck; }

    /**
     * @brief Set auto-check setting.
     * @param enabled Whether to auto-check.
     */
    void setAutoCheck(bool enabled);

    //!< @brief Return auto-download setting.
    bool autoDownload() const { return m_autoDownload; }

    /**
     * @brief Set auto-download setting.
     * @param enabled Whether to auto-download.
     */
    void setAutoDownload(bool enabled);

    //!< @brief Return preferred update source.
    QString sourcePreference() const { return m_sourcePreference; }

    /**
     * @brief Set preferred update source.
     * @param source Source preference ("auto", "website", "github").
     */
    void setSourcePreference(const QString& source);

    //!< @brief Return GitHub repo.
    QString githubRepo() const { return m_githubRepo; }

    /**
     * @brief Set GitHub repo.
     * @param repo Repo string (owner/repo).
     */
    void setGithubRepo(const QString& repo);

    //!< @brief Return manifest URL.
    QString manifestUrl() const { return m_manifestUrl; }

    /**
     * @brief Set manifest URL.
     * @param url Manifest URL.
     */
    void setManifestUrl(const QString& url);

    //!< @brief Whether update is available.
    bool updateAvailable() const { return m_updateAvailable; }

    //!< @brief Return latest version.
    QString latestVersion() const { return m_latestVersion; }

    //!< @brief Return release notes.
    QString releaseNotes() const { return m_releaseNotes; }

    //!< @brief Return download URL.
    QString downloadUrl() const { return m_downloadUrl; }

    //!< @brief Return download progress.
    qreal downloadProgress() const { return m_downloadProgress; }

    //!< @brief Return downloaded file path.
    QString downloadedPath() const { return m_downloadedPath; }

    //!< @brief Return status.
    QString status() const { return m_status; }

    //!< @brief Return last error.
    QString lastError() const { return m_lastError; }

    //!< @brief Whether a download is ready.
    bool downloadReady() const { return !m_downloadedPath.isEmpty(); }

    /**
     * @brief Trigger an immediate update check.
     *
     * The active update source is selected according to the configured
     * source preference and channel.
     */
    Q_INVOKABLE void checkNow();

    /**
     * @brief Download the currently available update.
     *
     * If auto-download is enabled, this may be triggered automatically
     * after a successful update check.
     */
    Q_INVOKABLE void downloadUpdate();

    /**
     * @brief Install the downloaded update (best-effort).
     *
     * Platform-specific installation steps are delegated to the host
     * application or external installer.
     */
    Q_INVOKABLE void installUpdate();


signals:
    //!< @brief Emitted when channel changes.
    void channelChanged();

    //!< @brief Emitted when auto-check changes.
    void autoCheckChanged();

    //!< @brief Emitted when auto-download changes.
    void autoDownloadChanged();

    //!< @brief Emitted when source preference changes.
    void sourcePreferenceChanged();

    //!< @brief Emitted when GitHub repo changes.
    void githubRepoChanged();

    //!< @brief Emitted when manifest URL changes.
    void manifestUrlChanged();

    //!< @brief Emitted when update availability changes.
    void updateAvailableChanged();

    //!< @brief Emitted when update info changes.
    void updateInfoChanged();

    //!< @brief Emitted when download progress changes.
    void downloadProgressChanged();

    //!< @brief Emitted when download ready changes.
    void downloadReadyChanged();

    //!< @brief Emitted when status changes.
    void statusChanged();

    //!< @brief Emitted when error changes.
    void lastErrorChanged();

private:
    //!< @brief Load settings from persistent store.
    void loadSettings();

    //!< @brief Save settings to persistent store.
    void saveSettings();

    //!< @brief Update status message.
    void setStatus(const QString& status);

    //!< @brief Update last error message.
    void setError(const QString& error);

    //!< @brief Clear cached update info.
    void resetUpdateInfo();

    //!< @brief Trigger auto-check if enabled.
    void maybeAutoCheck();

    /**
     * @brief Check update information using a website-hosted manifest.
     *
     * Expects a JSON payload describing the latest version and
     * platform-specific assets.
     */
    void checkWebsiteManifest();

    /**
     * @brief Check update information using GitHub Releases API.
     *
     * Optionally allows prerelease versions depending on the
     * configured update channel.
     */
    void checkGitHubReleases();

    //!< @brief Parse manifest JSON payload.
    void handleManifestJson(const QJsonDocument& doc);

    //!< @brief Parse GitHub JSON payload.
    void handleGitHubJson(const QJsonDocument& doc, bool allowPrerelease);

    //!< @brief Select best asset URL for this platform.
    QString selectAssetUrl(const QJsonArray& assets) const;

    //!< @brief Return platform key used for asset selection.
    QString platformKey() const;

    //!< @brief Choose target filename for download.
    QString pickFileNameFromUrl(const QString& url) const;

    QString m_currentVersion;                                //!< Current app version.
    QString m_channel = QStringLiteral("stable");            //!< Update channel.
    bool m_autoCheck = true;                                 //!< Auto-check toggle.
    bool m_autoDownload = false;                             //!< Auto-download toggle.
    QString m_sourcePreference = QStringLiteral("auto");     //!< Source preference.
    QString m_githubRepo = QStringLiteral("genyleap/raad");  //!< Default GitHub repo.
    QString m_manifestUrl;                                   //!< Website manifest URL.

    bool m_updateAvailable = false;                          //!< Update availability.
    QString m_latestVersion;                                 //!< Latest version string.
    QString m_releaseNotes;                                  //!< Release notes.
    QString m_downloadUrl;                                   //!< Download URL.
    qreal m_downloadProgress = 0.0;                          //!< Download progress.
    QString m_downloadedPath;                                //!< Downloaded file path.
    QString m_status;                                        //!< Status message.
    QString m_lastError;                                     //!< Last error message.

    QNetworkAccessManager m_net;                             //!< Network access manager.
    QNetworkReply* m_activeReply = nullptr;                  //!< Active check reply.
    QNetworkReply* m_downloadReply = nullptr;                //!< Active download reply.
    QFile* m_downloadFile = nullptr;                         //!< Active download file.
    QTimer m_autoTimer;                                      //!< Auto-check timer.
};

#include "update_client.moc"
