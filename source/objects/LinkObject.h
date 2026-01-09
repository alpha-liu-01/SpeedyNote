#pragma once

#include "InsertedObject.h"
#include <QColor>
#include <QUrl>
#include <QPainter>
#include <QPixmap>
#include <memory>

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
    
    // The 3 link slots (named linkSlots to avoid Qt 'slots' keyword conflict)
    LinkSlot linkSlots[SLOT_COUNT];
    
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
    // Icon rendering (lazy-loaded to avoid QPixmap before QApplication)
    static const QPixmap& iconPixmap();
    void ensureIconLoaded() const;  // Kept for API compatibility, now empty
    QPixmap tintedIcon(const QColor& color, qreal size) const;
    
    // Render cache to avoid recreating tinted icon every frame
    mutable QPixmap m_cachedTintedIcon;
    mutable QColor m_cachedColor;
    mutable qreal m_cachedSize = 0.0;
};

