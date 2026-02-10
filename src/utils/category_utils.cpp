module;
#include <QString>
#include <QStringList>

module raad.utils.category_utils;

namespace raad::utils {

QString detectCategory(const QString& filePath)
{
    const QString lower = filePath.toLower();
    const int dot = lower.lastIndexOf('.');
    const QString ext = dot >= 0 ? lower.mid(dot + 1) : QString();

    const QStringList video = { "mp4", "mkv", "mov", "avi", "webm" };
    const QStringList audio = { "mp3", "wav", "aac", "flac", "m4a", "ogg" };
    const QStringList images = { "jpg", "jpeg", "png", "gif", "bmp", "webp" };
    const QStringList archives = { "zip", "rar", "7z", "tar", "gz", "bz2" };
    const QStringList documents = { "pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "txt", "md" };
    const QStringList programs = { "dmg", "exe", "msi", "pkg", "app" };

    if (video.contains(ext)) return "Video";
    if (audio.contains(ext)) return "Audio";
    if (images.contains(ext)) return "Images";
    if (archives.contains(ext)) return "Archives";
    if (documents.contains(ext)) return "Documents";
    if (programs.contains(ext)) return "Programs";
    return "Other";
}

QStringList categoryNames()
{
    return { "Auto", "Video", "Audio", "Images", "Archives", "Documents", "Programs", "Other" };
}

} // namespace raad::utils
