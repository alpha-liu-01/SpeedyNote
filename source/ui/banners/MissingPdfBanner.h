#ifndef MISSINGPDFBANNER_H
#define MISSINGPDFBANNER_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QPropertyAnimation>

/**
 * @brief Non-blocking notification banner for unavailable PDF sources.
 * 
 * Appears across the top of a viewport pane when one or more referenced PDF
 * sources cannot serve their pages. Offers source review or session dismissal.
 *
 * One per pane, owned by SplitViewManager and parented to the pane's stack
 * rather than to the viewport: a viewport child would be baked into the canvas
 * snapshot that gestures blit around, and would have to be excluded from the
 * canvas input path by hand. The warning state itself stays on the viewport,
 * since it belongs to the document; see DocumentViewport::pdfWarning().
 * 
 * Design:
 * ┌──────────────────────────────────────────────────────────────────┐
 * │ ⚠️ PDF sources unavailable       [Review Sources...] [Dismiss] │
 * └──────────────────────────────────────────────────────────────────┘
 */
class MissingPdfBanner : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int slideOffset READ slideOffset WRITE setSlideOffset)

public:
    explicit MissingPdfBanner(QWidget* parent = nullptr);
    
    /**
     * @brief Set a source-aware warning summary.
     */
    void setSummary(int sourceCount, int affectedPages, const QString& singleSourceName = QString());
    
    /**
     * @brief Show the banner with slide-in animation.
     */
    void showAnimated();
    
    /**
     * @brief Hide the banner with slide-out animation.
     */
    void hideAnimated();

    /**
     * @brief Put the banner straight into its shown or hidden end state.
     *
     * For rebinding to a different document, where the banner is not reacting
     * to anything the user just did and a 200ms slide would read as a glitch.
     */
    void setShown(bool shown);

    /**
     * @brief Height of the banner, and so the strip it claims from the pane.
     */
    static constexpr int BANNER_HEIGHT = 40;

signals:
    /**
     * @brief Emitted when the user asks to review unavailable sources.
     */
    void reviewSourcesClicked();
    
    /**
     * @brief Emitted when user clicks "Dismiss" button.
     */
    void dismissed();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void setupUi();
    
    int slideOffset() const { return m_slideOffset; }
    void setSlideOffset(int offset);
    
    QLabel* m_iconLabel;
    QLabel* m_messageLabel;
    QPushButton* m_locateButton;
    QPushButton* m_dismissButton;
    
    QPropertyAnimation* m_animation;
    int m_slideOffset = 0;  // For slide animation (negative = hidden above)
    
    static constexpr int ANIMATION_DURATION = 200;  // ms
};

#endif // MISSINGPDFBANNER_H

