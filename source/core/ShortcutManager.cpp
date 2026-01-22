#include "ShortcutManager.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QRegularExpression>

// ============================================================================
// Shortcut Normalization Helper
// ============================================================================

/**
 * @brief Normalize a shortcut string to use number keys instead of shifted symbols.
 * 
 * When Shift is pressed with number keys, Qt may report the shifted symbol
 * (e.g., "Ctrl+Shift+!" instead of "Ctrl+Shift+1"). This function converts
 * these back to the base number format for consistent storage and matching.
 */
static QString normalizeShortcut(const QString& shortcut)
{
    if (shortcut.isEmpty()) {
        return shortcut;
    }
    
    // Only normalize if Shift is present and the shortcut ends with a shifted symbol
    if (!shortcut.contains("Shift+", Qt::CaseInsensitive)) {
        return shortcut;
    }
    
    // Map of shifted symbols to their base number keys
    static const QHash<QChar, QChar> shiftedToNumber = {
        {'!', '1'}, {'@', '2'}, {'#', '3'}, {'$', '4'}, {'%', '5'},
        {'^', '6'}, {'&', '7'}, {'*', '8'}, {'(', '9'}, {')', '0'}
    };
    
    // Check if the last character is a shifted symbol
    QChar lastChar = shortcut.at(shortcut.length() - 1);
    if (shiftedToNumber.contains(lastChar)) {
        QString normalized = shortcut;
        normalized.chop(1);
        normalized.append(shiftedToNumber.value(lastChar));
        return normalized;
    }
    
    return shortcut;
}

// ============================================================================
// Singleton Instance
// ============================================================================

ShortcutManager* ShortcutManager::s_instance = nullptr;

ShortcutManager* ShortcutManager::instance()
{
    if (!s_instance) {
        s_instance = new ShortcutManager();
        s_instance->registerDefaults();  // Register all default shortcuts first
        s_instance->loadUserShortcuts(); // Then load user overrides
    }
    return s_instance;
}

// ============================================================================
// Constructor
// ============================================================================

ShortcutManager::ShortcutManager(QObject* parent)
    : QObject(parent)
{
    // Determine config file path
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    
    // Ensure directory exists
    QDir dir(configDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    
    m_configPath = configDir + "/shortcuts.json";
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[ShortcutManager] Config path:" << m_configPath;
#endif
}

// ============================================================================
// Default Shortcuts Registration
// ============================================================================

void ShortcutManager::registerDefaults()
{
    // ===== File Operations =====
    registerAction("file.save", "Ctrl+S", tr("Save Document"), tr("File"));
    registerAction("file.new_paged", "Ctrl+N", tr("New Paged Notebook"), tr("File"));
    registerAction("file.new_edgeless", "Ctrl+Shift+N", tr("New Edgeless Canvas"), tr("File"));
    registerAction("file.open_pdf", "Ctrl+O", tr("Open PDF"), tr("File"));
    registerAction("file.open_notebook", "Ctrl+Shift+O", tr("Open Notebook"), tr("File"));
    registerAction("file.close_tab", "Ctrl+W", tr("Close Tab"), tr("File"));
    registerAction("file.export", "Ctrl+Shift+E", tr("Export/Share"), tr("File"));
    
    // ===== Document/Page Operations =====
    registerAction("document.add_page", "Ctrl+Shift+A", tr("Add Page (Append)"), tr("Document"));
    registerAction("document.insert_page", "Ctrl+Shift+I", tr("Insert Page"), tr("Document"));
    registerAction("document.delete_page", "Ctrl+Shift+D", tr("Delete Page"), tr("Document"));
    
    // ===== Navigation =====
    registerAction("navigation.launcher", "Ctrl+L", tr("Toggle Launcher"), tr("Navigation"));
    // Page navigation - only for paged documents (not edgeless)
    registerAction("navigation.prev_page", "Page Up", tr("Previous Page"), tr("Navigation"), Scope::PagedOnly);
    registerAction("navigation.next_page", "Page Down", tr("Next Page"), tr("Navigation"), Scope::PagedOnly);
    registerAction("navigation.first_page", "Home", tr("First Page"), tr("Navigation"), Scope::PagedOnly);
    registerAction("navigation.last_page", "End", tr("Last Page"), tr("Navigation"), Scope::PagedOnly);
    registerAction("navigation.go_to_page", "Ctrl+G", tr("Go to Page..."), tr("Navigation"), Scope::PagedOnly);
    // Tab navigation - global
    registerAction("navigation.next_tab", "Ctrl+Tab", tr("Next Tab"), tr("Navigation"));
    registerAction("navigation.prev_tab", "Ctrl+Shift+Tab", tr("Previous Tab"), tr("Navigation"));
    registerAction("navigation.escape", "Escape", tr("Escape/Cancel"), tr("Navigation"));
    
    // ===== Tools (Photoshop-style) =====
    registerAction("tool.pen", "B", tr("Pen Tool"), tr("Tools"));
    registerAction("tool.eraser", "E", tr("Eraser Tool"), tr("Tools"));
    registerAction("tool.lasso", "L", tr("Lasso Tool"), tr("Tools"));
    registerAction("tool.highlighter", "T", tr("Text Highlighter Tool"), tr("Tools"));
    registerAction("tool.marker", "M", tr("Marker Tool"), tr("Tools"));
    registerAction("tool.object_select", "V", tr("Object Select Tool"), tr("Tools"));
    registerAction("tool.pan", "H", tr("Pan Tool (Hold)"), tr("Tools"));
    
    // ===== Editing =====
    registerAction("edit.undo", "Ctrl+Z", tr("Undo"), tr("Edit"));
    registerAction("edit.redo", "Ctrl+Shift+Z", tr("Redo"), tr("Edit"));
    registerAction("edit.redo_alt", "Ctrl+Y", tr("Redo (Alternative)"), tr("Edit"));
    registerAction("edit.copy", "Ctrl+C", tr("Copy"), tr("Edit"));
    registerAction("edit.cut", "Ctrl+X", tr("Cut"), tr("Edit"));
    registerAction("edit.paste", "Ctrl+V", tr("Paste"), tr("Edit"));
    registerAction("edit.delete", "Delete", tr("Delete"), tr("Edit"));
    registerAction("edit.select_all", "Ctrl+A", tr("Select All"), tr("Edit"));
    registerAction("edit.deselect", "Ctrl+D", tr("Deselect"), tr("Edit"));
    
    // ===== Zoom =====
    registerAction("zoom.in", "Ctrl++", tr("Zoom In"), tr("Zoom"));
    registerAction("zoom.in_alt", "Ctrl+=", tr("Zoom In (Alternative)"), tr("Zoom"));
    registerAction("zoom.out", "Ctrl+-", tr("Zoom Out"), tr("Zoom"));
    registerAction("zoom.fit", "Ctrl+0", tr("Zoom to Fit"), tr("Zoom"));
    registerAction("zoom.100", "Ctrl+1", tr("Zoom to 100%"), tr("Zoom"));
    registerAction("zoom.fit_width", "Ctrl+2", tr("Zoom to Fit Width"), tr("Zoom"));
    
    // ===== Object Z-Order (Photoshop-style) =====
    registerAction("object.bring_front", "Ctrl+Shift+]", tr("Bring to Front"), tr("Objects"));
    registerAction("object.bring_forward", "Ctrl+]", tr("Bring Forward"), tr("Objects"));
    registerAction("object.send_backward", "Ctrl+[", tr("Send Backward"), tr("Objects"));
    registerAction("object.send_back", "Ctrl+Shift+[", tr("Send to Back"), tr("Objects"));
    
    // ===== Object Affinity (SpeedyNote-specific) =====
    registerAction("object.affinity_up", "Alt+]", tr("Increase Affinity"), tr("Objects"));
    registerAction("object.affinity_down", "Alt+[", tr("Decrease Affinity"), tr("Objects"));
    registerAction("object.affinity_background", "Alt+\\", tr("Send to Background"), tr("Objects"));
    
    // ===== Object Insert/Action Mode =====
    registerAction("object.mode_image", "I", tr("Image Insert Mode"), tr("Objects"));
    registerAction("object.mode_link", "Ctrl+.", tr("Link Insert Mode"), tr("Objects"));
    registerAction("object.mode_create", "Ctrl+6", tr("Object Create Mode"), tr("Objects"));
    registerAction("object.mode_select", "Ctrl+7", tr("Object Select Mode"), tr("Objects"));
    
    // ===== Link Slots =====
    registerAction("link.slot_1", "Ctrl+8", tr("Activate Link Slot 1"), tr("Links"));
    registerAction("link.slot_2", "Ctrl+9", tr("Activate Link Slot 2"), tr("Links"));
    registerAction("link.slot_3", "Alt+0", tr("Activate Link Slot 3"), tr("Links"));
    
    // ===== Layer Operations =====
    registerAction("layer.new", "Ctrl+Alt+Shift+N", tr("New Layer"), tr("Layers"));
    registerAction("layer.toggle_visibility", "Ctrl+,", tr("Toggle Layer Visibility"), tr("Layers"));
    registerAction("layer.select_all", "Ctrl+Alt+A", tr("Select All Layers"), tr("Layers"));
    registerAction("layer.select_top", "Alt+.", tr("Select Top Layer"), tr("Layers"));
    registerAction("layer.select_bottom", "Alt+,", tr("Select Bottom Layer"), tr("Layers"));
    registerAction("layer.merge", "Ctrl+E", tr("Merge Layers"), tr("Layers"));
    
    // ===== View =====
    registerAction("view.fullscreen", "F11", tr("Toggle Fullscreen"), tr("View"));
    registerAction("view.debug_overlay", "F12", tr("Toggle Debug Overlay"), tr("View"));
    registerAction("view.auto_layout", "Ctrl+Shift+2", tr("Toggle Auto Layout"), tr("View"));
    registerAction("view.left_sidebar", "Ctrl+Shift+L", tr("Toggle Left Sidebar"), tr("View"));
    registerAction("view.right_sidebar", "Ctrl+Shift+M", tr("Toggle Right Sidebar"), tr("View"));
    
    // ===== PDF Features =====
    registerAction("pdf.auto_highlight", "Ctrl+H", tr("Toggle Auto-Highlight"), tr("PDF"));
    
    // ===== Application =====
    registerAction("app.settings", "Ctrl+K", tr("Settings"), tr("Application"));
    registerAction("app.keyboard_shortcuts", "Ctrl+Alt+Shift+K", tr("Keyboard Shortcuts"), tr("Application"));
    registerAction("app.find", "Ctrl+F", tr("Find in Document"), tr("Application"));
    registerAction("app.find_next", "F3", tr("Find Next"), tr("Application"));
    registerAction("app.find_prev", "Shift+F3", tr("Find Previous"), tr("Application"));
    
    // ===== Edgeless Navigation (only for edgeless documents) =====
    registerAction("edgeless.home", "Home", tr("Return to Origin"), tr("Edgeless"), Scope::EdgelessOnly);
    registerAction("edgeless.go_back", "Backspace", tr("Go Back"), tr("Edgeless"), Scope::EdgelessOnly);
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[ShortcutManager] Registered" << m_shortcuts.size() << "default shortcuts";
#endif
}

// ============================================================================
// Action Registration
// ============================================================================

void ShortcutManager::registerAction(const QString& actionId,
                                     const QString& defaultShortcut,
                                     const QString& displayName,
                                     const QString& category,
                                     Scope scope)
{
    if (actionId.isEmpty()) {
        qWarning() << "[ShortcutManager] Cannot register action with empty ID";
        return;
    }
    
    if (m_shortcuts.contains(actionId)) {
        // Update existing entry but preserve user override
        ShortcutEntry& entry = m_shortcuts[actionId];
        entry.defaultShortcut = defaultShortcut;
        entry.displayName = displayName;
        entry.category = category;
        entry.scope = scope;
    } else {
        // New entry
        ShortcutEntry entry;
        entry.defaultShortcut = defaultShortcut;
        entry.userShortcut = QString();  // No override initially
        entry.displayName = displayName;
        entry.category = category;
        entry.scope = scope;
        m_shortcuts.insert(actionId, entry);
    }
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[ShortcutManager] Registered:" << actionId 
             << "default:" << defaultShortcut
             << "category:" << category;
#endif
}

bool ShortcutManager::scopesCanConflict(Scope a, Scope b)
{
    // Global conflicts with everything
    if (a == Scope::Global || b == Scope::Global) {
        return true;
    }
    // PagedOnly and EdgelessOnly don't conflict with each other
    // (they're mutually exclusive contexts)
    return a == b;
}

bool ShortcutManager::hasAction(const QString& actionId) const
{
    return m_shortcuts.contains(actionId);
}

// ============================================================================
// Shortcut Retrieval
// ============================================================================

QString ShortcutManager::shortcutForAction(const QString& actionId) const
{
    auto it = m_shortcuts.constFind(actionId);
    if (it == m_shortcuts.constEnd()) {
        return QString();
    }
    
    const ShortcutEntry& entry = it.value();
    
    // Return user override if set, otherwise default
    if (!entry.userShortcut.isEmpty()) {
        return entry.userShortcut;
    }
    return entry.defaultShortcut;
}

QKeySequence ShortcutManager::keySequenceForAction(const QString& actionId) const
{
    QString shortcut = shortcutForAction(actionId);
    if (shortcut.isEmpty()) {
        return QKeySequence();
    }
    return QKeySequence(shortcut);
}

QString ShortcutManager::defaultShortcutForAction(const QString& actionId) const
{
    auto it = m_shortcuts.constFind(actionId);
    if (it == m_shortcuts.constEnd()) {
        return QString();
    }
    return it.value().defaultShortcut;
}

bool ShortcutManager::isUserOverridden(const QString& actionId) const
{
    auto it = m_shortcuts.constFind(actionId);
    if (it == m_shortcuts.constEnd()) {
        return false;
    }
    return !it.value().userShortcut.isEmpty();
}

// ============================================================================
// User Customization
// ============================================================================

void ShortcutManager::setUserShortcut(const QString& actionId, const QString& shortcut)
{
    auto it = m_shortcuts.find(actionId);
    if (it == m_shortcuts.end()) {
        qWarning() << "[ShortcutManager] Cannot set shortcut for unregistered action:" 
                   << actionId;
        return;
    }
    
    // Normalize the shortcut to handle shifted symbols (e.g., "Ctrl+Shift+@" → "Ctrl+Shift+2")
    QString normalizedShortcut = normalizeShortcut(shortcut);
    
    ShortcutEntry& entry = it.value();
    QString oldShortcut = shortcutForAction(actionId);
    
    // If the new shortcut matches the default, clear the override instead
    // This avoids storing redundant overrides
    if (normalizedShortcut == entry.defaultShortcut) {
        entry.userShortcut.clear();
    } else {
        entry.userShortcut = normalizedShortcut;
    }
    
    QString newShortcut = shortcutForAction(actionId);
    if (oldShortcut != newShortcut) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[ShortcutManager] Shortcut changed:" << actionId
                 << oldShortcut << "->" << newShortcut;
#endif
        emit shortcutChanged(actionId, newShortcut);
    }
}

void ShortcutManager::clearUserShortcut(const QString& actionId)
{
    auto it = m_shortcuts.find(actionId);
    if (it == m_shortcuts.end()) {
        return;
    }
    
    ShortcutEntry& entry = it.value();
    
    if (entry.userShortcut.isEmpty()) {
        return;  // No override to clear
    }
    
    QString oldShortcut = entry.userShortcut;
    entry.userShortcut = QString();
    
    QString newShortcut = entry.defaultShortcut;
    if (oldShortcut != newShortcut) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[ShortcutManager] Reverted to default:" << actionId
                 << "->" << newShortcut;
#endif
        emit shortcutChanged(actionId, newShortcut);
    }
}

void ShortcutManager::resetAllToDefaults()
{
    QStringList changedActions;
    
    // Collect all actions that have overrides
    for (auto it = m_shortcuts.begin(); it != m_shortcuts.end(); ++it) {
        if (!it.value().userShortcut.isEmpty()) {
            changedActions.append(it.key());
        }
    }
    
    // Clear overrides and emit signals
    for (const QString& actionId : changedActions) {
        ShortcutEntry& entry = m_shortcuts[actionId];
        entry.userShortcut = QString();
        emit shortcutChanged(actionId, entry.defaultShortcut);
    }
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[ShortcutManager] Reset" << changedActions.size() << "shortcuts to defaults";
#endif
}

// ============================================================================
// Persistence
// ============================================================================

void ShortcutManager::loadUserShortcuts()
{
    QFile file(m_configPath);
    
    if (!file.exists()) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[ShortcutManager] No shortcuts.json found, using defaults";
#endif
        return;
    }
    
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[ShortcutManager] Failed to open shortcuts.json:" 
                   << file.errorString();
        return;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "[ShortcutManager] JSON parse error:" << parseError.errorString();
        return;
    }
    
    if (!doc.isObject()) {
        qWarning() << "[ShortcutManager] Invalid shortcuts.json format (not an object)";
        return;
    }
    
    QJsonObject root = doc.object();
    
    // Check version (for future compatibility)
    int version = root.value("version").toInt(1);
    if (version > 1) {
        qWarning() << "[ShortcutManager] Unsupported shortcuts.json version:" << version;
        // Continue anyway, try to load what we can
    }
    
    // Load overrides
    QJsonObject overrides = root.value("overrides").toObject();
    
    int loadedCount = 0;
    for (auto it = overrides.begin(); it != overrides.end(); ++it) {
        QString actionId = it.key();
        // Normalize the shortcut to handle old format (e.g., "Ctrl+Shift+@" → "Ctrl+Shift+2")
        QString shortcut = normalizeShortcut(it.value().toString());
        
        // Only apply if action exists (it may have been registered by now,
        // or will be registered later - we store the override anyway)
        if (m_shortcuts.contains(actionId)) {
            m_shortcuts[actionId].userShortcut = shortcut;
            loadedCount++;
        } else {
            // Store for later - action might be registered after load
            // Create a placeholder entry
            ShortcutEntry entry;
            entry.defaultShortcut = QString();
            entry.userShortcut = shortcut;
            entry.displayName = actionId;  // Placeholder
            entry.category = "Unknown";
            m_shortcuts.insert(actionId, entry);
        }
    }
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[ShortcutManager] Loaded" << loadedCount << "shortcut overrides";
#endif
}

void ShortcutManager::saveUserShortcuts()
{
    QJsonObject overrides;
    
    // Collect all overrides
    for (auto it = m_shortcuts.constBegin(); it != m_shortcuts.constEnd(); ++it) {
        const ShortcutEntry& entry = it.value();
        if (!entry.userShortcut.isEmpty()) {
            overrides.insert(it.key(), entry.userShortcut);
        }
    }
    
    // Build JSON document
    QJsonObject root;
    root.insert("version", 1);
    root.insert("overrides", overrides);
    
    QJsonDocument doc(root);
    
    // Write to file
    QFile file(m_configPath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "[ShortcutManager] Failed to save shortcuts.json:" 
                   << file.errorString();
        return;
    }
    
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[ShortcutManager] Saved" << overrides.size() << "shortcut overrides";
#endif
}

QString ShortcutManager::configFilePath() const
{
    return m_configPath;
}

// ============================================================================
// Conflict Detection
// ============================================================================

QStringList ShortcutManager::findConflicts(const QString& shortcut,
                                           const QString& excludeActionId) const
{
    QStringList conflicts;
    
    if (shortcut.isEmpty()) {
        return conflicts;
    }
    
    // Normalize the shortcut for comparison
    QKeySequence targetSeq(shortcut);
    if (targetSeq.isEmpty()) {
        return conflicts;
    }
    
    // Get the scope of the action we're checking (if provided)
    Scope excludeScope = Scope::Global;
    if (!excludeActionId.isEmpty()) {
        auto excludeIt = m_shortcuts.constFind(excludeActionId);
        if (excludeIt != m_shortcuts.constEnd()) {
            excludeScope = excludeIt.value().scope;
        }
    }
    
    for (auto it = m_shortcuts.constBegin(); it != m_shortcuts.constEnd(); ++it) {
        if (it.key() == excludeActionId) {
            continue;
        }
        
        QString currentShortcut = shortcutForAction(it.key());
        if (currentShortcut.isEmpty()) {
            continue;
        }
        
        QKeySequence currentSeq(currentShortcut);
        if (currentSeq == targetSeq) {
            // Check if scopes can conflict
            // PagedOnly and EdgelessOnly don't conflict with each other
            if (scopesCanConflict(excludeScope, it.value().scope)) {
                conflicts.append(it.key());
            }
        }
    }
    
    return conflicts;
}

// ============================================================================
// UI Helpers
// ============================================================================

QStringList ShortcutManager::allActionIds() const
{
    return m_shortcuts.keys();
}

QStringList ShortcutManager::allCategories() const
{
    QSet<QString> categories;
    
    for (auto it = m_shortcuts.constBegin(); it != m_shortcuts.constEnd(); ++it) {
        if (!it.value().category.isEmpty()) {
            categories.insert(it.value().category);
        }
    }
    
    QStringList result = categories.values();
    result.sort();
    return result;
}

QStringList ShortcutManager::actionsInCategory(const QString& category) const
{
    QStringList actions;
    
    for (auto it = m_shortcuts.constBegin(); it != m_shortcuts.constEnd(); ++it) {
        if (it.value().category == category) {
            actions.append(it.key());
        }
    }
    
    actions.sort();
    return actions;
}

QString ShortcutManager::displayNameForAction(const QString& actionId) const
{
    auto it = m_shortcuts.constFind(actionId);
    if (it == m_shortcuts.constEnd()) {
        return QString();
    }
    return it.value().displayName;
}

QString ShortcutManager::categoryForAction(const QString& actionId) const
{
    auto it = m_shortcuts.constFind(actionId);
    if (it == m_shortcuts.constEnd()) {
        return QString();
    }
    return it.value().category;
}

