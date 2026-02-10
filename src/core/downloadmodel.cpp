module;
#include <algorithm>
#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QString>
#include <QVariant>
#include <QVector>
#include <QtGlobal>

module raad.core.downloadmodel;

DownloadModel::DownloadModel(QObject *parent) : QAbstractListModel(parent) {}

int DownloadModel::rowCount(const QModelIndex &parent) const {
    Q_UNUSED(parent)
    return m_downloads.size();
}

QVariant DownloadModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_downloads.size()) return {};
    const auto &item = m_downloads[index.row()];

    switch (role) {
    case FileNameRole: return item.fileName;
    case ProgressRole: return item.total > 0 ? (double)item.received / item.total : (double)item.received;
    case FinishedRole: return item.finished;
    case TaskRole: return QVariant::fromValue(static_cast<QObject*>(item.task));
    case StatusRole: return item.task ? item.task->stateString() : QString();
    case BytesReceivedRole: return item.received;   // raw bytes downloaded
    case BytesTotalRole:    return item.total;      // total bytes (0 if unknown)
    case QueueRole: return item.queueName;
    case CategoryRole: return item.category;
    }
    return {};
}

QHash<int, QByteArray> DownloadModel::roleNames() const {
    return {
        {FileNameRole, "fileName"},
        {ProgressRole, "progress"},
        {FinishedRole, "finished"},
        {TaskRole, "task"},
        {StatusRole, "status"},
        {BytesReceivedRole, "bytesReceived"},
        {BytesTotalRole, "bytesTotal"},
        {QueueRole, "queueName"},
        {CategoryRole, "category"}
    };
}

void DownloadModel::addDownload(DownloaderTask* task, const QString& queueName, const QString& category) {
    beginInsertRows(QModelIndex(), m_downloads.size(), m_downloads.size());
    DownloadItem item;
    item.fileName = task->fileName();
    item.queueName = queueName;
    item.category = category;
    item.task = task;
    m_downloads.append(item);
    endInsertRows();

    connect(task, &DownloaderTask::progress, this, &DownloadModel::onTaskProgress);
    connect(task, &DownloaderTask::finished, this, &DownloadModel::onTaskFinished);
}

void DownloadModel::updateMetadata(DownloaderTask* task, const QString& queueName, const QString& category) {
    if (!task) return;
    for (int i = 0; i < m_downloads.size(); ++i) {
        if (m_downloads[i].task == task) {
            m_downloads[i].queueName = queueName;
            m_downloads[i].category = category;
            QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {QueueRole, CategoryRole});
            break;
        }
    }
}

void DownloadModel::seedProgress(DownloaderTask* task, qint64 bytesReceived, qint64 bytesTotal)
{
    if (!task) return;
    for (int i = 0; i < m_downloads.size(); ++i) {
        if (m_downloads[i].task == task) {
            m_downloads[i].received = bytesReceived;
            m_downloads[i].total = bytesTotal;
            QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {ProgressRole, BytesReceivedRole, BytesTotalRole});
            break;
        }
    }
}

void DownloadModel::seedFinished(DownloaderTask* task, bool finished)
{
    if (!task) return;
    for (int i = 0; i < m_downloads.size(); ++i) {
        if (m_downloads[i].task == task) {
            if (m_downloads[i].finished == finished) break;
            m_downloads[i].finished = finished;
            QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {FinishedRole});
            break;
        }
    }
}

void DownloadModel::updateFileName(DownloaderTask* task, const QString& fileName)
{
    if (!task) return;
    for (int i = 0; i < m_downloads.size(); ++i) {
        if (m_downloads[i].task == task) {
            if (m_downloads[i].fileName == fileName) break;
            m_downloads[i].fileName = fileName;
            QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {FileNameRole});
            break;
        }
    }
}

void DownloadModel::sortBy(const QString& roleName, bool ascending)
{
    int role = FileNameRole;
    if (roleName == "fileName") role = FileNameRole;
    else if (roleName == "bytesTotal") role = BytesTotalRole;
    else if (roleName == "bytesReceived") role = BytesReceivedRole;
    else if (roleName == "queueName") role = QueueRole;
    else if (roleName == "category") role = CategoryRole;
    else if (roleName == "status") role = StatusRole;

    beginResetModel();
    std::stable_sort(m_downloads.begin(), m_downloads.end(), [role, ascending](const DownloadItem& a, const DownloadItem& b) {
        auto less = [ascending](const auto& lhs, const auto& rhs) {
            return ascending ? (lhs < rhs) : (lhs > rhs);
        };
        switch (role) {
        case FileNameRole:
            return less(a.fileName.toLower(), b.fileName.toLower());
        case BytesTotalRole:
            return less(a.total, b.total);
        case BytesReceivedRole:
            return less(a.received, b.received);
        case QueueRole:
            return less(a.queueName.toLower(), b.queueName.toLower());
        case CategoryRole:
            return less(a.category.toLower(), b.category.toLower());
        case StatusRole: {
            const QString sa = a.task ? a.task->stateString() : QString();
            const QString sb = b.task ? b.task->stateString() : QString();
            return less(sa.toLower(), sb.toLower());
        }
        default:
            return less(a.fileName.toLower(), b.fileName.toLower());
        }
    });
    endResetModel();
}

DownloaderTask* DownloadModel::taskAt(int index) const {
    if (index < 0 || index >= m_downloads.size()) return nullptr;
    return m_downloads[index].task;
}

bool DownloadModel::isFinishedAt(int index) const {
    if (index < 0 || index >= m_downloads.size()) return false;
    return m_downloads[index].finished;
}

void DownloadModel::removeAt(int index) {
    if (index < 0 || index >= m_downloads.size()) return;
    beginRemoveRows(QModelIndex(), index, index);
    DownloadItem item = m_downloads.takeAt(index);
    endRemoveRows();
    if (item.task) item.task->deleteLater();
}

void DownloadModel::onTaskProgress(qint64 bytesReceived, qint64 bytesTotal) {
    auto* senderTask = qobject_cast<DownloaderTask*>(sender());
    for (int i = 0; i < m_downloads.size(); ++i) {
        if (m_downloads[i].task == senderTask) {
            m_downloads[i].received = bytesReceived;
            m_downloads[i].total = bytesTotal;
            QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {ProgressRole, BytesReceivedRole, BytesTotalRole});
            break;
        }
    }
}

void DownloadModel::onTaskFinished(bool) {
    auto* senderTask = qobject_cast<DownloaderTask*>(sender());
    for (int i = 0; i < m_downloads.size(); ++i) {
        if (m_downloads[i].task == senderTask) {
            m_downloads[i].finished = true;
            QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {FinishedRole});
            break;
        }
    }
}
