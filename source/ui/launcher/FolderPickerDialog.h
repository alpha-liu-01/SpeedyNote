#ifndef FOLDERPICKERDIALOG_H
#define FOLDERPICKERDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>

/**
 * @brief Modal dialog for selecting a folder.
 * 
 * FolderPickerDialog provides a touchscreen-friendly interface for selecting
 * a starred folder. It displays:
 * - A search bar to filter folders
 * - Recent folders section (last 5 used)
 * - All folders section (alphabetically sorted)
 * - Option to create a new folder
 * 
 * The dialog is designed for touch input with large tap targets and
 * comfortable spacing.
 * 
 * L-008: Part of the Folder Picker UI for Many Folders feature.
 */
class FolderPickerDialog : public QDialog {
    Q_OBJECT

public:
    explicit FolderPickerDialog(QWidget* parent = nullptr);
    
    /**
     * @brief Get the selected folder name.
     * @return The folder name, or empty string if cancelled.
     */
    QString selectedFolder() const { return m_selectedFolder; }
    
    /**
     * @brief Set the dialog title.
     * @param title Custom title (e.g., "Move 3 notebooks to...").
     */
    void setTitle(const QString& title);
    
    /**
     * @brief Set dark mode for theming.
     * @param dark True for dark mode.
     */
    void setDarkMode(bool dark);
    
    /**
     * @brief Static convenience method to show the dialog and get a folder.
     * @param parent Parent widget.
     * @param title Dialog title.
     * @return Selected folder name, or empty string if cancelled.
     */
    static QString getFolder(QWidget* parent, 
                             const QString& title = QString());

private slots:
    void onSearchTextChanged(const QString& text);
    void onFolderClicked(QListWidgetItem* item);
    void onFolderContextMenu(const QPoint& pos);
    void onNewFolderClicked();

private:
    void setupUi();
    void applyTheme();
    void populateFolders();
    void filterFolders(const QString& text);
    
    /**
     * @brief Create a section header item (non-selectable).
     */
    QListWidgetItem* createSectionHeader(const QString& text);
    
    /**
     * @brief Create a folder item.
     */
    QListWidgetItem* createFolderItem(const QString& folderName);
    
    /**
     * @brief Check if a folder is empty (has no notebooks).
     */
    bool isFolderEmpty(const QString& folderName) const;
    
    /**
     * @brief Delete a folder after confirmation.
     */
    void deleteFolder(const QString& folderName);
    
    // UI components
    QLabel* m_titleLabel = nullptr;
    QPushButton* m_closeButton = nullptr;
    QLineEdit* m_searchInput = nullptr;
    
    // Single unified list for all folders (scrolls together)
    QListWidget* m_folderList = nullptr;
    
    QPushButton* m_newFolderButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
    
    // State
    QString m_selectedFolder;
    QStringList m_recentFolders;
    QStringList m_allFolders;
    bool m_darkMode = false;
    
    // Item type markers (stored in Qt::UserRole + 1)
    enum ItemType {
        SectionHeader = 0,
        FolderItem = 1
    };
    
    // Layout constants for touch-friendly UI
    static constexpr int ITEM_HEIGHT = 48;          // Touch-friendly item height
    static constexpr int SECTION_HEADER_HEIGHT = 32; // Section header height
    static constexpr int BUTTON_HEIGHT = 44;        // Touch-friendly button height
    static constexpr int MARGIN = 16;               // Dialog margins
    static constexpr int SPACING = 12;              // Spacing between elements
    static constexpr int SEARCH_HEIGHT = 44;        // Search input height
    static constexpr int DIALOG_MIN_WIDTH = 320;    // Minimum dialog width
    static constexpr int DIALOG_MIN_HEIGHT = 400;   // Minimum dialog height
};

#endif // FOLDERPICKERDIALOG_H
