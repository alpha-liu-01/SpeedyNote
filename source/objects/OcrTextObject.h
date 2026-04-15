#pragma once

#include "TextBoxObject.h"
#include "../ocr/OcrTextBlock.h"

#include <QVector>
#include <memory>

class Page;

class OcrTextObject : public TextBoxObject {
public:
    QVector<QString> sourceStrokeIds;
    float confidence = 0.0f;
    QString engineId;
    bool ocrDirty = false;
    bool ocrLocked = false;
    bool showConfidence = false;
    bool ocrSnapEnabled = false;
    bool ocrCjkGridMode = false;
    int ocrGridSpacing = 32;
    QVector<OcrTextBlock::WordSegment> wordSegments;

    OcrTextObject() { visible = false; showBorder = false; }

    QString type() const override { return QStringLiteral("ocr_text"); }
    void render(QPainter& painter, qreal zoom) const override;
    QJsonObject toJson() const override;
    void loadFromJson(const QJsonObject& obj) override;

    static std::unique_ptr<OcrTextObject> createFromBlock(
        const OcrTextBlock& block,
        const QColor& strokeColor = QColor(60, 60, 60),
        bool darkMode = false);

    /**
     * @brief Compute the dominant stroke color from a page's vector layers.
     * @param page The page containing strokes.
     * @param strokeIds IDs of strokes to sample.
     * @return The most common color, or dark gray if none found.
     */
    static QColor dominantStrokeColor(const Page* page,
                                      const QVector<QString>& strokeIds);

    /**
     * @brief Determine the layer affinity from source strokes.
     * @param page The page containing strokes organized by layer.
     * @param strokeIds IDs of source strokes for an OCR text block.
     * @return Layer affinity value (layerIndex - 1), or -1 if no strokes found.
     */
    static int resolveLayerAffinity(const Page* page,
                                    const QVector<QString>& strokeIds);

private:
    void drawLockBadge(QPainter& painter, const QRectF& rect) const;
};
