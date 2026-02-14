// ============================================================================
// SpeedyNote - Main Entry Point
// ============================================================================

#include <QApplication>
#include <QTranslator>
#include <QLocale>
#include <QSettings>
#include <QStandardPaths>
#include <QLibraryInfo>
#include <QFont>

#include "MainWindow.h"
#include "ui/launcher/Launcher.h"
#include "platform/SystemNotification.h"

// CLI support (Desktop only)
#ifndef Q_OS_ANDROID
#include <QGuiApplication>
#include "cli/CliParser.h"
#endif

// Platform-specific includes
#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#endif

// Controller support
#ifdef SPEEDYNOTE_CONTROLLER_SUPPORT
#include <SDL2/SDL.h>
#define SPEEDYNOTE_SDL_QUIT() SDL_Quit()
#else
#define SPEEDYNOTE_SDL_QUIT() ((void)0)
#endif

// Android helpers
#ifdef Q_OS_ANDROID
#include <QDebug>
#include <QPalette>
#include <QJniObject>

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
        QColor blue(0, 120, 215);
        
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
        lightPalette.setColor(QPalette::Link, blue);
        lightPalette.setColor(QPalette::LinkVisited, QColor(blue).darker());
        lightPalette.setColor(QPalette::Highlight, blue);
        lightPalette.setColor(QPalette::HighlightedText, Qt::white);
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
#if !defined(Q_OS_ANDROID) && defined(SPEEDYNOTE_DEBUG)
#include "core/PageTests.h"
#include "core/DocumentTests.h"
#include "core/DocumentViewportTests.h"
#include "ui/ToolbarButtonTests.h"
#include "objects/LinkObjectTests.h"
#include "pdf/MuPdfExporterTests.h"
#include "ui/ToolbarButtonTestWidget.h"
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
    font.setHintingPreference(QFont::PreferFullHinting);
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
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
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

    QString langCode;
    if (useSystemLanguage) {
        langCode = QLocale::system().name().section('_', 0, 0);
    } else {
        langCode = settings.value("languageOverride", "en").toString();
    }
    
    // Load Qt's base translations (for standard dialogs: Save, Cancel, etc.)
    // This must be loaded before the app translator so app translations take priority
    static QTranslator qtBaseTranslator;
    QString qtTranslationsPath = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    if (qtBaseTranslator.load("qtbase_" + langCode, qtTranslationsPath)) {
        app.installTranslator(&qtBaseTranslator);
    }

    // Load SpeedyNote's translations
    QStringList translationPaths = {
        QCoreApplication::applicationDirPath(),
        QCoreApplication::applicationDirPath() + "/translations",
        "/usr/share/speedynote/translations",
        "/usr/local/share/speedynote/translations",
        QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                               "speedynote/translations", QStandardPaths::LocateDirectory),
        ":/resources/translations"
    };

    for (const QString& path : translationPaths) {
        if (translator.load(path + "/app_" + langCode + ".qm")) {
            app.installTranslator(&translator);
            break;
        }
    }
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

// ============================================================================
// Test Runners (Desktop Debug Builds Only)
// ============================================================================

#if !defined(Q_OS_ANDROID) && defined(SPEEDYNOTE_DEBUG)
static int runTests(const QString& testType)
{
#ifdef Q_OS_WIN
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
#endif

    bool success = false;

    if (testType == "page") {
        success = PageTests::runAllTests();
    } else if (testType == "document") {
        success = DocumentTests::runAllTests();
    } else if (testType == "linkobject") {
        success = LinkObjectTests::runAllTests();
    } else if (testType == "pdfexporter") {
        success = MuPdfExporterTests::runAllTests();
    } else if (testType == "buttons") {
        return QTest::qExec(new ToolbarButtonTests());
    }

    SPEEDYNOTE_SDL_QUIT();
    return success ? 0 : 1;
}
#endif

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
#ifndef Q_OS_ANDROID
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
#ifdef SPEEDYNOTE_CONTROLLER_SUPPORT
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH, "1");
    SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK);
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
#endif

    QTranslator translator;
    loadTranslations(app, translator);

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

#if !defined(Q_OS_ANDROID) && defined(SPEEDYNOTE_DEBUG)
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
#if !defined(Q_OS_ANDROID) && defined(SPEEDYNOTE_DEBUG)
        else if (arg == "--test-page") {
            testToRun = "page";
        } else if (arg == "--test-document") {
            testToRun = "document";
        } else if (arg == "--test-viewport") {
            runViewportTests = true;
        } else if (arg == "--test-buttons") {
            testToRun = "buttons";
        } else if (arg == "--test-buttons-visual") {
            runButtonVisualTest = true;
        } else if (arg == "--test-linkobject") {
            testToRun = "linkobject";
        } else if (arg == "--test-pdfexporter") {
            testToRun = "pdfexporter";
        }
#endif
        else if (!arg.startsWith("--") && inputFile.isEmpty()) {
            inputFile = arg;
        }
    }

#if !defined(Q_OS_ANDROID) && defined(SPEEDYNOTE_DEBUG)
    // Handle test commands
    if (!testToRun.isEmpty()) {
        return runTests(testToRun);
    }

    if (runViewportTests) {
        int result = DocumentViewportTests::runVisualTest();
        SPEEDYNOTE_SDL_QUIT();
        return result;
    }

    if (runButtonVisualTest) {
        auto* testWidget = new ToolbarButtonTestWidget();
        testWidget->setAttribute(Qt::WA_DeleteOnClose);
        testWidget->show();
        int result = app.exec();
        SPEEDYNOTE_SDL_QUIT();
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
                SPEEDYNOTE_SDL_QUIT();
                return 0;
            }
        }
        SPEEDYNOTE_SDL_QUIT();
        return 0;
    }

    // ========== Launch Application ==========
    int exitCode = 0;

    if (!inputFile.isEmpty()) {
        // File argument provided - open directly in MainWindow
        auto* w = new MainWindow();
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
        w->openFileInNewTab(inputFile);
        exitCode = app.exec();
    } else {
        // No file - show Launcher
        auto* launcher = new Launcher();
        launcher->setAttribute(Qt::WA_DeleteOnClose);
        connectLauncherSignals(launcher);
        launcher->show();
        exitCode = app.exec();
    }

    SPEEDYNOTE_SDL_QUIT();
    return exitCode;
}
