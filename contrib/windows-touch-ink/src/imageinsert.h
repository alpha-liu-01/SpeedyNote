// SPDX-License-Identifier: GPL-3.0-or-later
// Image entry points for the installed SpeedyNote 1.6.3 object system.
#pragma once
#include <QApplication>
#include <QAbstractButton>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMetaEnum>
#include <QMetaMethod>
#include <QMimeData>
#include <QPointer>
#include <QSettings>
#include <QToolButton>
#include <QUrl>
#include <memory>

class ImageInsertController final : public QObject
{
public:
    ImageInsertController(QWidget *toolbar, QToolButton *button)
        : QObject(toolbar), m_toolbar(toolbar), m_button(button)
    {
        button->setCheckable(false);
        button->setText(QStringLiteral("插入图片"));
        button->setAccessibleName(QStringLiteral("插入图片"));
        button->setToolTip(QStringLiteral("选择图片加入当前笔记；也可将图片拖到页面。\n右侧箭头可粘贴截图或切换到图片调整工具。"));
        button->setPopupMode(QToolButton::MenuButtonPopup);
        auto *menu = new QMenu(button);
        m_paste = menu->addAction(QStringLiteral("粘贴图片（Ctrl+V）"));
        auto *adjust = menu->addAction(QStringLiteral("调整图片位置和大小"));
        button->setMenu(menu);
        connect(button, &QToolButton::clicked, this, [this] { chooseFile(); });
        connect(m_paste, &QAction::triggered, this, [this] { pasteClipboard(); });
        connect(adjust, &QAction::triggered, this, [this] {
            if (auto *canvas = currentCanvas()) activateImageTool(canvas);
        });
        connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this] {
            m_paste->setEnabled(hasImage(QApplication::clipboard()->mimeData()));
        });
        m_paste->setEnabled(hasImage(QApplication::clipboard()->mimeData()));
        toolbar->setProperty("touchInkImageController", QVariant::fromValue<QObject *>(this));
        qApp->installEventFilter(this);
    }

    bool importFile(const QString &path, QWidget *canvas) { return insertFile(path, canvas); }

protected:
    bool eventFilter(QObject *receiver, QEvent *event) override
    {
        auto *canvas = qobject_cast<QWidget *>(receiver);
        if (!canvas || !canvas->inherits("DocumentViewport") ||
            canvas->window() != m_toolbar->window()) return false;
        if (event->type() == QEvent::FocusIn || event->type() == QEvent::MouseButtonPress ||
            event->type() == QEvent::TouchBegin || event->type() == QEvent::TabletPress)
            m_canvas = canvas;
        if (m_busy) return false;
        if ((event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) &&
            static_cast<QKeyEvent *>(event)->matches(QKeySequence::Paste) &&
            hasImage(QApplication::clipboard()->mimeData())) {
            // Only the canvas owns this shortcut. Text editors retain ordinary paste.
            m_canvas = canvas;
            event->accept();
            if (event->type() == QEvent::KeyPress) pasteClipboard();
            return true;
        }
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            auto *drag = static_cast<QDragMoveEvent *>(event);
            if (!firstImageFile(drag->mimeData()).isEmpty()) {
                drag->setDropAction(Qt::CopyAction);
                drag->accept();
                return true;
            }
        }
        if (event->type() == QEvent::Drop) {
            auto *drop = static_cast<QDropEvent *>(event);
            const QString path = firstImageFile(drop->mimeData());
            if (!path.isEmpty()) {
                m_canvas = canvas;
                drop->setDropAction(Qt::CopyAction);
                drop->accept();
                insertFile(path, canvas);
                return true;
            }
        }
        return false;
    }

private:
    QPointer<QWidget> m_toolbar, m_canvas;
    QPointer<QToolButton> m_button;
    QAction *m_paste = nullptr;
    bool m_busy = false;

    static QString firstImageFile(const QMimeData *mime)
    {
        if (!mime) return {};
        for (const QUrl &url : mime->urls()) {
            if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile() &&
                QImageReader(url.toLocalFile()).canRead()) return url.toLocalFile();
        }
        return {};
    }

    static bool hasImage(const QMimeData *mime)
    {
        return mime && (mime->hasImage() || !firstImageFile(mime).isEmpty());
    }

    QWidget *currentCanvas()
    {
        // NoFocus on the toolbar preserves the focused pane in split view.
        for (QWidget *focus = QApplication::focusWidget(); focus; focus = focus->parentWidget()) {
            if (focus->inherits("DocumentViewport") && focus->window() == m_toolbar->window()) {
                m_canvas = focus;
                return focus;
            }
        }
        if (m_canvas && m_canvas->isVisible()) return m_canvas;
        for (auto *widget : m_toolbar->window()->findChildren<QWidget *>()) {
            if (widget->inherits("DocumentViewport") && widget->isVisible()) {
                m_canvas = widget;
                return widget;
            }
        }
        return nullptr;
    }

    QWidget *objectBar() const
    {
        for (auto *widget : m_toolbar->window()->findChildren<QWidget *>())
            if (widget->inherits("ObjectSelectActionBar")) return widget;
        return nullptr;
    }

    static bool selectMode(QWidget *canvas, QWidget *bar)
    {
        const auto *meta = canvas->metaObject();
        const int enumIndex = meta->indexOfEnumerator("ObjectActionMode");
        if (enumIndex < 0) return false;
        bool ok = false;
        const int value = meta->enumerator(enumIndex).keyToValue("Select", &ok);
        if (!ok) return false;
        const auto *barMeta = bar->metaObject();
        for (int i = barMeta->methodOffset(); i < barMeta->methodCount(); ++i) {
            const QMetaMethod method = barMeta->method(i);
            if (method.name() == "actionModeChanged" && method.parameterCount() == 1) {
                const QByteArray type = method.parameterTypes().first();
                return method.invoke(bar, Qt::DirectConnection, QGenericArgument(type.constData(), &value));
            }
        }
        return false;
    }

    bool activateImageTool(QWidget *canvas)
    {
        QWidget *bar = objectBar();
        // v1.6.3 direct ToolButtons are Lasso, Image, Link, Text, Pan. Using
        // the real button keeps the native tool, subtype and pagination in sync
        // without linking private application layouts or relying on translations.
        QList<QAbstractButton *> tools;
        for (auto *button : m_toolbar->findChildren<QAbstractButton *>(QString(), Qt::FindDirectChildrenOnly))
            if (button->inherits("ToolButton")) tools.append(button);
        if (!bar || tools.size() < 2) {
            error(QStringLiteral("当前程序版本的图片工具不可用。请使用配套的 SpeedyNote 电容笔版。"));
            return false;
        }
        canvas->setFocus(Qt::OtherFocusReason);
        tools.at(1)->click();
        return selectMode(canvas, bar);
    }

    bool nativePaste(QWidget *canvas)
    {
        if (!activateImageTool(canvas)) return false;
        // The real action bar is connected by MainWindow to its active viewport.
        // This creates native ImageObjects, undo entries and embedded assets.
        return QMetaObject::invokeMethod(objectBar(), "pasteRequested", Qt::DirectConnection);
    }

    void error(const QString &message)
    {
        QMessageBox::warning(m_toolbar->window(), QStringLiteral("插入图片"), message);
    }

    void chooseFile()
    {
        if (m_busy) return;
        QPointer<QWidget> canvas = currentCanvas();
        if (!canvas) return;
        m_busy = true;
        QSettings settings(QStringLiteral("SpeedyNote"), QStringLiteral("App"));
        QStringList patterns;
        for (const auto &format : QImageReader::supportedImageFormats())
            patterns.append(QStringLiteral("*.") + QString::fromLatin1(format));
        const QString path = QFileDialog::getOpenFileName(m_toolbar->window(),
            QStringLiteral("插入图片"), settings.value(QStringLiteral("touchInk/imageDirectory")).toString(),
            QStringLiteral("图片 (%1);;所有文件 (*)").arg(patterns.join(QLatin1Char(' '))));
        m_busy = false;
        if (path.isEmpty() || !canvas) return;
        if (insertFile(path, canvas))
            settings.setValue(QStringLiteral("touchInk/imageDirectory"), QFileInfo(path).absolutePath());
    }

    static std::unique_ptr<QMimeData> copyMime(const QMimeData *source)
    {
        auto copy = std::make_unique<QMimeData>();
        if (!source) return copy;
        // Materialize delayed Windows clipboard formats before replacing the owner.
        for (const QString &format : source->formats()) copy->setData(format, source->data(format));
        if (source->hasImage()) copy->setImageData(source->imageData());
        if (source->hasUrls()) copy->setUrls(source->urls());
        return copy;
    }

    bool insertFile(const QString &path, QWidget *canvas)
    {
        if (m_busy) return false;
        if (QFileInfo(path).size() > 64LL * 1024 * 1024) {
            error(QStringLiteral("图片文件超过 64 MB，请先缩小图片后再插入。"));
            return false;
        }
        QImageReader reader(path);
        reader.setAutoTransform(true);
        if (reader.read().isNull()) {
            error(QStringLiteral("无法读取这张图片：%1\n%2").arg(QFileInfo(path).fileName(), reader.errorString()));
            return false;
        }
        m_busy = true;
        QClipboard *clipboard = QApplication::clipboard();
        auto previous = copyMime(clipboard->mimeData());
        auto *fileMime = new QMimeData;
        fileMime->setUrls({QUrl::fromLocalFile(path)});
        clipboard->setMimeData(fileMime);
        const bool invoked = nativePaste(canvas);
        // Do not replace a new clipboard item supplied by another application.
        if (clipboard->mimeData() == fileMime) clipboard->setMimeData(previous.release());
        m_busy = false;
        return invoked;
    }

    void pasteClipboard()
    {
        if (m_busy) return;
        QWidget *canvas = currentCanvas();
        if (!canvas || !hasImage(QApplication::clipboard()->mimeData())) return;
        m_busy = true;
        nativePaste(canvas);
        m_busy = false;
    }
};
