module;

#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QMimeDatabase>
#include <QMimeType>
#include <QString>
#include <QStringList>
#include <QUrl>

module raad.utils.category_utils;

namespace raad::utils {
namespace {

/*!
 * @brief Returns all categories in stable order.
 */
[[nodiscard]] auto& categoryValues() noexcept
{
    static const QList<DownloadCategory> values = {
        DownloadCategory::Auto,
        DownloadCategory::Video,
        DownloadCategory::Audio,
        DownloadCategory::Images,
        DownloadCategory::Subtitles,
        DownloadCategory::Archives,
        DownloadCategory::Documents,
        DownloadCategory::Programs,
        DownloadCategory::DiskImages,
        DownloadCategory::Fonts,
        DownloadCategory::Code,
        DownloadCategory::Torrents,
        DownloadCategory::NFT,
        DownloadCategory::Other
    };
    return values;
}

/*!
 * @brief Returns the fallback category for unknown content.
 */
[[nodiscard]] constexpr auto fallbackCategory() noexcept -> DownloadCategory
{
    return DownloadCategory::Other;
}

/*!
 * @brief Converts a path or URL into a normalized classification input.
 */
[[nodiscard]] QString normalizeInput(QString value)
{
    value = value.trimmed();

    if (value.isEmpty()) {
        return {};
    }

    const auto url = QUrl::fromUserInput(value);
    if (url.isValid() && !url.scheme().isEmpty()) {
        if (url.isLocalFile()) {
            value = url.toLocalFile();
        } else {
            value = url.path();
        }
    }

    if (const auto hashIndex = value.indexOf(u'#'); hashIndex >= 0) {
        value.truncate(hashIndex);
    }

    if (const auto queryIndex = value.indexOf(u'?'); queryIndex >= 0) {
        value.truncate(queryIndex);
    }

    while (!value.isEmpty() &&
           (value.endsWith(u'/') || value.endsWith(u'\\')))
    {
        value.chop(1);
    }

    return value;
}

/*!
 * @brief Returns the lowercase file name component for the normalized input.
 */
[[nodiscard]] QString lowerFileName(const QString& value)
{
    return QFileInfo{ value }.fileName().toLower();
}

/*!
 * @brief Returns a normalized lowercase extension without the leading dot.
 */
[[nodiscard]] QString extractExtension(const QString& normalizedPath)
{
    if (normalizedPath.isEmpty()) {
        return {};
    }

    const auto fileName = lowerFileName(normalizedPath);
    if (fileName.isEmpty()) {
        return {};
    }

    const auto lastDot = fileName.lastIndexOf(u'.');
    if (lastDot < 0 || lastDot + 1 >= fileName.size()) {
        return {};
    }

    return fileName.sliced(lastDot + 1);
}

/*!
 * @brief Detects well-known multi-part archive naming patterns.
 */
[[nodiscard]] DownloadCategory detectMultipartArchiveCategory(const QString& fileName)
{
    if (fileName.isEmpty()) {
        return DownloadCategory::Other;
    }

    if (fileName.endsWith(u".tar.gz") ||
        fileName.endsWith(u".tar.bz2") ||
        fileName.endsWith(u".tar.xz") ||
        fileName.endsWith(u".tar.zst") ||
        fileName.endsWith(u".tar.lz") ||
        fileName.endsWith(u".tar.lz4") ||
        fileName.endsWith(u".tar.lzma"))
    {
        return DownloadCategory::Archives;
    }

    if (fileName.contains(u".part") && fileName.contains(u".rar")) {
        return DownloadCategory::Archives;
    }

    if (fileName.endsWith(u".001") ||
        fileName.endsWith(u".002") ||
        fileName.endsWith(u".003"))
    {
        return DownloadCategory::Archives;
    }

    return DownloadCategory::Other;
}

/*!
 * @brief Creates the extension-to-category lookup table.
 */
[[nodiscard]] auto buildExtensionCategoryMap() -> QHash<QString, DownloadCategory>
{
    using Pair = std::pair<QString, DownloadCategory>;

    const auto make = [](std::initializer_list<Pair> items) {
        QHash<QString, DownloadCategory> map;
        map.reserve(static_cast<qsizetype>(items.size()));

        for (const auto& [extension, category] : items) {
            map.insert(extension, category);
        }

        return map;
    };

    return make({
        // Video
        { "mp4", DownloadCategory::Video },
        { "m4v", DownloadCategory::Video },
        { "mkv", DownloadCategory::Video },
        { "avi", DownloadCategory::Video },
        { "mov", DownloadCategory::Video },
        { "wmv", DownloadCategory::Video },
        { "flv", DownloadCategory::Video },
        { "webm", DownloadCategory::Video },
        { "ts", DownloadCategory::Video },
        { "mts", DownloadCategory::Video },
        { "m2ts", DownloadCategory::Video },
        { "mpg", DownloadCategory::Video },
        { "mpeg", DownloadCategory::Video },
        { "3gp", DownloadCategory::Video },
        { "3g2", DownloadCategory::Video },
        { "ogv", DownloadCategory::Video },
        { "vob", DownloadCategory::Video },
        { "rm", DownloadCategory::Video },
        { "rmvb", DownloadCategory::Video },
        { "asf", DownloadCategory::Video },
        { "f4v", DownloadCategory::Video },
        { "qt", DownloadCategory::Video },

        // Audio
        { "mp3", DownloadCategory::Audio },
        { "aac", DownloadCategory::Audio },
        { "m4a", DownloadCategory::Audio },
        { "flac", DownloadCategory::Audio },
        { "wav", DownloadCategory::Audio },
        { "ogg", DownloadCategory::Audio },
        { "opus", DownloadCategory::Audio },
        { "oga", DownloadCategory::Audio },
        { "wma", DownloadCategory::Audio },
        { "aiff", DownloadCategory::Audio },
        { "aif", DownloadCategory::Audio },
        { "ape", DownloadCategory::Audio },
        { "alac", DownloadCategory::Audio },
        { "amr", DownloadCategory::Audio },
        { "mid", DownloadCategory::Audio },
        { "midi", DownloadCategory::Audio },
        { "ac3", DownloadCategory::Audio },
        { "dts", DownloadCategory::Audio },
        { "caf", DownloadCategory::Audio },

        // Images
        { "jpg", DownloadCategory::Images },
        { "jpeg", DownloadCategory::Images },
        { "png", DownloadCategory::Images },
        { "gif", DownloadCategory::Images },
        { "bmp", DownloadCategory::Images },
        { "webp", DownloadCategory::Images },
        { "svg", DownloadCategory::Images },
        { "svgz", DownloadCategory::Images },
        { "tif", DownloadCategory::Images },
        { "tiff", DownloadCategory::Images },
        { "ico", DownloadCategory::Images },
        { "heic", DownloadCategory::Images },
        { "heif", DownloadCategory::Images },
        { "avif", DownloadCategory::Images },
        { "jxl", DownloadCategory::Images },
        { "psd", DownloadCategory::Images },
        { "ai", DownloadCategory::Images },
        { "eps", DownloadCategory::Images },
        { "raw", DownloadCategory::Images },
        { "cr2", DownloadCategory::Images },
        { "nef", DownloadCategory::Images },
        { "arw", DownloadCategory::Images },
        { "dng", DownloadCategory::Images },

        // Subtitles
        { "srt", DownloadCategory::Subtitles },
        { "ass", DownloadCategory::Subtitles },
        { "ssa", DownloadCategory::Subtitles },
        { "vtt", DownloadCategory::Subtitles },
        { "sub", DownloadCategory::Subtitles },
        { "sup", DownloadCategory::Subtitles },
        { "idx", DownloadCategory::Subtitles },
        { "ttml", DownloadCategory::Subtitles },

        // Archives
        { "zip", DownloadCategory::Archives },
        { "rar", DownloadCategory::Archives },
        { "7z", DownloadCategory::Archives },
        { "tar", DownloadCategory::Archives },
        { "gz", DownloadCategory::Archives },
        { "bz2", DownloadCategory::Archives },
        { "xz", DownloadCategory::Archives },
        { "lz", DownloadCategory::Archives },
        { "lz4", DownloadCategory::Archives },
        { "lzma", DownloadCategory::Archives },
        { "zst", DownloadCategory::Archives },
        { "tgz", DownloadCategory::Archives },
        { "tbz", DownloadCategory::Archives },
        { "tbz2", DownloadCategory::Archives },
        { "txz", DownloadCategory::Archives },
        { "tlz", DownloadCategory::Archives },
        { "tzst", DownloadCategory::Archives },
        { "cab", DownloadCategory::Archives },
        { "arj", DownloadCategory::Archives },
        { "cpio", DownloadCategory::Archives },
        { "ace", DownloadCategory::Archives },
        { "jar", DownloadCategory::Archives },
        { "war", DownloadCategory::Archives },
        { "ear", DownloadCategory::Archives },
        { "apk", DownloadCategory::Archives },
        { "xpi", DownloadCategory::Archives },
        { "crx", DownloadCategory::Archives },
        { "vsix", DownloadCategory::Archives },

        // Documents
        { "pdf", DownloadCategory::Documents },
        { "txt", DownloadCategory::Documents },
        { "rtf", DownloadCategory::Documents },
        { "md", DownloadCategory::Documents },
        { "markdown", DownloadCategory::Documents },
        { "doc", DownloadCategory::Documents },
        { "docx", DownloadCategory::Documents },
        { "odt", DownloadCategory::Documents },
        { "pages", DownloadCategory::Documents },
        { "xls", DownloadCategory::Documents },
        { "xlsx", DownloadCategory::Documents },
        { "ods", DownloadCategory::Documents },
        { "csv", DownloadCategory::Documents },
        { "tsv", DownloadCategory::Documents },
        { "numbers", DownloadCategory::Documents },
        { "ppt", DownloadCategory::Documents },
        { "pptx", DownloadCategory::Documents },
        { "odp", DownloadCategory::Documents },
        { "key", DownloadCategory::Documents },
        { "epub", DownloadCategory::Documents },
        { "mobi", DownloadCategory::Documents },
        { "azw", DownloadCategory::Documents },
        { "azw3", DownloadCategory::Documents },
        { "djvu", DownloadCategory::Documents },
        { "tex", DownloadCategory::Documents },
        { "log", DownloadCategory::Documents },

        // Programs
        { "exe", DownloadCategory::Programs },
        { "msi", DownloadCategory::Programs },
        { "msix", DownloadCategory::Programs },
        { "appx", DownloadCategory::Programs },
        { "appxbundle", DownloadCategory::Programs },
        { "dmg", DownloadCategory::Programs },
        { "pkg", DownloadCategory::Programs },
        { "app", DownloadCategory::Programs },
        { "deb", DownloadCategory::Programs },
        { "rpm", DownloadCategory::Programs },
        { "run", DownloadCategory::Programs },
        { "bin", DownloadCategory::Programs },
        { "sh", DownloadCategory::Programs },
        { "bash", DownloadCategory::Programs },
        { "command", DownloadCategory::Programs },
        { "ps1", DownloadCategory::Programs },
        { "bat", DownloadCategory::Programs },
        { "cmd", DownloadCategory::Programs },
        { "com", DownloadCategory::Programs },
        { "scr", DownloadCategory::Programs },
        { "wsf", DownloadCategory::Programs },

        // Disk Images
        { "iso", DownloadCategory::DiskImages },
        { "img", DownloadCategory::DiskImages },
        { "toast", DownloadCategory::DiskImages },
        { "nrg", DownloadCategory::DiskImages },
        { "cue", DownloadCategory::DiskImages },
        { "mdf", DownloadCategory::DiskImages },
        { "mds", DownloadCategory::DiskImages },
        { "vcd", DownloadCategory::DiskImages },
        { "vdi", DownloadCategory::DiskImages },
        { "vhd", DownloadCategory::DiskImages },
        { "vhdx", DownloadCategory::DiskImages },
        { "vmdk", DownloadCategory::DiskImages },
        { "qcow", DownloadCategory::DiskImages },
        { "qcow2", DownloadCategory::DiskImages },

        // Fonts
        { "ttf", DownloadCategory::Fonts },
        { "otf", DownloadCategory::Fonts },
        { "woff", DownloadCategory::Fonts },
        { "woff2", DownloadCategory::Fonts },
        { "eot", DownloadCategory::Fonts },
        { "ttc", DownloadCategory::Fonts },

        // Code
        { "c", DownloadCategory::Code },
        { "cc", DownloadCategory::Code },
        { "cpp", DownloadCategory::Code },
        { "cxx", DownloadCategory::Code },
        { "c++", DownloadCategory::Code },
        { "h", DownloadCategory::Code },
        { "hh", DownloadCategory::Code },
        { "hpp", DownloadCategory::Code },
        { "hxx", DownloadCategory::Code },
        { "ixx", DownloadCategory::Code },
        { "cppm", DownloadCategory::Code },
        { "mpp", DownloadCategory::Code },
        { "qml", DownloadCategory::Code },
        { "js", DownloadCategory::Code },
        { "mjs", DownloadCategory::Code },
        { "cjs", DownloadCategory::Code },
        { "ts", DownloadCategory::Code },
        { "tsx", DownloadCategory::Code },
        { "jsx", DownloadCategory::Code },
        { "json", DownloadCategory::Code },
        { "jsonc", DownloadCategory::Code },
        { "xml", DownloadCategory::Code },
        { "yml", DownloadCategory::Code },
        { "yaml", DownloadCategory::Code },
        { "toml", DownloadCategory::Code },
        { "ini", DownloadCategory::Code },
        { "cfg", DownloadCategory::Code },
        { "conf", DownloadCategory::Code },
        { "cmake", DownloadCategory::Code },
        { "gradle", DownloadCategory::Code },
        { "qrc", DownloadCategory::Code },
        { "ui", DownloadCategory::Code },
        { "java", DownloadCategory::Code },
        { "kt", DownloadCategory::Code },
        { "kts", DownloadCategory::Code },
        { "swift", DownloadCategory::Code },
        { "go", DownloadCategory::Code },
        { "rs", DownloadCategory::Code },
        { "py", DownloadCategory::Code },
        { "pyi", DownloadCategory::Code },
        { "php", DownloadCategory::Code },
        { "rb", DownloadCategory::Code },
        { "pl", DownloadCategory::Code },
        { "lua", DownloadCategory::Code },
        { "r", DownloadCategory::Code },
        { "sql", DownloadCategory::Code },
        { "html", DownloadCategory::Code },
        { "htm", DownloadCategory::Code },
        { "css", DownloadCategory::Code },
        { "scss", DownloadCategory::Code },
        { "sass", DownloadCategory::Code },
        { "less", DownloadCategory::Code },
        { "vue", DownloadCategory::Code },
        { "svelte", DownloadCategory::Code },
        { "dart", DownloadCategory::Code },
        { "zig", DownloadCategory::Code },

        // Torrents
        { "torrent", DownloadCategory::Torrents },

        // NFT / 3D / Assets
        { "glb", DownloadCategory::NFT },
        { "gltf", DownloadCategory::NFT },
        { "obj", DownloadCategory::NFT },
        { "fbx", DownloadCategory::NFT },
        { "usd", DownloadCategory::NFT },
        { "usda", DownloadCategory::NFT },
        { "usdc", DownloadCategory::NFT },
        { "usdz", DownloadCategory::NFT },
        { "blend", DownloadCategory::NFT },
        { "dae", DownloadCategory::NFT },
        { "stl", DownloadCategory::NFT },
        { "ply", DownloadCategory::NFT },
        { "vox", DownloadCategory::NFT }
    });
}

/*!
 * @brief Returns the extension lookup table.
 */
[[nodiscard]] const auto& extensionCategoryMap()
{
    static const auto map = buildExtensionCategoryMap();
    return map;
}

/*!
 * @brief Returns the exact MIME-to-category map.
 */
[[nodiscard]] const auto& mimeExactCategoryMap()
{
    static const QHash<QString, DownloadCategory> map = {
        { "application/pdf", DownloadCategory::Documents },
        { "application/rtf", DownloadCategory::Documents },
        { "application/msword", DownloadCategory::Documents },
        { "application/vnd.openxmlformats-officedocument.wordprocessingml.document", DownloadCategory::Documents },
        { "application/vnd.ms-excel", DownloadCategory::Documents },
        { "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", DownloadCategory::Documents },
        { "application/vnd.ms-powerpoint", DownloadCategory::Documents },
        { "application/vnd.openxmlformats-officedocument.presentationml.presentation", DownloadCategory::Documents },
        { "application/epub+zip", DownloadCategory::Documents },

        { "application/zip", DownloadCategory::Archives },
        { "application/x-7z-compressed", DownloadCategory::Archives },
        { "application/x-rar-compressed", DownloadCategory::Archives },
        { "application/x-tar", DownloadCategory::Archives },
        { "application/gzip", DownloadCategory::Archives },
        { "application/x-bzip2", DownloadCategory::Archives },
        { "application/x-xz", DownloadCategory::Archives },
        { "application/zstd", DownloadCategory::Archives },
        { "application/java-archive", DownloadCategory::Archives },
        { "application/vnd.android.package-archive", DownloadCategory::Archives },

        { "application/x-bittorrent", DownloadCategory::Torrents },

        { "application/x-iso9660-image", DownloadCategory::DiskImages },
        { "application/x-apple-diskimage", DownloadCategory::DiskImages },
        { "application/x-qemu-disk", DownloadCategory::DiskImages },

        { "application/x-msdownload", DownloadCategory::Programs },
        { "application/x-msi", DownloadCategory::Programs },
        { "application/vnd.microsoft.portable-executable", DownloadCategory::Programs },
        { "application/x-debian-package", DownloadCategory::Programs },
        { "application/x-rpm", DownloadCategory::Programs },
        { "application/x-sh", DownloadCategory::Programs },
        { "application/x-shellscript", DownloadCategory::Programs },

        { "application/json", DownloadCategory::Code },
        { "application/xml", DownloadCategory::Code },
        { "application/yaml", DownloadCategory::Code },
        { "application/x-yaml", DownloadCategory::Code },
        { "application/toml", DownloadCategory::Code },

        { "model/gltf+json", DownloadCategory::NFT },
        { "model/gltf-binary", DownloadCategory::NFT },
        { "model/obj", DownloadCategory::NFT },
        { "model/stl", DownloadCategory::NFT },
        { "model/vnd.usdz+zip", DownloadCategory::NFT }
    };
    return map;
}

/*!
 * @brief Returns prefix-based MIME mappings.
 */
[[nodiscard]] const auto& mimePrefixCategoryMap()
{
    static const QHash<QString, DownloadCategory> map = {
        { "video/", DownloadCategory::Video },
        { "audio/", DownloadCategory::Audio },
        { "image/", DownloadCategory::Images },
        { "font/", DownloadCategory::Fonts },
        { "text/", DownloadCategory::Documents }
    };
    return map;
}

/*!
 * @brief Resolves a MIME type from the supplied input when possible.
 */
[[nodiscard]] QString resolveMimeType(const QString& input)
{
    if (input.trimmed().isEmpty()) {
        return {};
    }

    QMimeDatabase database;

    const auto url = QUrl::fromUserInput(input);
    if (url.isValid() && url.isLocalFile()) {
        const auto mime = database.mimeTypeForFile(url.toLocalFile(), QMimeDatabase::MatchExtension);
        return mime.isValid() ? mime.name().toLower() : QString{};
    }

    const auto normalized = normalizeInput(input);
    if (normalized.isEmpty()) {
        return {};
    }

    const auto mime = database.mimeTypeForFile(normalized, QMimeDatabase::MatchExtension);
    return mime.isValid() ? mime.name().toLower() : QString{};
}

} // namespace

DownloadCategory detectCategory(const QString& filePath)
{
    const auto normalized = normalizeInput(filePath);
    const auto fileName = lowerFileName(normalized);

    if (const auto multipartCategory = detectMultipartArchiveCategory(fileName);
        multipartCategory != DownloadCategory::Other)
    {
        return multipartCategory;
    }

    if (const auto extension = extractExtension(normalized); !extension.isEmpty()) {
        const auto& map = extensionCategoryMap();
        if (const auto it = map.constFind(extension); it != map.cend()) {
            return it.value();
        }
    }

    if (const auto mimeType = resolveMimeType(filePath); !mimeType.isEmpty()) {
        return detectCategoryFromMime(mimeType);
    }

    return fallbackCategory();
}

DownloadCategory detectCategoryFromMime(const QString& mimeType)
{
    const auto normalized = mimeType.trimmed().toLower();
    if (normalized.isEmpty()) {
        return fallbackCategory();
    }

    const auto& exactMap = mimeExactCategoryMap();
    if (const auto it = exactMap.constFind(normalized); it != exactMap.cend()) {
        return it.value();
    }

    if (normalized == u"application/octet-stream") {
        return fallbackCategory();
    }

    const auto& prefixMap = mimePrefixCategoryMap();
    for (auto it = prefixMap.cbegin(); it != prefixMap.cend(); ++it) {
        if (normalized.startsWith(it.key())) {
            return it.value();
        }
    }

    if (normalized.contains(u"subtitle") ||
        normalized.contains(u"subrip") ||
        normalized.contains(u"webvtt"))
    {
        return DownloadCategory::Subtitles;
    }

    if (normalized.contains(u"javascript") ||
        normalized.contains(u"ecmascript") ||
        normalized.contains(u"python") ||
        normalized.contains(u"java") ||
        normalized.contains(u"c++") ||
        normalized.contains(u"xml") ||
        normalized.contains(u"json") ||
        normalized.contains(u"yaml") ||
        normalized.contains(u"toml"))
    {
        return DownloadCategory::Code;
    }

    return fallbackCategory();
}

QString toString(const DownloadCategory category)
{
    switch (category) {
    case DownloadCategory::Auto:
        return "Auto";
    case DownloadCategory::Video:
        return "Video";
    case DownloadCategory::Audio:
        return "Audio";
    case DownloadCategory::Images:
        return "Images";
    case DownloadCategory::Subtitles:
        return "Subtitles";
    case DownloadCategory::Archives:
        return "Archives";
    case DownloadCategory::Documents:
        return "Documents";
    case DownloadCategory::Programs:
        return "Programs";
    case DownloadCategory::DiskImages:
        return "Disk Images";
    case DownloadCategory::Fonts:
        return "Fonts";
    case DownloadCategory::Code:
        return "Code";
    case DownloadCategory::Torrents:
        return "Torrents";
    case DownloadCategory::NFT:
        return "NFT";
    case DownloadCategory::Other:
        return "Other";
    }

    return "Other";
}

DownloadCategory fromString(const QString& categoryName)
{
    const auto normalized = categoryName.trimmed().toLower();

    if (normalized == u"auto") {
        return DownloadCategory::Auto;
    }
    if (normalized == u"video") {
        return DownloadCategory::Video;
    }
    if (normalized == u"audio") {
        return DownloadCategory::Audio;
    }
    if (normalized == u"images") {
        return DownloadCategory::Images;
    }
    if (normalized == u"subtitles") {
        return DownloadCategory::Subtitles;
    }
    if (normalized == u"archives") {
        return DownloadCategory::Archives;
    }
    if (normalized == u"documents") {
        return DownloadCategory::Documents;
    }
    if (normalized == u"programs") {
        return DownloadCategory::Programs;
    }
    if (normalized == u"disk images") {
        return DownloadCategory::DiskImages;
    }
    if (normalized == u"fonts") {
        return DownloadCategory::Fonts;
    }
    if (normalized == u"code") {
        return DownloadCategory::Code;
    }
    if (normalized == u"torrents") {
        return DownloadCategory::Torrents;
    }
    if (normalized == u"nft") {
        return DownloadCategory::NFT;
    }

    return DownloadCategory::Other;
}

QString iconName(const DownloadCategory category)
{
    switch (category) {
    case DownloadCategory::Auto:
        return "category-auto";
    case DownloadCategory::Video:
        return "video";
    case DownloadCategory::Audio:
        return "audio";
    case DownloadCategory::Images:
        return "image";
    case DownloadCategory::Subtitles:
        return "subtitles";
    case DownloadCategory::Archives:
        return "archive";
    case DownloadCategory::Documents:
        return "document";
    case DownloadCategory::Programs:
        return "program";
    case DownloadCategory::DiskImages:
        return "disk-image";
    case DownloadCategory::Fonts:
        return "font";
    case DownloadCategory::Code:
        return "code";
    case DownloadCategory::Torrents:
        return "torrent";
    case DownloadCategory::NFT:
        return "nft";
    case DownloadCategory::Other:
        return "other";
    }

    return "other";
}

QString fileExtension(const QString& filePath)
{
    return extractExtension(normalizeInput(filePath));
}

QStringList categoryNames()
{
    QStringList names;
    names.reserve(categoryValues().size());

    for (const auto category : categoryValues()) {
        names.append(toString(category));
    }

    return names;
}

QList<DownloadCategory> categories()
{
    return categoryValues();
}

bool isKnownCategory(const QString& categoryName)
{
    return fromString(categoryName) != DownloadCategory::Other ||
           categoryName.trimmed().compare(u"Other", Qt::CaseInsensitive) == 0;
}

bool isKnownCategory(const DownloadCategory category)
{
    return categoryValues().contains(category);
}

} // namespace raad::utils
