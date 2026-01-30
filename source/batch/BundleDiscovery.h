#ifndef BUNDLEDISCOVERY_H
#define BUNDLEDISCOVERY_H

/**
 * @file BundleDiscovery.h
 * @brief Utilities for discovering SpeedyNote bundles in directories.
 * 
 * Provides functions to:
 * - Find .snb bundle folders in a directory
 * - Detect bundles without .snb extension (via document.json)
 * - Find .snbx package files
 * - Expand CLI input paths to bundle lists
 * 
 * @see docs/private/BATCH_OPERATIONS.md for design documentation
 */

#include <QString>
#include <QStringList>

namespace BatchOps {

/**
 * @brief Options for bundle discovery.
 */
struct DiscoveryOptions {
    bool recursive = false;     ///< Search subdirectories
    bool detectAll = false;     ///< Find bundles without .snb extension
};

/**
 * @brief Check if a directory is a valid SpeedyNote bundle.
 * 
 * A valid bundle contains a document.json file.
 * 
 * @param path Path to directory to check
 * @return true if directory contains document.json
 */
bool isValidBundle(const QString& path);

/**
 * @brief Find SpeedyNote bundles in a directory.
 * 
 * Default behavior: finds folders ending in .snb that contain document.json
 * With detectAll: finds any folder containing document.json
 * 
 * @param directory Directory to search
 * @param options Discovery options
 * @return List of bundle paths (absolute), sorted alphabetically
 */
QStringList discoverBundles(const QString& directory,
                            const DiscoveryOptions& options = {});

/**
 * @brief Find SNBX packages in a directory.
 * 
 * @param directory Directory to search
 * @param recursive Search subdirectories
 * @return List of .snbx file paths (absolute), sorted alphabetically
 */
QStringList discoverPackages(const QString& directory,
                             bool recursive = false);

/**
 * @brief Expand input paths to bundle list.
 * 
 * Handles various input types:
 * - Direct bundle paths (folders with document.json) → included as-is
 * - Directories → discovers bundles inside using options
 * - Non-existent paths → skipped with warning
 * - Files (not directories) → skipped
 * 
 * Note: Glob patterns are expanded by the shell before reaching this function.
 * 
 * @param inputPaths List of input paths from CLI
 * @param options Discovery options (for directory inputs)
 * @return List of valid bundle paths (absolute), deduplicated and sorted
 */
QStringList expandInputPaths(const QStringList& inputPaths,
                             const DiscoveryOptions& options = {});

/**
 * @brief Expand input paths to SNBX package list.
 * 
 * Similar to expandInputPaths but for .snbx files:
 * - Direct .snbx file paths → included as-is
 * - Directories → discovers packages inside
 * - Non-existent paths → skipped with warning
 * 
 * @param inputPaths List of input paths from CLI
 * @param recursive Search subdirectories when input is a directory
 * @return List of valid .snbx file paths (absolute), deduplicated and sorted
 */
QStringList expandPackagePaths(const QStringList& inputPaths,
                               bool recursive = false);

} // namespace BatchOps

#endif // BUNDLEDISCOVERY_H
