#ifndef TEXTBOXFORMATBAR_H
#define TEXTBOXFORMATBAR_H

#include "../../objects/TextBoxObject.h"

#include <QPointer>
#include <QWidget>

class ColorPresetButton;
class QButtonGroup;
class QColorDialog;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QSlider;
class QToolButton;
class QTimer;

/**
 * Compact, viewport-owned controls for a selected user text box.
 *
 * The bar intentionally keeps no object pointer. DocumentViewport owns the
 * target lookup, constrained preview, and undo transaction.
 */
class TextBoxFormatBar : public QWidget
{
    Q_OBJECT

public:
    explicit TextBoxFormatBar(QWidget* parent = nullptr);
    ~TextBoxFormatBar() override;

    void setValues(const TextBoxState& state);
    void setDarkMode(bool darkMode);
    void closePopups(bool acceptPreview = false);
    bool hasOpenPopup() const;
    bool controlHasFocus() const;

signals:
    void interactionStarted();
    void fontSizePreviewRequested(qreal size);
    void fontFamilyPreviewRequested(const QString& family);
    void alignmentPreviewRequested(TextAlignment alignment);
    void fontColorPreviewRequested(const QColor& color);
    void backgroundColorPreviewRequested(const QColor& color);
    void backgroundOpacityPreviewRequested(int opacity);
    void borderPreviewRequested(bool visible);
    void interactionFinished(bool accept);
    void cancelInlineEditRequested();
    void commitInlineEditRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void ensureInteractionStarted();
    void finishInteraction(bool accept);
    void scheduleBurstFinish();
    void openColorDialog(bool background);
    void updateAlignmentIcons();
    void updateSwatches();

    QDoubleSpinBox* m_fontSize = nullptr;
    QFontComboBox* m_fontFamily = nullptr;
    QButtonGroup* m_alignmentGroup = nullptr;
    QToolButton* m_alignLeft = nullptr;
    QToolButton* m_alignCenter = nullptr;
    QToolButton* m_alignRight = nullptr;
    ColorPresetButton* m_fontColor = nullptr;
    ColorPresetButton* m_backgroundColor = nullptr;
    QSlider* m_backgroundOpacity = nullptr;
    QToolButton* m_border = nullptr;
    QTimer* m_burstTimer = nullptr;
    QPointer<QColorDialog> m_colorDialog;
    QColor m_currentFontColor;
    QColor m_currentBackgroundColor;
    bool m_colorDialogIsBackground = false;
    bool m_darkMode = false;
    bool m_syncing = false;
    bool m_interactionActive = false;
    bool m_closingPopups = false;
};

#endif // TEXTBOXFORMATBAR_H
