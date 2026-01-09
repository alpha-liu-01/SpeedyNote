# Phase C: LinkObject Implementation Plan

## Overview

This document contains the detailed implementation plan for Phase C of the PDF text extraction feature: LinkObject integration.

**Prerequisites:** Phase A (Text Selection) and Phase B (Highlight Strokes) complete.

**Reference:** Design decisions documented in `docs/PDF_TEXT_EXTRACTION_QA.md` (Questions U through AK).

---

## Phase Summary

| Phase | Description | Est. Lines | Dependencies | Status |
|-------|-------------|------------|--------------|--------|
| **C.0** | Infrastructure (Page UUID with cache, cleanup, insert from file) | ~200 | None | ✅ COMPLETE |
| **C.1** | LinkObject class foundation | ~290 | C.0 | ✅ COMPLETE |
| **C.2** | Selection & manipulation | ~60 | C.1 | ✅ COMPLETE |
| **C.3** | Highlighter integration | ~50 | C.1, C.2 | Pending |
| **C.4** | Keyboard shortcuts | ~155 | C.2 | ✅ COMPLETE |
| **C.5** | Slot functionality (Position, URL) | ~80 | C.1 | Pending |
| **C.6** | Markdown integration | ~95 | C.5 | Pending |
| **Total** | | **~930** | | |

---

# Phase C.0: Infrastructure

**Goal:** Prepare the architecture for LinkObject and fix existing issues.

---

## Task C.0.1: Add UUID to Page ✅ COMPLETE

**Location:** `source/core/Page.h`, `source/core/Page.cpp`

**Purpose:** Enable stable cross-references for LinkObject Position links. UUIDs are NOT used for undo/redo (current behavior of clearing undo on page change is preserved).

**Implemented:**
- Added `QString uuid` member to Page class
- Both constructors generate UUID via `QUuid::createUuid().toString(QUuid::WithoutBraces)`
- Serialization in `toJson()` and deserialization in `fromJson()` with legacy fallback
- Updated `Document::addPage()`, `insertPage()`, and `saveBundle()` to use page's own UUID

**Estimated lines:** ~30

---

## Task C.0.2: Add Cached UUID→Index Lookup ✅ COMPLETE

**Location:** `source/core/Document.h`, `source/core/Document.cpp`

**Design Decision:** Use cached mapping for O(1) lookups. The cache is rebuilt O(n) only when pages change (insert/delete/move), not on every lookup.

**Implemented:**
- Added `m_uuidToIndexCache` (`QHash<QString, int>`) and `m_uuidCacheDirty` members
- Added `rebuildUuidCache()` private method (uses `m_pageOrder` directly - no separate metadata needed)
- Added `pageIndexByUuid()` public method with lazy cache rebuild
- Added `invalidateUuidCache()` public method
- Called `invalidateUuidCache()` in: `addPage()`, `insertPage()`, `removePage()`, `movePage()`, and legacy migration in `saveBundle()`

**Simplification vs Plan:** The plan suggested storing UUID in `PageMetadata`, but this is unnecessary because `m_pageOrder` already contains UUIDs in order. The cache rebuild simply iterates `m_pageOrder`.

**Complexity:**
| Action | Cost |
|--------|------|
| Click position link | O(1) - cache hit |
| Page insert/delete/move | O(1) - just sets dirty flag |
| First lookup after page change | O(n) - rebuild cache |

**Estimated lines:** ~45

---

## Task C.0.3: Store UUID in Page Metadata (Lazy Loading) — SKIPPED

**Reason:** Not needed with current architecture.

The plan suggested adding UUID to `PageMetadata` struct, but this is unnecessary because:
- `m_pageOrder` already contains UUIDs in the correct order
- `rebuildUuidCache()` iterates `m_pageOrder` directly (O(n), no disk I/O)
- The UUID is already stored both in:
  1. `m_pageOrder` (from manifest's `page_order` array)
  2. Each page's JSON file (via `Page::toJson()`)

**No code changes needed.**

---

## ~~Task C.0.3: Update PageUndoAction to Use UUID~~ (REMOVED)

**Decision:** Keep current undo/redo behavior. Undo history is cleared when pages are inserted/deleted (via `clearUndoStacksFrom()`). This is simpler and avoids any O(n) operations in the undo path.

**Rationale:**
- Undo/redo can be rapid (holding Ctrl+Z)
- Current clearing behavior is predictable and safe
- UUID lookup is only needed for LinkObject navigation (user-initiated, not rapid)

---

## Task C.0.4: Lazy Asset Cleanup on Document Close ✅ COMPLETE

**Location:** `source/core/Document.h`, `source/core/Document.cpp`, `source/core/DocumentManager.cpp`

**Implemented:**
- Added `cleanupOrphanedAssets()` declaration to Document.h with documentation
- Implemented `cleanupOrphanedAssets()` in Document.cpp:
  - Uses helper lambda to scan pages for ImageObject references
  - Handles both paged mode (scans all pages via `page(i)`) and edgeless mode (scans loaded tiles)
  - Lists files in assets/images and deletes unreferenced ones
  - Debug logging for deleted files
- Called in `DocumentManager::closeDocument()` before document deletion

**Note:** For edgeless mode, only loaded tiles are scanned. Evicted tiles are not loaded for cleanup (performance tradeoff). Orphans from evicted tiles remain until tile is loaded again.

**Estimated lines:** ~60

---

## Task C.0.5: Insert Image from File Dialog ✅ COMPLETE

**Location:** `source/core/DocumentViewport.h`, `source/core/DocumentViewport.cpp`

**Implemented:**
- Added `insertImageFromDialog()` declaration to header
- Implemented method that opens `QFileDialog::getOpenFileName()` and calls `insertImageFromFile()`
- Added `#include <QFileDialog>`
- Supports PNG, JPG, JPEG, BMP, GIF, WebP formats
- Image is positioned at viewport center (existing behavior in `insertImageFromFile()`)

**Better UX: Click-to-place mode** (deferred to C.4 with keyboard shortcuts)

**Estimated lines:** ~20

---

## Task C.0.6: Verify Page Insert/Delete with Objects

**Testing checklist (manual):**
- [ ] Insert page before page with objects → objects stay on correct page
- [ ] Delete page with objects → objects deleted, undo restores them
- [ ] Undo page insert → objects on subsequent pages still work
- [ ] Undo page delete → objects restored correctly

**If issues found:** Fix in this phase before proceeding.

**Estimated lines:** ~10 (any fixes needed)

---

## C.0 Testing Checklist

- [ ] New pages have UUID generated
- [ ] Loading old documents generates UUID for pages without one
- [ ] `pageIndexByUuid()` returns correct index (O(1) after cache built)
- [ ] `pageIndexByUuid()` returns -1 for unknown UUID
- [ ] Cache is invalidated on page insert/delete/move
- [ ] First lookup after page change triggers cache rebuild
- [ ] Multiple lookups without page changes are O(1)
- [ ] Orphaned assets cleaned up on document close
- [ ] Insert image from file dialog works

---

# Phase C.1: LinkObject Class Foundation ✅ COMPLETE

**Goal:** Create the LinkObject class with 3-slot architecture.

**Completed:** All 6 tasks implemented and tested.

---

## Task C.1.1: Create LinkSlot Struct ✅ COMPLETE

**Location:** `source/objects/LinkObject.h`

```cpp
#pragma once

#include "InsertedObject.h"
#include <QColor>
#include <QUrl>

/**
 * @brief A single link slot in a LinkObject.
 * 
 * Each LinkObject has 3 slots that can each hold a different type of link.
 */
struct LinkSlot {
    enum class Type {
        Empty,      ///< Slot is unused
        Position,   ///< Links to a page position (pageUuid + coordinates)
        Url,        ///< Links to an external URL
        Markdown    ///< Links to a markdown note (by ID)
    };
    
    Type type = Type::Empty;
    
    // Position link data
    QString targetPageUuid;
    QPointF targetPosition;
    
    // URL link data
    QString url;
    
    // Markdown link data
    QString markdownNoteId;
    
    // Serialization
    QJsonObject toJson() const;
    static LinkSlot fromJson(const QJsonObject& obj);
    
    bool isEmpty() const { return type == Type::Empty; }
    void clear() { *this = LinkSlot(); }
};
```

**Estimated lines:** ~40

---

## Task C.1.2: Create LinkObject Class ✅ COMPLETE

**Location:** `source/objects/LinkObject.h`, `source/objects/LinkObject.cpp`

```cpp
// In LinkObject.h

/**
 * @brief A link/annotation object with 3 configurable link slots.
 * 
 * LinkObject is created:
 * - Automatically when highlighting PDF text (description = extracted text)
 * - Manually via ObjectSelect tool (description empty or user-entered)
 * 
 * Each slot can independently link to:
 * - A position in the document (page + coordinates)
 * - An external URL
 * - A markdown note
 */
class LinkObject : public InsertedObject {
public:
    static constexpr int SLOT_COUNT = 3;
    static constexpr qreal ICON_SIZE = 24.0;  ///< Icon size at 100% zoom
    
    // Content
    QString description;    ///< Extracted text or user description
    QColor iconColor = QColor(100, 100, 100, 180);  ///< Icon tint color
    
    // The 3 link slots
    LinkSlot slots[SLOT_COUNT];
    
    // Constructor
    LinkObject();
    
    // InsertedObject interface
    void render(QPainter& painter, qreal zoom) const override;
    QString type() const override { return QStringLiteral("link"); }
    QJsonObject toJson() const override;
    void loadFromJson(const QJsonObject& obj) override;
    bool containsPoint(const QPointF& pt) const override;
    
    // LinkObject-specific methods
    int filledSlotCount() const;
    bool hasEmptySlot() const;
    int firstEmptySlotIndex() const;
    
    // Copy with back-link
    std::unique_ptr<LinkObject> cloneWithBackLink(const QString& sourcePageUuid) const;
    
private:
    // Icon rendering (cached)
    static QPixmap s_iconPixmap;
    static bool s_iconLoaded;
    void ensureIconLoaded() const;
    QPixmap tintedIcon(const QColor& color, qreal size) const;
};
```

**Estimated lines:** ~60 (header)

---

## Task C.1.3: Implement LinkObject Methods ✅ COMPLETE

**Location:** `source/objects/LinkObject.cpp`

```cpp
#include "LinkObject.h"
#include <QPainter>
#include <QJsonArray>

// Static icon cache
QPixmap LinkObject::s_iconPixmap;
bool LinkObject::s_iconLoaded = false;

LinkObject::LinkObject()
{
    // Default size is icon size
    size = QSizeF(ICON_SIZE, ICON_SIZE);
}

void LinkObject::render(QPainter& painter, qreal zoom) const
{
    if (!visible) return;
    
    ensureIconLoaded();
    
    qreal scaledSize = ICON_SIZE * zoom;
    QPixmap icon = tintedIcon(iconColor, scaledSize);
    
    QPointF drawPos(position.x() * zoom, position.y() * zoom);
    painter.drawPixmap(drawPos.toPoint(), icon);
}

void LinkObject::ensureIconLoaded() const
{
    if (!s_iconLoaded) {
        // Load from resources (white icon for tinting)
        s_iconPixmap = QPixmap(":/icons/link_quote.svg");
        if (s_iconPixmap.isNull()) {
            // Fallback: create simple icon programmatically
            s_iconPixmap = QPixmap(24, 24);
            s_iconPixmap.fill(Qt::transparent);
            QPainter p(&s_iconPixmap);
            p.setPen(QPen(Qt::white, 2));
            p.setFont(QFont("Arial", 16, QFont::Bold));
            p.drawText(s_iconPixmap.rect(), Qt::AlignCenter, "\"");
        }
        s_iconLoaded = true;
    }
}

QPixmap LinkObject::tintedIcon(const QColor& color, qreal size) const
{
    ensureIconLoaded();
    
    // Scale icon
    QPixmap scaled = s_iconPixmap.scaled(
        size, size, 
        Qt::KeepAspectRatio, 
        Qt::SmoothTransformation
    );
    
    // Apply color tint
    QImage img = scaled.toImage();
    for (int y = 0; y < img.height(); y++) {
        for (int x = 0; x < img.width(); x++) {
            QColor pixel = img.pixelColor(x, y);
            if (pixel.alpha() > 0) {
                pixel.setRed(color.red());
                pixel.setGreen(color.green());
                pixel.setBlue(color.blue());
                pixel.setAlpha(pixel.alpha() * color.alpha() / 255);
                img.setPixelColor(x, y, pixel);
            }
        }
    }
    
    return QPixmap::fromImage(img);
}

bool LinkObject::containsPoint(const QPointF& pt) const
{
    // Use icon bounds for hit testing
    QRectF iconRect(position, QSizeF(ICON_SIZE, ICON_SIZE));
    return iconRect.contains(pt);
}

QJsonObject LinkObject::toJson() const
{
    QJsonObject obj = InsertedObject::toJson();
    
    obj["description"] = description;
    obj["iconColor"] = iconColor.name(QColor::HexArgb);
    
    QJsonArray slotsArray;
    for (int i = 0; i < SLOT_COUNT; i++) {
        slotsArray.append(slots[i].toJson());
    }
    obj["slots"] = slotsArray;
    
    return obj;
}

void LinkObject::loadFromJson(const QJsonObject& obj)
{
    InsertedObject::loadFromJson(obj);
    
    description = obj["description"].toString();
    iconColor = QColor(obj["iconColor"].toString());
    if (!iconColor.isValid()) {
        iconColor = QColor(100, 100, 100, 180);
    }
    
    QJsonArray slotsArray = obj["slots"].toArray();
    for (int i = 0; i < SLOT_COUNT && i < slotsArray.size(); i++) {
        slots[i] = LinkSlot::fromJson(slotsArray[i].toObject());
    }
}

int LinkObject::filledSlotCount() const
{
    int count = 0;
    for (int i = 0; i < SLOT_COUNT; i++) {
        if (!slots[i].isEmpty()) count++;
    }
    return count;
}

bool LinkObject::hasEmptySlot() const
{
    return firstEmptySlotIndex() >= 0;
}

int LinkObject::firstEmptySlotIndex() const
{
    for (int i = 0; i < SLOT_COUNT; i++) {
        if (slots[i].isEmpty()) return i;
    }
    return -1;
}

std::unique_ptr<LinkObject> LinkObject::cloneWithBackLink(const QString& sourcePageUuid) const
{
    auto copy = std::make_unique<LinkObject>();
    copy->description = description;
    copy->iconColor = iconColor;
    // Note: position will be set by caller
    
    // Auto-fill slot 0 with back-link to original position
    copy->slots[0].type = LinkSlot::Type::Position;
    copy->slots[0].targetPageUuid = sourcePageUuid;
    copy->slots[0].targetPosition = position;
    
    return copy;
}
```

**Estimated lines:** ~130

---

## Task C.1.4: Implement LinkSlot Serialization ✅ COMPLETE

**Location:** `source/objects/LinkObject.cpp`

```cpp
QJsonObject LinkSlot::toJson() const
{
    QJsonObject obj;
    
    switch (type) {
        case Type::Empty:
            obj["type"] = "empty";
            break;
        case Type::Position:
            obj["type"] = "position";
            obj["pageUuid"] = targetPageUuid;
            obj["x"] = targetPosition.x();
            obj["y"] = targetPosition.y();
            break;
        case Type::Url:
            obj["type"] = "url";
            obj["url"] = url;
            break;
        case Type::Markdown:
            obj["type"] = "markdown";
            obj["noteId"] = markdownNoteId;
            break;
    }
    
    return obj;
}

LinkSlot LinkSlot::fromJson(const QJsonObject& obj)
{
    LinkSlot slot;
    QString typeStr = obj["type"].toString();
    
    if (typeStr == "position") {
        slot.type = Type::Position;
        slot.targetPageUuid = obj["pageUuid"].toString();
        slot.targetPosition = QPointF(obj["x"].toDouble(), obj["y"].toDouble());
    } else if (typeStr == "url") {
        slot.type = Type::Url;
        slot.url = obj["url"].toString();
    } else if (typeStr == "markdown") {
        slot.type = Type::Markdown;
        slot.markdownNoteId = obj["noteId"].toString();
    } else {
        slot.type = Type::Empty;
    }
    
    return slot;
}
```

**Estimated lines:** ~50

---

## Task C.1.5: Register LinkObject in Factory ✅ COMPLETE

**Location:** `source/objects/InsertedObject.cpp`

```cpp
#include "LinkObject.h"  // Add include

std::unique_ptr<InsertedObject> InsertedObject::fromJson(const QJsonObject& obj)
{
    QString objectType = obj["type"].toString();
    
    std::unique_ptr<InsertedObject> result;
    
    if (objectType == "image") {
        result = std::make_unique<ImageObject>();
    }
    else if (objectType == "link") {  // Add this
        result = std::make_unique<LinkObject>();
    }
    // Future object types...
    else {
        return nullptr;
    }
    
    if (result) {
        result->loadFromJson(obj);
    }
    
    return result;
}
```

**Estimated lines:** ~5

---

## Task C.1.6: Update CMakeLists.txt ✅ COMPLETE

**Location:** `CMakeLists.txt`

Add to sources:
```cmake
source/objects/LinkObject.h
source/objects/LinkObject.cpp
```

**Estimated lines:** ~2

---

## C.1 Testing Checklist

- [x] LinkObject can be created programmatically
- [x] LinkObject renders icon at correct position
- [x] Icon tinting works (different colors)
- [x] containsPoint() correctly detects clicks on icon
- [x] toJson() / fromJson() round-trips correctly
- [x] Slots serialize/deserialize correctly
- [x] Factory creates LinkObject from JSON
- [x] cloneWithBackLink() works

**Unit Tests:** `./NoteApp --test-linkobject` - ALL PASSED

**Implementation Notes:**
- Renamed `slots` → `linkSlots` to avoid Qt keyword conflict
- Changed static `QPixmap` to function-local static to avoid startup crash
- Icon loaded from `:/resources/icons/link_quote.svg`

---

# Phase C.2: LinkObject Selection & Manipulation

**Goal:** Enable selecting, moving, and deleting LinkObjects.

---

## Task C.2.1: Hit Testing for LinkObject

**Current state:** ObjectSelect tool already does hit testing via `page->objectAtPoint()`.

**Verify:** The existing code should work because `LinkObject::containsPoint()` is implemented.

**Test:** Click on LinkObject with ObjectSelect tool → should select.

**Estimated lines:** ~0 (should work already)

---

## Task C.2.2: Selection Visual for LinkObject

**Current state:** Selected objects get selection handles (see `renderSelectionHandles()`).

**Issue:** LinkObject is 24x24, might look odd with 8 resize handles.

**Options:**
1. Use same handles as ImageObject (current behavior)
2. Only show move handle (no resize)
3. Show smaller handles

**Recommendation:** Option 1 for simplicity - same code path as ImageObject.

**If needed:** Add check in resize handler to skip resize for LinkObject (only move).

```cpp
// In updateScaleFromHandle() or similar
if (auto* link = dynamic_cast<LinkObject*>(m_selectedObjects[0])) {
    // LinkObject doesn't resize - just move
    return;
}
```

**Estimated lines:** ~10

---

## Task C.2.3: Copy/Paste with Back-Link

**Location:** `source/core/DocumentViewport.cpp`

**Modify `copySelectedObjects()`:**
```cpp
void DocumentViewport::copySelectedObjects()
{
    m_objectClipboard.clear();
    
    for (InsertedObject* obj : m_selectedObjects) {
        // For LinkObject, use cloneWithBackLink
        if (auto* link = dynamic_cast<LinkObject*>(obj)) {
            // Get source page UUID
            QString sourcePageUuid;
            // ...find which page contains this object...
            
            auto clone = link->cloneWithBackLink(sourcePageUuid);
            m_objectClipboard.append(clone->toJson());
        } else {
            m_objectClipboard.append(obj->toJson());
        }
    }
}
```

**Estimated lines:** ~20

---

## Task C.2.4: Mode Switching on Selection

**Location:** `source/core/DocumentViewport.cpp`

**When selecting an object, switch mode to match:**

```cpp
void DocumentViewport::selectObject(InsertedObject* obj, bool addToSelection)
{
    // ...existing selection logic...
    
    // Auto-switch mode based on selected object type
    if (m_selectedObjects.size() == 1) {
        InsertedObject* selected = m_selectedObjects[0];
        if (dynamic_cast<ImageObject*>(selected)) {
            m_objectInsertMode = ObjectInsertMode::Image;
        } else if (dynamic_cast<LinkObject*>(selected)) {
            m_objectInsertMode = ObjectInsertMode::Link;
        }
        emit objectInsertModeChanged(m_objectInsertMode);
    }
}
```

**Add mode enum:**
```cpp
// In DocumentViewport.h
enum class ObjectInsertMode { Image, Link };
Q_ENUM(ObjectInsertMode)

ObjectInsertMode m_objectInsertMode = ObjectInsertMode::Image;

signals:
    void objectInsertModeChanged(ObjectInsertMode mode);
```

**Estimated lines:** ~30

---

## C.2 Testing Checklist ✅ ALL PASSED

- [x] Click LinkObject with ObjectSelect → selects it
- [x] Selection handles appear around LinkObject
- [x] Drag LinkObject → moves it
- [x] Delete key → deletes LinkObject
- [x] Undo delete → restores LinkObject
- [x] Copy LinkObject → clipboard contains serialized data
- [x] Paste LinkObject → new object with back-link in slot 0
- [x] Selecting ImageObject switches to ImgInsert mode
- [x] Selecting LinkObject switches to LinkInsert mode

---

# Phase C.3: Highlighter Integration

**Goal:** Auto-create LinkObject when creating highlight strokes.

---

## Task C.3.1: Modify createHighlightStrokes()

**Location:** `source/core/DocumentViewport.cpp`

```cpp
QVector<QString> DocumentViewport::createHighlightStrokes()
{
    QVector<QString> createdIds;
    
    // ...existing validation...
    
    // Get the page where selection exists
    int pageIndex = m_textSelection.pageIndex;
    Page* page = m_document->page(pageIndex);
    if (!page) {
        return createdIds;
    }
    
    // ...existing stroke creation code...
    
    // Phase C.3: Create LinkObject alongside strokes
    if (!m_textSelection.highlightRects.isEmpty()) {
        createLinkObjectForHighlight(pageIndex);
    }
    
    // ...rest of function...
    return createdIds;
}
```

**Estimated lines:** ~5 (modification)

---

## Task C.3.2: Implement createLinkObjectForHighlight()

**Location:** `source/core/DocumentViewport.cpp`

```cpp
// In DocumentViewport.h
void createLinkObjectForHighlight(int pageIndex);

// In DocumentViewport.cpp
void DocumentViewport::createLinkObjectForHighlight(int pageIndex)
{
    if (!m_document || m_textSelection.highlightRects.isEmpty()) {
        return;
    }
    
    Page* page = m_document->page(pageIndex);
    if (!page) {
        return;
    }
    
    // Create LinkObject
    auto linkObj = std::make_unique<LinkObject>();
    
    // Position at start of first highlight rect (convert from PDF to page coords)
    QRectF firstRect = m_textSelection.highlightRects[0];
    linkObj->position = QPointF(
        firstRect.x() * PDF_TO_PAGE_SCALE,
        firstRect.y() * PDF_TO_PAGE_SCALE
    );
    
    // Set description to extracted text
    linkObj->description = m_textSelection.selectedText;
    
    // Use highlighter color for icon
    linkObj->iconColor = m_highlighterColor;
    linkObj->iconColor.setAlpha(180);
    
    // Set default affinity (activeLayer - 1)
    int activeLayer = page->activeLayerIndex;
    linkObj->setLayerAffinity(activeLayer - 1);
    
    // Keep raw pointer for undo
    LinkObject* rawPtr = linkObj.get();
    
    // Add to page
    page->addObject(std::move(linkObj));
    
    // Push undo action
    pushObjectInsertUndo(rawPtr, pageIndex, {});  // Empty tile coord for paged mode
    
#ifdef QT_DEBUG
    qDebug() << "Created LinkObject for highlight on page" << pageIndex
             << "description:" << rawPtr->description.left(30);
#endif
}
```

**Estimated lines:** ~45

---

## C.3 Testing Checklist

- [ ] Create highlight with auto-highlight ON → stroke + LinkObject created
- [ ] LinkObject positioned at start of first highlight line
- [ ] LinkObject description = extracted text
- [ ] LinkObject icon color matches highlighter color
- [ ] Undo → removes both stroke and LinkObject
- [ ] Create highlight with auto-highlight OFF → no LinkObject

---

# Phase C.4: Keyboard Shortcuts

**Goal:** Implement temporary shortcuts until subtoolbar is available.

---

## Task C.4.1: Add ObjectInsertMode State

**Location:** `source/core/DocumentViewport.h`

```cpp
// Object insertion modes
enum class ObjectInsertMode { Image, Link };
enum class ObjectActionMode { Create, Select };

ObjectInsertMode m_objectInsertMode = ObjectInsertMode::Image;
ObjectActionMode m_objectActionMode = ObjectActionMode::Select;  // Default to select

signals:
    void objectInsertModeChanged(ObjectInsertMode mode);
    void objectActionModeChanged(ObjectActionMode mode);
```

**Estimated lines:** ~15

---

## Task C.4.2: Implement Keyboard Shortcuts

**Location:** `source/core/DocumentViewport.cpp` (keyPressEvent)

```cpp
// In keyPressEvent, add section for ObjectSelect tool:
if (m_currentTool == ToolType::ObjectSelect) {
    // Mode switching
    if (event->modifiers() == Qt::ControlModifier) {
        // Ctrl+< → Image mode
        if (event->key() == Qt::Key_Less || event->key() == Qt::Key_Comma) {
            m_objectInsertMode = ObjectInsertMode::Image;
            emit objectInsertModeChanged(m_objectInsertMode);
            qDebug() << "Switched to Image insert mode";
            event->accept();
            return;
        }
        // Ctrl+> → Link mode
        if (event->key() == Qt::Key_Greater || event->key() == Qt::Key_Period) {
            m_objectInsertMode = ObjectInsertMode::Link;
            emit objectInsertModeChanged(m_objectInsertMode);
            qDebug() << "Switched to Link insert mode";
            event->accept();
            return;
        }
        // Ctrl+6 → Create mode
        if (event->key() == Qt::Key_6) {
            m_objectActionMode = ObjectActionMode::Create;
            emit objectActionModeChanged(m_objectActionMode);
            qDebug() << "Switched to Create mode";
            event->accept();
            return;
        }
        // Ctrl+7 → Select mode
        if (event->key() == Qt::Key_7) {
            m_objectActionMode = ObjectActionMode::Select;
            emit objectActionModeChanged(m_objectActionMode);
            qDebug() << "Switched to Select mode";
            event->accept();
            return;
        }
        // Ctrl+8/9/0 → Access slots
        if (event->key() == Qt::Key_8) {
            activateLinkSlot(0);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_9) {
            activateLinkSlot(1);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_0) {
            activateLinkSlot(2);
            event->accept();
            return;
        }
    }
}
```

**Estimated lines:** ~50

---

## Task C.4.3: Implement activateLinkSlot()

**Location:** `source/core/DocumentViewport.cpp`

```cpp
// In DocumentViewport.h
void activateLinkSlot(int slotIndex);

// In DocumentViewport.cpp
void DocumentViewport::activateLinkSlot(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= LinkObject::SLOT_COUNT) {
        return;
    }
    
    // Must have exactly one LinkObject selected
    if (m_selectedObjects.size() != 1) {
        qDebug() << "activateLinkSlot: Need exactly one object selected";
        return;
    }
    
    LinkObject* link = dynamic_cast<LinkObject*>(m_selectedObjects[0]);
    if (!link) {
        qDebug() << "activateLinkSlot: Selected object is not a LinkObject";
        return;
    }
    
    const LinkSlot& slot = link->slots[slotIndex];
    
    if (slot.isEmpty()) {
        // Empty slot - show menu to add link (Phase C.5)
        qDebug() << "Slot" << slotIndex << "is empty - TODO: show add menu";
        // For now, just log
        return;
    }
    
    // Activate the slot based on type
    switch (slot.type) {
        case LinkSlot::Type::Position:
            navigateToPosition(slot.targetPageUuid, slot.targetPosition);
            break;
        case LinkSlot::Type::Url:
            QDesktopServices::openUrl(QUrl(slot.url));
            break;
        case LinkSlot::Type::Markdown:
            // Phase C.6
            qDebug() << "TODO: Open markdown note" << slot.markdownNoteId;
            break;
        default:
            break;
    }
}
```

**Estimated lines:** ~45

---

## Task C.4.4: Implement Create Mode Behavior

**Location:** `source/core/DocumentViewport.cpp`

**Modify handlePointerPress for ObjectSelect tool:**

```cpp
// In handlePointerPress, ObjectSelect tool section:
if (m_currentTool == ToolType::ObjectSelect) {
    if (m_objectActionMode == ObjectActionMode::Create) {
        // Create mode - insert object at click position
        PageHit hit = viewportToPage(pe.viewportPos);
        if (!hit.valid()) return;
        
        if (m_objectInsertMode == ObjectInsertMode::Image) {
            // Open file dialog and insert image at position
            insertImageAtPosition(hit.pageIndex, hit.pagePoint);
        } else {
            // Create empty LinkObject at position
            createLinkObjectAtPosition(hit.pageIndex, hit.pagePoint);
        }
        return;
    }
    
    // Select mode - existing selection logic
    // ...
}
```

**Estimated lines:** ~20

---

## Task C.4.5: Implement createLinkObjectAtPosition()

**Location:** `source/core/DocumentViewport.cpp`

```cpp
void DocumentViewport::createLinkObjectAtPosition(int pageIndex, const QPointF& pagePos)
{
    if (!m_document) return;
    
    Page* page = m_document->page(pageIndex);
    if (!page) return;
    
    auto linkObj = std::make_unique<LinkObject>();
    linkObj->position = pagePos;
    linkObj->description = QString();  // Empty for manual creation
    
    // Default affinity
    int activeLayer = page->activeLayerIndex;
    linkObj->setLayerAffinity(activeLayer - 1);
    
    LinkObject* rawPtr = linkObj.get();
    page->addObject(std::move(linkObj));
    
    pushObjectInsertUndo(rawPtr, pageIndex, {});
    
    // Select the new object
    deselectAllObjects();
    selectObject(rawPtr, false);
    
    emit documentModified();
    update();
}
```

**Estimated lines:** ~25

---

## C.4 Testing Checklist ✅ ALL PASSED

- [x] Ctrl+< switches to Image mode
- [x] Ctrl+> switches to Link mode
- [x] Ctrl+6 switches to Create mode
- [x] Ctrl+7 switches to Select mode
- [x] In Create+Image mode, click → file dialog → image inserted
- [x] In Create+Link mode, click → empty LinkObject created
- [x] Ctrl+8/9/0 activates slots (logs for empty slots)
- [x] Mode signals emitted correctly
- [x] LinkObject serialization verified (correct JSON structure)

---

# Phase C.5: Slot Functionality

**Goal:** Implement Position and URL slot navigation.

---

## Task C.5.1: Implement navigateToPosition()

**Location:** `source/core/DocumentViewport.cpp`

```cpp
// In DocumentViewport.h
void navigateToPosition(const QString& pageUuid, const QPointF& position);

// In DocumentViewport.cpp
void DocumentViewport::navigateToPosition(const QString& pageUuid, const QPointF& position)
{
    if (!m_document || pageUuid.isEmpty()) {
        qDebug() << "navigateToPosition: Invalid target";
        return;
    }
    
    int targetPageIndex = m_document->pageIndexByUuid(pageUuid);
    if (targetPageIndex < 0) {
        qDebug() << "navigateToPosition: Page not found for UUID" << pageUuid;
        // TODO: Show user message "Target not found"
        return;
    }
    
    // Navigate to page
    scrollToPage(targetPageIndex);
    
    // Center view on position
    QPointF targetDocPos = pageToDocument(targetPageIndex, position);
    
    // Calculate pan to center this position
    QPointF viewportCenter(width() / 2.0, height() / 2.0);
    QPointF targetViewportPos = documentToViewport(targetDocPos);
    QPointF panDelta = viewportCenter - targetViewportPos;
    
    setPanOffset(m_panOffset + panDelta);
    
    update();
    
#ifdef QT_DEBUG
    qDebug() << "Navigated to page" << targetPageIndex << "position" << position;
#endif
}
```

**Estimated lines:** ~35

---

## Task C.5.2: URL Slot Opening

**Already implemented in activateLinkSlot():**
```cpp
case LinkSlot::Type::Url:
    QDesktopServices::openUrl(QUrl(slot.url));
    break;
```

**Add include:** `#include <QDesktopServices>`

**Estimated lines:** ~2

---

## Task C.5.3: Add Link to Slot (Basic UI)

**For now, use a simple input dialog. Full UI deferred to subtoolbar.**

```cpp
void DocumentViewport::addLinkToSlot(int slotIndex)
{
    if (m_selectedObjects.size() != 1) return;
    
    LinkObject* link = dynamic_cast<LinkObject*>(m_selectedObjects[0]);
    if (!link) return;
    
    if (slotIndex < 0 || slotIndex >= LinkObject::SLOT_COUNT) return;
    
    // Simple menu
    QMenu menu;
    QAction* posAction = menu.addAction("Add Position Link");
    QAction* urlAction = menu.addAction("Add URL Link");
    QAction* mdAction = menu.addAction("Add Markdown Note");
    
    QAction* selected = menu.exec(QCursor::pos());
    
    if (selected == posAction) {
        // TODO: Enter "pick position" mode
        qDebug() << "TODO: Pick position mode";
    } else if (selected == urlAction) {
        QString url = QInputDialog::getText(this, "Add URL", "Enter URL:");
        if (!url.isEmpty()) {
            link->slots[slotIndex].type = LinkSlot::Type::Url;
            link->slots[slotIndex].url = url;
            emit documentModified();
        }
    } else if (selected == mdAction) {
        // Phase C.6
        qDebug() << "TODO: Create/link markdown note";
    }
}
```

**Estimated lines:** ~40

---

## Task C.5.4: Position Picking Mode (Optional)

**Deferred to later** - complex UX. For now, position links are only created via copy with back-link.

---

## C.5 Testing Checklist

- [ ] Click Position slot → navigates to target page/position
- [ ] Click URL slot → opens URL in browser
- [ ] Empty slot → shows add menu
- [ ] Add URL via dialog → slot populated
- [ ] Navigate to non-existent page → shows error message

---

# Phase C.6: Markdown Integration

**Goal:** Connect LinkObject to markdown notes.

**Note:** This phase depends on existing markdown infrastructure. May need adjustment based on what exists.

---

## Task C.6.1: Assess Existing Markdown System

**Before implementing:**
- [ ] Review existing markdown note storage
- [ ] Understand note ID format
- [ ] Understand how notes are loaded/saved

**Estimated lines:** ~0 (research)

---

## Task C.6.2: Create Markdown Note from Slot

```cpp
void DocumentViewport::createMarkdownNoteForSlot(int slotIndex)
{
    if (m_selectedObjects.size() != 1) return;
    
    LinkObject* link = dynamic_cast<LinkObject*>(m_selectedObjects[0]);
    if (!link) return;
    
    // Generate note ID
    QString noteId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    
    // Create note file (path depends on existing markdown system)
    // ...
    
    // Populate slot
    link->slots[slotIndex].type = LinkSlot::Type::Markdown;
    link->slots[slotIndex].markdownNoteId = noteId;
    
    // Open markdown editor
    emit openMarkdownNote(noteId);
    
    emit documentModified();
}
```

**Estimated lines:** ~40

---

## Task C.6.3: Open Existing Markdown Note

```cpp
void DocumentViewport::openMarkdownNote(const QString& noteId)
{
    // Check if note exists
    // If not, remove broken reference from slot
    // If yes, emit signal to open editor
    
    emit requestOpenMarkdownNote(noteId);
}
```

**Estimated lines:** ~20

---

## Task C.6.4: Soft Cascade Delete

**On LinkObject delete, orphan markdown notes (don't delete them).**

This is the default behavior - notes are separate files. No special code needed unless we want to notify the user.

**Optional:** Show message "This LinkObject has linked notes that will become orphaned."

**Estimated lines:** ~20

---

## Task C.6.5: Clean Broken References

**When accessing a markdown slot that doesn't exist:**

```cpp
// In activateLinkSlot(), Markdown case:
case LinkSlot::Type::Markdown:
    if (!markdownNoteExists(slot.markdownNoteId)) {
        qDebug() << "Markdown note not found, clearing reference";
        link->slots[slotIndex].clear();
        emit documentModified();
        // TODO: Notify user
    } else {
        emit requestOpenMarkdownNote(slot.markdownNoteId);
    }
    break;
```

**Estimated lines:** ~15

---

## C.6 Testing Checklist

- [ ] Create markdown note from empty slot
- [ ] Open existing markdown note from slot
- [ ] Delete LinkObject → notes become orphaned (not deleted)
- [ ] Access broken markdown reference → reference cleared
- [ ] Note editor opens when slot activated

---

# Summary

## Total Estimated Lines by Phase

| Phase | Lines |
|-------|-------|
| C.0 | ~200 |
| C.1 | ~290 |
| C.2 | ~60 |
| C.3 | ~50 |
| C.4 | ~155 |
| C.5 | ~80 |
| C.6 | ~95 |
| **Total** | **~930** |

## Implementation Order

1. **C.0** - Must be first (Page UUID affects everything)
2. **C.1** - LinkObject class
3. **C.2** - Selection/manipulation
4. **C.3** - Highlighter integration
5. **C.4** - Keyboard shortcuts
6. **C.5** - Slot functionality
7. **C.6** - Markdown integration

## Files to Create/Modify

**New files:**
- `source/objects/LinkObject.h`
- `source/objects/LinkObject.cpp`

**Modified files:**
- `source/core/Page.h` / `Page.cpp` (UUID)
- `source/core/Document.h` / `Document.cpp` (pageByUuid, cleanup)
- `source/core/DocumentViewport.h` / `DocumentViewport.cpp` (most changes)
- `source/objects/InsertedObject.cpp` (factory)
- `CMakeLists.txt`

---

## Ready to Begin?

Start with Phase C.0 (Infrastructure) when ready.

