/*!
 * @file        category_utils.cppm
 * @brief       Download category detection and classification helpers.
 * @details     Provides strongly typed category detection utilities for
 *              RAAD Download Manager.
 *
 *              This module supports classification using:
 *              - file paths
 *              - file names
 *              - URLs
 *              - MIME types
 *              - multi-part archive naming patterns
 *
 *              It centralizes category handling for queue views, history,
 *              filters, sorting, grouping, icon binding, and post-download
 *              workflows.
 *
 * @author      <a href="https://github.com/thecompez">Kambiz Asadzadeh</a>
 * @since       09 Feb 2026
 * @copyright   Copyright (c) 2026 Genyleap. All rights reserved.
 * @license     https://github.com/genyleap/raad/blob/main/LICENSE.md
 */

module;

#include <QList>
#include <QString>
#include <QStringList>

#ifndef Q_MOC_RUN
export module raad.utils.category_utils;
#endif

#ifdef Q_MOC_RUN
#define RAAD_MODULE_EXPORT
#else
#define RAAD_MODULE_EXPORT export
#endif

RAAD_MODULE_EXPORT namespace raad::utils {

/*!
 * @brief Strongly typed logical download categories.
 */
enum class DownloadCategory : unsigned char {
    Auto = 0,
    Video,
    Audio,
    Images,
    Subtitles,
    Archives,
    Documents,
    Programs,
    DiskImages,
    Fonts,
    Code,
    Torrents,
    NFT,
    Other
};

/*!
 * @brief Detects the logical download category for a given file path or URL.
 *
 * @param filePath Full file name, local path, or URL.
 * @return Strongly typed detected category.
 */
[[nodiscard]] DownloadCategory detectCategory(const QString& filePath);

/*!
 * @brief Detects the logical download category from a MIME type.
 *
 * @param mimeType MIME type string such as "video/mp4".
 * @return Strongly typed detected category.
 */
[[nodiscard]] DownloadCategory detectCategoryFromMime(const QString& mimeType);

/*!
 * @brief Converts a category enum to its user-facing label.
 *
 * @param category Category enum.
 * @return Stable display label.
 */
[[nodiscard]] QString toString(DownloadCategory category);

/*!
 * @brief Converts a user-facing category label to enum form.
 * @details Matching is case-insensitive. Unknown labels resolve to
 *          DownloadCategory::Other.
 *
 * @param categoryName Category display label.
 * @return Strongly typed category enum.
 */
[[nodiscard]] DownloadCategory fromString(const QString& categoryName);

/*!
 * @brief Returns an icon name suitable for QML or UI binding.
 *
 * @param category Category enum.
 * @return Stable icon key.
 */
[[nodiscard]] QString iconName(DownloadCategory category);

/*!
 * @brief Returns the normalized lowercase file extension without leading dot.
 *
 * @param filePath Full file name, local path, or URL.
 * @return Lowercase file extension, or empty string when unavailable.
 */
[[nodiscard]] QString fileExtension(const QString& filePath);

/*!
 * @brief Returns all supported category labels in stable UI order.
 *
 * @return Ordered category name list.
 */
[[nodiscard]] QStringList categoryNames();

/*!
 * @brief Returns all supported category enum values in stable UI order.
 *
 * @return Ordered list of category enums.
 */
[[nodiscard]] QList<DownloadCategory> categories();

/*!
 * @brief Returns whether the given category label is supported.
 *
 * @param categoryName Category display label.
 * @return True when the category label is recognized.
 */
[[nodiscard]] bool isKnownCategory(const QString& categoryName);

/*!
 * @brief Returns whether the given category enum is supported.
 *
 * @param category Category enum.
 * @return True when the enum value is valid.
 */
[[nodiscard]]
bool isKnownCategory(DownloadCategory category);

} // namespace raad::utils
