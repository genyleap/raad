/*!
 * @file        downloadmodel.cppm
 * @brief       QAbstractListModel implementation for download tasks.
 * @details     Exposes active and completed download tasks as a Qt item model,
 *              making download metadata available to QML via custom roles.
 *
 *              The model acts as a thin presentation layer between the
 *              download core (DownloaderTask) and the QML UI. It maintains
 *              a list of lightweight view items and reacts to task signals
 *              to keep progress, status, and completion state in sync.
 *
 *              This model is designed for:
 *              - Real-time progress updates
 *              - Sorting and filtering in QML
 *              - History views for completed downloads
 *
 * @author      <a href='https://github.com/thecompez'>Kambiz Asadzadeh</a>
 * @since       09 Feb 2026
 * @copyright   Copyright (c) 2026 Genyleap. All rights reserved.
 * @license     https://github.com/genyleap/raad/blob/main/LICENSE.md
 */

module;
#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QVariant>
#include <QVector>

#ifndef Q_MOC_RUN
export module raad.core.downloadmodel;
import raad.core.downloadertask;
#endif

#ifdef Q_MOC_RUN
#define RAAD_MODULE_EXPORT
#else
#define RAAD_MODULE_EXPORT export
#endif

/**
 * @brief Lightweight data container representing a single download row.
 *
 * Holds UI-relevant metadata derived from a DownloaderTask instance.
 * This structure is intentionally kept simple and is owned by DownloadModel.
 */
RAAD_MODULE_EXPORT struct DownloadItem {

    //!< @brief Display file name or target path.
    QString fileName;

    //!< @brief Logical queue name associated with the download.
    QString queueName;

    //!< @brief Category label used for grouping and filtering.
    QString category;

    //!< @brief Pointer to the underlying download task.
    DownloaderTask* task = nullptr;

    //!< @brief Number of bytes received so far.
    qint64 received = 0;

    //!< @brief Total number of bytes expected (0 if unknown).
    qint64 total = 0;

    //!< @brief Indicates whether the download has finished (for history view).
    bool finished = false;
};

/**
 * @brief Qt list model exposing download tasks to QML.
 *
 * Implements QAbstractListModel and exposes download-related roles
 * such as progress, status, queue, and category to the UI layer.
 *
 * The model listens to signals emitted by DownloaderTask instances
 * and updates rows accordingly.
 */
RAAD_MODULE_EXPORT class DownloadModel : public QAbstractListModel {
    Q_OBJECT

public:
    /**
     * @brief Custom model roles exposed to QML.
     */
    enum Roles {
        FileNameRole = Qt::UserRole + 1,  //!< Display file name
        ProgressRole,                     //!< Progress ratio (0.0 – 1.0)
        FinishedRole,                     //!< Completion state
        TaskRole,                         //!< Pointer to DownloaderTask
        StatusRole,                       //!< Human-readable status string
        BytesReceivedRole,                //!< Raw bytes received
        BytesTotalRole,                   //!< Raw bytes total
        QueueRole,                        //!< Queue name
        CategoryRole                      //!< Category label
    };

    /**
     * @brief Constructs an empty download model.
     *
     * @param parent Optional QObject parent.
     */
    explicit DownloadModel(QObject* parent = nullptr);

    /**
     * @brief Returns the number of rows in the model.
     */
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;

    /**
     * @brief Returns data for a given row and role.
     */
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    /**
     * @brief Returns role name mappings for QML access.
     */
    QHash<int, QByteArray> roleNames() const override;

    /**
     * @brief Adds a new download task to the model.
     *
     * The model takes ownership of tracking the task but does not
     * assume ownership of the task object's lifetime.
     *
     * @param task Pointer to the download task.
     * @param queueName Logical queue name.
     * @param category Category label.
     */
    void addDownload(DownloaderTask* task,
                     const QString& queueName,
                     const QString& category);

    /**
     * @brief Updates queue and category metadata for an existing task.
     */
    void updateMetadata(DownloaderTask* task,
                        const QString& queueName,
                        const QString& category);

    /**
     * @brief Seeds progress values for restored or resumed tasks.
     *
     * Used when reconstructing the model from persisted state.
     */
    void seedProgress(DownloaderTask* task,
                      qint64 bytesReceived,
                      qint64 bytesTotal);

    /**
     * @brief Seeds the finished state for restored tasks.
     */
    void seedFinished(DownloaderTask* task, bool finished);

    /**
     * @brief Updates the display file name of a task.
     */
    void updateFileName(DownloaderTask* task, const QString& fileName);

    /**
     * @brief Sorts the model by a role name.
     *
     * Exposed to QML for user-driven sorting.
     *
     * @param roleName Role name string (as exposed via roleNames()).
     * @param ascending Sort order.
     */
    Q_INVOKABLE void sortBy(const QString& roleName, bool ascending);

    /**
     * @brief Returns the task associated with a given row.
     */
    DownloaderTask* taskAt(int index) const;

    /**
     * @brief Checks whether the download at the given index has finished.
     */
    bool isFinishedAt(int index) const;

    /**
     * @brief Removes a row from the model.
     *
     * The associated task is scheduled for deletion if applicable.
     */
    void removeAt(int index);

private slots:
    /**
     * @brief Updates progress values in response to task progress signals.
     */
    void onTaskProgress(qint64 bytesReceived, qint64 bytesTotal);

    /**
     * @brief Marks a task as finished when completion is signaled.
     *
     * @param success Indicates whether the download completed successfully.
     */
    void onTaskFinished(bool success);

private:
    //!< @brief Internal storage for download items.
    QVector<DownloadItem> m_downloads;
};

#include "downloadmodel.moc"
