// ============================================================================
// SpeedyNote - Main Entry Point
// ============================================================================

#include <QApplication>
#include <QElapsedTimer>
#include <QTranslator>
#include <QLocale>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QLibraryInfo>
#include <QFont>
#include <algorithm>

#include "MainWindow.h"
#include "ui/launcher/Launcher.h"
#include "platform/SystemNotification.h"
#include "core/DocumentViewport.h"
// CLI support (Desktop only). Excluded on HarmonyOS as well: Qt builds the app
// as a shared module loaded by an ArkTS host there, so there is no argv-taking
// executable for a command line to attach to.
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS) && !defined(Q_OS_HARMONY)
#include <QGuiApplication>
#include "cli/CliParser.h"
#endif

// Platform-specific includes
#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#endif

// Platform helpers
#ifdef Q_OS_ANDROID
#include <QDebug>
#include <QPalette>
#include <QJniObject>
#endif

#ifdef Q_OS_IOS
#include "ios/IOSPlatformHelper.h"
#include "ios/IOSTouchTracker.h"
#endif

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
#include <QDialog>
#include <QEvent>
#include <QPointer>
#include <QScreen>
#include <QTimer>

/**
 * @brief Event filter that maximizes top-level QDialog windows on mobile.
 *
 * Android: on some OEM skins (notably Samsung One UI), Qt's QDialog windows
 * are placed behind the main activity window, making them invisible while
 * still blocking input (modal). This makes the app appear frozen. The deferred
 * raise handles skins that process the show event asynchronously and override
 * the initial z-order.
 *
 * iOS: every top-level QWindow is a QUIView inside a single UIWindow, without
 * decorations or any move/resize implementation, so whatever geometry a dialog
 * requests is final -- the user cannot adjust it, not even with a pencil.
 * Maximizing also keeps desktop-sized dialogs from overflowing a narrow Split
 * View or Stage Manager window.
 *
 * Qt::WindowMaximized rather than Qt::WindowFullScreen is deliberate:
 * QIOSWindow derives the maximized geometry from availableGeometry()
 * intersected with the current UIWindow bounds, so the dialog stays inside the
 * safe area and follows multitasking resizes.
 */
class MobileDialogFilter : public QObject {
public:
    using QObject::QObject;
protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::Show) {
            auto* dialog = qobject_cast<QDialog*>(obj);
            if (dialog && dialog->isWindow()) {
                // Desktop-oriented size constraints would keep the dialog from
                // filling the window, and neither platform offers a way to
                // resize it by hand.
                dialog->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
                if (QScreen* screen = dialog->screen()) {
                    const QSize avail = screen->availableGeometry().size();
                    dialog->setMinimumSize(
                        qMin(dialog->minimumWidth(), avail.width()),
                        qMin(dialog->minimumHeight(), avail.height()));
                }
                dialog->setWindowState(dialog->windowState() | Qt::WindowMaximized);
                dialog->raise();
                dialog->activateWindow();
#ifdef Q_OS_ANDROID
                QPointer<QDialog> guard(dialog);
                QTimer::singleShot(50, dialog, [guard]() {
                    if (guard) {
                        guard->raise();
                        guard->activateWindow();
                    }
                });
#endif
            }
        }
        return QObject::eventFilter(obj, event);
    }
};
#endif

#ifdef Q_OS_HARMONY
#include <QDialog>
#include <QEvent>
#include <QScreen>
#include <QTimer>
#include <QWindow>

// The window a dialog should be centred on: the one that raised it, whose
// geometry is also the only rect that knows where the status bar ends. Falls
// back to the screen, which reports no safe-area margins here and so sits half a
// status bar high -- still close enough to read as centred.
static QRect harmonyDialogReference(const QDialog* dialog)
{
    const QWidget* reference = dialog->parentWidget() != nullptr
        ? dialog->parentWidget()->window()
        : QApplication::activeWindow();

    // activeWindow() is the dialog itself once it has been shown, and centring it
    // on itself would leave it wherever it already is.
    if (reference != nullptr && reference != dialog && reference->geometry().isValid()) {
        return reference->geometry();
    }
    if (const QScreen* screen = dialog->screen()) {
        return screen->availableGeometry();
    }
    return QRect();
}

static void centreHarmonyDialog(QDialog* dialog)
{
    const QRect reference = harmonyDialogReference(dialog);
    if (!reference.isValid()) {
        return;
    }

    // Where the dialog should end up, in the coordinates the dialog is drawn in.
    QRect target(QPoint(), dialog->size());
    target.moveCenter(reference.center());

    // A dialog taller or wider than the window it is centred on has to keep its
    // top-left corner reachable -- that is where the title and the buttons are --
    // so those two edges are clamped last and win.
    if (target.right() > reference.right()) {
        target.moveRight(reference.right());
    }
    if (target.bottom() > reference.bottom()) {
        target.moveBottom(reference.bottom());
    }
    if (target.left() < reference.left()) {
        target.moveLeft(reference.left());
    }
    if (target.top() < reference.top()) {
        target.moveTop(reference.top());
    }

    QWindow* handle = dialog->windowHandle();
    if (handle == nullptr) {
        // Nothing to submit a position to yet, so WA_Moved is what carries it:
        // QWidget::create() sends a position for a moved widget and only a size for any
        // other. This is the path the dialogs that place themselves take, and the reason
        // they are the ones that already came up centred.
        dialog->move(target.topLeft());
        return;
    }

    // Through the window, because QWidget::move() and QWidget::resize() do not carry a
    // position here at all: the platform applies the size in a widget-level geometry and
    // leaves the window where it was, whatever the timing. Only QWindow::setGeometry()
    // moves a dialog.
    //
    // This is exact only because the window is frameless. With a frame the platform
    // places the window a frame margin away from the rect submitted, and -- the part
    // that actually hurts -- the QPA keeps mapping touches from the rect it was given,
    // so the dialog draws in one place and answers taps a title bar lower.
    // What Qt has cached is not a reliable picture of where this platform put a
    // sub-window: the position submitted when the window was created is ignored
    // here, so the two disagree from the start. Asking Qt is worse than useless,
    // because QWindow::setGeometry() also returns early on a rect it believes is
    // already current -- so a dialog whose constructor happened to compute the
    // centred rect, as BatchImportDialog's does, would never have it applied and
    if (handle->geometry() != target) {
        handle->setGeometry(target);
    }
}

/**
 * @brief Event filter that centres top-level QDialog windows on HarmonyOS.
 *
 * The platform is only told where to put a window when Qt's
 * QWindowPrivate::positionAutomatic is false, and only an explicit move() or
 * setGeometry() clears that: QWidget::create() positions the native window if
 * Qt::WA_Moved is set and merely resizes it otherwise. QDialog does centre
 * itself, through adjustPosition() in its showEvent(), but then clears WA_Moved
 * again -- "not really an explicit position" -- so no position is ever submitted
 * and every dialog lands in the top-left corner with its title behind the status
 * bar. Qt believes it is centred all the while, so nothing in the app can notice.
 *
 * BatchExportDialog is the exception that gives the game away: it moves itself in
 * its constructor, before the native window exists, so WA_Moved is still set when
 * create() runs and the position goes out with the window. That is the easy half
 * of what this filter does. The hard half is that once the window exists a
 * position can only be submitted through QWindow, and that only lands where it is
 * asked to -- and only keeps the dialog clickable where it is drawn -- if the
 * window has no frame, which is why the flag is dropped before it is created.
 */
class HarmonyDialogCentring : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* obj, QEvent* event) override
    {
        auto* dialog = qobject_cast<QDialog*>(obj);
        if (dialog == nullptr || !dialog->isWindow()) {
            return QObject::eventFilter(obj, event);
        }

        switch (event->type()) {
        case QEvent::ChildAdded:
            // The window has to be frameless for the placement below to be exact, and a
            // frame can only be dropped before the platform window exists. ChildAdded is
            // the last event that arrives that early -- it comes from the constructor,
            // while QDialog creates its window later, inside setVisible(). Re-applied on
            // every child so a constructor that assigns its own flags after adding a
            // widget does not silently take the frame back.
            //
            // Nothing is lost: the platform draws no decoration on these sub-windows, so
            // the frame this gives up was only ever a margin in the geometry arithmetic.
            //
            // overrideWindowFlags() and not setWindowFlag(), because this runs inside the
            // dialog's constructor and setWindowFlag() does not stay inside the flags: it
            // reparents, the reparent re-inherits the style, and that delivers StyleChange
            // to an object whose constructor has not finished. QMessageBox answers a style
            // change by re-applying its icon, through an iconLabel it has not assigned yet,
            // and the app dies in QLabel::setPixmap() on a garbage pointer -- every message
            // box in the app, not just this one. The override is a plain assignment to the
            // same flags create() will read, and it is safe here for precisely the reason
            // its documented warning exists elsewhere: there is no platform window yet for
            // it to leave out of sync.
            if (dialog->windowHandle() == nullptr) {
                dialog->overrideWindowFlags(dialog->windowFlags() | Qt::FramelessWindowHint);
            }
            break;
        case QEvent::Show:
            // Nothing here places itself on purpose. Every QDialog in the app that
            // moves itself is trying to centre and does it in its constructor --
            // BatchImportDialog, SaveDocumentDialog and ExportResultsDialog all run
            // the same move(parent->geometry().center() - rect().center()) -- with a
            // rect() the layout has not filled in yet and a parent rect that says
            // nothing about where the status bar ends. So none of those placements is
            // worth keeping, and WA_Moved is what made them stick: while it is set
            // this filter stands aside, and QDialog::showEvent() skips its own
            // adjustPosition() for the same reason, which left all three pinned to
            // the top-left corner. Dropping it puts them on the path every other
            // dialog already takes.
            dialog->setAttribute(Qt::WA_Moved, false);

            // And again once the platform has had the window: a position submitted
            // from here goes out before the window is on screen, and the platform
            // answers with its own placement -- the top-left corner -- which lands
            // back in Qt as the window's geometry and wins. Dialogs that let
            // QDialog::showEvent() place them recover on their own, because
            // adjustPosition() moves them afterwards and the Move event brings this
            // filter back; a dialog that placed itself gets no such second event, so
            // without this it keeps the corner. exec() is running an event loop by
            // then, so a queued call gets its turn. Placement is idempotent, so the
            // dialogs that did not need this are unaffected.
            QTimer::singleShot(0, dialog, [dialog] { centreHarmonyDialog(dialog); });
            Q_FALLTHROUGH();
        case QEvent::Move:
        case QEvent::Resize:
            // Move and Resize as well as Show, because the show is not the end of it: a
            // dialog that asked for less room than its layout needs is resized on the
            // first layout pass afterwards, and the position has to be reasserted for the
            // size it ends up with.
            //
            // WA_Moved means something placed this window deliberately, which covers the
            // dialogs that position themselves and, if this filter is ever widened past
            // QDialog, every menu and tooltip -- they are all given a point to open at.
            // Only Qt's own centring leaves it clear, and QDialog clears it again on the
            // way out, so this reads the same on the first show and every later one.
            if (!dialog->testAttribute(Qt::WA_Moved)) {
                centreHarmonyDialog(dialog);
            }
            break;
        default:
            break;
        }

        return QObject::eventFilter(obj, event);
    }
};

// Put the dialog that is currently taking input back in front of everything else.
// Queued from the filter below rather than run inside event delivery: raising a window
// while its own deactivation is still being delivered asks the platform to undo what it
// is in the middle of doing, and waiting also means the modal is read after Qt has
// finished bookkeeping, so a dialog that was on its way out is gone by now and a dialog
// that was underneath one has already inherited the role.
static void restoreHarmonyActiveModal()
{
    QWidget* modal = QApplication::activeModalWidget();
    if (modal == nullptr || !modal->isVisible()) {
        return;
    }
    // Not while the app is in the background: a dialog losing activation because the
    // user left the app is not the case this is here for, and raise() on a sub-window
    // goes to the top of the whole app, which would drag it back into view.
    if (QGuiApplication::applicationState() != Qt::ApplicationActive) {
        return;
    }
    modal->raise();
    modal->activateWindow();
}

/**
 * @brief Event filter that keeps a modal dialog in front of the windows it blocks.
 *
 * The platform does not enforce modality in the z-order. A tap outside an open dialog
 * raises the window under it -- the dialog drops from ZOrd 104 to 103 and the fullscreen
 * MainWindow it blocks takes 104 -- so the dialog ends up buried while keeping its
 * geometry and its modality. Qt's modal event loop then discards every press that lands
 * on the window now in front, and with the dialog out of sight the app reads as frozen.
 *
 * Almost none of that is reported to Qt, which is why this has two triggers rather than
 * one obvious one:
 *
 * - A tap outside sends WindowDeactivate to the dialog and sets the focus window to
 *   nullptr. The window that came forward is never activated, because Qt's own modality
 *   is what blocks it, so there is nothing to react to on that side.
 * - A dialog closing over another one is reported to neither. Dismissing the colour
 *   picker opened from the settings dialog leaves the settings dialog at the z-order it
 *   was pushed down to, without so much as a WindowActivate, so its own hide is the only
 *   place the dialog underneath can be rescued from.
 */
class HarmonyModalKeeper : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* obj, QEvent* event) override
    {
        auto* widget = qobject_cast<QWidget*>(obj);
        if (widget == nullptr || !widget->isWindow()) {
            return QObject::eventFilter(obj, event);
        }

        switch (event->type()) {
        case QEvent::WindowDeactivate:
            // Only for the topmost modal. A dialog losing activation to a modal of its
            // own is no longer the active modal by this point, and is meant to stay
            // where it is: behind the one that now is.
            if (widget == QApplication::activeModalWidget()) {
                QTimer::singleShot(0, widget, restoreHarmonyActiveModal);
            }
            break;
        case QEvent::Hide:
            // A modal going away uncovers whichever one it was opened from, which by
            // then may have been pushed under a window it blocks by a tap outside.
            if (widget->isModal()) {
                QTimer::singleShot(0, qApp, restoreHarmonyActiveModal);
            }
            break;
        default:
            break;
        }

        return QObject::eventFilter(obj, event);
    }
};
#endif

#ifdef Q_OS_MACOS
#include "macos/MacMenuBar.h"
#endif

#ifdef Q_OS_HARMONY
#include "harmony/HarmonyEnvironment.h"
#include "harmony/HarmonyPersistentGrant.h"
#endif

#include "harmony/HarmonyWindowSwitch.h"

#ifdef Q_OS_ANDROID

static void logAndroidPaths()
{
    // Log storage paths for debugging
    qDebug() << "=== Android Storage Paths ===";
    qDebug() << "  AppDataLocation:" << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    qDebug() << "  DocumentsLocation:" << QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    qDebug() << "  DownloadLocation:" << QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    qDebug() << "  CacheLocation:" << QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    qDebug() << "=============================";
    
    // Note: On Android 13+ (API 33+), READ_EXTERNAL_STORAGE is deprecated.
    // PDF file access requires Storage Access Framework (SAF).
    // QFileDialog uses SAF, but content:// URI handling in Qt may have issues.
}

/**
 * Query Android system for dark mode setting via JNI.
 * Calls SpeedyNoteActivity.isDarkMode() static method.
 */
static bool isAndroidDarkMode()
{
    // callStaticMethod<jboolean> returns the primitive directly, not a QJniObject
    return QJniObject::callStaticMethod<jboolean>(
        "org/speedynote/app/SpeedyNoteActivity",
        "isDarkMode",
        "()Z"
    );
}

/**
 * Apply appropriate palette based on Android system theme.
 * Uses Fusion style for consistent cross-platform theming.
 */
static void applyAndroidPalette(QApplication& app)
{
    // Use Fusion style on Android - it properly respects palette colors
    // The default "android" style has inconsistent palette support
    app.setStyle("Fusion");
    
    bool darkMode = isAndroidDarkMode();
    qDebug() << "Android dark mode:" << darkMode;
    
    if (darkMode) {
        // Dark palette - same colors as Windows dark mode for consistency
        QPalette darkPalette;
        
        QColor darkGray(53, 53, 53);
        QColor gray(128, 128, 128);
        QColor blue("#316882");  // SpeedyNote default teal accent
        
        darkPalette.setColor(QPalette::Window, QColor(45, 45, 45));
        darkPalette.setColor(QPalette::WindowText, Qt::white);
        darkPalette.setColor(QPalette::Base, QColor(35, 35, 35));
        darkPalette.setColor(QPalette::AlternateBase, darkGray);
        darkPalette.setColor(QPalette::Text, Qt::white);
        darkPalette.setColor(QPalette::ToolTipBase, QColor(60, 60, 60));
        darkPalette.setColor(QPalette::ToolTipText, Qt::white);
        darkPalette.setColor(QPalette::Button, darkGray);
        darkPalette.setColor(QPalette::ButtonText, Qt::white);
        darkPalette.setColor(QPalette::Light, QColor(80, 80, 80));
        darkPalette.setColor(QPalette::Midlight, QColor(65, 65, 65));
        darkPalette.setColor(QPalette::Dark, QColor(35, 35, 35));
        darkPalette.setColor(QPalette::Mid, QColor(50, 50, 50));
        darkPalette.setColor(QPalette::Shadow, QColor(20, 20, 20));
        darkPalette.setColor(QPalette::BrightText, Qt::red);
        darkPalette.setColor(QPalette::Link, blue);
        darkPalette.setColor(QPalette::LinkVisited, QColor(blue).lighter());
        darkPalette.setColor(QPalette::Highlight, blue);
        darkPalette.setColor(QPalette::HighlightedText, Qt::white);
        darkPalette.setColor(QPalette::PlaceholderText, gray);
        
        darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, gray);
        darkPalette.setColor(QPalette::Disabled, QPalette::Text, gray);
        darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, gray);
        darkPalette.setColor(QPalette::Disabled, QPalette::Base, QColor(50, 50, 50));
        darkPalette.setColor(QPalette::Disabled, QPalette::Button, QColor(50, 50, 50));
        darkPalette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(80, 80, 80));
        
        app.setPalette(darkPalette);
    } else {
        // Light palette - explicitly set for consistency
        QPalette lightPalette;
        
        QColor lightGray(240, 240, 240);
        QColor gray(160, 160, 160);
        QColor linkBlue(0, 120, 215);
        QColor accent("#cffff5");  // SpeedyNote light mint accent
        
        lightPalette.setColor(QPalette::Window, QColor(240, 240, 240));
        lightPalette.setColor(QPalette::WindowText, Qt::black);
        lightPalette.setColor(QPalette::Base, Qt::white);
        lightPalette.setColor(QPalette::AlternateBase, lightGray);
        lightPalette.setColor(QPalette::Text, Qt::black);
        lightPalette.setColor(QPalette::ToolTipBase, QColor(255, 255, 220));
        lightPalette.setColor(QPalette::ToolTipText, Qt::black);
        lightPalette.setColor(QPalette::Button, lightGray);
        lightPalette.setColor(QPalette::ButtonText, Qt::black);
        lightPalette.setColor(QPalette::Light, Qt::white);
        lightPalette.setColor(QPalette::Midlight, QColor(227, 227, 227));
        lightPalette.setColor(QPalette::Dark, QColor(160, 160, 160));
        lightPalette.setColor(QPalette::Mid, QColor(200, 200, 200));
        lightPalette.setColor(QPalette::Shadow, QColor(105, 105, 105));
        lightPalette.setColor(QPalette::BrightText, Qt::red);
        lightPalette.setColor(QPalette::Link, linkBlue);
        lightPalette.setColor(QPalette::LinkVisited, QColor(linkBlue).darker());
        lightPalette.setColor(QPalette::Highlight, accent);
        lightPalette.setColor(QPalette::HighlightedText, Qt::black);
        lightPalette.setColor(QPalette::PlaceholderText, gray);
        
        lightPalette.setColor(QPalette::Disabled, QPalette::WindowText, gray);
        lightPalette.setColor(QPalette::Disabled, QPalette::Text, gray);
        lightPalette.setColor(QPalette::Disabled, QPalette::ButtonText, gray);
        lightPalette.setColor(QPalette::Disabled, QPalette::Base, lightGray);
        lightPalette.setColor(QPalette::Disabled, QPalette::Button, lightGray);
        lightPalette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(180, 180, 180));
        
        app.setPalette(lightPalette);
    }
}

/**
 * Apply proper fonts for Android with CJK (Chinese-Japanese-Korean) support.
 * 
 * Qt on Android doesn't properly use Android's locale-aware font fallback,
 * causing CJK characters to display with mixed glyphs (SC/TC/JP variants).
 * 
 * This function sets up a font family list that:
 * 1. Uses Roboto as the primary font (Android's default)
 * 2. Falls back to Noto Sans CJK SC for Simplified Chinese
 * 3. Includes other CJK variants as additional fallbacks
 */
static void applyAndroidFonts(QApplication& app)
{
    // Get current system locale to determine CJK preference
    QString locale = QLocale::system().name();  // e.g., "zh_CN", "zh_TW", "ja_JP"
    
    QFont font("Roboto", 14);  // Android's default font, slightly larger for touch
    font.setStyleHint(QFont::SansSerif);
    
    // Set up CJK fallback chain based on locale
    // The order matters - first matching font with the glyph wins
    if (locale.startsWith("zh_CN") || locale.startsWith("zh_Hans")) {
        // Simplified Chinese - prioritize SC variant
        font.setFamilies({"Roboto", "Noto Sans CJK SC", "Noto Sans SC", 
                          "Source Han Sans SC", "Droid Sans Fallback"});
    } else if (locale.startsWith("zh_TW") || locale.startsWith("zh_HK") || locale.startsWith("zh_Hant")) {
        // Traditional Chinese - prioritize TC variant
        font.setFamilies({"Roboto", "Noto Sans CJK TC", "Noto Sans TC",
                          "Source Han Sans TC", "Droid Sans Fallback"});
    } else if (locale.startsWith("ja")) {
        // Japanese - prioritize JP variant
        font.setFamilies({"Roboto", "Noto Sans CJK JP", "Noto Sans JP",
                          "Source Han Sans JP", "Droid Sans Fallback"});
    } else if (locale.startsWith("ko")) {
        // Korean - prioritize KR variant
        font.setFamilies({"Roboto", "Noto Sans CJK KR", "Noto Sans KR",
                          "Source Han Sans KR", "Droid Sans Fallback"});
    } else {
        // Default: use SC as fallback (most complete CJK coverage)
        font.setFamilies({"Roboto", "Noto Sans CJK SC", "Noto Sans SC",
                          "Droid Sans Fallback"});
    }
    
    app.setFont(font);
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Android font configured for locale:" << locale 
             << "families:" << font.families();
    #endif
}
#endif

// Test includes (desktop debug builds only)
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS) && defined(SPEEDYNOTE_DEBUG)
#include "core/PageTests.h"
#include "core/DocumentTests.h"
#include "core/NotebookLibraryTests.h"
#include "core/SandboxPdfOwnershipTests.h"
#include "core/DocumentViewportTests.h"
#include "ui/ToolbarButtonTests.h"
#include "objects/LinkObjectTests.h"
#include "objects/TextBoxObjectTests.h"
#include "pdf/MuPdfExporterTests.h"
#include "ui/actionbars/ActionBarContainerTests.h"
#include "ui/ToolbarButtonTestWidget.h"
#include "ocr/OcrRasterTests.h"
#include "ocr/OcrGoldenTests.h"
#ifdef SPEEDYNOTE_HAS_VISION_OCR
#include "ocr/OcrVisionTests.h"
#endif
#ifdef SPEEDYNOTE_HAS_PADDLE_OCR
#include "ocr/OcrPaddleTests.h"
#endif
#endif

// ============================================================================
// Platform Helpers
// ============================================================================

#ifdef Q_OS_WIN
static bool isWindowsDarkMode()
{
    QSettings settings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                       QSettings::NativeFormat);
    return settings.value("AppsUseLightTheme", 1).toInt() == 0;
}

static bool isWindows11()
{
    return QSysInfo::kernelVersion().split('.')[2].toInt() >= 22000;
}

static void applyWindowsFonts(QApplication& app)
{
    QFont font("Segoe UI", 9);
    font.setStyleHint(QFont::SansSerif);
    // Full hinting snaps glyphs to integer pixel boundaries, causing
    // inconsistent character spacing from accumulated rounding errors.
    // Affects Qt5 (GDI engine) and Qt6 (DirectWrite) on older Windows versions.
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setFamilies({"Segoe UI", "Dengxian", "Microsoft YaHei", "SimHei"});
    app.setFont(font);
}

static void applyWindowsPalette(QApplication& app)
{
    if (!isWindows11()) {
        app.setStyle(isWindowsDarkMode() ? "Fusion" : "windowsvista");
    }

    if (isWindowsDarkMode()) {
        QPalette darkPalette;

        QColor darkGray(53, 53, 53);
        QColor gray(128, 128, 128);
        QColor blue("#316882");  // SpeedyNote default teal accent

        darkPalette.setColor(QPalette::Window, QColor(45, 45, 45));
        darkPalette.setColor(QPalette::WindowText, Qt::white);
        darkPalette.setColor(QPalette::Base, QColor(35, 35, 35));
        darkPalette.setColor(QPalette::AlternateBase, darkGray);
        darkPalette.setColor(QPalette::Text, Qt::white);
        darkPalette.setColor(QPalette::ToolTipBase, QColor(60, 60, 60));
        darkPalette.setColor(QPalette::ToolTipText, Qt::white);
        darkPalette.setColor(QPalette::Button, darkGray);
        darkPalette.setColor(QPalette::ButtonText, Qt::white);
        darkPalette.setColor(QPalette::Light, QColor(80, 80, 80));
        darkPalette.setColor(QPalette::Midlight, QColor(65, 65, 65));
        darkPalette.setColor(QPalette::Dark, QColor(35, 35, 35));
        darkPalette.setColor(QPalette::Mid, QColor(50, 50, 50));
        darkPalette.setColor(QPalette::Shadow, QColor(20, 20, 20));
        darkPalette.setColor(QPalette::BrightText, Qt::red);
        darkPalette.setColor(QPalette::Link, blue);
        darkPalette.setColor(QPalette::LinkVisited, QColor(blue).lighter());
        darkPalette.setColor(QPalette::Highlight, blue);
        darkPalette.setColor(QPalette::HighlightedText, Qt::white);
        darkPalette.setColor(QPalette::PlaceholderText, gray);

        darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, gray);
        darkPalette.setColor(QPalette::Disabled, QPalette::Text, gray);
        darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, gray);
        darkPalette.setColor(QPalette::Disabled, QPalette::Base, QColor(50, 50, 50));
        darkPalette.setColor(QPalette::Disabled, QPalette::Button, QColor(50, 50, 50));
        darkPalette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(80, 80, 80));

        app.setPalette(darkPalette);
    }
}

static void enableDebugConsole()
{
#ifdef SPEEDYNOTE_DEBUG
    // Only attach a console when stdout has nowhere to go. Reopening the
    // standard streams on CONOUT$ points them at the console device, which
    // would discard any pipe or file redirection the caller set up, making
    // test-suite output impossible to capture.
    const HANDLE existingStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (existingStdOut == nullptr || existingStdOut == INVALID_HANDLE_VALUE) {
        AllocConsole();
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
#else
    FreeConsole();
#endif
}
#endif // Q_OS_WIN

// ============================================================================
// Translation Loading
// ============================================================================

static void loadTranslations(QApplication& app, QTranslator& translator)
{
    QSettings settings("SpeedyNote", "App");
    bool useSystemLanguage = settings.value("useSystemLanguage", true).toBool();

    // Preserve the full BCP-47 / region-tagged code when in system mode so the
    // QTranslator::load(QLocale, ...) overload below can probe regional Qt
    // catalogs (e.g. qtbase_zh_CN.qm exists; qtbase_zh.qm does not).
    QString fullLocale;
    QString langCode;
    if (useSystemLanguage) {
        fullLocale = QLocale::system().name();
        langCode   = fullLocale.section('_', 0, 0);
    } else {
        langCode   = settings.value("languageOverride", "en").toString();
        fullLocale = langCode;
    }
    QLocale uiLocale(fullLocale);

    // Common search paths for Qt's own catalog. Order matters:
    //   1. Deployed/install layouts (so a packaged build never accidentally
    //      picks up a translation from the build machine's Qt prefix).
    //   2. ":/qt-translations" - embedded fallback populated on mobile by
    //      CMakeLists.txt's qt_translations.qrc generation. Universal, so
    //      desktop also benefits if the on-disk paths are unreadable for
    //      any reason.
    //   3. QLibraryInfo::TranslationsPath - dev convenience for running
    //      out of the build tree against a system Qt.
    QStringList qtSearchPaths;
    qtSearchPaths
        << QCoreApplication::applicationDirPath() + "/translations"
        << QCoreApplication::applicationDirPath()
        << "/usr/share/speedynote/translations"
        << "/usr/local/share/speedynote/translations"
        << QStringLiteral(":/qt-translations");
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    qtSearchPaths << QLibraryInfo::path(QLibraryInfo::TranslationsPath);
#else
    qtSearchPaths << QLibraryInfo::location(QLibraryInfo::TranslationsPath);
#endif

    // Load Qt's base translations (Save / Discard / Cancel / Open / Yes / No
    // and other QMessageBox / QFileDialog standard strings).
    // QTranslator::load(QLocale, ...) probes the full regional fallback chain
    // (e.g. qtbase_zh_CN -> qtbase_zh -> qtbase), so we don't need a manual
    // langCode -> region map. We try `qtbase` first (the Qt 6 catalog that
    // actually carries the widget strings) and fall back to `qt` for
    // dev/Qt 5 setups and for the legacy aggregated catalog that
    // windeployqt6 still ships.
    //
    // qtBaseTranslator is static so it survives the function return; the
    // QApplication translator stack holds a non-owning pointer. Each load()
    // discards the previous catalog, so repeated calls (e.g. after a
    // hypothetical hot language switch) stay leak-free.
    static QTranslator qtBaseTranslator;
    bool qtLoaded = false;
    for (const QString& path : qtSearchPaths) {
        if (path.isEmpty()) continue;
        if (qtBaseTranslator.load(uiLocale, "qtbase", "_", path) ||
            qtBaseTranslator.load(uiLocale, "qt",     "_", path)) {
            app.installTranslator(&qtBaseTranslator);
            qtLoaded = true;
            break;
        }
    }
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[i18n] Qt base translator loaded:" << qtLoaded
             << "| locale =" << uiLocale.name() << "| langCode =" << langCode;
#else
    Q_UNUSED(qtLoaded);
#endif

    // Load SpeedyNote's translations. Same QLocale-aware overload so a user
    // whose system locale is e.g. de_AT still picks up app_de.qm.
    // ":/resources/translations" is the universal embedded fallback baked
    // into the binary via resources.qrc - works on every platform.
    QStringList translationPaths = {
        QCoreApplication::applicationDirPath(),
        QCoreApplication::applicationDirPath() + "/translations",
        "/usr/share/speedynote/translations",
        "/usr/local/share/speedynote/translations",
        QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                               "speedynote/translations", QStandardPaths::LocateDirectory),
        QStringLiteral(":/resources/translations")
    };

    bool appLoaded = false;
    for (const QString& path : translationPaths) {
        if (path.isEmpty()) continue;
        if (translator.load(uiLocale, "app", "_", path)) {
            app.installTranslator(&translator);
            appLoaded = true;
            break;
        }
    }
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[i18n] App translator loaded:" << appLoaded
             << "| locale =" << uiLocale.name() << "| langCode =" << langCode;
#else
    Q_UNUSED(appLoaded);
#endif
}

// ============================================================================
// Launcher Setup
// ============================================================================

static void connectLauncherSignals(Launcher* launcher)
{
    // Helper to get or create MainWindow
    auto getMainWindow = [](Launcher* l) -> std::pair<MainWindow*, bool> {
        MainWindow* w = MainWindow::findExistingMainWindow();
        bool existing = (w != nullptr);
        if (!w) {
            w = new MainWindow();
            w->setAttribute(Qt::WA_DeleteOnClose);
        }
        w->preserveWindowState(l, existing);
        w->bringToFront();
        return {w, existing};
    };

    QObject::connect(launcher, &Launcher::notebookSelected, [=](const QString& bundlePath) {
        auto [w, _] = getMainWindow(launcher);
        if (!w->switchToDocument(bundlePath)) {
            w->openFileInNewTab(bundlePath);
        }
        launcher->hideWithAnimation();
    });

    QObject::connect(launcher, &Launcher::createNewEdgeless, [=]() {
        auto [w, _] = getMainWindow(launcher);
        w->addNewEdgelessTab();
        launcher->hideWithAnimation();
    });

    QObject::connect(launcher, &Launcher::createNewPaged, [=]() {
        auto [w, _] = getMainWindow(launcher);
        w->addNewTab();
        launcher->hideWithAnimation();
    });

    QObject::connect(launcher, &Launcher::openPdfRequested, [=]() {
        auto [w, _] = getMainWindow(launcher);
        w->showOpenPdfDialog();
        launcher->hideWithAnimation();
    });

    QObject::connect(launcher, &Launcher::openNotebookRequested, [=]() {
        auto [w, _] = getMainWindow(launcher);
        w->loadFolderDocument();
        launcher->hideWithAnimation();
    });
    
    // Handle Escape/return to MainWindow request
    // Only return if MainWindow exists and has open tabs
    QObject::connect(launcher, &Launcher::returnToMainWindowRequested, [=]() {
        MainWindow* w = MainWindow::findExistingMainWindow();
        if (w && w->tabCount() > 0) {
            // MainWindow exists with open tabs - toggle back to it
            w->preserveWindowState(launcher, true);
            w->bringToFront();
            launcher->hideWithAnimation();
        }
        // Otherwise, do nothing (stay on Launcher)
    });
}

// Pre-create the Launcher used for the entire app lifetime. On macOS,
// also show() it immediately and run one event-loop tick so NSApp
// transitions to .regular activation before the heavyweight MainWindow
// is shown on top. Without this priming, on macOS 26 NSApp stays in a
// half-active state where QMenu popups (Add / Overflow), the fullscreen
// toggle, and the back-to-Launcher button all misbehave until the user
// manually activates the app via the dock icon.
//
// On Windows / Linux / iOS / Android this is a no-op (Launcher created
// but not shown), preserving today's behavior on those platforms.
static Launcher* createLauncherForColdStart()
{
    auto* launcher = new Launcher();
    launcher->setAttribute(Qt::WA_DeleteOnClose);
    connectLauncherSignals(launcher);
#if defined(Q_OS_MACOS) || defined(Q_OS_HARMONY)
    launcher->show();
    // Exclude user-input events: at this point in main() the only events we
    // want to process are the ones that realize the native window (and on
    // macOS activate NSApp). We don't want the launcher to spuriously react
    // to a stray mouse or key event delivered during the priming tick.
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
#endif

#ifdef Q_OS_HARMONY
    // One tick is not always enough, and every rect used for the rest of the cold
    // start is measured against this window. The platform sizes it asynchronously,
    // and until it does, geometry() reads as QWidget's default 640x480 -- a rect
    // nothing treats as suspect, so the session prompt centred on it lands in the
    // top-left quadrant and a MainWindow given it comes up small. It depends purely
    // on whether the resize arrived in time, which is why it showed up as an
    // intermittent misplacement on roughly the first launch after a cold start.
    //
    // Filling the display's width is what says the platform has had its say. The
    // wait is bounded because a window manager that gives the Launcher less than the
    // full width -- PC mode does -- would never satisfy that, and a mispositioned
    // dialog is a far better outcome than a startup that hangs.
    if (const QScreen* screen = launcher->screen()) {
        QElapsedTimer settling;
        settling.start();
        while (launcher->width() < screen->geometry().width() && settling.elapsed() < 500) {
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
        }
    }
#endif

    return launcher;
}

// Show MainWindow at cold start. On macOS this routes through the
// already-visible Launcher using the exact sequence that the proven-
// working Launcher::notebookSelected click uses (preserveWindowState +
// bringToFront + launcher->hide()). On other platforms it just calls
// show().
static void showMainWindowAtColdStart(MainWindow* w, Launcher* launcher)
{
#ifdef Q_OS_MACOS
    w->preserveWindowState(launcher, /*existing*/false);
    w->bringToFront();
    launcher->hide();
#elif defined(Q_OS_HARMONY)
    // The launcher is already visible here (see createLauncherForColdStart) and
    // stays that way: hide() would minimise its ability instance, and the paired
    // restore() is broken, so the user's first toggle back to the Launcher would
    // find nothing to restore. Raising the MainWindow over it is the whole
    // dismissal. See HarmonyWindowSwitch.
    HarmonyWindowSwitch::switchTo(/*incoming*/w, /*outgoing*/launcher);
#else
    (void)launcher;
    w->show();
#endif
}

// Make Launcher visible at cold start for branches that want to land on
// the Launcher. On macOS and HarmonyOS the Launcher is already visible from
// createLauncherForColdStart(), so this is a no-op there.
static void showLauncherAtColdStart(Launcher* launcher)
{
#if !defined(Q_OS_MACOS) && !defined(Q_OS_HARMONY)
    launcher->show();
#else
    (void)launcher;
#endif
}

// ============================================================================
// Test Runners (Desktop Debug Builds Only)
// ============================================================================

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS) && defined(SPEEDYNOTE_DEBUG)
static int runTests(const QString& testType)
{
    // Windows console setup already happened in enableDebugConsole().

    bool success = false;

    if (testType == "page") {
        success = PageTests::runAllTests();
    } else if (testType == "document") {
        success = DocumentTests::runAllTests();
    } else if (testType == "notebooklibrary") {
        success = NotebookLibraryTests::runAllTests();
    } else if (testType == "sandboxpdf") {
        success = SandboxPdfOwnershipTests::runAllTests();
    } else if (testType == "viewport-unit") {
        success = DocumentViewportTests::runUnitTests();
    } else if (testType == "linkobject") {
        success = LinkObjectTests::runAllTests();
    } else if (testType == "textboxobject") {
        success = TextBoxObjectTests::runAllTests();
    } else if (testType == "pdfexporter") {
        success = MuPdfExporterTests::runAllTests();
    } else if (testType == "actionbars") {
        success = ActionBarContainerTests::runAllTests();
    } else if (testType == "ocr-raster") {
        success = OcrRasterTests::runAllTests();
    } else if (testType == "ocr-golden") {
        success = OcrGoldenTests::runAllTests();
#ifdef SPEEDYNOTE_HAS_VISION_OCR
    } else if (testType == "ocr-vision") {
        success = OcrVisionTests::runAllTests();
#endif
#ifdef SPEEDYNOTE_HAS_PADDLE_OCR
    } else if (testType == "ocr-paddle") {
        success = OcrPaddleTests::runAllTests();
#endif
    } else if (testType == "buttons") {
        return QTest::qExec(new ToolbarButtonTests());
    }

    return success ? 0 : 1;
}
#endif

// ============================================================================
// macOS: QFileOpenEvent handler (works on macOS, NOT on iOS)
// ============================================================================
#if defined(Q_OS_MACOS)
#include <QFileOpenEvent>
#include <QFileInfo>
#include <QDir>

class FileOpenEventFilter : public QObject
{
public:
    using QObject::QObject;

    void setLauncher(Launcher *l) { m_launcher = l; }
    void setMainWindow(MainWindow *w) { m_mainWindow = w; }

protected:
    bool eventFilter(QObject *obj, QEvent *event) override
    {
        if (event->type() != QEvent::FileOpen)
            return QObject::eventFilter(obj, event);

        auto *foe = static_cast<QFileOpenEvent *>(event);
        QString path = foe->file();
        if (path.isEmpty())
            return true;
        #ifdef SPEEDYNOTE_DEBUG
        fprintf(stderr, "[FileOpen] received: %s\n", qPrintable(path));
        #endif

        QFileInfo fi(path);
        QString ext = fi.suffix().toLower();

        if (ext == "snbx") {
            if (m_launcher) {
                m_launcher->importFiles(QStringList{path});
            }
        } else if (ext == "pdf" || (fi.isDir() && path.endsWith(".snb"))) {
            MainWindow *w = m_mainWindow
                ? m_mainWindow
                : MainWindow::findExistingMainWindow();
            if (w) {
                w->openFileInNewTab(path);
            }
        }
        return true;
    }

private:
    Launcher *m_launcher = nullptr;
    MainWindow *m_mainWindow = nullptr;
};
#endif // Q_OS_MACOS

// ============================================================================
// iOS: Inbox directory watcher
// ============================================================================
// Qt's iOS plugin does NOT deliver QFileOpenEvent. Instead it tries
// QPlatformServices::openDocument() which is unimplemented. However iOS
// correctly copies incoming files to Documents/Inbox/. We watch that
// directory and process new arrivals.
#if defined(Q_OS_IOS)
#include <QFileSystemWatcher>
#include <QTimer>
#include <QFileInfo>
#include <QDir>
#include <QPointer>

class IOSInboxWatcher : public QObject
{
public:
    explicit IOSInboxWatcher(QObject *parent = nullptr)
        : QObject(parent)
    {
        m_inboxPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                      + QStringLiteral("/Inbox");
        QDir().mkpath(m_inboxPath);

        m_watcher.addPath(m_inboxPath);
        connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
                this, &IOSInboxWatcher::onDirectoryChanged);

        // Process anything already sitting in Inbox at launch
        QTimer::singleShot(800, this, &IOSInboxWatcher::processInbox);
    }

    void setLauncher(Launcher *l) { m_launcher = l; }
    void setMainWindow(MainWindow *w) { m_mainWindow = w; }

private:
    void onDirectoryChanged(const QString &)
    {
        if (m_processing)
            return;
        QTimer::singleShot(400, this, &IOSInboxWatcher::processInbox);
    }

    QString copyPdfToPermanentStorage(const QString &inboxPath)
    {
        QString pdfsDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                          + QStringLiteral("/pdfs");
        QDir().mkpath(pdfsDir);

        QFileInfo fi(inboxPath);
        QString destPath = pdfsDir + "/" + fi.fileName();

        if (QFile::exists(destPath)) {
            QString baseName = fi.completeBaseName();
            QString ext = fi.suffix();
            int counter = 1;
            do {
                destPath = pdfsDir + "/" + baseName
                           + QString("_%1.").arg(counter++) + ext;
            } while (QFile::exists(destPath));
        }

        if (QFile::copy(inboxPath, destPath))
            return destPath;
        return QString();
    }

    MainWindow* getOrCreateMainWindow()
    {
        MainWindow *w = m_mainWindow
            ? m_mainWindow.data()
            : MainWindow::findExistingMainWindow();
        if (w)
            return w;

        w = new MainWindow();
        w->setAttribute(Qt::WA_DeleteOnClose);
        if (m_launcher) {
            w->preserveWindowState(m_launcher, false);
            m_launcher->hideWithAnimation();
        }
        w->show();
        m_mainWindow = w;
        return w;
    }

    void processInbox()
    {
        if (m_processing)
            return;
        m_processing = true;

        QDir inbox(m_inboxPath);
        const QFileInfoList entries = inbox.entryInfoList(
            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

        QStringList snbxFiles;

        for (const QFileInfo &fi : entries) {
            QString ext = fi.suffix().toLower();
            QString path = fi.absoluteFilePath();

            if (ext == "snbx") {
                snbxFiles.append(path);
            } else if (ext == "pdf") {
                QString permanentPath = copyPdfToPermanentStorage(path);
                if (!permanentPath.isEmpty()) {
                    MainWindow *w = getOrCreateMainWindow();
                    if (w)
                        w->openFileInNewTab(permanentPath);
                }
                QFile::remove(path);
            } else if (fi.isDir() && path.endsWith(QStringLiteral(".snb"))) {
                MainWindow *w = getOrCreateMainWindow();
                if (w)
                    w->openFileInNewTab(path);
            }
        }

        // performBatchImport handles Inbox cleanup internally, no need to
        // remove snbx files here (it would be a harmless double-delete).
        if (!snbxFiles.isEmpty() && m_launcher) {
            m_launcher->importFiles(snbxFiles);
        }

        // Re-add the path in case QFileSystemWatcher dropped it
        if (!m_watcher.directories().contains(m_inboxPath)) {
            m_watcher.addPath(m_inboxPath);
        }

        m_processing = false;
    }

    QFileSystemWatcher m_watcher;
    QString m_inboxPath;
    QPointer<Launcher> m_launcher;
    QPointer<MainWindow> m_mainWindow;
    bool m_processing = false;
};
#endif // Q_OS_IOS

// ============================================================================
// Main Entry Point
// ============================================================================

int main(int argc, char* argv[])
{
    // ========== CLI Mode Detection (Desktop Only) ==========
    // Check for CLI commands before creating QApplication to avoid full GUI overhead.
    // CLI mode uses QGuiApplication (not QCoreApplication) because:
    // - PDF export needs to render ImageObjects which use QPixmap
    // - QPixmap requires a GUI application context (platform plugin)
    // - QGuiApplication is lightweight and doesn't create any windows
    //
    // IMPORTANT: This must happen BEFORE enableDebugConsole() on Windows.
    // In release builds, enableDebugConsole() calls FreeConsole() to hide the
    // console window in GUI mode, but that would also disconnect stdout/stderr
    // for CLI mode, causing all terminal output to be silently lost.
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS) && !defined(Q_OS_HARMONY)
    if (Cli::isCliMode(argc, argv)) {
        QGuiApplication app(argc, argv);
        app.setOrganizationName("SpeedyNote");
        app.setApplicationName("App");
        return Cli::run(app, argc, argv);
    }
#endif

#ifdef Q_OS_WIN
    enableDebugConsole();
#endif

    // ========== GUI Mode ==========
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif
#endif

    QApplication app(argc, argv);
    app.setOrganizationName("SpeedyNote");
    app.setApplicationName("App");

#ifdef Q_OS_WIN
    applyWindowsPalette(app);
    applyWindowsFonts(app);
#endif

#ifdef Q_OS_ANDROID
    logAndroidPaths();
    applyAndroidPalette(app);
    applyAndroidFonts(app);
#elif defined(Q_OS_IOS)
    IOSPlatformHelper::applyPalette(app);
    IOSPlatformHelper::applyFonts(app);
    IOSPlatformHelper::installKeyboardFilter(app);
    IOSTouchTracker::install();
#elif defined(Q_OS_HARMONY)
    // Probe user-folder access once, up front. Whether this succeeds decides
    // where file dialogs open and whether saving outside the sandbox works at
    // all, and it is the platform's most common failure mode, so the result is
    // worth having in the log of every run rather than only after a failed save.
    // HarmonyEnvironment caches it, so this also keeps the dialogs snappy.
    qInfo() << "HarmonyOS: documents root =" << HarmonyEnvironment::writableDocumentsRoot()
            << "(user folder access:" << HarmonyEnvironment::hasUserDocumentsAccess() << ")";
    // Logged because several dialogs still default to it, and on this platform it
    // names a directory that exists and is listable but is not necessarily writable.
    qInfo() << "HarmonyOS: QStandardPaths DocumentsLocation ="
            << QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    // Probed here rather than where it is used so that every run records the
    // answer, including runs on devices that have user folders and never need it.
    // It is one query against a service, and we would otherwise be guessing at
    // which retail devices support persistence from a documentation set that
    // contradicts itself.
    HarmonyPersistentGrant::isSupported();
#endif

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    app.installEventFilter(new MobileDialogFilter(&app));
#endif

#ifdef Q_OS_HARMONY
    app.installEventFilter(new HarmonyDialogCentring(&app));
    app.installEventFilter(new HarmonyModalKeeper(&app));
#endif

    QTranslator translator;
    loadTranslations(app, translator);

    // ========== Initialize Tool Settings ==========
    // Note: the old global minimum stroke width has moved to per-preset state
    // on PenSubToolbar.  The legacy `tools/minStrokeWidth` key is read there
    // as a one-shot migration seed and requires no bootstrap here.
    {
        QSettings toolSettings("SpeedyNote", "App");
        DocumentViewport::setWheelScrollSpeed(
            toolSettings.value("tools/wheelScrollSpeed", 40.0).toDouble());
        DocumentViewport::setPanOutsidePagesEnabled(
            toolSettings.value("tools/panOutsidePages", true).toBool());
    }

    // ========== Initialize System Notifications ==========
    // Step 3.11: Initialize notification system for export/import completion
    // On Android: Creates notification channel (required for Android 8.0+)
    // On Linux: Initializes DBus connection for desktop notifications
    SystemNotification::initialize();
    
    // Request notification permission on Android 13+
    // This shows the permission dialog if not already granted
    if (!SystemNotification::hasPermission()) {
        SystemNotification::requestPermission();
    }

    // ========== Parse Command Line Arguments ==========
    QString inputFile;
    bool createNewPackage = false;

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS) && defined(SPEEDYNOTE_DEBUG)
    QString testToRun;
    bool runButtonVisualTest = false;
    bool runViewportTests = false;
#endif

    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromLocal8Bit(argv[i]);

        if (arg == "--create-new" && i + 1 < argc) {
            createNewPackage = true;
            inputFile = QString::fromLocal8Bit(argv[++i]);
        }
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS) && defined(SPEEDYNOTE_DEBUG)
        else if (arg == "--test-page") {
            testToRun = "page";
        } else if (arg == "--test-document") {
            testToRun = "document";
        } else if (arg == "--test-notebooklibrary") {
            testToRun = "notebooklibrary";
        } else if (arg == "--test-sandboxpdf") {
            testToRun = "sandboxpdf";
        } else if (arg == "--test-viewport-unit") {
            testToRun = "viewport-unit";
        } else if (arg == "--test-viewport") {
            runViewportTests = true;
        } else if (arg == "--test-buttons") {
            testToRun = "buttons";
        } else if (arg == "--test-buttons-visual") {
            runButtonVisualTest = true;
        } else if (arg == "--test-linkobject") {
            testToRun = "linkobject";
        } else if (arg == "--test-textboxobject") {
            testToRun = "textboxobject";
        } else if (arg == "--test-pdfexporter") {
            testToRun = "pdfexporter";
        } else if (arg == "--test-actionbars") {
            testToRun = "actionbars";
        } else if (arg == "--test-ocr-raster") {
            testToRun = "ocr-raster";
        } else if (arg == "--test-ocr-golden") {
            testToRun = "ocr-golden";
        } else if (arg == "--test-ocr-vision") {
            testToRun = "ocr-vision";
        } else if (arg == "--test-ocr-paddle") {
            testToRun = "ocr-paddle";
        }
#endif
        else if (!arg.startsWith("--") && inputFile.isEmpty()) {
            inputFile = arg;
        }
    }

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS) && defined(SPEEDYNOTE_DEBUG)
    // Handle test commands
    if (!testToRun.isEmpty()) {
        return runTests(testToRun);
    }

    if (runViewportTests) {
        int result = DocumentViewportTests::runVisualTest();
        return result;
    }

    if (runButtonVisualTest) {
        auto* testWidget = new ToolbarButtonTestWidget();
        testWidget->setAttribute(Qt::WA_DeleteOnClose);
        testWidget->show();
        int result = app.exec();
        return result;
    }
#endif

    // ========== Single Instance Check ==========
    if (MainWindow::isInstanceRunning()) {
        if (!inputFile.isEmpty()) {
            QString command = createNewPackage
                ? QString("--create-new|%1").arg(inputFile)
                : inputFile;

            if (MainWindow::sendToExistingInstance(command)) {
                return 0;
            }
        }
        return 0;
    }

    // ========== macOS File Open Handler ==========
#if defined(Q_OS_MACOS)
    // MAC.2: Instantiate the global system menu bar before any MainWindow.
    // The QMenuBar inside MacMenuBar is parent-less so it stays alive for
    // the QApplication's lifetime and remains visible when no window is open.
    MacMenuBar::instance();

    FileOpenEventFilter fileOpenFilter;
    app.installEventFilter(&fileOpenFilter);
#endif

    // ========== iOS Inbox Watcher ==========
#if defined(Q_OS_IOS)
    IOSInboxWatcher inboxWatcher;
#endif

    // ========== Session Restore ==========
    QSettings sessionSettings("SpeedyNote", "App");
    QStringList sessionTabs = sessionSettings.value("session/lastOpenTabs").toStringList();
    int sessionActiveIndex = sessionSettings.value("session/activeTabIndex", 0).toInt();

    // Clear immediately so a crash during restore doesn't loop
    sessionSettings.remove("session/lastOpenTabs");
    sessionSettings.remove("session/activeTabIndex");
    sessionSettings.sync();

    // Filter out files that no longer exist
    sessionTabs.erase(std::remove_if(sessionTabs.begin(), sessionTabs.end(),
        [](const QString& p) { return !QFileInfo::exists(p); }), sessionTabs.end());

    // If launching with a file argument, remove it from session list to avoid duplicate
    bool inputFileWasInSession = false;
    if (!inputFile.isEmpty() && !sessionTabs.isEmpty()) {
        QString normalizedInput = QFileInfo(inputFile).absoluteFilePath();
        int sizeBefore = sessionTabs.size();
        sessionTabs.erase(std::remove_if(sessionTabs.begin(), sessionTabs.end(),
            [&normalizedInput](const QString& p) {
                return QFileInfo(p).absoluteFilePath() == normalizedInput;
            }), sessionTabs.end());
        inputFileWasInSession = (sessionTabs.size() < sizeBefore);
    }

    // ========== Launch Application ==========
    // Always create the Launcher upfront. On macOS this also show()s it
    // briefly to prime NSApp activation; on other platforms it's hidden
    // until a branch decides to surface it. See createLauncherForColdStart.
    auto* launcher = createLauncherForColdStart();

    // Local helpers fold the per-branch platform-conditional registration
    // into one place. fileOpenFilter / inboxWatcher are local-to-main and
    // platform-conditional themselves, hence the lambdas (a free function
    // would force ifdef'd parameters).
    auto registerMainWindowWithPlatform = [&](MainWindow* w) {
#if defined(Q_OS_MACOS)
        fileOpenFilter.setMainWindow(w);
#elif defined(Q_OS_IOS)
        inboxWatcher.setMainWindow(w);
        QTimer::singleShot(0, []{ IOSPlatformHelper::disableEditMenuOverlay(); });
#endif
        (void)w;
    };
    auto registerLauncherWithPlatform = [&](Launcher* l) {
#if defined(Q_OS_MACOS)
        fileOpenFilter.setLauncher(l);
#elif defined(Q_OS_IOS)
        inboxWatcher.setLauncher(l);
        QTimer::singleShot(0, []{ IOSPlatformHelper::disableEditMenuOverlay(); });
#endif
        (void)l;
    };

    // Parent for the session-restore prompt in the no-file branch.
    // On macOS the launcher is visible (priming pass), so we get a real
    // sheet/window-modal dialog. On other platforms the launcher is
    // hidden, so a hidden parent would weaken modality - keep nullptr
    // (preserves the original application-modal behavior).
    //
    // HarmonyOS has no choice in the matter: a parentless prompt here is fatal,
    // not merely less modal. Every top-level Qt window is backed by an ability
    // instance, and starting one requires the app to be in the foreground --
    // which it stops being the moment it has no window at all. So a prompt shown
    // before any window exists would take the app to the background when it
    // closed, and the show() immediately after would abort inside libqohos.so
    // with "Failed to start the Ability with instance id: 1", killing the app
    // whichever button was pressed. The launcher is therefore shown up front
    // (see createLauncherForColdStart) and owns this dialog, which also suits a
    // platform that downgrades Qt::ApplicationModal to WindowModal anyway.
#if defined(Q_OS_MACOS) || defined(Q_OS_HARMONY)
    QWidget* sessionPromptParent = launcher;
#else
    QWidget* sessionPromptParent = nullptr;
#endif

    if (!inputFile.isEmpty()) {
        // File argument provided - open in MainWindow.
        auto* w = new MainWindow();
        w->setAttribute(Qt::WA_DeleteOnClose);
        showMainWindowAtColdStart(w, launcher);
        w->openFileInNewTab(inputFile);

        if (!sessionTabs.isEmpty()) {
            auto reply = QMessageBox::question(w,
                QObject::tr("Restore Previous Session"),
                QObject::tr("You had %1 other tab(s) open last time. Restore them?")
                    .arg(sessionTabs.size()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (reply == QMessageBox::Yes) {
                for (const QString& path : sessionTabs)
                    w->openFileInNewTab(path);
                int adjustedIndex = inputFileWasInSession
                    ? sessionActiveIndex : sessionActiveIndex + 1;
                if (adjustedIndex >= 0 && adjustedIndex < w->tabCount())
                    w->switchToTabIndex(adjustedIndex);
            }
        }

        registerMainWindowWithPlatform(w);
    } else if (!sessionTabs.isEmpty()) {
        // No file, but previous session exists - ask to restore.
        auto reply = QMessageBox::question(sessionPromptParent,
            QObject::tr("Restore Previous Session"),
            QObject::tr("You had %1 tab(s) open last time. Restore them?")
                .arg(sessionTabs.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

        if (reply == QMessageBox::Yes) {
            auto* w = new MainWindow();
            w->setAttribute(Qt::WA_DeleteOnClose);
            showMainWindowAtColdStart(w, launcher);
            for (const QString& path : sessionTabs)
                w->openFileInNewTab(path);
            if (sessionActiveIndex >= 0 && sessionActiveIndex < w->tabCount())
                w->switchToTabIndex(sessionActiveIndex);

            registerMainWindowWithPlatform(w);
        } else {
            // User declined - land on the Launcher.
            showLauncherAtColdStart(launcher);
            registerLauncherWithPlatform(launcher);
        }
    } else {
        // No file, no session - land on the Launcher.
        showLauncherAtColdStart(launcher);
        registerLauncherWithPlatform(launcher);
    }

    int exitCode = app.exec();

    return exitCode;
}
