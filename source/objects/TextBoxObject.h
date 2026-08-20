#pragma once

#include "InsertedObject.h"
#include <QColor>
#include <QFont>
#include <QVector>
#include <memory>

class QTextDocument;

enum class TextAlignment { Left, Center, Right };

struct TextBoxState {
    QString text;
    QString fontFamily;
    qreal fontSize = 0.0;
    QColor fontColor;
    QColor backgroundColor;
    TextAlignment alignment = TextAlignment::Left;
    bool showBorder = true;
    int textLayoutVersion = 0;
    QPointF position;
    QSizeF size;
    qreal rotation = 0.0;
};

struct TextBoxLayoutInput {
    QString text;
    QString fontFamily;
    qreal fontSize = 0.0;
    QColor fontColor;
    TextAlignment alignment = TextAlignment::Left;
    QSizeF objectSize;
    int textLayoutVersion = 0;
    bool userTextBox = true;
    qreal legacyZoom = 1.0;
};

/**
 * A caller-thread-owned text layout. PdfSearchEngine deliberately builds one
 * of these locally instead of touching the viewport's cached QTextDocument.
 */
struct TextBoxLayoutResult {
    TextBoxLayoutResult();
    ~TextBoxLayoutResult();

    std::unique_ptr<QTextDocument> document;
    QRectF contentRect;
    QFont plainFont;
    Qt::Alignment plainAlignment = Qt::AlignLeft;
    qreal renderScale = 1.0;
    qreal normalizedHeight = 0.0;
    bool versioned = false;
    bool markdownDocument = false;
    bool legacySingleLine = false;

    QVector<QRectF> findTextRects(const QString& query,
                                  bool caseSensitive,
                                  bool wholeWord) const;
    QString anchorAtObjectPoint(const QPointF& objectPoint) const;
};

class TextBoxObject : public InsertedObject {
public:
    static constexpr int CURRENT_TEXT_LAYOUT_VERSION = 1;
    static constexpr qreal DEFAULT_BASE_FONT_SIZE = 16.0;
    static constexpr qreal DEFAULT_CREATION_WIDTH = 220.0;
    static constexpr qreal MINIMUM_WIDTH = 40.0;
    static constexpr qreal CONTENT_PADDING = 6.0;
    static constexpr qreal LEGACY_SCREEN_PADDING = 2.0;

    QString text;
    QString fontFamily;
    qreal fontSize = 0.0;                          // 0 = auto-calculated from boundingRect height
    QColor fontColor = QColor(60, 60, 60);
    QColor backgroundColor = QColor(255, 255, 255, 160);
    TextAlignment alignment = TextAlignment::Left;
    bool showBorder = true;   // true for manual textbox, false for ocr_text
    int textLayoutVersion = 0; ///< 0/missing = legacy rectangle-driven layout

    TextBoxObject() = default;
    ~TextBoxObject() override;

    void render(QPainter& painter, qreal zoom) const override;
    void renderWithTextSuppressed(QPainter& painter, qreal zoom) const;
    QString type() const override { return QStringLiteral("textbox"); }
    QJsonObject toJson() const override;
    void loadFromJson(const QJsonObject& obj) override;

    bool isMarkdown() const;
    bool usesCurrentLayout() const;
    bool usesLegacyLayout() const;

    TextBoxLayoutInput layoutInput(qreal legacyZoom = 1.0) const;
    static std::unique_ptr<TextBoxLayoutResult> buildLayout(
        const TextBoxLayoutInput& input);
    const TextBoxLayoutResult* ensureLayout(qreal legacyZoom = 1.0) const;

    QSizeF normalizedSizeForWidth(qreal width) const;
    /**
     * Geometry/text callers must also invalidate active PdfSearchEngine
     * results; cached match rectangles are tied to this layout.
     */
    void reflowToWidth(qreal width);
    bool upgradeToCurrentLayout(TextBoxState* before = nullptr,
                                TextBoxState* after = nullptr);

    TextBoxState captureState() const;
    void applyState(const TextBoxState& state);

    QString anchorAtLocalPoint(const QPointF& localPoint,
                               qreal legacyZoom = 1.0) const;

    void invalidateLayoutCache() const;
    void invalidateDocCache() const { invalidateLayoutCache(); }

protected:
    void renderInternal(QPainter& painter, qreal zoom,
                        bool suppressText) const;
    mutable std::unique_ptr<TextBoxLayoutResult> m_cachedLayout;
    mutable TextBoxLayoutInput m_cachedLayoutInput;
    mutable bool m_hasCachedLayoutInput = false;
};
