// ============================================================================
// SpeedyNote - Main Entry Point
// ============================================================================

#include <QApplication>
#include <QTranslator>
#include <QLocale>
#include <QSettings>
#include <QStandardPaths>
#include <QFont>

#include "MainWindow.h"
#include "ui/launcher/Launcher.h"

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
        QColor blue(42, 130, 218);
        
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
#endif

// Test includes (desktop only)
#ifndef Q_OS_ANDROID
#include "core/PageTests.h"
#include "core/DocumentTests.h"
#include "core/DocumentViewportTests.h"
#include "ui/ToolbarButtonTests.h"
#include "objects/LinkObjectTests.h"
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
        QColor blue(42, 130, 218);

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
}

// ============================================================================
// Test Runners (Desktop Only)
// ============================================================================

#ifndef Q_OS_ANDROID
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
#ifdef Q_OS_WIN
    enableDebugConsole();
#endif

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
#endif

    QTranslator translator;
    loadTranslations(app, translator);

    // ========== Parse Command Line Arguments ==========
    QString inputFile;
    bool createNewPackage = false;

#ifndef Q_OS_ANDROID
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
#ifndef Q_OS_ANDROID
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
        }
#endif
        else if (!arg.startsWith("--") && inputFile.isEmpty()) {
            inputFile = arg;
        }
    }

#ifndef Q_OS_ANDROID
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
