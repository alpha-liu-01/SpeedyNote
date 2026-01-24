#pragma once

// ============================================================================
// DocumentManager - Centralized document lifecycle management
// ============================================================================
// Part of the SpeedyNote document architecture (Phase 3.0.1)
//
// DocumentManager is responsible for:
// - Creating new Documents
// - Loading Documents from files (.snb bundles and .pdf)
// - Saving Documents to files (.snb bundles)
// - Tracking which Documents have unsaved changes
// - Managing recent documents list
//
// DocumentManager OWNS all Document instances. When a document is closed,
// DocumentManager deletes it. DocumentViewport only holds a pointer.
// ============================================================================

#include <QObject>
#include <QVector>
#include <QMap>
#include <QStringList>
#include <QSettings>
#include <memory>

class Document;

/**
 * @brief Centralized manager for Document lifecycle.
 * 
 * DocumentManager owns all Document instances and handles file I/O.
 * It provides signals for UI updates when documents are created, loaded, 
 * saved, closed, or modified.
 */
class DocumentManager : public QObject {
    Q_OBJECT

public:
    explicit DocumentManager(QObject* parent = nullptr);
    ~DocumentManager() override;

    // =========================================================================
    // Document Lifecycle
    // =========================================================================

    /**
     * @brief Create a new blank document.
     * @param name Optional name for the document (default: "Untitled").
     * @return Pointer to the newly created document.
     * 
     * The document is owned by DocumentManager.
     * Creates a document with one blank page.
     * Emits documentCreated() signal.
     */
    Document* createDocument(const QString& name = QString());
    
    /**
     * @brief Create a new edgeless (infinite canvas) document.
     * @param name Optional name for the document (default: tr("Untitled Canvas")).
     * @return Pointer to the newly created document.
     * 
     * The document is owned by DocumentManager.
     * Creates an edgeless document with no tiles (tiles created on-demand).
     * Immediately creates a temp .snb bundle directory to enable tile eviction.
     * Emits documentCreated() signal.
     */
    Document* createEdgelessDocument(const QString& name = QString());

    /**
     * @brief Check if a document is using a temporary bundle path.
     * @param doc Document to check.
     * @return True if the document has a temp bundle path (not user-saved).
     */
    bool isUsingTempBundle(Document* doc) const;

    /**
     * @brief Get the temp bundle path for a document.
     * @param doc Document to check.
     * @return Temp bundle path, or empty string if not using temp bundle.
     */
    QString tempBundlePath(Document* doc) const;

    /**
     * @brief Clean up temp bundle directory for a document.
     * @param doc Document whose temp bundle should be cleaned up.
     * 
     * Call this after successfully saving to a user-specified location.
     */
    void cleanupTempBundle(Document* doc);

    /**
     * @brief Auto-save all modified documents (for Android background save).
     * @return Number of documents saved.
     * 
     * For documents with existing paths: saves in-place.
     * For new documents: saves to app storage with auto-generated name.
     * All saved documents are added to NotebookLibrary.
     * 
     * This is designed for Android where the app may be killed without
     * closeEvent() when swiped from recents. Call this when the app
     * goes to background (ApplicationSuspended/ApplicationInactive).
     */
    int autoSaveModifiedDocuments();

    /**
     * @brief Load a document from a file.
     * @param path Path to the file (.snb bundle or .pdf).
     * @return Pointer to the loaded document, or nullptr on failure.
     * 
     * The document is owned by DocumentManager.
     * Emits documentLoaded() signal on success.
     * Adds the path to recent documents on success.
     */
    Document* loadDocument(const QString& path);

    /**
     * @brief Save a document to its current path.
     * @param doc Document to save.
     * @return True on success, false on failure.
     * 
     * If the document has no path, returns false (use saveDocumentAs).
     * Emits documentSaved() signal on success.
     */
    bool saveDocument(Document* doc);

    /**
     * @brief Save a document to a new path.
     * @param doc Document to save.
     * @param path Path to save to.
     * @return True on success, false on failure.
     * 
     * Updates the document's path on success.
     * Emits documentSaved() signal on success.
     * Adds the path to recent documents on success.
     */
    bool saveDocumentAs(Document* doc, const QString& path);

    /**
     * @brief Close a document and release its resources.
     * @param doc Document to close.
     * 
     * The document is deleted. Any pointers to it become invalid.
     * Emits documentClosed() signal before deletion.
     */
    void closeDocument(Document* doc);

    // =========================================================================
    // Document Access
    // =========================================================================

    /**
     * @brief Get a document by index.
     * @param index 0-based index.
     * @return Pointer to the document, or nullptr if index is out of range.
     */
    Document* documentAt(int index) const;

    /**
     * @brief Get the number of open documents.
     */
    int documentCount() const;

    /**
     * @brief Get the index of a document.
     * @param doc Document to find.
     * @return Index, or -1 if not found.
     */
    int indexOf(Document* doc) const;

    // =========================================================================
    // Document State
    // =========================================================================

    /**
     * @brief Check if a document has unsaved changes.
     * @param doc Document to check.
     * @return True if the document has been modified since last save.
     */
    bool hasUnsavedChanges(Document* doc) const;

    /**
     * @brief Get the file path of a document.
     * @param doc Document to check.
     * @return File path, or empty string if document is new (never saved).
     */
    QString documentPath(Document* doc) const;

    /**
     * @brief Update the file path of a document.
     * @param doc Document to update.
     * @param path New file path.
     * 
     * Use this when a document has been renamed externally (e.g., in the Launcher)
     * but is still open in a tab. This keeps the path tracking in sync.
     */
    void setDocumentPath(Document* doc, const QString& path);

    /**
     * @brief Mark a document as modified.
     * @param doc Document to mark.
     * 
     * Called internally when document changes. Also emits documentModified().
     */
    void markModified(Document* doc);

    /**
     * @brief Clear the modified flag for a document.
     * @param doc Document to mark as clean.
     * 
     * Called after successful save.
     */
    void clearModified(Document* doc);

    // =========================================================================
    // Recent Documents
    // =========================================================================

    /**
     * @brief Get the list of recently opened documents.
     * @return List of file paths, most recent first.
     */
    QStringList recentDocuments() const;

    /**
     * @brief Add a path to the recent documents list.
     * @param path Path to add.
     * 
     * If the path already exists, it's moved to the front.
     * Emits recentDocumentsChanged() signal.
     */
    void addToRecent(const QString& path);

    /**
     * @brief Clear the recent documents list.
     * 
     * Emits recentDocumentsChanged() signal.
     */
    void clearRecentDocuments();

    /**
     * @brief Remove a path from the recent documents list.
     * @param path Path to remove.
     * 
     * Use when a file no longer exists.
     * Emits recentDocumentsChanged() signal if removed.
     */
    void removeFromRecent(const QString& path);

signals:
    /**
     * @brief Emitted when a new document is created.
     * @param doc The newly created document.
     */
    void documentCreated(Document* doc);

    /**
     * @brief Emitted when a document is loaded from file.
     * @param doc The loaded document.
     */
    void documentLoaded(Document* doc);

    /**
     * @brief Emitted when a document is saved.
     * @param doc The saved document.
     */
    void documentSaved(Document* doc);

    /**
     * @brief Emitted just before a document is closed.
     * @param doc The document being closed.
     * 
     * After this signal, the document pointer becomes invalid.
     */
    void documentClosed(Document* doc);

    /**
     * @brief Emitted when a document is modified.
     * @param doc The modified document.
     */
    void documentModified(Document* doc);

    /**
     * @brief Emitted when the recent documents list changes.
     */
    void recentDocumentsChanged();

private:
    // Owned documents
    QVector<Document*> m_documents;

    // Document metadata (keyed by Document pointer)
    QMap<Document*, QString> m_documentPaths;     // Document → file path
    QMap<Document*, bool> m_modifiedFlags;        // Document → has unsaved changes
    QMap<Document*, QString> m_tempBundlePaths;   // Document → temp bundle path (for unsaved edgeless)

    // Recent documents
    QStringList m_recentPaths;
    static const int MAX_RECENT = 10;

    // Settings key for recent documents
    static const QString SETTINGS_RECENT_KEY;
    
    // Temp bundle prefixes for unsaved documents
    static const QString TEMP_EDGELESS_PREFIX;
    static const QString TEMP_PAGED_PREFIX;

    // Load/save recent documents from/to QSettings
    void loadRecentFromSettings();
    void saveRecentToSettings();

    // Internal: actually perform the save
    bool doSave(Document* doc, const QString& path);
    
    // Create a unique temp bundle path for a document (edgeless or paged)
    QString createTempBundlePath(Document* doc);
    
    // Create a permanent auto-save path in app storage (for Android)
    QString createAutoSavePath(Document* doc);
};
