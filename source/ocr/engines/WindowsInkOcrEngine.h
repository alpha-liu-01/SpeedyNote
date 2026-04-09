#pragma once

#ifdef SPEEDYNOTE_HAS_WINDOWS_INK

// ============================================================================
// WindowsInkOcrEngine - WinRT InkAnalyzer OCR backend
// ============================================================================
// Part of the OCR Phase 1A infrastructure.
//
// Uses the Windows.UI.Input.Inking.Analysis.InkAnalyzer API
// (Windows 10 1703+) for handwriting recognition.
// PIMPL pattern isolates WinRT headers from Qt headers.
// ============================================================================

#include "../OcrEngine.h"
#include <memory>

class WindowsInkOcrEngine : public OcrEngine {
public:
    WindowsInkOcrEngine();
    ~WindowsInkOcrEngine() override;

    WindowsInkOcrEngine(const WindowsInkOcrEngine&) = delete;
    WindowsInkOcrEngine& operator=(const WindowsInkOcrEngine&) = delete;

    QString engineId() const override { return QStringLiteral("windows_ink"); }
    bool isAvailable() const override;

    QStringList availableLanguages() const override;
    void setLanguage(const QString& recognizerName) override;
    QString language() const override;

    void addStrokes(const QVector<VectorStroke>& strokes) override;
    void removeStrokes(const QVector<QString>& strokeIds) override;
    void clearStrokes() override;
    QVector<Result> analyze() override;

private:
    QVector<Result> analyzeWithInkAnalyzer();
    QVector<Result> analyzeWithRecognizer();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

#endif // SPEEDYNOTE_HAS_WINDOWS_INK
