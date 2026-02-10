/*!
 * @file        download_utils.cppm
 * @brief       Common utility helpers for download path, URL, and checksum handling.
 * @details     Provides a collection of small, reusable helper functions shared
 *              across download core components. These utilities handle common
 *              tasks such as path normalization, filename inference, checksum
 *              processing, and partial download inspection.
 *
 *              All helpers are designed to be side-effect free and safe for use
 *              in both core download logic and UI-related code.
 *
 * @author      <a href='https://github.com/thecompez'>Kambiz Asadzadeh</a>
 * @since       09 Feb 2026
 * @copyright   Copyright (c) 2026 Genyleap. All rights reserved.
 * @license     https://github.com/genyleap/raad/blob/main/LICENSE.md
 */

module;
#include <QUrl>
#include <QString>
#include <QtGlobal>

#ifndef Q_MOC_RUN
export module raad.utils.download_utils;
#endif

#ifdef Q_MOC_RUN
#define RAAD_MODULE_EXPORT
#else
#define RAAD_MODULE_EXPORT export
#endif

RAAD_MODULE_EXPORT namespace raad::utils {

/**
 * @brief Normalizes a local filesystem path or file URL.
 *
 * Converts file URLs to local paths and ensures a consistent representation
 * suitable for filesystem operations.
 *
 * @param path Local path or file:// URL.
 * @return Normalized local filesystem path.
 */
QString normalizeFilePath(const QString& path);

/**
 * @brief Calculates the total number of bytes received on disk.
 *
 * Sums the sizes of the main file and any associated partial segment files
 * (e.g. `.part` files) to determine total downloaded progress.
 *
 * @param filePath Target file path.
 * @param segments Number of download segments.
 * @return Total number of bytes currently present on disk.
 */
qint64 bytesReceivedOnDisk(const QString& filePath, int segments);

/**
 * @brief Decodes a URL query string value.
 *
 * Handles standard percent-decoding and converts '+' characters into spaces,
 * as commonly used in application/x-www-form-urlencoded data.
 *
 * @param value Encoded query value.
 * @return Decoded string.
 */
QString decodeQueryValue(const QString& value);

/**
 * @brief Extracts a filename from a Content-Disposition header value.
 *
 * Supports common Content-Disposition formats as defined by RFC standards.
 *
 * @param value Raw Content-Disposition header value.
 * @return Extracted filename, or an empty string if none could be determined.
 */
QString filenameFromDisposition(const QString& value);

/**
 * @brief Infers a filename from a URL.
 *
 * Attempts to derive a meaningful filename from the URL path or query
 * parameters when no explicit filename is provided by the server.
 *
 * @param url Source URL.
 * @return Inferred filename string.
 */
QString fileNameFromUrl(const QUrl& url);

/**
 * @brief Normalizes a host string.
 *
 * Converts the host to lowercase and removes any scheme, path, or port
 * components to ensure consistent host comparison.
 *
 * @param host Input host string.
 * @return Normalized host name.
 */
QString normalizeHost(const QString& host);

/**
 * @brief Checks whether a filename resembles a GUID/UUID.
 *
 * Useful for detecting server-generated or non-human-friendly filenames.
 *
 * @param name Filename to inspect.
 * @return true if the name matches a GUID-like pattern, false otherwise.
 */
bool looksLikeGuidName(const QString& name);

/**
 * @brief Normalizes a checksum string.
 *
 * Converts the checksum to lowercase and removes whitespace to ensure
 * consistent comparison.
 *
 * @param value Raw checksum string.
 * @return Normalized checksum string.
 */
QString normalizeChecksum(const QString& value);

/**
 * @brief Detects the checksum algorithm based on hash length.
 *
 * Commonly used to infer algorithms such as MD5, SHA-1, SHA-256, etc.
 *
 * @param expected Expected checksum value.
 * @return Name of the detected checksum algorithm, or an empty string if unknown.
 */
QString detectChecksumAlgo(const QString& expected);

/**
 * @brief Generates a unique file path if the given path already exists.
 *
 * Appends a numeric suffix to avoid overwriting existing files.
 *
 * @param path Desired file path.
 * @return A unique, non-existing file path.
 */
QString uniqueFilePath(const QString& path);

/**
 * @brief Checks whether a normalized path exists and refers to a regular file.
 *
 * @param path Normalized filesystem path.
 * @return true if the path exists and is a file, false otherwise.
 */
bool fileExistsPath(const QString& path);

} // namespace raad::utils
