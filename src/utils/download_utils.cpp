module;
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QUrlQuery>
#include <QtGlobal>

module raad.utils.download_utils;

namespace raad::utils {

static int checksumLengthForAlgo(const QString& algo)
{
    const QString upper = algo.trimmed().toUpper();
    if (upper == QStringLiteral("MD5")) return 32;
    if (upper == QStringLiteral("SHA1")) return 40;
    if (upper == QStringLiteral("SHA256")) return 64;
    if (upper == QStringLiteral("SHA512")) return 128;
    return 0;
}

static QString extractChecksumCandidate(const QString& value, int preferredLength = 0)
{
    const QString trimmed = value.trimmed().toLower();
    if (trimmed.isEmpty()) return QString();

    const QList<int> lengths = preferredLength > 0
        ? QList<int>{preferredLength, 128, 64, 40, 32}
        : QList<int>{128, 64, 40, 32};

    for (const int length : lengths) {
        if (length <= 0) continue;
        const QRegularExpression re(QStringLiteral("(^|[^0-9a-f])([0-9a-f]{%1})(?=$|[^0-9a-f])").arg(length));
        const QRegularExpressionMatch match = re.match(trimmed);
        if (match.hasMatch()) {
            return match.captured(2);
        }
    }
    return QString();
}

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

static QString extractNameCandidate(const QString& value)
{
    const QString decoded = decodeQueryValue(value).trimmed();
    if (decoded.isEmpty()) return QString();

    QString clean = decoded;
    const int hashIdx = clean.indexOf(QLatin1Char('#'));
    if (hashIdx >= 0) clean = clean.left(hashIdx);
    const int queryIdx = clean.indexOf(QLatin1Char('?'));
    if (queryIdx >= 0) clean = clean.left(queryIdx);
    while (clean.endsWith(QLatin1Char('/'))) clean.chop(1);
    if (clean.isEmpty()) return QString();

    const QString base = QFileInfo(clean).fileName();
    return base.isEmpty() ? clean : base;
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
        const QString dispName = extractNameCandidate(fromDisp);
        if (!dispName.isEmpty()) return dispName;
    }
    const QStringList queryNameKeys{
        QStringLiteral("filename"),
        QStringLiteral("file"),
        QStringLiteral("f"),
        QStringLiteral("name"),
        QStringLiteral("download"),
        QStringLiteral("path")
    };
    for (const QString& key : queryNameKeys) {
        const QString value = query.queryItemValue(key);
        if (value.isEmpty()) continue;
        const QString inferred = extractNameCandidate(value);
        if (!inferred.isEmpty()) return inferred;
    }

    QString path = url.path();
    while (path.endsWith(QLatin1Char('/'))) path.chop(1);
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
    const QString extracted = extractChecksumCandidate(value);
    if (!extracted.isEmpty()) return extracted;

    QString out = value.trimmed().toLower();
    out.remove(QRegularExpression(QStringLiteral("\\s+")));

    static const QStringList prefixes{
        QStringLiteral("md5:"),
        QStringLiteral("sha1:"),
        QStringLiteral("sha256:"),
        QStringLiteral("sha512:")
    };
    for (const QString& prefix : prefixes) {
        if (out.startsWith(prefix)) {
            out = out.mid(prefix.size());
            break;
        }
    }
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

QString extractChecksumFromText(const QString& text, const QString& fileName, const QString& preferredAlgo)
{
    const QString normalizedFileName = QFileInfo(fileName.trimmed()).fileName().toLower();
    const int preferredLength = checksumLengthForAlgo(preferredAlgo);
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);

    QString fallback;
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) continue;

        const QString candidate = extractChecksumCandidate(line, preferredLength);
        if (candidate.isEmpty()) continue;

        if (normalizedFileName.isEmpty()) {
            return candidate;
        }

        const QString lowerLine = line.toLower();
        if (lowerLine.contains(normalizedFileName)) {
            return candidate;
        }
        if (fallback.isEmpty()) {
            fallback = candidate;
        }
    }

    if (!fallback.isEmpty()) return fallback;
    return extractChecksumCandidate(text, preferredLength);
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
