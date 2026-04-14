#include "OcrTextObject.h"
#include "../core/Page.h"
#include "../layers/VectorLayer.h"

#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QFontMetricsF>
#include <algorithm>

static bool isCjkChar(QChar ch)
{
    ushort u = ch.unicode();
    return (u >= 0x4E00 && u <= 0x9FFF)
        || (u >= 0x3400 && u <= 0x4DBF)
        || (u >= 0xF900 && u <= 0xFAFF)
        || (u >= 0x3000 && u <= 0x303F)
        || (u >= 0x3040 && u <= 0x309F)
        || (u >= 0x30A0 && u <= 0x30FF);
}

void OcrTextObject::drawLockBadge(QPainter& painter, const QRectF& rect) const
{
    static QPixmap lightIcon(QStringLiteral(":/resources/icons/lock.png"));
    static QPixmap darkIcon(QStringLiteral(":/resources/icons/lock_reversed.png"));

    constexpr int badgeSize = 18;
    constexpr int iconSize = 12;
    constexpr int margin = 2;

    qreal x = rect.right() - badgeSize - margin;
    qreal y = rect.top() + margin;
    QRectF badgeRect(x, y, badgeSize, badgeSize);

    painter.save();

    bool dark = backgroundColor.lightness() < 100;
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(dark ? 255 : 0, dark ? 255 : 0, dark ? 255 : 0, 60));
    painter.drawRoundedRect(badgeRect, 3, 3);

    const QPixmap& icon = dark ? darkIcon : lightIcon;
    if (!icon.isNull()) {
        qreal off = (badgeSize - iconSize) / 2.0;
        painter.drawPixmap(QRectF(x + off, y + off, iconSize, iconSize), icon, QRectF(icon.rect()));
    }

    painter.restore();
}

void OcrTextObject::render(QPainter& painter, qreal zoom) const
{
    if (!visible || text.isEmpty())
        return;

    if (wordSegments.isEmpty() || ocrLocked) {
        TextBoxObject::render(painter, zoom);
        if (ocrLocked) {
            QRectF lr(position.x() * zoom, position.y() * zoom,
                      size.width() * zoom, size.height() * zoom);
            drawLockBadge(painter, lr);
        }
        return;
    }

    QRectF lineRect(
        position.x() * zoom,
        position.y() * zoom,
        size.width() * zoom,
        size.height() * zoom
    );

    if (lineRect.width() < 1.0 || lineRect.height() < 1.0)
        return;

    // --- Step 1: sort segments by x position (defensive) ---
    struct Seg {
        QString text;
        QRectF rect;
    };
    QVector<Seg> sorted;
    sorted.reserve(wordSegments.size());
    for (const auto& ws : wordSegments) {
        if (!ws.text.isEmpty() && ws.boundingRect.isValid())
            sorted.append({ws.text, ws.boundingRect});
    }
    if (sorted.isEmpty()) {
        TextBoxObject::render(painter, zoom);
        return;
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const Seg& a, const Seg& b) { return a.rect.x() < b.rect.x(); });

    // --- Step 2: compute average segment height for gap threshold ---
    qreal totalH = 0;
    for (const auto& s : sorted)
        totalH += s.rect.height();
    qreal avgH = totalH / sorted.size();
    qreal gapThreshold = avgH * 1.0;

    // --- Step 3: group into runs by horizontal proximity ---
    struct Run {
        QString text;
        QRectF rect;
    };
    QVector<Run> runs;
    Run cur;
    cur.text = sorted[0].text;
    cur.rect = sorted[0].rect;

    for (int i = 1; i < sorted.size(); ++i) {
        qreal gap = sorted[i].rect.left() - cur.rect.right();
        if (gap < gapThreshold) {
            // Merge: decide separator
            bool needSpace = true;
            if (!cur.text.isEmpty() && !sorted[i].text.isEmpty()) {
                if (isCjkChar(cur.text.back()) || isCjkChar(sorted[i].text.front()))
                    needSpace = false;
            }
            if (needSpace)
                cur.text += QLatin1Char(' ');
            cur.text += sorted[i].text;
            cur.rect = cur.rect.united(sorted[i].rect);
        } else {
            runs.append(cur);
            cur.text = sorted[i].text;
            cur.rect = sorted[i].rect;
        }
    }
    runs.append(cur);

    // --- Step 4: draw background for the full line rect ---
    painter.save();

    if (backgroundColor.alpha() > 0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(backgroundColor);
        painter.drawRect(lineRect);
    }

    // --- Step 5: render each run with TextBoxObject-style font sizing ---
    for (const auto& run : runs) {
        QRectF runScreenRect(
            run.rect.x() * zoom,
            run.rect.y() * zoom,
            run.rect.width() * zoom,
            run.rect.height() * zoom
        );

        if (runScreenRect.width() < 1.0 || runScreenRect.height() < 1.0)
            continue;

        constexpr qreal pad = 2.0;
        QRectF textRect = runScreenRect.adjusted(pad, pad, -pad, -pad);
        if (textRect.width() < 1.0 || textRect.height() < 1.0)
            textRect = runScreenRect;

        qreal effectivePixelSize = run.rect.height() * zoom * 0.75;
        if (effectivePixelSize > 1.0) {
            QFont probe;
            if (!fontFamily.isEmpty())
                probe.setFamily(fontFamily);
            probe.setPixelSize(static_cast<int>(effectivePixelSize));
            QFontMetricsF fm(probe);
            qreal textW = fm.horizontalAdvance(run.text);
            if (textW > textRect.width() && textW > 0.0)
                effectivePixelSize *= textRect.width() / textW;
        }
        if (effectivePixelSize < 1.0)
            effectivePixelSize = 1.0;

        QFont font;
        if (!fontFamily.isEmpty())
            font.setFamily(fontFamily);
        font.setPixelSize(static_cast<int>(effectivePixelSize));

        painter.setFont(font);
        QColor penColor = fontColor;
        if (showConfidence && !ocrLocked) {
            if (confidence < 0.5f)
                penColor = QColor(220, 60, 60);
            else if (confidence < 0.8f)
                penColor = QColor(220, 160, 40);
        }
        painter.setPen(penColor);
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, run.text);
    }

    if (ocrLocked)
        drawLockBadge(painter, lineRect);

    painter.restore();
}

QJsonObject OcrTextObject::toJson() const
{
    QJsonObject obj = TextBoxObject::toJson();

    QJsonArray sids;
    for (const auto& sid : sourceStrokeIds)
        sids.append(sid);
    obj["sourceStrokeIds"] = sids;
    obj["confidence"] = static_cast<double>(confidence);
    obj["engineId"] = engineId;
    obj["ocrDirty"] = ocrDirty;
    if (ocrLocked)
        obj["ocrLocked"] = true;
    if (!wordSegments.isEmpty()) {
        QJsonArray words;
        for (const auto& seg : wordSegments) {
            QJsonObject w;
            w["t"] = seg.text;
            QJsonArray r;
            r.append(seg.boundingRect.x());
            r.append(seg.boundingRect.y());
            r.append(seg.boundingRect.width());
            r.append(seg.boundingRect.height());
            w["r"] = r;
            words.append(w);
        }
        obj["words"] = words;
    }
    return obj;
}

void OcrTextObject::loadFromJson(const QJsonObject& obj)
{
    TextBoxObject::loadFromJson(obj);
    sourceStrokeIds.clear();
    for (const auto& val : obj["sourceStrokeIds"].toArray())
        sourceStrokeIds.append(val.toString());
    confidence = static_cast<float>(obj["confidence"].toDouble(0.0));
    engineId = obj["engineId"].toString();
    ocrDirty = obj["ocrDirty"].toBool(false);
    ocrLocked = obj["ocrLocked"].toBool(false);
    wordSegments.clear();
    for (const auto& val : obj["words"].toArray()) {
        QJsonObject w = val.toObject();
        OcrTextBlock::WordSegment seg;
        seg.text = w["t"].toString();
        QJsonArray r = w["r"].toArray();
        if (r.size() == 4)
            seg.boundingRect = QRectF(r[0].toDouble(), r[1].toDouble(),
                                      r[2].toDouble(), r[3].toDouble());
        wordSegments.append(seg);
    }
}

std::unique_ptr<OcrTextObject> OcrTextObject::createFromBlock(
    const OcrTextBlock& block,
    const QColor& strokeColor,
    bool darkMode)
{
    auto obj = std::make_unique<OcrTextObject>();
    obj->id = block.id;
    obj->position = block.boundingRect.topLeft();
    obj->size = block.boundingRect.size();
    obj->text = block.text;
    obj->confidence = block.confidence;
    obj->sourceStrokeIds = block.sourceStrokeIds;
    obj->engineId = block.engineId;
    obj->ocrDirty = block.dirty;
    obj->wordSegments = block.wordSegments;
    obj->fontColor = strokeColor;
    obj->backgroundColor = darkMode ? QColor(40, 40, 40, 180)
                                    : QColor(255, 255, 255, 180);
    obj->visible = false;
    return obj;
}

QColor OcrTextObject::dominantStrokeColor(const Page* page,
                                          const QVector<QString>& strokeIds)
{
    if (!page || strokeIds.isEmpty())
        return QColor(60, 60, 60);

    QHash<QRgb, int> colorCounts;
    QSet<QString> idSet(strokeIds.begin(), strokeIds.end());

    for (const auto& layer : page->vectorLayers) {
        if (!layer)
            continue;
        for (const auto& stroke : layer->strokes()) {
            if (idSet.contains(stroke.id)) {
                QRgb rgb = stroke.color.rgb();
                colorCounts[rgb]++;
            }
        }
    }

    if (colorCounts.isEmpty())
        return QColor(60, 60, 60);

    QRgb best = 0;
    int bestCount = 0;
    for (auto it = colorCounts.constBegin(); it != colorCounts.constEnd(); ++it) {
        if (it.value() > bestCount) {
            bestCount = it.value();
            best = it.key();
        }
    }
    return QColor::fromRgb(best);
}

int OcrTextObject::resolveLayerAffinity(const Page* page,
                                        const QVector<QString>& strokeIds)
{
    if (!page || strokeIds.isEmpty())
        return -1;

    QSet<QString> idSet(strokeIds.begin(), strokeIds.end());
    int bestLayerIdx = 0;
    int bestCount = 0;

    for (int i = 0; i < static_cast<int>(page->vectorLayers.size()); ++i) {
        const auto& layer = page->vectorLayers[i];
        if (!layer) continue;
        int count = 0;
        for (const auto& stroke : layer->strokes()) {
            if (idSet.contains(stroke.id))
                ++count;
        }
        if (count > bestCount) {
            bestCount = count;
            bestLayerIdx = i;
        }
    }

    // affinity = layerIndex - 1: objects with affinity K have their visibility
    // controlled by Layer K+1, so to tie to Layer N we use affinity N-1.
    return bestLayerIdx - 1;
}
