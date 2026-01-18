#ifndef FLOATINGACTIONBUTTON_H
#define FLOATINGACTIONBUTTON_H

#include <QWidget>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QList>

/**
 * @brief Floating Action Button (FAB) for creating new notebooks.
 * 
 * A Squid-style FAB that sits in the bottom-right corner and expands
 * upward to reveal action buttons for creating different notebook types.
 * 
 * Features:
 * - Round main button with "+" icon
 * - Rotates to "×" when expanded
 * - Unfolds upward with 4 action buttons:
 *   1. New Edgeless Canvas
 *   2. New Paged Notebook
 *   3. Open PDF for annotation
 *   4. Open existing .snb notebook
 * - Icons with tooltips
 * - Smooth expand/collapse animation
 * - Click outside to collapse
 * 
 * Phase P.3.7: Part of the new Launcher implementation.
 */
class FloatingActionButton : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal expandProgress READ expandProgress WRITE setExpandProgress)
    Q_PROPERTY(qreal rotation READ rotation WRITE setRotation)

public:
    explicit FloatingActionButton(QWidget* parent = nullptr);
    
    /**
     * @brief Check if the FAB is expanded.
     */
    bool isExpanded() const { return m_expanded; }
    
    /**
     * @brief Expand or collapse the FAB.
     */
    void setExpanded(bool expanded);
    
    /**
     * @brief Toggle expanded state.
     */
    void toggle();
    
    /**
     * @brief Set dark mode for theming.
     */
    void setDarkMode(bool dark);
    
    /**
     * @brief Position the FAB in bottom-right corner of parent.
     */
    void positionInParent();
    
    // Animation properties
    qreal expandProgress() const { return m_expandProgress; }
    void setExpandProgress(qreal progress);
    
    qreal rotation() const { return m_rotation; }
    void setRotation(qreal rotation);

signals:
    /**
     * @brief Create new edgeless canvas.
     */
    void createEdgeless();
    
    /**
     * @brief Create new paged notebook.
     */
    void createPaged();
    
    /**
     * @brief Open PDF for annotation.
     */
    void openPdf();
    
    /**
     * @brief Open existing .snb notebook.
     */
    void openNotebook();
    
    /**
     * @brief Import a .snbx package.
     */
    void importPackage();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupUi();
    void setupAnimations();
    void updateActionButtonPositions();
    void updateMainButtonIcon();
    QPushButton* createActionButton(const QString& iconName, 
                                    const QString& tooltip);
    
    // Main FAB button
    QPushButton* m_mainButton = nullptr;
    
    // Action buttons (in order from bottom to top when expanded)
    QPushButton* m_edgelessBtn = nullptr;
    QPushButton* m_pagedBtn = nullptr;
    QPushButton* m_pdfBtn = nullptr;
    QPushButton* m_openBtn = nullptr;
    QPushButton* m_importBtn = nullptr;
    QList<QPushButton*> m_actionButtons;
    
    // Animation
    QPropertyAnimation* m_expandAnim = nullptr;
    QPropertyAnimation* m_rotateAnim = nullptr;
    QParallelAnimationGroup* m_animGroup = nullptr;
    
    bool m_expanded = false;
    bool m_darkMode = false;
    qreal m_expandProgress = 0.0;  // 0 = collapsed, 1 = expanded
    qreal m_rotation = 0.0;        // 0 = +, 45 = ×
    
    // Layout constants
    static constexpr int MAIN_BUTTON_SIZE = 56;
    static constexpr int ACTION_BUTTON_SIZE = 48;
    static constexpr int BUTTON_SPACING = 12;
    static constexpr int MARGIN = 24;
    static constexpr int ANIMATION_DURATION = 200;
};

#endif // FLOATINGACTIONBUTTON_H

