#pragma once

// ============================================================================
// DebugOverlay - Floating debug information panel
// ============================================================================
// A modular, toggleable overlay that displays real-time debug information
// about the document viewport. Designed for development/debugging but can
// be easily disabled for production builds.
//
// Architecture:
// - MainWindow owns and manages the overlay
// - Overlay queries DocumentViewport for data
// - Uses a timer-based update for smooth 30 FPS display
// - Extensible: new debug sections can be added easily
//
// To disable in production: #define SPEEDYNOTE_NO_DEBUG_OVERLAY
// ============================================================================

#include <QWidget>
#include <QTimer>
#include <QPointer>
#include <functional>
#include <vector>

class DocumentViewport;
class Document;

/**
 * @brief A debug info section that can be dynamically added/removed.
 *
 * Each section has a name and a callback that generates the display text.
 * This allows external code to add custom debug info without modifying
 * the DebugOverlay class.
 */
struct DebugSection {
    QString name;                               ///< Section identifier (for removal)
    std::function<QString()> generator;         ///< Callback to generate display text
    bool enabled = true;                        ///< Whether this section is shown
};

/**
 * @brief Floating debug overlay that displays viewport information.
 *
 * Features:
 * - Auto-updates at 30 FPS when visible
 * - Semi-transparent background for readability
 * - Draggable to reposition
 * - Extensible via addSection() API
 * - Keyboard toggle (default: D key in viewport)
 */
class DebugOverlay : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Construct a DebugOverlay.
     * @param parent Parent widget (typically MainWindow or DocumentViewport's parent).
     */
    explicit DebugOverlay(QWidget* parent = nullptr);
    ~DebugOverlay() override;

    // ===== Viewport Connection =====

    /**
     * @brief Set the viewport to monitor.
     * @param viewport The DocumentViewport to query for debug data.
     *
     * Call this when the active viewport changes (e.g., tab switch).
     * Pass nullptr to disconnect.
     */
    void setViewport(DocumentViewport* viewport);

    /**
     * @brief Get the currently monitored viewport.
     */
    DocumentViewport* viewport() const { return m_viewport; }

    // ===== Toggle & Visibility =====

    /**
     * @brief Toggle overlay visibility.
     *
     * When hidden, the update timer stops to save CPU.
     */
    void toggle();

    /**
     * @brief Check if the overlay is currently shown.
     */
    bool isOverlayVisible() const { return isVisible(); }

    // ===== Extensibility =====

    /**
     * @brief Add a custom debug section.
     * @param name Unique identifier for this section.
     * @param generator Callback that returns the text to display.
     *
     * Example:
     * @code
     * overlay->addSection("Memory", []() {
     *     return QString("Heap: %1 MB").arg(getHeapUsage() / 1024 / 1024);
     * });
     * @endcode
     */
    void addSection(const QString& name, std::function<QString()> generator);

    /**
     * @brief Remove a custom debug section by name.
     * @param name The section identifier passed to addSection().
     */
    void removeSection(const QString& name);

    /**
     * @brief Enable or disable a section by name.
     * @param name The section identifier.
     * @param enabled Whether to show this section.
     */
    void setSectionEnabled(const QString& name, bool enabled);

    /**
     * @brief Clear all custom sections (keeps built-in sections).
     */
    void clearCustomSections();

    // ===== Configuration =====

    /**
     * @brief Set the update interval in milliseconds.
     * @param ms Update interval (default: 33ms = ~30 FPS).
     */
    void setUpdateInterval(int ms);

    /**
     * @brief Set background opacity.
     * @param alpha Opacity from 0 (transparent) to 255 (opaque). Default: 200.
     */
    void setBackgroundOpacity(int alpha);

signals:
    /**
     * @brief Emitted when the overlay is shown.
     */
    void shown();

    /**
     * @brief Emitted when the overlay is hidden.
     */
    void hidden();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    /**
     * @brief Update the displayed information.
     */
    void updateInfo();

private:
    /**
     * @brief Generate the built-in debug text for edgeless mode.
     */
    QString generateEdgelessInfo() const;

    /**
     * @brief Generate the built-in debug text for paged mode.
     */
    QString generatePagedInfo() const;

    /**
     * @brief Generate text for all custom sections.
     */
    QString generateCustomSections() const;

    /**
     * @brief Get the tool name string for display.
     */
    QString toolName() const;

    // ===== Members =====

    QPointer<DocumentViewport> m_viewport;      ///< The viewport to monitor
    QTimer m_updateTimer;                       ///< Timer for periodic updates
    
    std::vector<DebugSection> m_customSections; ///< User-added debug sections
    QString m_cachedText;                       ///< Pre-rendered debug text

    // Drag support
    bool m_dragging = false;
    QPoint m_dragOffset;

    // Style
    int m_backgroundOpacity = 200;
    QFont m_font;
};
