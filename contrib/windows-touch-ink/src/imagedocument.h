// SPDX-License-Identifier: GPL-3.0-or-later
// Build a native .snb image page using the application's fixed page-background
// path. The source bitmap fills an internal lossless one-page PDF; no movable
// ImageObject is created. The background is carried inside the notebook bundle.
#pragma once
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPageSize>
#include <QPdfWriter>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace ImageDocument {
inline bool ensureDirectory(const QString &path)
{
    if (QFileInfo(path).isDir()) return true;
    const QString parent = QFileInfo(path).absolutePath();
    if (parent == path || !ensureDirectory(parent)) return false;
    const QString native = QDir::toNativeSeparators(path);
    return CreateDirectoryW(reinterpret_cast<LPCWSTR>(native.utf16()), nullptr) ||
        (GetLastError() == ERROR_ALREADY_EXISTS && QFileInfo(path).isDir());
}

inline bool writeFile(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

inline QString create(const QString &sourcePath, const QImage &image, QString &error)
{
    if (image.isNull()) { error = QStringLiteral("图片为空。"); return {}; }
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        QStringLiteral("/image-notebooks");
    if (!ensureDirectory(base)) { error = QStringLiteral("无法创建图片批注存储目录。"); return {}; }
    QTemporaryDir staging(base + QStringLiteral("/.import-XXXXXX"));
    if (!staging.isValid() || !ensureDirectory(staging.path() + "/pages")) {
        error = QStringLiteral("无法创建图片批注页面。"); return {};
    }
    const auto uuid = [] { return QUuid::createUuid().toString(QUuid::WithoutBraces); };
    const QString notebookId = uuid(), pageId = uuid();
    const QString bundle = base + "/" + notebookId + ".snb";
    QSaveFile pdf(staging.path() + "/image.pdf");
    if (!pdf.open(QIODevice::WriteOnly)) {
        error = QStringLiteral("无法保存图片底图。"); return {};
    }
    {
        QPdfWriter writer(&pdf);
        writer.setTitle(QFileInfo(sourcePath).completeBaseName());
        writer.setCreator(QStringLiteral("SpeedyNote 电容笔版"));
        writer.setResolution(96);
        // SpeedyNote displays PDF points at 96 logical pixels per inch.
        writer.setPageSize(QPageSize(QSizeF(image.width() * 0.75, image.height() * 0.75),
            QPageSize::Point, QStringLiteral("Image"), QPageSize::ExactMatch));
        writer.setPageMargins(QMarginsF(), QPageLayout::Point);
        QPainter painter(&writer);
        if (!painter.isActive()) {
            error = QStringLiteral("无法生成图片批注底图。"); return {};
        }
        painter.setRenderHint(QPainter::LosslessImageRendering);
        painter.fillRect(QRect(0, 0, writer.width(), writer.height()), Qt::white);
        painter.drawImage(QRect(0, 0, writer.width(), writer.height()), image);
        if (!painter.end()) {
            error = QStringLiteral("图片批注底图写入失败。"); return {};
        }
    }
    if (!pdf.commit()) { error = QStringLiteral("保存图片底图失败。"); return {}; }

    QJsonObject background;
    background.insert("type", "none");
    background.insert("color", "#ffffffff");
    background.insert("page_width", image.width());
    background.insert("page_height", image.height());
    QJsonObject size;
    size.insert("width", image.width());
    size.insert("height", image.height());
    // Native save prunes PDF sources from this lazy page index, even when
    // the page JSON has already been loaded and rendered successfully.
    size.insert("pdf_page", 0);
    QJsonObject pageMetadata;
    pageMetadata.insert(pageId, size);
    QJsonArray pageOrder;
    pageOrder.append(pageId);
    QJsonObject manifest;
    manifest.insert("bundle_format_version", 3);
    manifest.insert("notebook_id", notebookId);
    manifest.insert("name", QFileInfo(sourcePath).completeBaseName());
    manifest.insert("author", "");
    manifest.insert("mode", "paged");
    manifest.insert("page_count", 1);
    manifest.insert("page_order", pageOrder);
    manifest.insert("page_metadata", pageMetadata);
    manifest.insert("default_background", background);
    manifest.insert("pdf_path", bundle + "/image.pdf");
    manifest.insert("pdf_relative_path", "image.pdf");
    manifest.insert("last_accessed_page", 0);
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    manifest.insert("created", now);
    manifest.insert("last_modified", now);

    QJsonObject inkLayer;
    inkLayer.insert("id", uuid());
    inkLayer.insert("name", QStringLiteral("批注"));
    inkLayer.insert("visible", true);
    inkLayer.insert("locked", false);
    inkLayer.insert("opacity", 1);
    inkLayer.insert("strokes", QJsonArray());
    QJsonArray layers;
    layers.append(inkLayer);
    QJsonObject page;
    page.insert("uuid", pageId);
    page.insert("pageIndex", 0);
    page.insert("width", image.width());
    page.insert("height", image.height());
    page.insert("backgroundType", 1);
    page.insert("backgroundColor", "#ffffffff");
    page.insert("pdfPageNumber", 0);
    page.insert("activeLayerIndex", 0);
    page.insert("isBookmarked", false);
    page.insert("layers", layers);
    page.insert("objects", QJsonArray());

    if (!writeFile(staging.path() + "/pages/" + pageId + ".json", QJsonDocument(page).toJson(QJsonDocument::Compact)) ||
        !writeFile(staging.path() + "/document.json", QJsonDocument(manifest).toJson())) {
        error = QStringLiteral("保存图片批注页面失败，请检查可用磁盘空间。"); return {};
    }
    if (!QDir().rename(staging.path(), bundle)) {
        error = QStringLiteral("无法完成图片批注页面的保存。"); return {};
    }
    staging.setAutoRemove(false);
    return bundle;
}
} // namespace ImageDocument
