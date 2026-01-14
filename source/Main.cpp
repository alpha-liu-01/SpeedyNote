#ifdef _WIN32
#include <windows.h>
#endif

#include <QApplication>
#include <QPalette>
#include <QTranslator>
#include <QLocale>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include "MainWindow.h"
// #include "SpnPackageManager.h"  // TODO G.2: Re-enable after package format finalization

#include "core/PageTests.h" // Phase 1.1.7: Page unit tests
#include "core/DocumentTests.h" // Phase 1.2.8: Document unit tests
#include "core/DocumentViewportTests.h" // Phase 1.3.11: Viewport tests
#include "ui/ToolbarButtonTests.h" // Toolbar button unit tests
#include "objects/LinkObjectTests.h" // Phase C.1: LinkObject unit tests
#include "ui/ToolbarButtonTestWidget.h" // Toolbar button visual test

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#endif

#ifdef Q_OS_WIN
// Helper function to detect Windows dark mode
static bool isWindowsDarkMode() {
    QSettings settings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 
                       QSettings::NativeFormat);
    int appsUseLightTheme = settings.value("AppsUseLightTheme", 1).toInt();
    return (appsUseLightTheme == 0);
}

// Helper function to detect Windows 11
static bool isWindows11() {
    // Windows 11 is build 22000 or higher
    // Use QSysInfo to check Windows version
    return QSysInfo::kernelVersion().split('.')[2].toInt() >= 22000;
}
#endif

// Helper function to set nice Windows fonts (Segoe UI + Dengxian for Chinese)
static void applyWindowsFonts(QApplication &app) {
#ifdef Q_OS_WIN
    // Use Segoe UI as the primary font (Windows Metro/Fluent UI font)
    // with Dengxian (等线) as fallback for Chinese characters
    QFont font("Segoe UI", 9);
    font.setStyleHint(QFont::SansSerif);
    font.setHintingPreference(QFont::PreferFullHinting);
    
    // Set font families with fallbacks for Chinese text
    // Priority: Segoe UI -> Dengxian (等线) -> Microsoft YaHei (微软雅黑) -> system default
    QStringList fontFamilies;
    fontFamilies << "Segoe UI" << "Dengxian" << "Microsoft YaHei" << "SimHei";
    font.setFamilies(fontFamilies);
    
    app.setFont(font);
#else
    Q_UNUSED(app);
#endif
}

// Helper function to apply dark/light palette to Qt application
static void applySystemPalette(QApplication &app) {
#ifdef Q_OS_WIN
    // Windows 11 has native dark/light mode support with WinUI 3, so use default style
    // For older Windows versions, use Fusion style for proper dark mode support
    if (!isWindows11()) {
        if (isWindowsDarkMode()) {
            app.setStyle("Fusion");
        } else {
            app.setStyle("windowsvista");
        }
    }

    if (isWindowsDarkMode()) {
        // Create a comprehensive dark palette for Qt widgets
        QPalette darkPalette;

        // Base colors
        QColor darkGray(53, 53, 53);
        QColor gray(128, 128, 128);
        QColor black(25, 25, 25);
        QColor blue(42, 130, 218);
        QColor lightGray(180, 180, 180);

        // Window colors (main background)
        darkPalette.setColor(QPalette::Window, QColor(45, 45, 45));
        darkPalette.setColor(QPalette::WindowText, Qt::white);

        // Base (text input background) colors
        darkPalette.setColor(QPalette::Base, QColor(35, 35, 35));
        darkPalette.setColor(QPalette::AlternateBase, darkGray);
        darkPalette.setColor(QPalette::Text, Qt::white);

        // Tooltip colors
        darkPalette.setColor(QPalette::ToolTipBase, QColor(60, 60, 60));
        darkPalette.setColor(QPalette::ToolTipText, Qt::white);

        // Button colors (critical for dialogs)
        darkPalette.setColor(QPalette::Button, darkGray);
        darkPalette.setColor(QPalette::ButtonText, Qt::white);

        // 3D effects and borders (critical for proper widget rendering)
        darkPalette.setColor(QPalette::Light, QColor(80, 80, 80));
        darkPalette.setColor(QPalette::Midlight, QColor(65, 65, 65));
        darkPalette.setColor(QPalette::Dark, QColor(35, 35, 35));
        darkPalette.setColor(QPalette::Mid, QColor(50, 50, 50));
        darkPalette.setColor(QPalette::Shadow, QColor(20, 20, 20));

        // Bright text
        darkPalette.setColor(QPalette::BrightText, Qt::red);

        // Link colors
        darkPalette.setColor(QPalette::Link, blue);
        darkPalette.setColor(QPalette::LinkVisited, QColor(blue).lighter());

        // Highlight colors (selection)
        darkPalette.setColor(QPalette::Highlight, blue);
        darkPalette.setColor(QPalette::HighlightedText, Qt::white);

        // Placeholder text (for line edits, spin boxes, etc.)
        darkPalette.setColor(QPalette::PlaceholderText, gray);

        // Disabled colors (all color groups)
        darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, gray);
        darkPalette.setColor(QPalette::Disabled, QPalette::Text, gray);
        darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, gray);
        darkPalette.setColor(QPalette::Disabled, QPalette::Base, QColor(50, 50, 50));
        darkPalette.setColor(QPalette::Disabled, QPalette::Button, QColor(50, 50, 50));
        darkPalette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(80, 80, 80));

        app.setPalette(darkPalette);
    } else {
        // Use default Windows style and palette for light mode
        app.setPalette(QPalette());
    }
#else
    // On Linux, Qt usually handles this correctly via the desktop environment
    // So we don't override the palette
#endif
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
    // FreeConsole();  // Hide console safely on Windows

    
    // DEBUG: Show console for trackpad gesture debugging

    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    
    
#endif
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH, "1");
    SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK);

    /*
    qDebug() << "SDL2 version:" << SDL_GetRevision();
    qDebug() << "Num Joysticks:" << SDL_NumJoysticks();

    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            qDebug() << "Controller" << i << "is" << SDL_GameControllerNameForIndex(i);
        } else {
            qDebug() << "Joystick" << i << "is not a recognized controller";
        }
    }
    */  // For sdl2 debugging
    


    // Enable Windows IME support for multi-language input
    QApplication app(argc, argv);
    
    // High DPI scaling is automatically enabled in Qt 6+
    // No need to set AA_EnableHighDpiScaling or AA_UseHighDpiPixmaps

    // Apply system-appropriate palette (dark/light) on Windows
    applySystemPalette(app);
    
    // Apply nice Windows fonts (Segoe UI + Dengxian for Chinese)
    applyWindowsFonts(app);

    // TODO G.2: Re-enable after package format finalization
    // SpnPackageManager::cleanupOrphanedTempDirs();
    
    QTranslator translator;
    
    // Check for manual language override
    QSettings settings("SpeedyNote", "App");
    bool useSystemLanguage = settings.value("useSystemLanguage", true).toBool();
    QString langCode;
    
    if (useSystemLanguage) {
        // Use system language
        QString locale = QLocale::system().name(); // e.g., "zh_CN", "es_ES"
        langCode = locale.section('_', 0, 0); // e.g., "zh"
    } else {
        // Use manual override
        langCode = settings.value("languageOverride", "en").toString();
    }

    // Debug: Uncomment these lines to debug translation loading issues
    // printf("Locale: %s\n", locale.toStdString().c_str());
    // printf("Language Code: %s\n", langCode.toStdString().c_str());

    // Try multiple paths to find translation files
    QStringList translationPaths = {
        QCoreApplication::applicationDirPath(),  // Same directory as executable (Windows/portable)
        QCoreApplication::applicationDirPath() + "/translations",  // translations subdirectory (Windows)
        "/usr/share/speedynote/translations",  // Standard Linux installation path
        "/usr/local/share/speedynote/translations",  // Local Linux installation path
        QStandardPaths::locate(QStandardPaths::GenericDataLocation, "speedynote/translations", QStandardPaths::LocateDirectory),  // XDG data directories
        ":/resources/translations"  // Qt resource system (fallback)
    };
    
    bool translationLoaded = false;
    for (const QString &path : translationPaths) {
        QString translationFile = path + "/app_" + langCode + ".qm";
        if (translator.load(translationFile)) {
            app.installTranslator(&translator);
            translationLoaded = true;
            // Debug: Uncomment this line to see which translation file was loaded
            // printf("Translation loaded from: %s\n", translationFile.toStdString().c_str());
            break;
        }
    }
    
    if (!translationLoaded) {
        // Debug: Uncomment this line to see when translation loading fails
        // printf("No translation file found for language: %s\n", langCode.toStdString().c_str());
    }

    QString inputFile;
    bool createNewPackage = false;
    bool createSilent = false;
    bool runPageTests = false;
    bool runDocumentTests = false;
    bool runViewportTests = false;
    bool runButtonTests = false;
    bool runButtonVisualTest = false;
    bool runLinkObjectTests = false;
    // REMOVED Phase 3.1: useNewViewport - always using new architecture
    
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromLocal8Bit(argv[i]);
        
        // REMOVED Phase 3.1: --use-new-viewport flag - always using new architecture
        if (arg == "--create-new" && i + 1 < argc) {
            // Handle --create-new command (opens SpeedyNote)
            createNewPackage = true;
            inputFile = QString::fromLocal8Bit(argv[++i]);
        } else if (arg == "--create-silent" && i + 1 < argc) {
            // Handle --create-silent command (creates file and exits)
            createSilent = true;
            inputFile = QString::fromLocal8Bit(argv[++i]);
        } else if (arg == "--test-page") {
            // Phase 1.1.7: Run Page unit tests
            runPageTests = true;
        } else if (arg == "--test-document") {
            // Phase 1.2.8: Run Document unit tests
            runDocumentTests = true;
        } else if (arg == "--test-viewport") {
            // Phase 1.3.11: Run DocumentViewport visual test
            runViewportTests = true;
        } else if (arg == "--test-buttons") {
            // Run toolbar button unit tests
            runButtonTests = true;
        } else if (arg == "--test-buttons-visual") {
            // Run toolbar button visual test widget
            runButtonVisualTest = true;
        } else if (arg == "--test-linkobject") {
            // Phase C.1: Run LinkObject unit tests
            runLinkObjectTests = true;
        } else if (!arg.startsWith("--") && inputFile.isEmpty()) {
            // Regular file argument (first non-flag argument)
            inputFile = arg;
        }
    }
    // qDebug() << "Input file received:" << inputFile << "Create new:" << createNewPackage << "Create silent:" << createSilent;
    
    // Phase 1.1.7: Handle --test-page command
    if (runPageTests) {
#ifdef _WIN32
        // Re-enable console for test output on Windows
        AllocConsole();
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
#endif
        bool success = PageTests::runAllTests();
        SDL_Quit();
        return success ? 0 : 1;
    }
    
    // Phase 1.2.8: Handle --test-document command
    if (runDocumentTests) {
#ifdef _WIN32
        // Re-enable console for test output on Windows
        AllocConsole();
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
#endif
        bool success = DocumentTests::runAllTests();
        SDL_Quit();
        return success ? 0 : 1;
    }
    
    // Phase 1.3.11: Handle --test-viewport command
    if (runViewportTests) {
#ifdef _WIN32
        // Re-enable console for test output on Windows
        AllocConsole();
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
#endif
        int result = DocumentViewportTests::runVisualTest();
        SDL_Quit();
        return result;
    }
    
    // Handle --test-buttons command (unit tests)
    if (runButtonTests) {
#ifdef _WIN32
        AllocConsole();
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
#endif
        int result = runButtonTests ? runButtonTests : 0;
        result = runButtonTests ? QTest::qExec(new ToolbarButtonTests()) : 0;
        SDL_Quit();
        return result;
    }
    
    // Handle --test-buttons-visual command (visual test widget)
    if (runButtonVisualTest) {
        ToolbarButtonTestWidget *testWidget = new ToolbarButtonTestWidget();
        testWidget->setAttribute(Qt::WA_DeleteOnClose);
        testWidget->show();
        int result = app.exec();
        SDL_Quit();
        return result;
    }
    
    // Phase C.1: Handle --test-linkobject command
    if (runLinkObjectTests) {
#ifdef _WIN32
        // Re-enable console for test output on Windows
        AllocConsole();
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
#endif
        bool success = LinkObjectTests::runAllTests();
        SDL_Quit();
        return success ? 0 : 1;
    }

    // TODO G.2: Re-enable after package format finalization
    // Handle silent creation (context menu) - create file and exit immediately
    // if (createSilent && !inputFile.isEmpty()) {
    //     ... SpnPackageManager-based creation disabled ...
    // }
    Q_UNUSED(createSilent);

    // Check if another instance is already running
    if (MainWindow::isInstanceRunning()) {
        if (!inputFile.isEmpty()) {
            // Prepare command for existing instance
            QString command;
            if (createNewPackage) {
                command = QString("--create-new|%1").arg(inputFile);
            } else {
                command = inputFile;
            }
            
            // Send command to existing instance
            if (MainWindow::sendToExistingInstance(command)) {
                SDL_Quit(); // Clean up SDL before exiting
                return 0; // Exit successfully, command sent to existing instance
            }
        }
        // If no command to send or sending failed, just exit
        SDL_Quit(); // Clean up SDL before exiting
        return 0;
    }

    // Phase 3.1: Skip LauncherWindow, go directly to MainWindow
    // REMOVED: MainWindow::s_useNewViewport - always using new architecture
    
    // Determine which window to show based on command line arguments
    int exitCode = 0;
    
    // Phase 3.1: Always go to MainWindow (LauncherWindow will be reconnected later)
    MainWindow *w = new MainWindow();
    w->setAttribute(Qt::WA_DeleteOnClose); // Qt will delete when closed
    w->show();
    
    // Phase 3.1: File handling is stubbed for now
    // TODO Phase 3.5: Reconnect file operations
    if (!inputFile.isEmpty()) {
        qDebug() << "File argument received but file operations are stubbed:" << inputFile;
        // OLD: w->openSpnPackage(inputFile) or w->openPdfFile(inputFile)
    }
    
    exitCode = app.exec();
    
    /* Phase 3.1: LauncherWindow code commented out - will be reconnected later
    if (!inputFile.isEmpty()) {
        // If a file is specified, go directly to MainWindow
        MainWindow *w = new MainWindow();
        w->setAttribute(Qt::WA_DeleteOnClose);
        
        if (createNewPackage) {
            if (inputFile.toLower().endsWith(".spn")) {
                w->show();
                w->createNewSpnPackage(inputFile);
            } else {
                w->show();
            }
        } else {
            if (inputFile.toLower().endsWith(".pdf")) {
                w->show();
                w->openPdfDocument(inputFile);
            } else if (inputFile.toLower().endsWith(".spn")) {
                w->show();
                w->openSpnPackage(inputFile);
            } else {
                w->show();
            }
        }
        exitCode = app.exec();
    } else {
        // No file specified - show the launcher window
        LauncherWindow *launcher = new LauncherWindow();
        MainWindow::sharedLauncher = launcher;
        launcher->show();
        exitCode = app.exec();
    }
    */
    
    // Clean up SDL before exiting to properly release HID device handles
    // This is especially important on macOS where HID handles can remain locked
    SDL_Quit();
    
    return exitCode;
}
