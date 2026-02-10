/*!
 * @file        category_utils.cppm
 * @brief       Download category detection and classification helpers.
 * @details     Provides utility functions for mapping filenames or file paths
 *              to logical download categories based on file extensions.
 *              Also exposes the complete list of supported category labels
 *              used by the download manager.
 *
 *              These helpers are intended to centralize category logic and
 *              ensure consistent classification across the UI, queue manager,
 *              and post-download processing stages.
 *
 * @author      <a href='https://github.com/thecompez'>Kambiz Asadzadeh</a>
 * @since       09 Feb 2026
 * @copyright   Copyright (c) 2026 Genyleap. All rights reserved.
 * @license     https://github.com/genyleap/raad/blob/main/LICENSE.md
 */

module;
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

/**
 * @brief Detects the logical download category for a given file path.
 *
 * The category is inferred primarily from the file extension. If no matching
 * category is found, a default or "Other" category may be returned depending
 * on implementation.
 *
 * @param filePath Full file name or path.
 * @return A category label corresponding to the detected file type.
 */
QString detectCategory(const QString& filePath);

/**
 * @brief Returns the list of all supported download category names.
 *
 * This list is suitable for populating UI elements such as filters,
 * group views, or configuration dialogs.
 *
 * @return List of category name strings.
 */
QStringList categoryNames();

} // namespace raad::utils
