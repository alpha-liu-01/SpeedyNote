#ifndef STARREDVIEW_H
#define STARREDVIEW_H

#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QMap>

class NotebookCard;
class FolderHeader;

/**
 * @brief iOS homescreen-style view for starred notebooks with folders.
 * 
 * StarredView displays starred notebooks organized in folders with an
 * "Unfiled" section for notebooks not assigned to any folder.
 * 
 * Features:
 * - Collapsible folder sections
 * - Grid layout of NotebookCards within each folder
 * - Long-press folder header for context menu (rename/delete)
 * - Touch-friendly scrolling
 * - Dark mode support
 * 
 * Folder structure (per Q&A):
 * - Single-level folders (no nesting)
 * - Each notebook in one folder or "unfiled"
 * - Drag-and-drop reordering (future task)
 * 
 * Phase P.3.5: Part of the new Launcher implementation.
 */
class StarredView : public QWidget {
    Q_OBJECT

public:
    explicit StarredView(QWidget* parent = nullptr);
    
    /**
     * @brief Reload data from NotebookLibrary.
     */
    void reload();
    
    /**
     * @brief Set dark mode for theming.
     */
    void setDarkMode(bool dark);

signals:
    /**
     * @brief Emitted when a notebook card is clicked.
     */
    void notebookClicked(const QString& bundlePath);
    
    /**
     * @brief Emitted when a notebook card is long-pressed.
     */
    void notebookLongPressed(const QString& bundlePath);
    
    /**
     * @brief Emitted when a folder header is long-pressed.
     */
    void folderLongPressed(const QString& folderName);

private:
    void setupUi();
    void setupTouchScrolling();
    void buildContent();
    void clearContent();
    
    QWidget* createFolderSection(const QString& folderName, 
                                 const QList<struct NotebookInfo>& notebooks);
    QWidget* createNotebookGrid(const QList<struct NotebookInfo>& notebooks);
    
    void onFolderToggled(const QString& folderName, bool collapsed);
    void onNotebookClicked(const QString& bundlePath);
    void onNotebookLongPressed(const QString& bundlePath);
    
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_scrollContent = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;
    
    // Track collapsed state per folder
    QMap<QString, bool> m_collapsedFolders;
    
    // Track folder section widgets for collapse/expand
    QMap<QString, QWidget*> m_folderGrids;
    
    bool m_darkMode = false;
    
    // Layout constants
    static constexpr int SECTION_SPACING = 16;
    static constexpr int GRID_SPACING = 12;
    static constexpr int CONTENT_MARGIN = 16;
};

/**
 * @brief Header widget for a folder section.
 * 
 * Displays folder name with expand/collapse chevron.
 * Long-press triggers context menu signal.
 */
class FolderHeader : public QWidget {
    Q_OBJECT

public:
    explicit FolderHeader(const QString& folderName, QWidget* parent = nullptr);
    
    QString folderName() const { return m_folderName; }
    
    void setCollapsed(bool collapsed);
    bool isCollapsed() const { return m_collapsed; }
    
    void setDarkMode(bool dark);

signals:
    void clicked();
    void longPressed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QString m_folderName;
    bool m_collapsed = false;
    bool m_darkMode = false;
    bool m_hovered = false;
    bool m_pressed = false;
    
    QTimer* m_longPressTimer = nullptr;
    QPoint m_pressPos;
    bool m_longPressTriggered = false;
    
    static constexpr int HEADER_HEIGHT = 44;
    static constexpr int LONG_PRESS_MS = 500;
    static constexpr int LONG_PRESS_MOVE_THRESHOLD = 10;
};

#endif // STARREDVIEW_H

