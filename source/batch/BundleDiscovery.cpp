#include "BundleDiscovery.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>
#include <QDebug>

/**
 * @file BundleDiscovery.cpp
 * @brief Implementation of bundle discovery utilities.
 * 
 * @see BundleDiscovery.h for API documentation
 */

namespace BatchOps {

// ============================================================================
// Bundle Validation
// ============================================================================

bool isValidBundle(const QString& path)
{
    if (path.isEmpty()) {
        return false;
    }
    
    QFileInfo info(path);
    if (!info.exists() || !info.isDir()) {
        return false;
    }
    
    // A valid SpeedyNote bundle contains document.json
    QFileInfo manifestInfo(path + "/document.json");
    return manifestInfo.exists() && manifestInfo.isFile();
}

// ============================================================================
// Bundle Discovery
// ============================================================================

QStringList discoverBundles(const QString& directory, const DiscoveryOptions& options)
{
    QStringList results;
    
    QDir dir(directory);
    if (!dir.exists()) {
        qWarning() << "[BundleDiscovery] Directory does not exist:" << directory;
        return results;
    }
    
    QString absPath = dir.absolutePath();
    
    if (options.recursive) {
        // Recursive search using QDirIterator
        QDirIterator it(absPath, QDir::Dirs | QDir::NoDotAndDotDot, 
                        QDirIterator::Subdirectories);
        
        while (it.hasNext()) {
            QString subdir = it.next();
            
            if (options.detectAll) {
                // Deep scan: check every directory for document.json
                if (isValidBundle(subdir)) {
                    results.append(subdir);
                }
            } else {
                // Normal mode: only check .snb directories
                if (subdir.endsWith(".snb", Qt::CaseInsensitive)) {
                    if (isValidBundle(subdir)) {
                        results.append(subdir);
                    }
                }
            }
        }
    } else {
        // Non-recursive: only check immediate children
        QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        
        for (const QString& subdir : subdirs) {
            QString fullPath = absPath + "/" + subdir;
            
            if (options.detectAll) {
                // Deep scan: check every directory for document.json
                if (isValidBundle(fullPath)) {
                    results.append(fullPath);
                }
            } else {
                // Normal mode: only check .snb directories
                if (subdir.endsWith(".snb", Qt::CaseInsensitive)) {
                    if (isValidBundle(fullPath)) {
                        results.append(fullPath);
                    }
                }
            }
        }
    }
    
    // Sort results alphabetically for consistent ordering
    results.sort(Qt::CaseInsensitive);
    
    return results;
}

// ============================================================================
// Package Discovery
// ============================================================================

QStringList discoverPackages(const QString& directory, bool recursive)
{
    QStringList results;
    
    QDir dir(directory);
    if (!dir.exists()) {
        qWarning() << "[BundleDiscovery] Directory does not exist:" << directory;
        return results;
    }
    
    QString absPath = dir.absolutePath();
    
    if (recursive) {
        // Recursive search
        QDirIterator it(absPath, QStringList() << "*.snbx", 
                        QDir::Files, QDirIterator::Subdirectories);
        
        while (it.hasNext()) {
            results.append(it.next());
        }
    } else {
        // Non-recursive: only check immediate directory
        QStringList filters;
        filters << "*.snbx";
        
        QStringList files = dir.entryList(filters, QDir::Files);
        for (const QString& file : files) {
            results.append(absPath + "/" + file);
        }
    }
    
    // Sort results alphabetically
    results.sort(Qt::CaseInsensitive);
    
    return results;
}

// ============================================================================
// Path Expansion
// ============================================================================

QStringList expandInputPaths(const QStringList& inputPaths, const DiscoveryOptions& options)
{
    QSet<QString> seen;  // For deduplication
    QStringList results;
    
    for (const QString& inputPath : inputPaths) {
        QFileInfo info(inputPath);
        
        if (!info.exists()) {
            qWarning() << "[BundleDiscovery] Path does not exist:" << inputPath;
            continue;
        }
        
        QString absPath = info.absoluteFilePath();
        
        if (info.isDir()) {
            // Check if this directory is itself a bundle
            if (isValidBundle(absPath)) {
                // Direct bundle path
                if (!seen.contains(absPath)) {
                    seen.insert(absPath);
                    results.append(absPath);
                }
            } else {
                // Directory containing bundles - discover them
                QStringList bundles = discoverBundles(absPath, options);
                for (const QString& bundle : bundles) {
                    if (!seen.contains(bundle)) {
                        seen.insert(bundle);
                        results.append(bundle);
                    }
                }
            }
        } else {
            // Not a directory - skip (bundles are directories)
            qWarning() << "[BundleDiscovery] Not a directory, skipping:" << inputPath;
        }
    }
    
    // Sort results alphabetically
    results.sort(Qt::CaseInsensitive);
    
    return results;
}

QStringList expandPackagePaths(const QStringList& inputPaths, bool recursive)
{
    QSet<QString> seen;  // For deduplication
    QStringList results;
    
    for (const QString& inputPath : inputPaths) {
        QFileInfo info(inputPath);
        
        if (!info.exists()) {
            qWarning() << "[BundleDiscovery] Path does not exist:" << inputPath;
            continue;
        }
        
        QString absPath = info.absoluteFilePath();
        
        if (info.isDir()) {
            // Directory containing packages - discover them
            QStringList packages = discoverPackages(absPath, recursive);
            for (const QString& pkg : packages) {
                if (!seen.contains(pkg)) {
                    seen.insert(pkg);
                    results.append(pkg);
                }
            }
        } else if (info.isFile()) {
            // Check if it's a .snbx file
            if (absPath.endsWith(".snbx", Qt::CaseInsensitive)) {
                if (!seen.contains(absPath)) {
                    seen.insert(absPath);
                    results.append(absPath);
                }
            } else {
                qWarning() << "[BundleDiscovery] Not an SNBX file, skipping:" << inputPath;
            }
        }
    }
    
    // Sort results alphabetically
    results.sort(Qt::CaseInsensitive);
    
    return results;
}

} // namespace BatchOps
