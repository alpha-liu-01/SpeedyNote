#ifndef EXPORTPROGRESSWIDGET_H
#define EXPORTPROGRESSWIDGET_H

/**
 * @file ExportProgressWidget.h
 * @brief Floating progress widget for batch export operations.
 * 
 * Part of Phase 3: Launcher UI Integration for batch operations.
 * 
 * Features:
 * - Floating widget positioned in bottom-right corner
 * - Shows progress during export (filename, current/total, queued jobs)
 * - Shows completion state with success/skip/fail counts
 * - Auto-dismisses after 5 seconds on completion
 * - "Details" button to view full results
 * - Dark mode support via ThemeColors
 * 
 * @see docs/private/BATCH_OPERATIONS.md
 */

#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;
class QPropertyAnimation;
class QGraphicsOpacityEffect;

/**
 * @brief Floating progress widget for batch export operations.
 * 
 * Displays in the bottom-right corner of its parent widget to show
 * export progress without blocking user interaction.
 * 
 * States:
 * - Hidden: Widget is not visible
 * - Progress: Showing current export progress
 * - Complete: Showing summary, auto-dismisses after timeout
 * - Error: Showing error message
 * 
 * Usage:
 * @code
 * ExportProgressWidget* progress = new ExportProgressWidget(parentWidget);
 * progress->showProgress("MathBook.snb", 1, 10, 2);  // 1/10, 2 queued
 * // ... later ...
 * progress->showComplete(8, 1, 1);  // 8 success, 1 skipped, 1 failed
 * @endcode
 */
class ExportProgressWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)

public:
    /**
     * @brief Construct the progress widget.
     * @param parent Parent widget (widget positions relative to parent)
     */
    explicit ExportProgressWidget(QWidget* parent = nullptr);
    ~ExportProgressWidget();
    
    /**
     * @brief Show export progress.
     * @param currentFile Name of file currently being exported
     * @param current Current file number (1-based)
     * @param total Total number of files
     * @param queuedJobs Number of additional queued export jobs (0 if none)
     */
    void showProgress(const QString& currentFile, int current, int total, int queuedJobs = 0);
    
    /**
     * @brief Show completion state.
     * @param successCount Number of successfully exported files
     * @param failCount Number of failed exports
     * @param skipCount Number of skipped files (e.g., edgeless)
     */
    void showComplete(int successCount, int failCount, int skipCount);
    
    /**
     * @brief Show error state.
     * @param message Error message to display
     */
    void showError(const QString& message);
    
    /**
     * @brief Hide the widget (with optional animation).
     * @param animated If true, fade out before hiding
     */
    void dismiss(bool animated = true);
    
    /**
     * @brief Set dark mode appearance.
     * @param dark True for dark mode
     */
    void setDarkMode(bool dark);
    
    /**
     * @brief Get current opacity (for animation).
     */
    qreal opacity() const { return m_opacity; }
    
    /**
     * @brief Set opacity (for animation).
     */
    void setOpacity(qreal opacity);

signals:
    /**
     * @brief Emitted when user clicks the Details button.
     * 
     * The parent should show the full results dialog.
     */
    void detailsRequested();
    
    /**
     * @brief Emitted when the widget is dismissed (hidden).
     */
    void dismissed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onDismissTimerExpired();
    void onDetailsClicked();
    void onFadeAnimationFinished();

private:
    void setupUi();
    void positionInCorner();
    void startDismissTimer();
    void stopDismissTimer();
    void fadeIn();
    void fadeOut();
    
    // State
    enum class State {
        Hidden,
        Progress,
        Complete,
        Error
    };
    State m_state = State::Hidden;
    
    // Appearance
    bool m_darkMode = false;
    qreal m_opacity = 1.0;
    
    // Timers and animation
    QTimer* m_dismissTimer = nullptr;
    QPropertyAnimation* m_fadeAnimation = nullptr;
    QGraphicsOpacityEffect* m_opacityEffect = nullptr;
    
    // UI elements
    QLabel* m_iconLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_detailLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QPushButton* m_detailsButton = nullptr;
    
    // Last shown values (for re-display after theme change)
    int m_lastSuccess = 0;
    int m_lastFail = 0;
    int m_lastSkip = 0;
    
    // Constants
    static constexpr int WIDGET_WIDTH = 320;
    static constexpr int WIDGET_MIN_HEIGHT = 70;
    static constexpr int CORNER_MARGIN = 16;
    static constexpr int CORNER_RADIUS = 12;
    static constexpr int DISMISS_TIMEOUT_MS = 5000;  // 5 seconds
    static constexpr int FADE_DURATION_MS = 200;
};

#endif // EXPORTPROGRESSWIDGET_H
