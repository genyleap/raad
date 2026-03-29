module;

#include <QString>
#include <QStringList>
#include <QSet>

module raad.utils.category_utils;

namespace raad::utils {

// ---------- Internal helpers ----------

static QString extractExtension(QString input)
{
    input = input.toLower();

    // Remove query params (for URLs)
    const int queryIndex = input.indexOf('?');
    if (queryIndex >= 0)
        input = input.left(queryIndex);

    const int dot = input.lastIndexOf('.');
    if (dot < 0)
        return {};

    return input.mid(dot + 1);
}

// ---------- Category Sets ----------

static const QSet<QString> video = {
    "mp4","mkv","mov","avi","webm","flv","wmv","m4v","ts","mpeg","mpg"
};

static const QSet<QString> audio = {
    "mp3","wav","aac","flac","m4a","ogg","opus","wma"
};

static const QSet<QString> images = {
    "jpg","jpeg","png","gif","bmp","webp","svg","tiff","ico","heic","avif"
};

static const QSet<QString> subtitles = {
    "srt","ass","ssa","vtt","sub"
};

static const QSet<QString> archives = {
    "zip","rar","7z","tar","gz","bz2","xz","tgz","tbz2","lz","lzma"
};

static const QSet<QString> documents = {
    "pdf","doc","docx","xls","xlsx","ppt","pptx","txt","md","rtf","odt","ods","odp","csv"
};

static const QSet<QString> programs = {
    "exe","msi","apk","ipa","app","deb","rpm","pkg","bin","run"
};

static const QSet<QString> diskImages = {
    "iso","img","dmg","vhd","vhdx","qcow2"
};

static const QSet<QString> fonts = {
    "ttf","otf","woff","woff2"
};

static const QSet<QString> code = {
    "cppm", "cpp","hpp","h","c","cc","cxx","ixx",
    "js","ts","json","xml","html","css","ini","sql",
    "py","java","kt","sol","swift","go","rs","php","sh","bat"
};

static const QSet<QString> torrents = {
    "torrent"
};

static const QSet<QString> nft = {
    "glb","gltf","fbx","obj","usdz"
};

// ---------- Public API ----------

QString detectCategory(const QString& filePath)
{
    const QString ext = extractExtension(filePath);

    if (ext.isEmpty())
        return "Other";

    if (video.contains(ext))       return "Video";
    if (audio.contains(ext))       return "Audio";
    if (images.contains(ext))      return "Images";
    if (subtitles.contains(ext))   return "Subtitles";
    if (archives.contains(ext))    return "Archives";
    if (documents.contains(ext))   return "Documents";
    if (programs.contains(ext))    return "Programs";
    if (diskImages.contains(ext))  return "Disk Images";
    if (fonts.contains(ext))       return "Fonts";
    if (code.contains(ext))        return "Code";
    if (torrents.contains(ext))    return "Torrents";
    if (nft.contains(ext))         return "NFT";

    return "Other";
}

QStringList categoryNames()
{
    return {
        "Auto",
        "Video",
        "Audio",
        "Images",
        "Subtitles",
        "Archives",
        "Documents",
        "Programs",
        "Disk Images",
        "Fonts",
        "Code",
        "Torrents",
        "NFT",
        "Other"
    };
}

} // namespace raad::utils
