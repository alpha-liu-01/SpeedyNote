#include "CliSignal.h"

#include <QTextStream>

// Platform-specific includes
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <csignal>
#endif

/**
 * @file CliSignal.cpp
 * @brief Implementation of signal handling for CLI.
 * 
 * @see CliSignal.h for API documentation
 */

namespace Cli {

// Global cancellation flag - set by signal handler
static std::atomic<bool> g_cancelled(false);

// Flag to track if we've already printed the cancellation message
static std::atomic<bool> g_cancelMessagePrinted(false);

// =============================================================================
// Platform-specific Signal Handlers
// =============================================================================

#ifdef Q_OS_WIN

/**
 * Windows console control handler.
 * Called when Ctrl+C, Ctrl+Break, or window close events occur.
 */
static BOOL WINAPI consoleCtrlHandler(DWORD ctrlType)
{
    switch (ctrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
            // Set cancellation flag
            g_cancelled = true;
            
            // Print message (only once)
            if (!g_cancelMessagePrinted.exchange(true)) {
                // Use stderr to avoid interfering with JSON output
                QTextStream err(stderr);
                err << "\nCancellation requested. Finishing current file...\n";
                err.flush();
            }
            
            // Return TRUE to indicate we handled the signal
            // This prevents the default handler from terminating the process
            return TRUE;
            
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            // For these events, allow default handling (process termination)
            return FALSE;
            
        default:
            return FALSE;
    }
}

void installSignalHandlers()
{
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);
}

#else // Unix/Linux

/**
 * Unix signal handler for SIGINT.
 * Must be async-signal-safe - only set atomic flag, no I/O.
 */
static void sigintHandler(int signal)
{
    (void)signal;  // Unused parameter
    
    // Set cancellation flag (atomic operation is async-signal-safe)
    g_cancelled = true;
    
    // Note: We can't safely do I/O in a signal handler.
    // The message will be printed when the operation checks the flag.
}

void installSignalHandlers()
{
    // Install SIGINT handler
    struct sigaction sa;
    sa.sa_handler = sigintHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // No SA_RESTART - we want interrupted syscalls to return
    
    sigaction(SIGINT, &sa, nullptr);
    
    // Also handle SIGTERM for graceful shutdown
    sigaction(SIGTERM, &sa, nullptr);
}

#endif // Platform-specific

// =============================================================================
// Public API
// =============================================================================

std::atomic<bool>* getCancellationFlag()
{
    return &g_cancelled;
}

bool wasCancelled()
{
    return g_cancelled.load();
}

void resetCancellation()
{
    g_cancelled = false;
    g_cancelMessagePrinted = false;
}

} // namespace Cli
