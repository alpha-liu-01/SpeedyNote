#ifndef OBJECTSELECTACTIONBAR_H
#define OBJECTSELECTACTIONBAR_H

#include "ActionBar.h"
#include "../../core/DocumentViewport.h"

class ActionBarButton;

/**
 * @brief Action bar for object selection operations.
 * 
 * Provides an always-visible Add/Select mode toggle plus contextual clipboard,
 * delete, and layer ordering operations while ObjectSelect is active.
 * 
 * Layout (when selection exists):
 * - [Copy]     - Visible when selection exists
 * - [Paste]    - Visible when clipboard has object
 * - [Delete]   - Visible when selection exists
 * - ───────    - Separator (visible when selection exists)
 * - [Forward]  - Z-order up by 1 (Ctrl+]) - visible when selection exists
 * - [Backward] - Z-order down by 1 (Ctrl+[) - visible when selection exists
 * - [Affinity+] - Increase affinity (Alt+]) - visible when selection exists
 * - [Affinity-] - Decrease affinity (Alt+[) - visible when selection exists
 * 
 * Layout (paste-only mode, no selection but clipboard has object):
 * - [Paste]    - Visible when clipboard has object
 * - [Cancel]   - Clears clipboard and dismisses action bar (Esc)
 * 
 * This action bar appears whenever the current tool is ObjectSelect.
 */
class ObjectSelectActionBar : public ActionBar {
    Q_OBJECT

public:
    explicit ObjectSelectActionBar(QWidget* parent = nullptr);
    
    /**
     * @brief Update button visibility based on current state.
     * 
     * The Add/Select toggle is always visible; other actions depend on context.
     */
    void updateButtonStates() override;
    
    /**
     * @brief Set dark mode and update button icons.
     * @param darkMode True for dark mode, false for light mode.
     */
    void setDarkMode(bool darkMode) override;
    
    /**
     * @brief Set whether an object is in the clipboard.
     * @param hasObject True if clipboard has an object to paste.
     * 
     * Call this when the object clipboard changes.
     */
    void setHasObjectInClipboard(bool hasObject);
    void setHasImageInClipboard(bool hasImage);
    void setActionModeState(DocumentViewport::ObjectActionMode mode);
    
    /**
     * @brief Set whether objects are currently selected.
     * @param hasSelection True if one or more objects are selected.
     * 
     * When false and clipboard has object, shows paste-only mode.
     * When true, shows full action bar.
     */
    void setHasSelection(bool hasSelection);
    
    /**
     * @brief Update image-specific state for the aspect ratio lock button.
     * @param isImage True if a single ImageObject is selected.
     * @param aspectLocked Current maintainAspectRatio state of the image.
     */
    void updateImageSelection(bool isImage, bool aspectLocked);

    /**
     * @brief Update OCR text lock button visibility and state.
     * @param isOcrText True if a single OcrTextObject is selected.
     * @param isLocked Current ocrLocked state of the object.
     */
    void updateOcrLockSelection(bool isOcrText, bool isLocked);

signals:
    void actionModeChanged(DocumentViewport::ObjectActionMode mode);

    /**
     * @brief Emitted when the aspect ratio lock button is clicked.
     */
    void aspectRatioLockRequested();
    
    /**
     * @brief Emitted when Copy button is clicked.
     */
    void copyRequested();
    
    /**
     * @brief Emitted when Paste button is clicked.
     */
    void pasteRequested();
    
    /**
     * @brief Emitted when Delete button is clicked.
     */
    void deleteRequested();
    
    /**
     * @brief Emitted when Bring Forward button is clicked.
     * Equivalent to Ctrl+]
     */
    void bringForwardRequested();
    
    /**
     * @brief Emitted when Send Backward button is clicked.
     * Equivalent to Ctrl+[
     */
    void sendBackwardRequested();
    
    /**
     * @brief Emitted when Increase Affinity button is clicked.
     * Equivalent to Alt+]
     */
    void increaseAffinityRequested();
    
    /**
     * @brief Emitted when Decrease Affinity button is clicked.
     * Equivalent to Alt+[
     */
    void decreaseAffinityRequested();
    
    /**
     * @brief Emitted when Cancel button is clicked.
     * Clears clipboard and dismisses action bar.
     */
    void cancelRequested();

    /**
     * @brief Emitted when the OCR lock/unlock button is clicked.
     */
    void ocrLockToggleRequested();

    /**
     * @brief Emitted when the "convert OCR text to a text box" button is clicked.
     */
    void ocrConvertToTextBoxRequested();

private:
    void setupButtons();
    void updateActionModeButton();

    // Persistent interaction mode
    ActionBarButton* m_actionModeButton = nullptr;
    
    // Clipboard buttons
    ActionBarButton* m_copyButton = nullptr;
    ActionBarButton* m_pasteButton = nullptr;
    ActionBarButton* m_deleteButton = nullptr;
    
    // Separator
    QWidget* m_separator = nullptr;
    
    // Layer ordering buttons
    ActionBarButton* m_forwardButton = nullptr;
    ActionBarButton* m_backwardButton = nullptr;
    ActionBarButton* m_increaseAffinityButton = nullptr;
    ActionBarButton* m_decreaseAffinityButton = nullptr;
    
    // Aspect ratio lock (image-only)
    ActionBarButton* m_aspectLockButton = nullptr;
    
    // OCR lock (ocr-text-only)
    ActionBarButton* m_ocrLockButton = nullptr;

    // Convert recognized text into an editable text box (ocr-text-only)
    ActionBarButton* m_ocrConvertButton = nullptr;
    
    // Cancel button (for paste-only mode)
    ActionBarButton* m_cancelButton = nullptr;
    
    // State tracking
    bool m_hasObjectInClipboard = false;
    bool m_hasImageInClipboard = false;
    bool m_hasSelection = false;
    bool m_isImageSelected = false;
    bool m_isOcrTextSelected = false;
    DocumentViewport::ObjectActionMode m_actionMode =
        DocumentViewport::ObjectActionMode::Select;
};

#endif // OBJECTSELECTACTIONBAR_H

