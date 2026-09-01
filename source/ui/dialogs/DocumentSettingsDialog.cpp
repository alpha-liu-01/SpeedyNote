#include "DocumentSettingsDialog.h"
#include "../../MainWindow.h"
#include "../../core/Document.h"
#include "../../core/Page.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QColorDialog>
#include <QCompleter>
#include <QLocale>
#include <QSizeF>
#include <algorithm>

// Android/iOS keyboard fix (BUG-A001)
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
#include <QApplication>
#include <QGuiApplication>
#include <QInputMethod>
#include <QTimer>
#endif

DocumentSettingsDialog::DocumentSettingsDialog(MainWindow* mainWindow, Document* doc,
                                               QWidget* parent)
    : QDialog(parent), mainWindowRef(mainWindow), m_doc(doc) {

    setWindowTitle(tr("Current Document Settings"));
    resize(450, 400);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Document name header (mirrors the old OCR Language dialog).
    if (m_doc) {
        QLabel* docLabel = new QLabel(tr("Document: %1").arg(m_doc->name), this);
        docLabel->setStyleSheet("font-weight: bold;");
        mainLayout->addWidget(docLabel);
        mainLayout->addSpacing(4);
    }

    tabWidget = new QTabWidget(this);

    createPageTab();
    createLanguageTab();
    createThemeTab();
    // Follow-up plan: createToolsTab() (CJK grid-cell mode).

    mainLayout->addWidget(tabWidget);

    // === Buttons ===
    applyButton = new QPushButton(tr("Apply"), this);
    okButton = new QPushButton(tr("OK"), this);
    cancelButton = new QPushButton(tr("Cancel"), this);

    connect(applyButton, &QPushButton::clicked, this, &DocumentSettingsDialog::applyChanges);
    connect(okButton, &QPushButton::clicked, this, [this]() {
        applyChanges();
        accept();
    });
    connect(cancelButton, &QPushButton::clicked, this, &DocumentSettingsDialog::reject);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(applyButton);
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);
    mainLayout->addLayout(buttonLayout);

    // Load current document values into the UI.
    loadSettings();
}

void DocumentSettingsDialog::done(int result)
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // BUG-A001 Fix: defer close so Android/iOS keyboard operations complete
    // (mirrors ControlPanelDialog::done()).
    static bool isDeferring = false;
    if (isDeferring) {
        QDialog::done(result);
        return;
    }

    if (QWidget* focused = QApplication::focusWidget()) {
        focused->clearFocus();
    }
    if (QGuiApplication::inputMethod()) {
        QGuiApplication::inputMethod()->hide();
        QGuiApplication::inputMethod()->commit();
    }

    isDeferring = true;
    int savedResult = result;
    QTimer::singleShot(150, this, [this, savedResult]() {
        isDeferring = false;
        QDialog::done(savedResult);
    });
    return;
#else
    QDialog::done(result);
#endif
}

// ============================================================================
// Page tab - page size override
// ============================================================================

void DocumentSettingsDialog::createPageTab()
{
    pageTab = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(pageTab);

    layout->addSpacing(6);

    QLabel* descLabel = new QLabel(
        tr("These settings override the defaults for THIS document only. "
           "Page size applies to newly added pages (existing pages are not "
           "resized), while background changes apply to the whole document."),
        pageTab);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet("color: gray; font-size: 11px; margin-bottom: 10px;");
    layout->addWidget(descLabel);

    // ========== PAGE SIZE SECTION (new pages only) ==========
    QLabel* pageSectionLabel = new QLabel(tr("Page Size (new pages only)"), pageTab);
    pageSectionLabel->setStyleSheet("font-weight: bold; margin-top: 5px;");
    layout->addWidget(pageSectionLabel);

    // Paper size preset (same presets as the base Control Panel).
    QHBoxLayout* pageSizeLayout = new QHBoxLayout();
    QLabel* pageSizeLabel = new QLabel(tr("Paper Size:"), pageTab);
    pageSizeLabel->setMinimumWidth(120);
    pageSizeLayout->addWidget(pageSizeLabel);

    pageSizeCombo = new QComboBox(pageTab);
    // mm to px at 96 DPI: mm * 96 / 25.4
    pageSizeCombo->addItem(tr("A3 (297 × 420 mm)"), QSizeF(1123, 1587));
    pageSizeCombo->addItem(tr("B4 (250 × 353 mm)"), QSizeF(945, 1334));
    pageSizeCombo->addItem(tr("A4 (210 × 297 mm)"), QSizeF(794, 1123));
    pageSizeCombo->addItem(tr("B5 (176 × 250 mm)"), QSizeF(665, 945));
    pageSizeCombo->addItem(tr("A5 (148 × 210 mm)"), QSizeF(559, 794));
    pageSizeCombo->addItem(tr("US Letter (8.5 × 11 in)"), QSizeF(816, 1056));
    pageSizeCombo->addItem(tr("US Legal (8.5 × 14 in)"), QSizeF(816, 1344));
    pageSizeCombo->addItem(tr("US Tabloid (11 × 17 in)"), QSizeF(1056, 1632));

    connect(pageSizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DocumentSettingsDialog::onPageSizePresetChanged);
    pageSizeLayout->addWidget(pageSizeCombo, 1);
    layout->addLayout(pageSizeLayout);

    // Dimensions (read-only display).
    QHBoxLayout* pageDimLayout = new QHBoxLayout();
    QLabel* pageDimLabel = new QLabel(tr("Dimensions:"), pageTab);
    pageDimLabel->setMinimumWidth(120);
    pageDimLayout->addWidget(pageDimLabel);

    pageSizeDimLabel = new QLabel(pageTab);
    pageSizeDimLabel->setStyleSheet("color: #666; font-style: italic;");
    pageDimLayout->addWidget(pageSizeDimLabel);
    pageDimLayout->addStretch();
    layout->addLayout(pageDimLayout);

    layout->addSpacing(15);

    // ========== BACKGROUND SECTION (whole document) ==========
    QLabel* bgSectionLabel = new QLabel(tr("Background (entire document)"), pageTab);
    bgSectionLabel->setStyleSheet("font-weight: bold;");
    layout->addWidget(bgSectionLabel);

    // Background style
    QHBoxLayout* styleLayout = new QHBoxLayout();
    QLabel* styleLabel = new QLabel(tr("Background Style:"), pageTab);
    styleLabel->setMinimumWidth(120);
    styleLayout->addWidget(styleLabel);

    bgStyleCombo = new QComboBox(pageTab);
    // Values must match Page::BackgroundType enum: None=0, Grid=3, Lines=4.
    bgStyleCombo->addItem(tr("None"), static_cast<int>(Page::BackgroundType::None));
    bgStyleCombo->addItem(tr("Grid"), static_cast<int>(Page::BackgroundType::Grid));
    bgStyleCombo->addItem(tr("Lines"), static_cast<int>(Page::BackgroundType::Lines));
    styleLayout->addWidget(bgStyleCombo, 1);
    layout->addLayout(styleLayout);

    layout->addSpacing(10);

    // Background color
    QHBoxLayout* bgColorLayout = new QHBoxLayout();
    QLabel* bgColorLabel = new QLabel(tr("Background Color:"), pageTab);
    bgColorLabel->setMinimumWidth(120);
    bgColorLayout->addWidget(bgColorLabel);

    bgColorButton = new QPushButton(pageTab);
    bgColorButton->setFixedSize(100, 30);
    bgColorButton->setStyleSheet("background-color: #ffffff");
    connect(bgColorButton, &QPushButton::clicked, this, &DocumentSettingsDialog::chooseBackgroundColor);
    bgColorLayout->addWidget(bgColorButton);
    bgColorLayout->addStretch();
    layout->addLayout(bgColorLayout);

    // Grid/line color
    QHBoxLayout* gridColorLayout = new QHBoxLayout();
    QLabel* gridColorLabel = new QLabel(tr("Grid/Line Color:"), pageTab);
    gridColorLabel->setMinimumWidth(120);
    gridColorLayout->addWidget(gridColorLabel);

    gridColorButton = new QPushButton(pageTab);
    gridColorButton->setFixedSize(100, 30);
    gridColorButton->setStyleSheet("background-color: #c8c8c8");
    connect(gridColorButton, &QPushButton::clicked, this, &DocumentSettingsDialog::chooseGridColor);
    gridColorLayout->addWidget(gridColorButton);
    gridColorLayout->addStretch();
    layout->addLayout(gridColorLayout);

    layout->addSpacing(10);

    // Grid spacing
    QHBoxLayout* gridSpacingLayout = new QHBoxLayout();
    QLabel* gridSpacingLabel = new QLabel(tr("Grid Spacing:"), pageTab);
    gridSpacingLabel->setMinimumWidth(120);
    gridSpacingLayout->addWidget(gridSpacingLabel);

    gridSpacingSpin = new QSpinBox(pageTab);
    gridSpacingSpin->setRange(8, 128);
    gridSpacingSpin->setSingleStep(8);
    gridSpacingSpin->setSuffix(" px");
    gridSpacingSpin->setValue(32);
    gridSpacingLayout->addWidget(gridSpacingSpin);
    gridSpacingLayout->addStretch();
    layout->addLayout(gridSpacingLayout);

    // Line spacing
    QHBoxLayout* lineSpacingLayout = new QHBoxLayout();
    QLabel* lineSpacingLabel = new QLabel(tr("Line Spacing:"), pageTab);
    lineSpacingLabel->setMinimumWidth(120);
    lineSpacingLayout->addWidget(lineSpacingLabel);

    lineSpacingSpin = new QSpinBox(pageTab);
    lineSpacingSpin->setRange(8, 128);
    lineSpacingSpin->setSingleStep(8);
    lineSpacingSpin->setSuffix(" px");
    lineSpacingSpin->setValue(32);
    lineSpacingLayout->addWidget(lineSpacingSpin);
    lineSpacingLayout->addStretch();
    layout->addLayout(lineSpacingLayout);

    layout->addStretch();

    // Initialize swatch colours (overwritten by loadSettings() when a doc exists).
    selectedBgColor = QColor("#ffffff");
    selectedGridColor = QColor("#c8c8c8");

    if (!m_doc) {
        pageSizeCombo->setEnabled(false);
        bgStyleCombo->setEnabled(false);
        bgColorButton->setEnabled(false);
        gridColorButton->setEnabled(false);
        gridSpacingSpin->setEnabled(false);
        lineSpacingSpin->setEnabled(false);
    }

    tabWidget->addTab(pageTab, tr("Page"));
}

void DocumentSettingsDialog::chooseBackgroundColor()
{
    QColor chosen = QColorDialog::getColor(selectedBgColor, this, tr("Select Background Color"));
    if (chosen.isValid()) {
        selectedBgColor = chosen;
        bgColorButton->setStyleSheet(QString("background-color: %1").arg(selectedBgColor.name()));
    }
}

void DocumentSettingsDialog::chooseGridColor()
{
    QColor chosen = QColorDialog::getColor(selectedGridColor, this, tr("Select Grid/Line Color"));
    if (chosen.isValid()) {
        selectedGridColor = chosen;
        gridColorButton->setStyleSheet(QString("background-color: %1").arg(selectedGridColor.name()));
    }
}

void DocumentSettingsDialog::onPageSizePresetChanged(int index)
{
    if (index < 0 || !pageSizeCombo || !pageSizeDimLabel) return;

    QSizeF size = pageSizeCombo->itemData(index).toSizeF();
    pageSizeDimLabel->setText(tr("%1 × %2 px (at 96 DPI)")
        .arg(static_cast<int>(size.width()))
        .arg(static_cast<int>(size.height())));
}

// ============================================================================
// Language tab - per-document OCR recognizer override
// ============================================================================

void DocumentSettingsDialog::createLanguageTab()
{
    languageTab = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(languageTab);

    layout->addSpacing(6);

    QLabel* label = new QLabel(
        tr("Handwriting recognition language for this document:"), languageTab);
    label->setWordWrap(true);
    layout->addWidget(label);

    ocrLanguageCombo = new QComboBox(languageTab);
    ocrLanguageCombo->addItem(tr("Use global setting"), QStringLiteral(""));
    {
        const bool autoDetect = mainWindowRef ? mainWindowRef->ocrSupportsAutoLanguage() : true;
        ocrLanguageCombo->addItem(MainWindow::ocrAutoLanguageLabel(autoDetect),
                                  QStringLiteral("auto"));
        ocrLanguageCombo->setItemData(1, MainWindow::ocrAutoLanguageTooltip(autoDetect),
                                      Qt::ToolTipRole);
    }

    // Partition languages: common first, then the rest sorted by display name
    // (same behavior as the former MainWindow::showOcrLanguageDialog()).
    static const QStringList commonTags = {
        QStringLiteral("en-US"), QStringLiteral("en-GB"),
        QStringLiteral("zh-Hani-CN"), QStringLiteral("zh-Hani-TW"),
        QStringLiteral("ja"), QStringLiteral("ko"),
        QStringLiteral("es-ES"), QStringLiteral("fr-FR"),
        QStringLiteral("de-DE"), QStringLiteral("pt-BR"),
        QStringLiteral("it-IT"), QStringLiteral("ru"), QStringLiteral("ar"),
    };

    auto displayNameForTag = [](const QString& tag) -> QString {
        QLocale locale(QString(tag).replace(QLatin1Char('-'), QLatin1Char('_')));
        QString name = locale.nativeLanguageName();
        if (name.isEmpty() || name == QLatin1String("C"))
            return tag;
        return QStringLiteral("%1 (%2)").arg(name, tag);
    };

    const QStringList available =
        mainWindowRef ? mainWindowRef->ocrAvailableLanguages() : QStringList();

    QStringList common, rest;
    for (const auto& lang : available) {
        if (commonTags.contains(lang))
            common.append(lang);
        else
            rest.append(lang);
    }

    std::sort(rest.begin(), rest.end(), [&](const QString& a, const QString& b) {
        return displayNameForTag(a).toLower() < displayNameForTag(b).toLower();
    });

    for (const auto& tag : commonTags) {
        if (common.contains(tag))
            ocrLanguageCombo->addItem(displayNameForTag(tag), tag);
    }
    if (!common.isEmpty() && !rest.isEmpty())
        ocrLanguageCombo->insertSeparator(ocrLanguageCombo->count());
    for (const auto& tag : rest)
        ocrLanguageCombo->addItem(displayNameForTag(tag), tag);

    ocrLanguageCombo->setEditable(true);
    ocrLanguageCombo->setInsertPolicy(QComboBox::NoInsert);
    if (ocrLanguageCombo->completer())
        ocrLanguageCombo->completer()->setFilterMode(Qt::MatchContains);

    layout->addWidget(ocrLanguageCombo);

    layout->addSpacing(8);

    QLabel* globalNote = new QLabel(
        tr("\"Use global setting\" inherits from Settings > Language > "
           "Handwriting Recognition Language."),
        languageTab);
    globalNote->setWordWrap(true);
    globalNote->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(globalNote);

    layout->addStretch();

    if (!m_doc) {
        ocrLanguageCombo->setEnabled(false);
    }

    tabWidget->addTab(languageTab, tr("Language"));
}

// ============================================================================
// Theme tab - per-document PDF inversion overrides (PDF-backed documents only)
// ============================================================================

void DocumentSettingsDialog::createThemeTab()
{
    themeTab = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(themeTab);

    layout->addSpacing(6);

    // Show PDF-invert overrides for documents that have any PDF content, including
    // import-only documents whose PDF pages were copied in from another document
    // (they have no primary base PDF, so hasPdfReference() alone would hide these).
    const bool hasPdf = m_doc && (m_doc->hasPdfReference() || m_doc->hasAnyPdfSource());

    QLabel* descLabel = new QLabel(
        tr("These PDF display settings override the global defaults for THIS "
           "document only. They apply only to documents with a PDF loaded."),
        themeTab);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet("color: gray; font-size: 11px; margin-bottom: 10px;");
    layout->addWidget(descLabel);

    // Invert PDF lightness in dark mode
    QLabel* darkLabel = new QLabel(tr("Invert PDF Lightness in Dark Mode:"), themeTab);
    layout->addWidget(darkLabel);

    pdfInvertDarkCombo = new QComboBox(themeTab);
    pdfInvertDarkCombo->addItem(tr("Use global setting"), -1);
    pdfInvertDarkCombo->addItem(tr("On"), 1);
    pdfInvertDarkCombo->addItem(tr("Off"), 0);
    layout->addWidget(pdfInvertDarkCombo);

    QLabel* darkNote = new QLabel(
        tr("When enabled and dark mode is active, PDF page backgrounds are darkened "
           "and their content lightened for comfortable night reading."),
        themeTab);
    darkNote->setWordWrap(true);
    darkNote->setStyleSheet("color: gray; font-size: 10px;");
    layout->addWidget(darkNote);

    layout->addSpacing(12);

    // Invert entire page including images
    QLabel* imagesLabel = new QLabel(tr("Invert Entire Page Including Images:"), themeTab);
    layout->addWidget(imagesLabel);

    pdfInvertImagesCombo = new QComboBox(themeTab);
    pdfInvertImagesCombo->addItem(tr("Use global setting"), -1);
    pdfInvertImagesCombo->addItem(tr("On"), 1);
    pdfInvertImagesCombo->addItem(tr("Off"), 0);
    layout->addWidget(pdfInvertImagesCombo);

    QLabel* imagesNote = new QLabel(
        tr("By default, embedded photos and figures are detected and excluded from "
           "inversion. Enable this to invert the whole page, including images."),
        themeTab);
    imagesNote->setWordWrap(true);
    imagesNote->setStyleSheet("color: gray; font-size: 10px;");
    layout->addWidget(imagesNote);

    layout->addStretch();

    if (!hasPdf) {
        QLabel* noPdfNote = new QLabel(
            tr("This document has no PDF loaded, so these settings have no effect."),
            themeTab);
        noPdfNote->setWordWrap(true);
        noPdfNote->setStyleSheet("color: #b03c3c; font-size: 11px;");
        layout->addWidget(noPdfNote);
    }

    // "Invert entire page" only makes sense when lightness inversion isn't
    // explicitly Off (mirrors the Control Panel dependency).
    auto updateImagesEnabled = [this, hasPdf]() {
        if (!pdfInvertImagesCombo || !pdfInvertDarkCombo) return;
        const bool darkOff = pdfInvertDarkCombo->currentData().toInt() == 0;
        pdfInvertImagesCombo->setEnabled(hasPdf && !darkOff);
    };
    connect(pdfInvertDarkCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [updateImagesEnabled](int) { updateImagesEnabled(); });

    if (!hasPdf) {
        pdfInvertDarkCombo->setEnabled(false);
        pdfInvertImagesCombo->setEnabled(false);
    } else {
        updateImagesEnabled();
    }

    tabWidget->addTab(themeTab, tr("Theme"));
}

// ============================================================================
// Load / Apply
// ============================================================================

void DocumentSettingsDialog::loadSettings()
{
    if (!m_doc) return;

    // Page size: select the preset matching the document's default page size.
    if (pageSizeCombo) {
        const QSizeF current = m_doc->defaultPageSize;
        int matchIndex = -1;
        for (int i = 0; i < pageSizeCombo->count(); ++i) {
            QSizeF preset = pageSizeCombo->itemData(i).toSizeF();
            if (qFuzzyCompare(preset.width(), current.width()) &&
                qFuzzyCompare(preset.height(), current.height())) {
                matchIndex = i;
                break;
            }
        }
        if (matchIndex >= 0) {
            pageSizeCombo->setCurrentIndex(matchIndex);
        } else if (current.isValid() && !current.isEmpty()) {
            // No preset matches (e.g. a PDF-imported document with an arbitrary
            // page size). Insert a "Custom" entry holding the real size and
            // select it, so applying the dialog does NOT silently overwrite the
            // document's page size with the first preset.
            pageSizeCombo->insertItem(0,
                tr("Custom (%1 × %2 px)")
                    .arg(static_cast<int>(current.width()))
                    .arg(static_cast<int>(current.height())),
                current);
            pageSizeCombo->setCurrentIndex(0);
        }
        // Ensure the dimensions label reflects the current selection even when
        // the current size is custom (no matching preset).
        onPageSizePresetChanged(pageSizeCombo->currentIndex());
    }

    // Background: prefill from the document's current document-wide defaults.
    loadedBgTypeValue = static_cast<int>(m_doc->defaultBackgroundType);
    if (bgStyleCombo) {
        int styleIdx = bgStyleCombo->findData(loadedBgTypeValue);
        // findData returns -1 for a PDF/Custom-typed default (no combo entry,
        // matching the Control Panel which never lists them); display None but
        // remember it wasn't listed so applyChanges() preserves the real type.
        bgTypeInCombo = (styleIdx >= 0);
        bgStyleCombo->setCurrentIndex(bgTypeInCombo ? styleIdx : 0);
    }
    if (bgColorButton) {
        selectedBgColor = m_doc->defaultBackgroundColor;
        bgColorButton->setStyleSheet(QString("background-color: %1").arg(selectedBgColor.name()));
    }
    if (gridColorButton) {
        selectedGridColor = m_doc->defaultGridColor;
        gridColorButton->setStyleSheet(QString("background-color: %1").arg(selectedGridColor.name()));
    }
    if (gridSpacingSpin) gridSpacingSpin->setValue(m_doc->defaultGridSpacing);
    if (lineSpacingSpin) lineSpacingSpin->setValue(m_doc->defaultLineSpacing);

    // OCR language: pre-select the document's per-document override.
    if (ocrLanguageCombo) {
        int idx = ocrLanguageCombo->findData(m_doc->ocrLanguage);
        if (idx >= 0) ocrLanguageCombo->setCurrentIndex(idx);
    }

    // PDF display overrides: pre-select the tri-state values.
    if (pdfInvertDarkCombo) {
        int idx = pdfInvertDarkCombo->findData(m_doc->pdfInvertDarkOverride);
        if (idx >= 0) pdfInvertDarkCombo->setCurrentIndex(idx);
    }
    if (pdfInvertImagesCombo) {
        int idx = pdfInvertImagesCombo->findData(m_doc->pdfInvertIncludeImagesOverride);
        if (idx >= 0) pdfInvertImagesCombo->setCurrentIndex(idx);
    }
}

void DocumentSettingsDialog::applyChanges()
{
    if (!m_doc) return;

    // Page size override (option b): update the document default so newly added
    // pages (Ctrl+Shift+A) use it. Existing pages are intentionally NOT resized.
    // Only write (and dirty the document) when the size actually changed, so a
    // user who only edits the OCR language doesn't spuriously mark it modified.
    if (pageSizeCombo) {
        QSizeF selected = pageSizeCombo->currentData().toSizeF();
        if (selected.isValid() && !selected.isEmpty() &&
            (!qFuzzyCompare(selected.width(), m_doc->defaultPageSize.width()) ||
             !qFuzzyCompare(selected.height(), m_doc->defaultPageSize.height()))) {
            m_doc->defaultPageSize = selected;
            m_doc->markModified();
        }
    }

    // Background override (applies document-wide via MainWindow, which updates
    // the defaults + every page/tile, preserves PDF backgrounds, redraws, and
    // refreshes thumbnails). Only apply when something changed, so a no-op OK
    // doesn't dirty the document or trigger a full redraw/thumbnail rebuild.
    if (bgStyleCombo && mainWindowRef) {
        // Resolve the target type. When the document's original type isn't one of
        // the listed styles (PDF/Custom) and the user left the fallback selection
        // untouched (index 0), preserve the original type rather than clobbering
        // it to None; only a deliberate pick of a listed style overrides it.
        int typeValue;
        if (bgTypeInCombo || bgStyleCombo->currentIndex() != 0) {
            typeValue = bgStyleCombo->currentData().toInt();
        } else {
            typeValue = loadedBgTypeValue;
        }
        const auto type = static_cast<Page::BackgroundType>(typeValue);
        const int gridSpacing = gridSpacingSpin ? gridSpacingSpin->value() : m_doc->defaultGridSpacing;
        const int lineSpacing = lineSpacingSpin ? lineSpacingSpin->value() : m_doc->defaultLineSpacing;
        const bool bgChanged =
            type != m_doc->defaultBackgroundType ||
            selectedBgColor != m_doc->defaultBackgroundColor ||
            selectedGridColor != m_doc->defaultGridColor ||
            gridSpacing != m_doc->defaultGridSpacing ||
            lineSpacing != m_doc->defaultLineSpacing;
        if (bgChanged) {
            mainWindowRef->applyBackgroundSettings(
                type, selectedBgColor, selectedGridColor, gridSpacing, lineSpacing);
        }
    }

    // OCR language override (marks modified + refreshes the OCR worker). Only
    // apply when it actually changed, so opening the dialog and pressing OK
    // without touching this tab doesn't dirty the document or churn the worker.
    if (ocrLanguageCombo && mainWindowRef) {
        const QString selectedLang = ocrLanguageCombo->currentData().toString();
        if (selectedLang != m_doc->ocrLanguage) {
            mainWindowRef->applyDocumentOcrLanguage(m_doc, selectedLang);
        }
    }

    // PDF display overrides (tri-state: -1 inherit / 0 off / 1 on). Apply only on
    // change, then re-resolve + push to the viewport(s) showing this document.
    if (pdfInvertDarkCombo && pdfInvertImagesCombo && mainWindowRef) {
        const int darkVal = pdfInvertDarkCombo->currentData().toInt();
        const int imagesVal = pdfInvertImagesCombo->currentData().toInt();
        bool changed = false;
        if (darkVal != m_doc->pdfInvertDarkOverride) {
            m_doc->pdfInvertDarkOverride = darkVal;
            changed = true;
        }
        if (imagesVal != m_doc->pdfInvertIncludeImagesOverride) {
            m_doc->pdfInvertIncludeImagesOverride = imagesVal;
            changed = true;
        }
        if (changed) {
            m_doc->markModified();
            mainWindowRef->refreshPdfDisplaySettingsForDocument(m_doc);
        }
    }
}
