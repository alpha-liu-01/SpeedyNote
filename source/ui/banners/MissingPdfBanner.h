#ifndef MISSINGPDFBANNER_H
#define MISSINGPDFBANNER_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QPropertyAnimation>

/**
 * @brief Non-blocking notification banner for missing PDF files.
 * 
 * Appears at the top of the DocumentViewport when a document references
 * a PDF that cannot be found. Offers options to locate the PDF or dismiss.
 * 
 * Design:
 * ┌──────────────────────────────────────────────────────────────────┐
 * │ ⚠️ PDF file not found: document.pdf    [Locate PDF] [Dismiss]   │
 * └──────────────────────────────────────────────────────────────────┘
 */
class MissingPdfBanner : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int slideOffset READ slideOffset WRITE setSlideOffset)

public:
    explicit MissingPdfBanner(QWidget* parent = nullptr);
    
    /**
     * @brief Set the name of the missing PDF file to display.
     * @param pdfName Filename (not full path) of the missing PDF.
     */
    void setPdfName(const QString& pdfName);
    
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
     * @brief Emitted when user clicks "Locate PDF" button.
     */
    void locatePdfClicked();
    
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

