#pragma once

#ifdef SPEEDYNOTE_HAS_MLKIT_INK

// ============================================================================
// MlKitOcrEngine - Google ML Kit Digital Ink Recognition backend
// ============================================================================
// Part of the OCR Phase 3 cross-platform engine support.
//
// Uses ML Kit Digital Ink Recognition on Android (via JNI) and iOS
// (via Objective-C++ bridge). Shared C++ logic handles stroke buffering,
// line grouping, and result mapping. Platform-specific *Native() methods
// are implemented in MlKitOcrEngine_android.cpp / MlKitOcrEngine_ios.mm.
// ============================================================================

#include "../OcrEngine.h"
#include "../../strokes/VectorStroke.h"

#include <QVector>
#include <QHash>
#include <QString>
#include <QStringList>

class MlKitOcrEngine : public OcrEngine {
public:
    MlKitOcrEngine();
    ~MlKitOcrEngine() override;

    MlKitOcrEngine(const MlKitOcrEngine&) = delete;
    MlKitOcrEngine& operator=(const MlKitOcrEngine&) = delete;

    QString engineId() const override { return QStringLiteral("mlkit_digital_ink"); }
    bool isAvailable() const override;

    QStringList availableLanguages() const override;
    void setLanguage(const QString& languageTag) override;
    QString language() const override;

    void addStrokes(const QVector<VectorStroke>& strokes) override;
    void removeStrokes(const QVector<QString>& strokeIds) override;
    void clearStrokes() override;
    QVector<Result> analyze() override;

private:
    // Platform bridge -- implemented in _android.cpp or _ios.mm
    QString recognizeStrokesNative(const QVector<VectorStroke>& strokes);
    bool checkAvailabilityNative() const;
    QStringList queryLanguagesNative() const;
    bool ensureModelDownloadedNative(const QString& languageTag);

    QVector<VectorStroke> m_strokes;
    QHash<QString, int> m_strokeIndexById;

    QString m_languageTag;
    mutable QStringList m_cachedLanguages;
    bool m_available = false;
};

#endif // SPEEDYNOTE_HAS_MLKIT_INK
