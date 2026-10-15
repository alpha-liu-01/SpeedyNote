// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in integration probe; run ONLY in the isolated image test runtime.
#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFile>
#include <QGenericPlugin>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMetaMethod>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QToolButton>
#include <QThread>
#include <QWidget>
#include <windows.h>

class ImageProbe final : public QObject
{
    Q_OBJECT
    QTimer m_timer;
    QPointer<QWidget> m_canvas, m_toolbar;
    QString m_output, m_sample, m_bundle, m_saveDir;
    QJsonObject m_report;
    int m_steps = 0, m_ready = 0, m_modified = 0, m_launcherStage = 0;
    bool m_created = false;
    QColor m_color{230, 70, 170};
    QMimeData *m_originalClipboard = nullptr;
public:
    ImageProbe() {
        qApp->setAttribute(Qt::AA_DontUseNativeDialogs);
        m_output = qEnvironmentVariable("SPEEDYNOTE_IMAGE_PROBE_OUTPUT");
        m_sample = m_output + "-sample.png";
        m_saveDir = m_output + "-save-" + QString::number(QDateTime::currentMSecsSinceEpoch());
        m_bundle = m_saveDir + "/image-notebook.snb";
        connect(&m_timer, &QTimer::timeout, this, &ImageProbe::run);
        m_timer.start(250);
    }
private slots:
    void modified() { ++m_modified; }
    void run() {
        if (++m_steps > 80) return finish(false, "Timed out");
        for (auto *widget : QApplication::allWidgets()) {
            if (widget->inherits("DocumentViewport") && widget->isVisible()) m_canvas = widget;
            if (widget->inherits("Toolbar") && widget->isVisible()) m_toolbar = widget;
        }
        if (!m_canvas || !m_toolbar) {
            if (!m_created) for (auto *widget : QApplication::topLevelWidgets()) {
                if (widget->inherits("Launcher")) {
                    const char *method = qEnvironmentVariableIsSet("SPEEDYNOTE_IMAGE_EDGELESS")
                        ? "createNewEdgeless" : "createNewPaged";
                    if (qEnvironmentVariableIsSet("SPEEDYNOTE_IMAGE_PAGE") &&
                        !qEnvironmentVariableIsSet("SPEEDYNOTE_IMAGE_REOPEN")) {
                        auto *importButton = widget->findChild<QPushButton *>("touchInkLauncherImportImage");
                        if (!importButton) return;
                        auto *fab = importButton->parentWidget();
                        QPushButton *mainButton = nullptr;
                        for (auto *candidate : fab->findChildren<QPushButton *>(QString(), Qt::FindDirectChildrenOnly))
                            if (candidate->width() == 56) mainButton = candidate;
                        if (!mainButton) return finish(false, "Launcher plus missing");
                        if (m_launcherStage == 0) {
                            m_report.insert("launcherImageHiddenBeforePlus", !importButton->isVisible());
                            mainButton->click();
                            m_launcherStage = 1;
                            return;
                        }
                        if (m_launcherStage == 1) {
                            m_report.insert("launcherImageInPlusMenu", importButton->isVisible());
                            widget->grab().save(m_output + "-launcher-menu.png");
                            chooseInNextDialog({});
                            importButton->click();
                            m_report.insert("launcherCancelKeptHome", widget->isVisible());
                            mainButton->click();
                            m_launcherStage = 2;
                            return;
                        }
                        QImage sample(240, 140, QImage::Format_ARGB32);
                        sample.fill(m_color);
                        sample.save(m_sample);
                        widget->setProperty("imageProbeSourceHash", fileHash(m_sample));
                        chooseInNextDialog(m_sample);
                        importButton->click();
                        m_created = true;
                        m_report.insert("launcherImportRequested", true);
                        return;
                    }
                    if (qEnvironmentVariableIsSet("SPEEDYNOTE_IMAGE_REOPEN")) return;
                    m_created = QMetaObject::invokeMethod(widget, method, Qt::DirectConnection);
                    break;
                }
            }
            return;
        }
        auto *button = m_toolbar->findChild<QToolButton *>("touchInkInsertImage");
        if (!button || ++m_ready < 3) return;
        m_timer.stop();
        trace("notebook ready");
        connect(m_canvas, SIGNAL(documentModified()), this, SLOT(modified()));
        m_canvas->window()->resize(1200, 900);
        m_canvas->setFocus();
        QApplication::processEvents();
        if (qEnvironmentVariableIsSet("SPEEDYNOTE_IMAGE_PAGE")) return testImagePage();
        if (qEnvironmentVariableIsSet("SPEEDYNOTE_IMAGE_REOPEN")) {
            m_report.insert("reopenedImageVisible", !colorBounds().isEmpty());
            m_canvas->grab().save(m_output + "-reopened.png");
            return finish(!colorBounds().isEmpty(), {});
        }
        m_originalClipboard = new QMimeData;
        trace("snapshot clipboard");
        const QMimeData *oldClipboard = QApplication::clipboard()->mimeData();
        if (oldClipboard) {
            for (const QString &format : oldClipboard->formats())
                m_originalClipboard->setData(format, oldClipboard->data(format));
            if (oldClipboard->hasImage()) m_originalClipboard->setImageData(oldClipboard->imageData());
            if (oldClipboard->hasUrls()) m_originalClipboard->setUrls(oldClipboard->urls());
        }
        trace("clipboard snapshot complete");
        QJsonArray controls;
        for (auto *tool : m_toolbar->findChildren<QAbstractButton *>(QString(), Qt::FindDirectChildrenOnly)) {
            QJsonObject control;
            control.insert("class", tool->metaObject()->className());
            control.insert("tip", tool->toolTip());
            controls.append(control);
        }
        m_report.insert("nativeControls", controls);
        m_report.insert("imageButtonVisible", button->isVisible());
        QImage sample(240, 140, QImage::Format_ARGB32);
        sample.fill(m_color);
        sample.save(m_sample);
        auto *clipboard = QApplication::clipboard();
        auto *original = new QMimeData;
        original->setText("SpeedyNote image clipboard preservation check");
        original->setData("application/x-speedynote-image-test", "original-format-data");
        clipboard->setMimeData(original);
        const int before = m_modified;
        chooseInNextDialog(m_sample);
        trace("choose image");
        button->click();
        trace("image dialog closed");
        QApplication::processEvents();
        m_report.insert("fileInserted", m_modified > before && !colorBounds().isEmpty());
        m_report.insert("clipboardRestored", clipboard->text() == "SpeedyNote image clipboard preservation check" &&
            clipboard->mimeData()->data("application/x-speedynote-image-test") == "original-format-data");
        if (colorBounds().isEmpty()) return finish(false, "File insertion produced no image");
        m_toolbar->grab().save(m_output + "-toolbar.png");
        m_canvas->grab().save(m_output + "-inserted.png");
        QMetaObject::invokeMethod(m_toolbar, "undoClicked", Qt::DirectConnection);
        QApplication::processEvents();
        m_report.insert("undoRemovedImage", colorBounds().isEmpty());
        QMetaObject::invokeMethod(m_toolbar, "redoClicked", Qt::DirectConnection);
        QApplication::processEvents();
        m_report.insert("redoRestoredImage", !colorBounds().isEmpty());
        QRect initial = colorBounds();
        trace("move and resize");
        drag(initial.center(), initial.center() + QPoint(70, 45));
        QRect moved = colorBounds();
        m_report.insert("imageMoved", moved.center().x() - initial.center().x() > 55 &&
            moved.center().y() - initial.center().y() > 30);
        drag(moved.bottomRight() + QPoint(1, 1), moved.bottomRight() + QPoint(65, 40));
        QRect resized = colorBounds();
        m_report.insert("imageResized", resized.width() > moved.width() + 20);
        m_canvas->grab().save(m_output + "-adjusted.png");
        trace("screenshot paste");
        // Raw screenshot paste while the Pen tool is selected.
        for (auto *widget : m_toolbar->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
            if (widget->inherits("ExpandableToolButton")) {
                if (auto *pen = widget->findChild<QAbstractButton *>()) pen->click();
                break;
            }
        }
        QImage screenshot(100, 70, QImage::Format_ARGB32);
        screenshot.fill(QColor(25, 180, 100));
        clipboard->setImage(screenshot);
        const int prePaste = m_modified;
        QKeyEvent overrideEvent(QEvent::ShortcutOverride, Qt::Key_V, Qt::ControlModifier);
        QCoreApplication::sendEvent(m_canvas, &overrideEvent);
        QKeyEvent pasteEvent(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
        QCoreApplication::sendEvent(m_canvas, &pasteEvent);
        QApplication::processEvents();
        m_report.insert("screenshotPasteFromPen", m_modified > prePaste && overrideEvent.isAccepted());
        m_report.insert("pasteKeptClipboard", clipboard->image() == screenshot);
        const int preDrop = m_modified;
        trace("drag drop");
        QMimeData dropMime;
        dropMime.setUrls({QUrl::fromLocalFile(m_sample)});
        QDragEnterEvent enter(QPoint(300, 300), Qt::CopyAction, &dropMime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(m_canvas, &enter);
        QDropEvent drop(QPointF(300, 300), Qt::CopyAction, &dropMime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(m_canvas, &drop);
        QApplication::processEvents();
        m_report.insert("fileDragDrop", enter.isAccepted() && drop.isAccepted() && m_modified > preDrop);
        // Cancellation must preserve both document and current tool.
        const int preCancel = m_modified;
        trace("cancel picker");
        chooseInNextDialog({});
        button->click();
        m_report.insert("cancelKeptDocument", m_modified == preCancel);
        m_canvas->grab().save(m_output + "-all-images.png");
        CreateDirectoryW(reinterpret_cast<LPCWSTR>(m_saveDir.utf16()), nullptr);
        trace("save notebook");
        chooseInNextDialog(m_bundle, m_saveDir);
        const bool saveInvoked = QMetaObject::invokeMethod(m_canvas->window(), "saveDocument", Qt::DirectConnection);
        trace("save dialog closed");
        QApplication::processEvents();
        if (!QFileInfo::exists(m_bundle)) {
            const auto saved = QDir(m_saveDir).entryInfoList({"*.snb"}, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
            if (!saved.isEmpty()) m_bundle = saved.first().absoluteFilePath();
        }
        m_report.insert("savedNotebook", saveInvoked && QDir(m_bundle).exists());
        m_report.insert("bundlePath", m_bundle);
        m_report.insert("modifiedSignals", m_modified);
        m_report.insert("edgeless", qEnvironmentVariableIsSet("SPEEDYNOTE_IMAGE_EDGELESS"));
        bool passed = true;
        for (auto it = m_report.begin(); it != m_report.end(); ++it)
            if (it->isBool() && it.key() != "edgeless" && !it->toBool()) passed = false;
        finish(passed, passed ? QString() : "One or more image checks failed");
    }
private:
    static QByteArray fileHash(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
    }
    static QJsonObject readJson(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return QJsonDocument::fromJson(file.readAll()).object();
    }
    QJsonObject pageJson() {
        const auto manifest = readJson(m_bundle + "/document.json");
        const auto order = manifest.value("page_order").toArray();
        if (order.isEmpty()) return {};
        return readJson(m_bundle + "/pages/" + order.first().toString() + ".json");
    }
    int strokeCount() {
        int count = 0;
        for (const auto &layer : pageJson().value("layers").toArray())
            count += layer.toObject().value("strokes").toArray().size();
        return count;
    }
    void testImagePage() {
        const bool reopened = qEnvironmentVariableIsSet("SPEEDYNOTE_IMAGE_REOPEN");
        if (reopened) m_bundle = QCoreApplication::arguments().last();
        else for (auto *widget : QApplication::topLevelWidgets()) {
            if (widget->inherits("Launcher")) {
                m_bundle = widget->property("touchInkLastImportedImageBundle").toString();
                m_report.insert("sourceImageUnchanged", fileHash(m_sample) ==
                    widget->property("imageProbeSourceHash").toByteArray());
            }
        }
        m_report.insert("bundlePath", m_bundle);
        QJsonObject page = pageJson();
        m_report.insert("pageMatchesImage", page.value("width").toDouble() == 240 &&
            page.value("height").toDouble() == 140);
        m_report.insert("fixedPageBackground", page.value("backgroundType").toInt() == 1 &&
            page.value("pdfPageNumber").toInt() == 0 && page.value("objects").toArray().isEmpty());
        m_report.insert("backgroundInsideNotebook", QFileInfo::exists(m_bundle + "/image.pdf"));
        m_report.insert("imageVisible", !colorBounds().isEmpty());
        if (reopened) {
            m_report.insert("annotationSurvivedReopen", strokeCount() > 0);
            m_canvas->grab().save(m_output + "-image-page.png");
        } else {
            bool penActive = false;
            for (auto *tool : m_toolbar->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
                if (!tool->inherits("ExpandableToolButton")) continue;
                if (auto *pen = tool->findChild<QAbstractButton *>()) penActive = pen->isChecked();
                break;
            }
            m_report.insert("penActiveOnImport", penActive);
            QMetaObject::invokeMethod(m_toolbar, "undoClicked", Qt::DirectConnection);
            QApplication::processEvents();
            m_report.insert("undoCannotRemoveBackground", !colorBounds().isEmpty());
            const QRect bounds = colorBounds();
            const int before = m_modified;
            drag(bounds.center() - QPoint(80, 30), bounds.center() + QPoint(80, 30));
            QMetaObject::invokeMethod(m_canvas->window(), "saveDocument", Qt::DirectConnection);
            m_report.insert("directAnnotationWritten", m_modified > before && strokeCount() > 0);
            QMetaObject::invokeMethod(m_toolbar, "undoClicked", Qt::DirectConnection);
            QMetaObject::invokeMethod(m_canvas->window(), "saveDocument", Qt::DirectConnection);
            m_report.insert("undoOnlyRemovesInk", strokeCount() == 0 && !colorBounds().isEmpty());
            QMetaObject::invokeMethod(m_toolbar, "redoClicked", Qt::DirectConnection);
            QMetaObject::invokeMethod(m_canvas->window(), "saveDocument", Qt::DirectConnection);
            m_report.insert("redoRestoresInk", strokeCount() > 0);
            auto *imageButton = m_toolbar->findChild<QToolButton *>("touchInkInsertImage");
            imageButton->menu()->actions().last()->trigger();
            const int preMove = m_modified;
            drag(bounds.center(), bounds.center() + QPoint(60, 40));
            m_report.insert("backgroundCannotMoveAsObject", m_modified == preMove);
            QMetaObject::invokeMethod(m_canvas, "setZoomLevel", Qt::DirectConnection, Q_ARG(qreal, 2.0));
            QApplication::processEvents();
            m_report.insert("imageZooms", !colorBounds().isEmpty() && colorBounds().width() < bounds.width());
            m_canvas->grab().save(m_output + "-image-page.png");
            m_report.insert("sourceDetachedForReopen", QFile::rename(m_sample,
                m_sample + ".moved-" + QString::number(QDateTime::currentMSecsSinceEpoch())));
        }
        bool passed = true;
        for (auto it = m_report.begin(); it != m_report.end(); ++it)
            if (it->isBool() && !it->toBool()) passed = false;
        finish(passed, passed ? QString() : "Image page checks failed");
    }
    void trace(const char *message) {
        QFile file(m_output + "-trace.txt");
        if (file.open(QIODevice::WriteOnly | QIODevice::Append)) { file.write(message); file.write("\n"); }
    }
    void chooseInNextDialog(const QString &path, const QString &directory = {}) {
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [timer, path, directory] {
            for (auto *widget : QApplication::topLevelWidgets()) {
                auto *dialog = qobject_cast<QFileDialog *>(widget);
                if (!dialog || !dialog->isVisible()) continue;
                QFile traceFile(qEnvironmentVariable("SPEEDYNOTE_IMAGE_PROBE_OUTPUT") + "-trace.txt");
                if (traceFile.open(QIODevice::WriteOnly | QIODevice::Append))
                    traceFile.write((dialog->windowTitle() + " mode=" + QString::number(dialog->fileMode()) + "\n").toUtf8());
                timer->stop();
                if (path.isEmpty()) dialog->reject();
                else {
                    if (dialog->fileMode() == QFileDialog::Directory && !directory.isEmpty()) {
                        dialog->setDirectory(directory);
                    } else {
                        dialog->setDirectory(QFileInfo(path).absolutePath());
                        dialog->selectFile(QFileInfo(path).fileName());
                    }
                    auto *acceptTimer = new QTimer(dialog);
                    acceptTimer->setSingleShot(true);
                    connect(acceptTimer, &QTimer::timeout, dialog, [dialog, path, directory] {
                        // QFileSystemModel loads the directory asynchronously and
                        // can clear selectFile() while restoring the last location.
                        if (auto *edit = dialog->findChild<QLineEdit *>("fileNameEdit"))
                            edit->setText(dialog->fileMode() == QFileDialog::Directory && !directory.isEmpty()
                                ? directory : path);
                        const QString prefix = qEnvironmentVariable("SPEEDYNOTE_IMAGE_PROBE_OUTPUT");
                        QFile traceFile(prefix + "-trace.txt");
                        if (traceFile.open(QIODevice::WriteOnly | QIODevice::Append))
                            traceFile.write(("accept " + dialog->selectedFiles().join(";") +
                                " dir=" + dialog->directory().absolutePath() + "\n").toUtf8());
                        dialog->grab().save(prefix + "-dialog.png");
                        QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
                    });
                    acceptTimer->start(150);
                }
                timer->deleteLater();
                return;
            }
        });
        timer->start(100);
    }
    QRect colorBounds() {
        const QImage image = m_canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
        QRect bounds;
        for (int y = 0; y < image.height(); ++y) {
            const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x)
                if (qAbs(qRed(row[x]) - m_color.red()) < 5 &&
                    qAbs(qGreen(row[x]) - m_color.green()) < 5 &&
                    qAbs(qBlue(row[x]) - m_color.blue()) < 5)
                    bounds = bounds.united(QRect(x, y, 1, 1));
        }
        return bounds;
    }
    void drag(QPoint start, QPoint end) {
        const auto send = [this](QEvent::Type type, QPoint point) {
            QMouseEvent event(type, point, point, m_canvas->mapToGlobal(point),
                type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                Qt::NoModifier, Qt::MouseEventSynthesizedByApplication);
            QCoreApplication::sendEvent(m_canvas, &event);
        };
        send(QEvent::MouseButtonPress, start);
        QThread::msleep(25); // Native resize processing is throttled to ~60 fps.
        send(QEvent::MouseMove, end);
        send(QEvent::MouseButtonRelease, end);
        QApplication::processEvents();
    }
    void finish(bool passed, QString error) {
        m_timer.stop();
        if (m_originalClipboard) {
            QApplication::clipboard()->setMimeData(m_originalClipboard);
            m_originalClipboard = nullptr;
        }
        m_report.insert("passed", passed);
        if (!error.isEmpty()) m_report.insert("error", error);
        QFile file(m_output + ".json");
        if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(m_report).toJson());
        file.close();
        // Only the explicit isolated probe process contains these generated notes.
        TerminateProcess(GetCurrentProcess(), passed ? 0 : 1);
    }
};

class ImageProbePlugin final : public QGenericPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QGenericPluginFactoryInterface" FILE "image-probe.json")
public:
    QObject *create(const QString &key, const QString &) override {
        if (key != "speedynoteimageprobe" || qEnvironmentVariableIsEmpty("SPEEDYNOTE_IMAGE_PROBE_OUTPUT")) return nullptr;
        return new ImageProbe;
    }
};
#include "image-probe.moc"
