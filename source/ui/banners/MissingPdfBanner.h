#ifndef MISSINGPDFBANNER_H
#define MISSINGPDFBANNER_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QPropertyAnimation>

/**
 * @brief Non-blocking notification banner for unavailable PDF sources.
 * 
 * Appears at the top of the DocumentViewport when one or more referenced PDF
 * sources cannot serve their pages. Offers source review or session dismissal.
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
    
    static constexpr int BANNER_HEIGHT = 40;
    static constexpr int ANIMATION_DURATION = 200;  // ms
};

#endif // MISSINGPDFBANNER_H

