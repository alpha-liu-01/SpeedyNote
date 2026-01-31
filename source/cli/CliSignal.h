#ifndef CLISIGNAL_H
#define CLISIGNAL_H

/**
 * @file CliSignal.h
 * @brief Signal handling for CLI batch operations.
 * 
 * Provides graceful handling of Ctrl+C (SIGINT) during batch operations.
 * When the user presses Ctrl+C, the cancellation flag is set, allowing
 * operations to finish the current file and report partial results.
 * 
 * Platform support:
 * - Unix/Linux: Uses signal() for SIGINT
 * - Windows: Uses SetConsoleCtrlHandler() for CTRL_C_EVENT
 * 
 * @see docs/private/BATCH_OPERATIONS.md for design documentation
 */

#include <atomic>

namespace Cli {

/**
 * @brief Install signal handlers for graceful cancellation.
 * 
 * Sets up handlers for:
 * - SIGINT (Ctrl+C on Unix/Linux)
 * - CTRL_C_EVENT (Ctrl+C on Windows)
 * 
 * When triggered, sets the cancellation flag that can be checked
 * by batch operations.
 * 
 * Should be called once at CLI startup, before any batch operations.
 */
void installSignalHandlers();

/**
 * @brief Get pointer to the cancellation flag.
 * 
 * Returns a pointer to an atomic boolean that is set to true
 * when Ctrl+C is pressed. Pass this to batch operations to enable
 * graceful cancellation.
 * 
 * @return Pointer to the cancellation flag (never null)
 */
std::atomic<bool>* getCancellationFlag();

/**
 * @brief Check if cancellation was requested.
 * 
 * Convenience function to check the cancellation flag.
 * 
 * @return true if Ctrl+C was pressed
 */
bool wasCancelled();

/**
 * @brief Reset the cancellation flag.
 * 
 * Resets the flag to false. Useful if running multiple
 * batch operations in sequence.
 */
void resetCancellation();

} // namespace Cli

#endif // CLISIGNAL_H
