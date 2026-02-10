/*!
 * @file        downloadmanager.cppm
 * @brief       Central download orchestration and queue policy manager.
 * @details     Provides a high-level controller responsible for creating,
 *              managing, scheduling, and persisting download tasks.
 *
 *              This class acts as the primary façade between the QML UI layer
 *              and the download core. It enforces global and per-queue policies
 *              such as concurrency limits, bandwidth throttling, scheduling
 *              windows, quota enforcement, and power-aware behavior.
 *
 *              Responsibilities include:
 *              - Task lifecycle management (create, pause, resume, cancel)
 *              - Queue and category organization
 *              - Policy enforcement (speed, concurrency, schedule, quota)
 *              - Aggregate statistics and session persistence
 *              - Power-state–aware download control
 *
 * @author      <a href='https://github.com/thecompez'>Kambiz Asadzadeh</a>
 * @since       09 Feb 2026
 * @copyright   Copyright (c) 2026 Genyleap. All rights reserved.
 * @license     https://github.com/genyleap/raad/blob/main/LICENSE.md
 */

module;
#include <QObject>
#include <QHash>
#include <QDate>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QUrl>
#include <QPointer>
#include <QFutureWatcher>
#include <QVariantMap>

#ifndef Q_MOC_RUN
export module raad.core.downloadmanager;
import raad.core.downloadertask;
import raad.core.downloadmodel;
import raad.services.power_monitor;
#endif

#ifdef Q_MOC_RUN
#define RAAD_MODULE_EXPORT
#else
#define RAAD_MODULE_EXPORT export
#endif

/**
 * @brief Central coordinator for all download tasks and queues.
 *
 * DownloadManager exposes a comprehensive set of QML-invokable APIs
 * for managing downloads, queues, categories, and policies.
 *
 * It maintains the authoritative runtime state of:
 * - Active, queued, and completed download tasks
 * - Queue configurations and enforcement rules
 * - Aggregate bandwidth and progress statistics
 *
 * The class is designed to be UI-facing, policy-driven, and resilient
 * across application restarts via session persistence.
 */
RAAD_MODULE_EXPORT class DownloadManager : public QObject {

    Q_OBJECT

    //!< @brief Download list model exposed to QML.
    Q_PROPERTY(DownloadModel* model READ model CONSTANT)

    //!< @brief Global maximum number of concurrent downloads.
    Q_PROPERTY(int maxConcurrent READ maxConcurrent WRITE setMaxConcurrent NOTIFY maxConcurrentChanged)

    //!< @brief Number of currently active (running) downloads.
    Q_PROPERTY(int activeCount READ activeCount NOTIFY countsChanged)

    //!< @brief Number of queued (waiting) downloads.
    Q_PROPERTY(int queuedCount READ queuedCount NOTIFY countsChanged)

    //!< @brief Number of completed downloads.
    Q_PROPERTY(int completedCount READ completedCount NOTIFY countsChanged)

    //!< @brief Ordered list of defined queue names.
    Q_PROPERTY(QStringList queueNames READ queueNames NOTIFY queuesChanged)

    //!< @brief Global maximum download speed limit (bytes/sec).
    Q_PROPERTY(qint64 globalMaxSpeed READ globalMaxSpeed WRITE setGlobalMaxSpeed NOTIFY globalMaxSpeedChanged)

    //!< @brief Aggregate current download speed across all tasks.
    Q_PROPERTY(qint64 totalSpeed READ totalSpeed NOTIFY totalsChanged)

    //!< @brief Aggregate bytes received across all tasks.
    Q_PROPERTY(qint64 totalReceived READ totalReceived NOTIFY totalsChanged)

    //!< @brief Aggregate total bytes expected across all tasks.
    Q_PROPERTY(qint64 totalSize READ totalSize NOTIFY totalsChanged)

    //!< @brief Automatically pause downloads when running on battery power.
    Q_PROPERTY(bool pauseOnBattery READ pauseOnBattery WRITE setPauseOnBattery NOTIFY powerPolicyChanged)

    //!< @brief Automatically resume downloads when AC power is restored.
    Q_PROPERTY(bool resumeOnAC READ resumeOnAC WRITE setResumeOnAC NOTIFY powerPolicyChanged)

    //!< @brief Current system power state (battery vs AC).
    Q_PROPERTY(bool onBattery READ onBattery NOTIFY powerStateChanged)

public:
    /**
     * @brief Construct a new download manager.
     * @param parent Optional parent QObject.
     */
    explicit DownloadManager(QObject* parent = nullptr);

    /**
     * @brief Add a download with basic arguments.
     * @param urlStr URL of the file.
     * @param filePath Desired output path or folder.
     */
    Q_INVOKABLE void addDownload(const QString &urlStr, const QString &filePath);

    /**
     * @brief Add a download specifying queue and category.
     * @param urlStr URL of the file.
     * @param filePath Desired output path or folder.
     * @param queueName Queue name.
     * @param category Category name.
     */
    Q_INVOKABLE void addDownloadAdvanced(const QString &urlStr, const QString &filePath, const QString &queueName, const QString &category);

    /**
     * @brief Add a download with queue, category, and initial paused state.
     * @param urlStr URL of the file.
     * @param filePath Desired output path or folder.
     * @param queueName Queue name.
     * @param category Category name.
     * @param startPaused Whether to start paused.
     */
    Q_INVOKABLE void addDownloadAdvancedWithOptions(const QString &urlStr, const QString &filePath, const QString &queueName, const QString &category, bool startPaused);

    /**
     * @brief Add a download with advanced options.
     * @param urlStr URL of the file.
     * @param filePath Desired output path or folder.
     * @param queueName Queue name.
     * @param category Category name.
     * @param startPaused Whether to start paused.
     * @param options Extra options map (headers, mirrors, auth, etc.).
     */
    Q_INVOKABLE void addDownloadAdvancedWithExtras(const QString &urlStr, const QString &filePath, const QString &queueName, const QString &category, bool startPaused, const QVariantMap& options);

    /**
     * @brief Remove a download at the given row index.
     * @param index Row index.
     */
    Q_INVOKABLE void removeDownload(int index);

    /**
     * @brief Remove all finished downloads from the list.
     */
    Q_INVOKABLE void clearCompleted();

    /**
     * @brief Pause all running downloads.
     */
    Q_INVOKABLE void pauseAll();

    /**
     * @brief Resume all paused downloads.
     */
    Q_INVOKABLE void resumeAll();

    /**
     * @brief Cancel all downloads and clear the queue.
     */
    Q_INVOKABLE void cancelAll();

    /**
     * @brief Retry all failed downloads.
     */
    Q_INVOKABLE void retryFailed();

    /**
     * @brief Open the downloaded file for a row.
     * @param index Row index.
     */
    Q_INVOKABLE void openFile(int index);

    /**
     * @brief Reveal the downloaded file in its folder.
     * @param index Row index.
     */
    Q_INVOKABLE void revealInFolder(int index);

    /**
     * @brief Check whether the downloaded file exists for a row.
     * @param index Row index.
     * @return True if file exists.
     */
    Q_INVOKABLE bool fileExists(int index) const;

    /**
     * @brief Get per-task max speed limit in bytes per second.
     * @param index Row index.
     * @return Limit in bytes/sec.
     */
    Q_INVOKABLE qint64 taskMaxSpeed(int index) const;

    /**
     * @brief Set per-task max speed limit.
     * @param index Row index.
     * @param bytesPerSecond Limit in bytes/sec.
     */
    Q_INVOKABLE void setTaskMaxSpeed(int index, qint64 bytesPerSecond);

    /**
     * @brief Pause a specific task.
     * @param index Row index.
     */
    Q_INVOKABLE void pauseTask(int index);

    /**
     * @brief Resume a specific task.
     * @param index Row index.
     */
    Q_INVOKABLE void resumeTask(int index);

    /**
     * @brief Toggle pause/resume for a task.
     * @param index Row index.
     */
    Q_INVOKABLE void togglePause(int index);

    /**
     * @brief Return the completion time for a task.
     * @param index Row index.
     * @return Epoch milliseconds, or 0 if not completed.
     */
    Q_INVOKABLE qint64 taskCompletedAt(int index) const;

    /**
     * @brief Rename the file for a task on disk.
     * @param index Row index.
     * @param newName New file name.
     * @return True if rename succeeded.
     */
    Q_INVOKABLE bool renameTaskFile(int index, const QString& newName);

    /**
     * @brief Move the file for a task on disk.
     * @param index Row index.
     * @param newPath New file path.
     * @return True if move succeeded.
     */
    Q_INVOKABLE bool moveTaskFile(int index, const QString& newPath);

    /**
     * @brief Create a new queue.
     * @param name Queue name.
     */
    Q_INVOKABLE void createQueue(const QString& name);

    /**
     * @brief Remove a queue and reassign tasks to default.
     * @param name Queue name.
     */
    Q_INVOKABLE void removeQueue(const QString& name);

    /**
     * @brief Rename an existing queue.
     * @param oldName Current queue name.
     * @param newName New queue name.
     */
    Q_INVOKABLE void renameQueue(const QString& oldName, const QString& newName);

    /**
     * @brief Assign a task to a queue.
     * @param index Row index.
     * @param name Queue name.
     */
    Q_INVOKABLE void setTaskQueue(int index, const QString& name);

    /**
     * @brief Assign a category to a task.
     * @param index Row index.
     * @param category Category name.
     */
    Q_INVOKABLE void setTaskCategory(int index, const QString& category);

    /**
     * @brief Return ordered queue names.
     * @return Queue name list.
     */
    Q_INVOKABLE QStringList queueNames() const;

    /**
     * @brief Get max concurrent downloads for a queue.
     * @param name Queue name.
     * @return Max concurrent value.
     */
    Q_INVOKABLE int queueMaxConcurrent(const QString& name) const;

    /**
     * @brief Set max concurrent downloads for a queue.
     * @param name Queue name.
     * @param value Max concurrent value.
     */
    Q_INVOKABLE void setQueueMaxConcurrent(const QString& name, int value);

    /**
     * @brief Get max speed for a queue.
     * @param name Queue name.
     * @return Max speed in bytes/sec.
     */
    Q_INVOKABLE qint64 queueMaxSpeed(const QString& name) const;

    /**
     * @brief Set max speed for a queue.
     * @param name Queue name.
     * @param value Max speed in bytes/sec.
     */
    Q_INVOKABLE void setQueueMaxSpeed(const QString& name, qint64 value);

    /**
     * @brief Check if scheduling is enabled for a queue.
     * @param name Queue name.
     * @return True if enabled.
     */
    Q_INVOKABLE bool queueScheduleEnabled(const QString& name) const;

    /**
     * @brief Enable or disable scheduling for a queue.
     * @param name Queue name.
     * @param enabled Whether scheduling is enabled.
     */
    Q_INVOKABLE void setQueueScheduleEnabled(const QString& name, bool enabled);

    /**
     * @brief Get the queue schedule start time in minutes.
     * @param name Queue name.
     * @return Minutes since midnight.
     */
    Q_INVOKABLE int queueScheduleStartMinutes(const QString& name) const;

    /**
     * @brief Set the queue schedule start time in minutes.
     * @param name Queue name.
     * @param minutes Minutes since midnight.
     */
    Q_INVOKABLE void setQueueScheduleStartMinutes(const QString& name, int minutes);

    /**
     * @brief Get the queue schedule end time in minutes.
     * @param name Queue name.
     * @return Minutes since midnight.
     */
    Q_INVOKABLE int queueScheduleEndMinutes(const QString& name) const;

    /**
     * @brief Set the queue schedule end time in minutes.
     * @param name Queue name.
     * @param minutes Minutes since midnight.
     */
    Q_INVOKABLE void setQueueScheduleEndMinutes(const QString& name, int minutes);

    /**
     * @brief Check if quota enforcement is enabled for a queue.
     * @param name Queue name.
     * @return True if enabled.
     */
    Q_INVOKABLE bool queueQuotaEnabled(const QString& name) const;

    /**
     * @brief Enable or disable quota enforcement for a queue.
     * @param name Queue name.
     * @param enabled Whether quota is enabled.
     */
    Q_INVOKABLE void setQueueQuotaEnabled(const QString& name, bool enabled);

    /**
     * @brief Get the daily quota in bytes for a queue.
     * @param name Queue name.
     * @return Quota in bytes.
     */
    Q_INVOKABLE qint64 queueQuotaBytes(const QString& name) const;

    /**
     * @brief Set the daily quota in bytes for a queue.
     * @param name Queue name.
     * @param bytes Quota in bytes.
     */
    Q_INVOKABLE void setQueueQuotaBytes(const QString& name, qint64 bytes);

    /**
     * @brief Get bytes downloaded today for a queue.
     * @param name Queue name.
     * @return Bytes downloaded today.
     */
    Q_INVOKABLE qint64 queueDownloadedToday(const QString& name) const;

    /**
     * @brief Return the default queue name.
     * @return Default queue name.
     */
    Q_INVOKABLE QString defaultQueueName() const;

    /**
     * @brief Return supported category names.
     * @return Category name list.
     */
    Q_INVOKABLE QStringList categoryNames() const;

    /**
     * @brief Return the folder mapped to a category.
     * @param category Category name.
     * @return Folder path, or empty if none.
     */
    Q_INVOKABLE QString categoryFolder(const QString& category) const;

    /**
     * @brief Set the folder mapped to a category.
     * @param category Category name.
     * @param folder Folder path.
     */
    Q_INVOKABLE void setCategoryFolder(const QString& category, const QString& folder);

    /**
     * @brief Return domain rule host list.
     * @return Hosts with assigned queues.
     */
    Q_INVOKABLE QStringList domainRuleHosts() const;

    /**
     * @brief Return the queue mapped to a host.
     * @param host Host name.
     * @return Queue name.
     */
    Q_INVOKABLE QString domainRuleQueue(const QString& host) const;

    /**
     * @brief Map a host to a queue.
     * @param host Host name.
     * @param queue Queue name.
     */
    Q_INVOKABLE void setDomainRule(const QString& host, const QString& queue);

    /**
     * @brief Remove a host-to-queue mapping.
     * @param host Host name.
     */
    Q_INVOKABLE void removeDomainRule(const QString& host);

    /**
     * @brief Detect the category for a filename.
     * @param name File name or path.
     * @return Category name.
     */
    Q_INVOKABLE QString detectCategoryForName(const QString& name) const;

    /**
     * @brief Resolve the final download path for a URL and category.
     * @param urlStr URL of the file.
     * @param category Category name.
     * @param fallbackFolder Fallback folder path.
     * @return Resolved file path.
     */
    Q_INVOKABLE QString resolveDownloadPath(const QString& urlStr, const QString& category, const QString& fallbackFolder) const;

    /**
     * @brief Import a download list from a file.
     * @param path File path.
     */
    Q_INVOKABLE void importList(const QString& path);

    /**
     * @brief Export the download list to a file.
     * @param path File path.
     */
    Q_INVOKABLE void exportList(const QString& path);

    /**
     * @brief Verify a download's checksum.
     * @param index Row index.
     */
    Q_INVOKABLE void verifyTask(int index);

    /**
     * @brief Test a URL with a HEAD request.
     * @param urlStr URL to test.
     */
    Q_INVOKABLE void testUrl(const QString& urlStr);

    /**
     * @brief Get current clipboard text.
     * @return Clipboard text.
     */
    Q_INVOKABLE QString clipboardText() const;
    /**
     * @brief Copy text to clipboard.
     * @param text Text to copy.
     */
    Q_INVOKABLE void copyText(const QString& text) const;

    //!< @brief Access the underlying list model.
    DownloadModel* model() { return &m_model; }

    //!< @brief Return the global max concurrent value.
    int maxConcurrent() const { return m_maxConcurrent; }

    /**
     * @brief Set the global max concurrent value.
     * @param v Max concurrent value.
     */
    void setMaxConcurrent(int v);

    //!< @brief Return the number of active downloads.
    int activeCount() const;

    //!< @brief Return the number of queued downloads.
    int queuedCount() const;

    //!< @brief Return the number of completed downloads.
    int completedCount() const;

    //!< @brief Return the global max speed limit in bytes/sec.
    qint64 globalMaxSpeed() const { return m_globalMaxSpeed; }

    /**
     * @brief Set the global max speed limit in bytes/sec.
     * @param v Max speed limit.
     */
    void setGlobalMaxSpeed(qint64 v);

    //!< @brief Return aggregate speed in bytes/sec.
    qint64 totalSpeed() const { return m_totalSpeed; }

    //!< @brief Return aggregate received bytes.
    qint64 totalReceived() const { return m_totalReceived; }

    //!< @brief Return aggregate total bytes.
    qint64 totalSize() const { return m_totalSize; }

    //!< @brief Return pause-on-battery policy.
    bool pauseOnBattery() const { return m_pauseOnBattery; }

    /**
     * @brief Set pause-on-battery policy.
     * @param enabled Whether to pause on battery.
     */
    void setPauseOnBattery(bool enabled);

    //!< @brief Return resume-on-AC policy.
    bool resumeOnAC() const { return m_resumeOnAC; }

    /**
     * @brief Set resume-on-AC policy.
     * @param enabled Whether to resume on AC.
     */
    void setResumeOnAC(bool enabled);

    //!< @brief Return current power state.
    bool onBattery() const { return m_onBattery; }


signals:
    //!< @brief Emitted when max concurrent changes.
    void maxConcurrentChanged();

    //!< @brief Emitted when queue list changes.
    void queuesChanged();

    //!< @brief Emitted when category folder mapping changes.
    void categoryFoldersChanged();

    //!< @brief Emitted when domain rule mapping changes.
    void domainRulesChanged();

    //!< @brief Emitted when global max speed changes.
    void globalMaxSpeedChanged();

    //!< @brief Emitted when active/queued/completed counts change.
    void countsChanged();

    //!< @brief Emitted when aggregate totals change.
    void totalsChanged();

    //!< @brief Request a UI toast with message and kind.
    void toastRequested(const QString& message, const QString& kind);

    //!< @brief Emitted when power policy changes.
    void powerPolicyChanged();

    //!< @brief Emitted when power state changes.
    void powerStateChanged();


private slots:
    /**
     * @brief Handle task completion.
     * @param success Whether the task succeeded.
     */
    void onTaskFinishedWrapper(bool success);

    /**
     * @brief Handle task progress updates.
     * @param bytesReceived Received bytes.
     * @param bytesTotal Total bytes.
     */
    void onTaskProgress(qint64 bytesReceived, qint64 bytesTotal);

    /**
     * @brief Handle task speed updates.
     * @param bytesPerSecond Current speed.
     */
    void onTaskSpeedChanged(qint64 bytesPerSecond);

    //!< @brief Persist session state to disk.
    void saveSession();

    //!< @brief Periodic scheduler tick.
    void schedulerTick();

    //!< @brief Refresh power state.
    void updatePowerState();

private:
    /**
     * @brief Runtime configuration and accounting data for a download queue.
     *
     * Stores concurrency limits, bandwidth caps, scheduling windows,
     * and daily quota tracking for a single logical queue.
     */
    struct QueueInfo {
        QString name;                   //!< Logical queue name.
        int maxConcurrent = 2;          //!< Maximum concurrent downloads.
        qint64 maxSpeed = 0;            //!< Maximum speed (bytes/sec), 0 = unlimited.
        bool scheduleEnabled = false;   //!< Whether time-based scheduling is enabled.
        int startMinutes = 0;           //!< Schedule start time (minutes since midnight).
        int endMinutes = 0;             //!< Schedule end time (minutes since midnight).
        bool quotaEnabled = false;      //!< Whether daily quota enforcement is enabled.
        qint64 quotaBytes = 0;          //!< Daily quota in bytes.
        qint64 downloadedToday = 0;     //!< Bytes downloaded today.
        QDate lastResetDate;            //!< Date of last quota reset.
    };

    /**
     * @brief Create and register a task instance.
     * @param url Download URL.
     * @param filePath Target file path.
     * @param queueName Queue name.
     * @param category Category name.
     * @param segments Number of segments.
     * @return Created task instance.
     */
    DownloaderTask* createTask(const QUrl& url, const QString& filePath, const QString& queueName, const QString& category, int segments);

    /**
     * @brief Lookup mutable queue info.
     * @param name Queue name.
     * @return QueueInfo pointer or null.
     */
    QueueInfo* queueInfo(const QString& name);

    /**
     * @brief Lookup immutable queue info.
     * @param name Queue name.
     * @return QueueInfo pointer or null.
     */
    const QueueInfo* queueInfo(const QString& name) const;

    //!< @brief Ensure a default queue exists.
    void ensureDefaultQueue();

    /**
     * @brief Apply effective speed limits to a task.
     * @param task Task instance.
     */
    void applyTaskSpeed(DownloaderTask* task);

    /**
     * @brief Enforces queue scheduling and quota policies.
     *
     * Evaluates current time and quota counters and pauses or resumes
     * tasks accordingly.
     */
    void enforceQueuePolicies();

    /**
     * @brief Check if a time is within a queue schedule window.
     * @param info Queue info.
     * @param now Current time.
     * @return True if within schedule.
     */
    bool isWithinSchedule(const QueueInfo& info, const QTime& now) const;

    /**
     * @brief Check if a queue is allowed to run now.
     * @param info Queue info.
     * @param now Current time.
     * @return True if allowed.
     */
    bool isQueueAllowed(const QueueInfo& info, const QTime& now) const;

    //!< @brief Load persisted session state.
    void loadSession();

    //!< @brief Schedule a session save.
    void scheduleSave();

    /**
     * @brief Start queued tasks that are allowed under current policies.
     */
    void startQueued();

    /**
     * @brief Recompute aggregate speed and byte counters.
     *
     * Emits totalsChanged() when values are updated.
     */
    void updateTotals();

    /**
     * @brief Rename task files on disk, including segment parts.
     * @param oldPath Old file path.
     * @param newPath New file path.
     * @param segments Number of segments.
     * @return True if rename succeeded.
     */
    bool renameTaskFilesOnDisk(const QString& oldPath, const QString& newPath, int segments) const;

    /**
     * @brief Resolve a category folder mapping.
     * @param category Category name.
     * @return Folder path or empty.
     */
    QString categoryFolderForName(const QString& category) const;

    /**
     * @brief Internal add-download implementation.
     * @param urlStr URL string.
     * @param filePath Target path or folder.
     * @param queueName Queue name.
     * @param category Category name.
     * @param startPaused Whether to start paused.
     * @param options Optional extras map.
     * @return Created task instance or null.
     */
    DownloaderTask* addDownloadInternal(const QString &urlStr, const QString &filePath, const QString &queueName, const QString &category, bool startPaused, const QVariantMap* options);

    /**
     * @brief Apply extra options to a task.
     * @param task Task instance.
     * @param options Options map.
     */
    void applyTaskOptions(DownloaderTask* task, const QVariantMap& options);

    /**
     * @brief Execute post-download actions for a task.
     * @param task Task instance.
     */
    void applyPostActions(DownloaderTask* task);

    /**
     * @brief Verify checksum asynchronously.
     * @param task Task instance.
     */
    void verifyChecksumAsync(DownloaderTask* task);

    /**
     * @brief Reveal a file path in the file manager.
     * @param path File path.
     */
    void revealPath(const QString& path) const;


    DownloadModel m_model;                                                          //!< Backing list model.
    int m_maxConcurrent = 3;                                                        //!< Global max concurrent downloads.
    qint64 m_globalMaxSpeed = 0;                                                    //!< Global speed limit in bytes/sec.
    qint64 m_totalSpeed = 0;                                                        //!< Aggregate speed in bytes/sec.
    qint64 m_totalReceived = 0;                                                     //!< Aggregate received bytes.
    qint64 m_totalSize = 0;                                                         //!< Aggregate total bytes.

    QHash<DownloaderTask*, qint64> m_taskSpeed;                                     //!< Per-task current speed.
    QHash<DownloaderTask*, qint64> m_taskReceived;                                  //!< Per-task received bytes.
    QHash<DownloaderTask*, qint64> m_taskTotal;                                     //!< Per-task total bytes.
    QHash<DownloaderTask*, qint64> m_taskLastReceived;                              //!< Per-task last received bytes.
    QHash<DownloaderTask*, qint64> m_taskMaxSpeed;                                  //!< Per-task speed limit.
    QHash<DownloaderTask*, qint64> m_taskCompletedAt;                               //!< Per-task completion time.
    QHash<DownloaderTask*, int> m_taskRetryCount;                                   //!< Per-task retry count.
    QHash<DownloaderTask*, QString> m_taskQueue;                                    //!< Per-task queue mapping.
    QHash<DownloaderTask*, QString> m_taskCategory;                                 //!< Per-task category mapping.
    QHash<DownloaderTask*, bool> m_taskPausedBySchedule;                            //!< Paused by schedule.
    QHash<DownloaderTask*, bool> m_taskPausedByQuota;                               //!< Paused by quota.
    QHash<DownloaderTask*, bool> m_taskPausedByBattery;                             //!< Paused by battery.
    QHash<DownloaderTask*, QPointer<QFutureWatcher<QString>>> m_checksumWatchers;   //!< Async checksum watchers.

    QVector<DownloaderTask*> m_queue;                                               //!< Queue in insertion order.
    QHash<QString, QueueInfo> m_queues;                                             //!< Queue config map.
    QStringList m_queueOrder;                                                       //!< Queue ordering list.
    QHash<QString, QString> m_categoryFolders;                                      //!< Category folder mapping.
    QHash<QString, QString> m_domainRules;                                          //!< Host-to-queue mapping.
    QTimer m_saveTimer;                                                             //!< Debounced session save timer.
    QTimer m_schedulerTimer;                                                        //!< Scheduler tick timer.
    QTimer m_powerTimer;                                                            //!< Power polling timer.

    bool m_pauseOnBattery = false;                                                  //!< Pause on battery policy.
    bool m_resumeOnAC = true;                                                       //!< Resume on AC policy.
    bool m_onBattery = false;                                                       //!< Cached power state.
    bool m_restoreInProgress = false;                                               //!< Session restore guard.
    bool m_bulkCancelInProgress = false;                                            //!< Bulk cancel guard.
    int m_autoRetryMax = 2;                                                         //!< Default retry attempts.
    int m_autoRetryDelaySec = 5;                                                    //!< Default retry delay in seconds.

    QString m_sessionPath;                                                          //!< Session persistence path.
    PowerMonitor m_powerMonitor;                                                    //!< Power state helper.
};

#include "downloadmanager.moc"
