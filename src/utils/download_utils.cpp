module;
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QtGlobal>

module raad.utils.download_utils;

namespace raad::utils {

QString normalizeFilePath(const QString& path)
{
    if (path.startsWith("file://")) {
        QUrl url(path);
        if (url.isValid() && url.isLocalFile()) {
            return url.toLocalFile();
        }
    }
    return path;
}

qint64 bytesReceivedOnDisk(const QString& filePath, int segments)
{
    const QString localPath = normalizeFilePath(filePath);
    if (localPath.isEmpty()) return 0;

    qint64 partsTotal = 0;
    bool anyParts = false;

    const int maxParts = qMax(1, segments);
    for (int i = 0; i < maxParts; ++i) {
        const QString partPath = QString("%1.part%2").arg(localPath).arg(i);
        QFileInfo info(partPath);
        if (!info.exists() || !info.isFile()) continue;
        anyParts = true;
        partsTotal += info.size();
    }
    if (anyParts) return partsTotal;

    QFileInfo singlePart(localPath + ".part");
    if (singlePart.exists() && singlePart.isFile()) {
        return singlePart.size();
    }

    QFileInfo mainInfo(localPath);
    if (mainInfo.exists() && mainInfo.isFile()) {
        return mainInfo.size();
    }
    return 0;
}

QString decodeQueryValue(const QString& value)
{
    QString v = value;
    v.replace('+', ' ');
    return QUrl::fromPercentEncoding(v.toUtf8());
}

QString filenameFromDisposition(const QString& value)
{
    const QString decoded = decodeQueryValue(value);
    if (decoded.isEmpty()) return QString();
    QRegularExpression re(QStringLiteral("filename\\*?=(?:UTF-8''|\"?)([^\";]+)"));
    auto match = re.match(decoded);
    if (match.hasMatch()) return match.captured(1).trimmed();
    return QString();
}

QString fileNameFromUrl(const QUrl& url)
{
    if (!url.isValid()) return QString();
    QUrlQuery query(url);
    QString disp = query.queryItemValue(QStringLiteral("response-content-disposition"));
    if (disp.isEmpty()) disp = query.queryItemValue(QStringLiteral("content-disposition"));
    if (disp.isEmpty()) disp = query.queryItemValue(QStringLiteral("rscd"));
    if (!disp.isEmpty()) {
        const QString fromDisp = filenameFromDisposition(disp);
        if (!fromDisp.isEmpty()) return fromDisp;
    }
    const QString filename = query.queryItemValue(QStringLiteral("filename"));
    if (!filename.isEmpty()) return decodeQueryValue(filename);

    const QString path = url.path();
    const QString base = QFileInfo(path).fileName();
    return base;
}

QString normalizeHost(const QString& host)
{
    QString h = host.trimmed().toLower();
    if (h.isEmpty()) return QString();
    if (h.contains("://")) {
        QUrl u(h);
        if (u.isValid()) h = u.host().toLower();
    }
    const int slash = h.indexOf('/');
    if (slash >= 0) h = h.left(slash);
    return h;
}

bool looksLikeGuidName(const QString& name)
{
    if (name.isEmpty()) return false;
    QRegularExpression re(QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
    return re.match(name).hasMatch();
}

QString normalizeChecksum(const QString& value)
{
    QString out = value.trimmed().toLower();
    out.remove(' ');
    return out;
}

QString detectChecksumAlgo(const QString& expected)
{
    const QString norm = normalizeChecksum(expected);
    const int len = norm.length();
    if (len == 32) return QStringLiteral("MD5");
    if (len == 40) return QStringLiteral("SHA1");
    if (len == 64) return QStringLiteral("SHA256");
    if (len == 128) return QStringLiteral("SHA512");
    return QString();
}

QString uniqueFilePath(const QString& path)
{
    const QString normalized = normalizeFilePath(path);
    if (normalized.isEmpty()) return normalized;
    QFileInfo info(normalized);
    const QString dirPath = info.absolutePath();
    const QString base = info.completeBaseName();
    const QString suffix = info.completeSuffix();
    const auto existsCandidate = [](const QString& candidate) {
        return QFile::exists(candidate) || QFile::exists(candidate + ".part");
    };
    if (!existsCandidate(normalized)) return normalized;

    QDir dir(dirPath);
    for (int i = 1; i < 10000; ++i) {
        const QString name = suffix.isEmpty()
            ? QString("%1 (%2)").arg(base).arg(i)
            : QString("%1 (%2).%3").arg(base).arg(i).arg(suffix);
        const QString candidate = dir.filePath(name);
        if (!existsCandidate(candidate)) return candidate;
    }
    return normalized;
}

bool fileExistsPath(const QString& path)
{
    const QString normalized = normalizeFilePath(path);
    if (normalized.isEmpty()) return false;
    QFileInfo info(normalized);
    return info.exists() && info.isFile();
}

} // namespace raad::utils
