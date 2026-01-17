#ifndef SAVEDOCUMENTDIALOG_H
#define SAVEDOCUMENTDIALOG_H

#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;
class QPushButton;

/**
 * @brief Touch-friendly dialog for saving documents on Android.
 * 
 * Replaces QInputDialog::getText() with a properly sized, mobile-friendly
 * dialog with large touch targets and clear visual design.
 * 
 * Features:
 * - Large text input field with clear button
 * - Touch-friendly buttons (48px+ height)
 * - Keyboard-aware layout
 * - Input validation (prevents empty names)
 * 
 * Created as part of BUG-A002 fix improvements.
 */
class SaveDocumentDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Construct the save dialog.
     * @param title Dialog title (e.g., "Save Document" or "Save Canvas")
     * @param defaultName Default document name
     * @param parent Parent widget
     */
    explicit SaveDocumentDialog(const QString& title, 
                                const QString& defaultName,
                                QWidget* parent = nullptr);
    
    /**
     * @brief Get the entered document name.
     * @return Sanitized document name (invalid filename characters replaced)
     */
    QString documentName() const;
    
    /**
     * @brief Static convenience method to get a document name.
     * @param parent Parent widget
     * @param title Dialog title
     * @param defaultName Default document name
     * @param ok Set to true if user accepted, false if cancelled
     * @return Document name, or empty string if cancelled
     */
    static QString getDocumentName(QWidget* parent,
                                   const QString& title,
                                   const QString& defaultName,
                                   bool* ok = nullptr);

private slots:
    void onTextChanged(const QString& text);

private:
    void setupUI();
    QString sanitizeFilename(const QString& name) const;
    
    QString m_title;
    QString m_defaultName;
    
    // UI elements
    QLabel* m_titleLabel = nullptr;
    QLabel* m_promptLabel = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
};

#endif // SAVEDOCUMENTDIALOG_H

