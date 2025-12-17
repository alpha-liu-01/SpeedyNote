#include "ControlPanelDialog.h"
#include "MainWindow.h"
#include "ButtonMappingTypes.h"
#include "SDLControllerManager.h"
#include "SpnPackageManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QSpacerItem>
#include <QTableWidget>
#include <QPushButton>
#include <QColorDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QApplication>
#include <QMetaObject>
#include <QIcon>
#include <QStandardPaths>
#include <QSettings>
#include <QTimer>
#include <QTabletEvent>

ControlPanelDialog::ControlPanelDialog(MainWindow *mainWindow, InkCanvas *targetCanvas, QWidget *parent)
    : QDialog(parent), canvas(targetCanvas), selectedColor(canvas->getBackgroundColor()), mainWindowRef(mainWindow) {

    setWindowTitle(tr("Canvas Control Panel"));
    resize(400, 200);

    tabWidget = new QTabWidget(this);

    // === Tabs ===
    createBackgroundTab();
    tabWidget->addTab(backgroundTab, tr("Background"));
    if (mainWindowRef) {
        createPerformanceTab();
        tabWidget->addTab(performanceTab, tr("Performance"));
        createToolbarTab();
    }
    createButtonMappingTab();
    createControllerMappingTab();
    createKeyboardMappingTab();
    createMouseDialTab();
    createThemeTab();
    createLanguageTab();
    createCompatibilityTab();
    createCacheTab();
    createAboutTab();
    // === Buttons ===
    applyButton = new QPushButton(tr("Apply"));
    okButton = new QPushButton(tr("OK"));
    cancelButton = new QPushButton(tr("Cancel"));

    connect(applyButton, &QPushButton::clicked, this, &ControlPanelDialog::applyChanges);
    connect(okButton, &QPushButton::clicked, this, [=]() {
        applyChanges();
        accept();
    });
    connect(cancelButton, &QPushButton::clicked, this, &ControlPanelDialog::reject);

    // === Layout ===
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(applyButton);
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(tabWidget);
    mainLayout->addLayout(buttonLayout);

    loadFromCanvas();
}


void ControlPanelDialog::createBackgroundTab() {
    backgroundTab = new QWidget(this);

    QLabel *styleLabel = new QLabel(tr("Background Style:"));
    styleCombo = new QComboBox();
    styleCombo->addItem(tr("None"), static_cast<int>(BackgroundStyle::None));
    styleCombo->addItem(tr("Grid"), static_cast<int>(BackgroundStyle::Grid));
    styleCombo->addItem(tr("Lines"), static_cast<int>(BackgroundStyle::Lines));

    QLabel *colorLabel = new QLabel(tr("Background Color:"));
    colorButton = new QPushButton();
    connect(colorButton, &QPushButton::clicked, this, &ControlPanelDialog::chooseColor);

    QLabel *densityLabel = new QLabel(tr("Density:"));
    densitySpin = new QSpinBox();
    densitySpin->setRange(10, 200);
    densitySpin->setSuffix(" px");
    densitySpin->setSingleStep(5);
    
    // PDF inversion checkbox
    pdfInversionCheckbox = new QCheckBox(tr("Invert PDF Colors (Dark Mode)"), this);
    QLabel *pdfInversionNote = new QLabel(tr("Inverts PDF colors for better readability in dark mode. Useful for PDFs with light backgrounds."), this);
    pdfInversionNote->setWordWrap(true);
    pdfInversionNote->setStyleSheet("color: gray; font-size: 10px;");

    QGridLayout *layout = new QGridLayout(backgroundTab);
    layout->addWidget(styleLabel, 0, 0);
    layout->addWidget(styleCombo, 0, 1);
    layout->addWidget(colorLabel, 1, 0);
    layout->addWidget(colorButton, 1, 1);
    layout->addWidget(densityLabel, 2, 0);
    layout->addWidget(densitySpin, 2, 1);
    layout->addWidget(pdfInversionCheckbox, 3, 0, 1, 2);
    layout->addWidget(pdfInversionNote, 4, 0, 1, 2);
    // layout->setColumnStretch(1, 1); // Stretch the second column
    layout->setRowStretch(5, 1); // Stretch the last row
}

void ControlPanelDialog::chooseColor() {
    QColor chosen = QColorDialog::getColor(selectedColor, this, tr("Select Background Color"));
    if (chosen.isValid()) {
        selectedColor = chosen;
        colorButton->setStyleSheet(QString("background-color: %1").arg(selectedColor.name()));
    }
}

void ControlPanelDialog::applyChanges() {
    if (!canvas) return;

    BackgroundStyle style = static_cast<BackgroundStyle>(
        styleCombo->currentData().toInt()
    );

    canvas->setBackgroundStyle(style);
    canvas->setBackgroundColor(selectedColor);
    canvas->setBackgroundDensity(densitySpin->value());
    canvas->setPdfInversionEnabled(pdfInversionCheckbox->isChecked());
    canvas->update();
    canvas->saveBackgroundMetadata();

    // ✅ Save these settings as defaults for new tabs
    if (mainWindowRef) {
        mainWindowRef->saveDefaultBackgroundSettings(style, selectedColor, densitySpin->value());
    }

    // ✅ Apply button mappings back to MainWindow with internal keys
    if (mainWindowRef) {
        for (const QString &buttonKey : holdMappingCombos.keys()) {
            QString displayString = holdMappingCombos[buttonKey]->currentText();
            QString internalKey = ButtonMappingHelper::displayToInternalKey(displayString, true);  // true = isDialMode
            mainWindowRef->setHoldMapping(buttonKey, internalKey);
        }
        for (const QString &buttonKey : pressMappingCombos.keys()) {
            QString displayString = pressMappingCombos[buttonKey]->currentText();
            QString internalKey = ButtonMappingHelper::displayToInternalKey(displayString, false);  // false = isAction
            mainWindowRef->setPressMapping(buttonKey, internalKey);
        }

        // ✅ Save to persistent settings
        mainWindowRef->saveButtonMappings();
        
        // ✅ Apply theme settings
        mainWindowRef->setUseCustomAccentColor(useCustomAccentCheckbox->isChecked());
        if (selectedAccentColor.isValid()) {
            mainWindowRef->setCustomAccentColor(selectedAccentColor);
        }
        
        // ✅ Apply color palette setting
        mainWindowRef->setUseBrighterPalette(useBrighterPaletteCheckbox->isChecked());
        
        // ✅ Apply mouse dial mappings
        for (const QString &combination : mouseDialMappingCombos.keys()) {
            QString displayString = mouseDialMappingCombos[combination]->currentText();
            QString internalKey = ButtonMappingHelper::displayToInternalKey(displayString, true); // true = isDialMode
            mainWindowRef->setMouseDialMapping(combination, internalKey);
        }
        
        // ✅ Apply language settings
        QSettings settings("SpeedyNote", "App");
        settings.setValue("useSystemLanguage", useSystemLanguageCheckbox->isChecked());
        if (!useSystemLanguageCheckbox->isChecked()) {
            QString selectedLang = languageCombo->currentData().toString();
            settings.setValue("languageOverride", selectedLang);
        }
    }
}

void ControlPanelDialog::loadFromCanvas() {
    styleCombo->setCurrentIndex(static_cast<int>(canvas->getBackgroundStyle()));
    densitySpin->setValue(canvas->getBackgroundDensity());
    selectedColor = canvas->getBackgroundColor();
    pdfInversionCheckbox->setChecked(canvas->isPdfInversionEnabled());

    colorButton->setStyleSheet(QString("background-color: %1").arg(selectedColor.name()));

    if (mainWindowRef) {
        for (const QString &buttonKey : holdMappingCombos.keys()) {
            QString internalKey = mainWindowRef->getHoldMapping(buttonKey);
            QString displayString = ButtonMappingHelper::internalKeyToDisplay(internalKey, true);  // true = isDialMode
            int index = holdMappingCombos[buttonKey]->findText(displayString);
            if (index >= 0) holdMappingCombos[buttonKey]->setCurrentIndex(index);
        }

        for (const QString &buttonKey : pressMappingCombos.keys()) {
            QString internalKey = mainWindowRef->getPressMapping(buttonKey);
            QString displayString = ButtonMappingHelper::internalKeyToDisplay(internalKey, false);  // false = isAction
            int index = pressMappingCombos[buttonKey]->findText(displayString);
            if (index >= 0) pressMappingCombos[buttonKey]->setCurrentIndex(index);
        }
        
        // Load theme settings
        useCustomAccentCheckbox->setChecked(mainWindowRef->isUsingCustomAccentColor());
        
        // Get the stored custom accent color
        selectedAccentColor = mainWindowRef->getCustomAccentColor();
        
        accentColorButton->setStyleSheet(QString("background-color: %1").arg(selectedAccentColor.name()));
        accentColorButton->setEnabled(useCustomAccentCheckbox->isChecked());
        
        // Load color palette setting
        useBrighterPaletteCheckbox->setChecked(mainWindowRef->isUsingBrighterPalette());
        
        // Load mouse dial mappings
        for (const QString &combination : mouseDialMappingCombos.keys()) {
            QString internalKey = mainWindowRef->getMouseDialMapping(combination);
            QString displayString = ButtonMappingHelper::internalKeyToDisplay(internalKey, true); // true = isDialMode
            int index = mouseDialMappingCombos[combination]->findText(displayString);
            if (index >= 0) mouseDialMappingCombos[combination]->setCurrentIndex(index);
        }
    }
}


void ControlPanelDialog::createPerformanceTab() {
    performanceTab = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(performanceTab);

    QCheckBox *previewToggle = new QCheckBox(tr("Enable Low-Resolution PDF Previews"));
    previewToggle->setChecked(mainWindowRef->isLowResPreviewEnabled());

    connect(previewToggle, &QCheckBox::toggled, mainWindowRef, &MainWindow::setLowResPreviewEnabled);

    QLabel *note = new QLabel(tr("Disabling this may improve dial smoothness on low-end devices."));
    note->setWordWrap(true);
    note->setStyleSheet("color: gray; font-size: 10px;");

    QLabel *dpiLabel = new QLabel(tr("PDF Rendering DPI:"));
    QComboBox *dpiSelector = new QComboBox();
    dpiSelector->addItems({"96", "120", "144", "168", "192"});
    dpiSelector->setCurrentText(QString::number(mainWindowRef->getPdfDPI()));

    connect(dpiSelector, &QComboBox::currentTextChanged, this, [=](const QString &value) {
        mainWindowRef->setPdfDPI(value.toInt());
    });

    QLabel *notePDF = new QLabel(tr("Adjust how the PDF is rendered. Higher DPI means better quality but slower performance. DO NOT CHANGE THIS OPTION WHEN MULTIPLE TABS ARE OPEN. THIS MAY LEAD TO UNDEFINED BEHAVIOR!"));
    notePDF->setWordWrap(true);
    notePDF->setStyleSheet("color: gray; font-size: 10px;");
    
    // Wayland DPI scale override
    QLabel *waylandDpiLabel = new QLabel(tr("Wayland DPI Scale Override:"));
    waylandDpiScaleSpinBox = new QDoubleSpinBox();
    waylandDpiScaleSpinBox->setRange(0.0, 3.0);  // Start at 0.0 to allow "Auto" as separate option
    waylandDpiScaleSpinBox->setSingleStep(0.05);
    waylandDpiScaleSpinBox->setDecimals(2);
    waylandDpiScaleSpinBox->setSpecialValueText(tr("Auto"));  // 0.0 = Auto
    
    QSettings settings;
    qreal savedScale = settings.value("display/waylandDpiScale", 0.0).toReal();
    waylandDpiScaleSpinBox->setValue(savedScale);
    
    connect(waylandDpiScaleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [](double value) {
        QSettings settings;
        settings.setValue("display/waylandDpiScale", value);
    });
    
    QLabel *noteWaylandDpi = new QLabel(tr("Wayland DPI scale override. Auto = system default, 1.00 = 100%, 1.50 = 150%, 2.00 = 200%. Only affects Wayland. Requires restart."));
    noteWaylandDpi->setWordWrap(true);
    noteWaylandDpi->setStyleSheet("color: gray; font-size: 10px;");

    layout->addWidget(previewToggle);
    layout->addWidget(note);
    layout->addWidget(dpiLabel);
    layout->addWidget(dpiSelector);
    layout->addWidget(notePDF);
    layout->addWidget(waylandDpiLabel);
    layout->addWidget(waylandDpiScaleSpinBox);
    layout->addWidget(noteWaylandDpi);

    layout->addStretch();

    // return performanceTab;
}

void ControlPanelDialog::createToolbarTab(){
    toolbarTab = new QWidget(this);
    QVBoxLayout *toolbarLayout = new QVBoxLayout(toolbarTab);

    // ✅ Checkbox to show/hide benchmark controls
    QCheckBox *benchmarkVisibilityCheckbox = new QCheckBox(tr("Show Benchmark Controls"), toolbarTab);
    benchmarkVisibilityCheckbox->setChecked(mainWindowRef->areBenchmarkControlsVisible());
    toolbarLayout->addWidget(benchmarkVisibilityCheckbox);
    QLabel *benchmarkNote = new QLabel(tr("This will show/hide the benchmark controls on the toolbar. Press the clock button to start/stop the benchmark."));
    benchmarkNote->setWordWrap(true);
    benchmarkNote->setStyleSheet("color: gray; font-size: 10px;");
    toolbarLayout->addWidget(benchmarkNote);

#ifdef Q_OS_LINUX
    // Palm rejection settings (Linux only - Windows has built-in palm rejection)
    toolbarLayout->addSpacing(15);
    
    QLabel *palmRejectionSectionLabel = new QLabel(tr("Palm Rejection (Linux Only)"), toolbarTab);
    palmRejectionSectionLabel->setStyleSheet("font-weight: bold; margin-top: 10px;");
    toolbarLayout->addWidget(palmRejectionSectionLabel);
    
    palmRejectionCheckbox = new QCheckBox(tr("Disable touch gestures when stylus is active"), toolbarTab);
    palmRejectionCheckbox->setChecked(mainWindowRef->isPalmRejectionEnabled());
    toolbarLayout->addWidget(palmRejectionCheckbox);
    
    QHBoxLayout *palmDelayLayout = new QHBoxLayout();
    QLabel *palmDelayLabel = new QLabel(tr("Restore delay:"), toolbarTab);
    palmRejectionDelaySpinBox = new QSpinBox(toolbarTab);
    palmRejectionDelaySpinBox->setRange(0, 5000);
    palmRejectionDelaySpinBox->setSingleStep(100);
    palmRejectionDelaySpinBox->setSuffix(" ms");
    palmRejectionDelaySpinBox->setValue(mainWindowRef->getPalmRejectionDelay());
    palmRejectionDelaySpinBox->setEnabled(palmRejectionCheckbox->isChecked());
    palmDelayLayout->addWidget(palmDelayLabel);
    palmDelayLayout->addWidget(palmRejectionDelaySpinBox);
    palmDelayLayout->addStretch();
    toolbarLayout->addLayout(palmDelayLayout);
    
    QLabel *palmRejectionNote = new QLabel(tr("When enabled, touch gestures are temporarily disabled while the stylus is hovering or touching the screen. "
                                              "After the stylus leaves, touch gestures are restored after the specified delay. "
                                              "This helps prevent accidental palm touches while writing."), toolbarTab);
    palmRejectionNote->setWordWrap(true);
    palmRejectionNote->setStyleSheet("color: gray; font-size: 10px;");
    toolbarLayout->addWidget(palmRejectionNote);
    
    // Connect checkbox to enable/disable delay spinbox
    connect(palmRejectionCheckbox, &QCheckBox::toggled, palmRejectionDelaySpinBox, &QSpinBox::setEnabled);
    
    // Apply settings immediately when changed
    connect(palmRejectionCheckbox, &QCheckBox::toggled, this, [this](bool checked) {
        mainWindowRef->setPalmRejectionEnabled(checked);
    });
    
    connect(palmRejectionDelaySpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        mainWindowRef->setPalmRejectionDelay(value);
    });
#endif

    // Stylus Side Button Mapping
    toolbarLayout->addSpacing(15);
    
    QLabel *stylusSectionLabel = new QLabel(tr("Stylus Side Buttons"), toolbarTab);
    stylusSectionLabel->setStyleSheet("font-weight: bold; margin-top: 10px;");
    toolbarLayout->addWidget(stylusSectionLabel);
    
    // Action options for stylus buttons
    QStringList stylusActionOptions = {
        tr("None"),
        tr("Hold Straight Line"),
        tr("Hold Lasso"),
        tr("Hold Eraser"),
        tr("Hold Text Selection")
    };
    
    // Button A row
    QHBoxLayout *buttonALayout = new QHBoxLayout();
    QString buttonAQtName = (mainWindowRef->getStylusButtonAQt() == Qt::MiddleButton) ? tr("Middle") : tr("Right");
    stylusButtonALabel = new QLabel(tr("Button A (%1):").arg(buttonAQtName), toolbarTab);
    stylusButtonALabel->setMinimumWidth(100);
    buttonALayout->addWidget(stylusButtonALabel);
    
    stylusButtonACombo = new QComboBox(toolbarTab);
    stylusButtonACombo->addItems(stylusActionOptions);
    stylusButtonACombo->setCurrentIndex(static_cast<int>(mainWindowRef->getStylusButtonAAction()));
    buttonALayout->addWidget(stylusButtonACombo);
    
    detectButtonAButton = new QPushButton(tr("Detect"), toolbarTab);
    detectButtonAButton->setFixedWidth(70);
    buttonALayout->addWidget(detectButtonAButton);
    buttonALayout->addStretch();
    toolbarLayout->addLayout(buttonALayout);
    
    // Button B row
    QHBoxLayout *buttonBLayout = new QHBoxLayout();
    QString buttonBQtName = (mainWindowRef->getStylusButtonBQt() == Qt::MiddleButton) ? tr("Middle") : tr("Right");
    stylusButtonBLabel = new QLabel(tr("Button B (%1):").arg(buttonBQtName), toolbarTab);
    stylusButtonBLabel->setMinimumWidth(100);
    buttonBLayout->addWidget(stylusButtonBLabel);
    
    stylusButtonBCombo = new QComboBox(toolbarTab);
    stylusButtonBCombo->addItems(stylusActionOptions);
    stylusButtonBCombo->setCurrentIndex(static_cast<int>(mainWindowRef->getStylusButtonBAction()));
    buttonBLayout->addWidget(stylusButtonBCombo);
    
    detectButtonBButton = new QPushButton(tr("Detect"), toolbarTab);
    detectButtonBButton->setFixedWidth(70);
    buttonBLayout->addWidget(detectButtonBButton);
    buttonBLayout->addStretch();
    toolbarLayout->addLayout(buttonBLayout);
    
    QLabel *stylusNote = new QLabel(tr("Map stylus side buttons to tools. Hold button to enable, release to disable. "
                                       "Use 'Detect' to identify which physical button is which. "
                                       "Button mapping may vary by tablet and driver configuration."), toolbarTab);
    stylusNote->setWordWrap(true);
    stylusNote->setStyleSheet("color: gray; font-size: 10px;");
    toolbarLayout->addWidget(stylusNote);
    
    // Connect stylus button combos
    connect(stylusButtonACombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        mainWindowRef->setStylusButtonAAction(static_cast<StylusButtonAction>(index));
    });
    
    connect(stylusButtonBCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        mainWindowRef->setStylusButtonBAction(static_cast<StylusButtonAction>(index));
    });
    
    // Connect detect buttons
    connect(detectButtonAButton, &QPushButton::clicked, this, [this]() {
        detectStylusButton(true);
    });
    
    connect(detectButtonBButton, &QPushButton::clicked, this, [this]() {
        detectStylusButton(false);
    });

    toolbarLayout->addStretch();
    toolbarTab->setLayout(toolbarLayout);
    tabWidget->addTab(toolbarTab, tr("Features"));


    // Connect the checkbox
    connect(benchmarkVisibilityCheckbox, &QCheckBox::toggled, mainWindowRef, &MainWindow::setBenchmarkControlsVisible);
}


void ControlPanelDialog::createButtonMappingTab() {
    QWidget *buttonTab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(buttonTab);

    QStringList buttonKeys = ButtonMappingHelper::getInternalButtonKeys();
    QStringList buttonDisplayNames = ButtonMappingHelper::getTranslatedButtons();
    QStringList dialModes = ButtonMappingHelper::getTranslatedDialModes();
    QStringList actions = ButtonMappingHelper::getTranslatedActions();

    for (int i = 0; i < buttonKeys.size(); ++i) {
        const QString &buttonKey = buttonKeys[i];
        const QString &buttonDisplayName = buttonDisplayNames[i];
        
        QHBoxLayout *h = new QHBoxLayout();
        h->addWidget(new QLabel(buttonDisplayName));  // Use translated button name

        QComboBox *holdCombo = new QComboBox();
        holdCombo->addItems(dialModes);  // Add translated dial mode names
        holdMappingCombos[buttonKey] = holdCombo;
        h->addWidget(new QLabel(tr("Hold:")));
        h->addWidget(holdCombo);

        QComboBox *pressCombo = new QComboBox();
        pressCombo->addItems(actions);  // Add translated action names
        pressMappingCombos[buttonKey] = pressCombo;
        h->addWidget(new QLabel(tr("Press:")));
        h->addWidget(pressCombo);

        layout->addLayout(h);
    }

    layout->addStretch();
    buttonTab->setLayout(layout);
    tabWidget->addTab(buttonTab, tr("Button Mapping"));
}

void ControlPanelDialog::createKeyboardMappingTab() {
    keyboardTab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(keyboardTab);
    
    // Instructions
    QLabel *instructionLabel = new QLabel(tr("Configure custom keyboard shortcuts for application actions:"), keyboardTab);
    instructionLabel->setWordWrap(true);
    layout->addWidget(instructionLabel);
    
    // Table to show current mappings
    keyboardTable = new QTableWidget(0, 2, keyboardTab);
    keyboardTable->setHorizontalHeaderLabels({tr("Key Sequence"), tr("Action")});
    keyboardTable->horizontalHeader()->setStretchLastSection(true);
    keyboardTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    keyboardTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(keyboardTable);
    
    // Buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    addKeyboardMappingButton = new QPushButton(tr("Add Mapping"), keyboardTab);
    removeKeyboardMappingButton = new QPushButton(tr("Remove Mapping"), keyboardTab);
    
    buttonLayout->addWidget(addKeyboardMappingButton);
    buttonLayout->addWidget(removeKeyboardMappingButton);
    buttonLayout->addStretch();
    
    layout->addLayout(buttonLayout);
    
    // Connections
    connect(addKeyboardMappingButton, &QPushButton::clicked, this, &ControlPanelDialog::addKeyboardMapping);
    connect(removeKeyboardMappingButton, &QPushButton::clicked, this, &ControlPanelDialog::removeKeyboardMapping);
    
    // Load current mappings
    if (mainWindowRef) {
        QMap<QString, QString> mappings = mainWindowRef->getKeyboardMappings();
        keyboardTable->setRowCount(mappings.size());
        int row = 0;
        for (auto it = mappings.begin(); it != mappings.end(); ++it) {
            keyboardTable->setItem(row, 0, new QTableWidgetItem(it.key()));
            QString displayAction = ButtonMappingHelper::internalKeyToDisplay(it.value(), false);
            keyboardTable->setItem(row, 1, new QTableWidgetItem(displayAction));
            row++;
        }
    }
    
    tabWidget->addTab(keyboardTab, tr("Keyboard Shortcuts"));
}

void ControlPanelDialog::createMouseDialTab() {
    mouseDialTab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(mouseDialTab);
    
    // Instructions
    QLabel *instructionLabel = new QLabel(tr("Configure mouse button combinations for dial control:"), mouseDialTab);
    instructionLabel->setWordWrap(true);
    layout->addWidget(instructionLabel);
    
    QLabel *usageLabel = new QLabel(tr("Hold mouse button combination for 0.5+ seconds, then use mouse wheel to control the dial."), mouseDialTab);
    usageLabel->setWordWrap(true);
    usageLabel->setStyleSheet("color: gray; font-size: 11px; margin-bottom: 15px;");
    layout->addWidget(usageLabel);
    
    // Available combinations and their mappings
    QStringList combinations = {
        tr("Right Button"),
        tr("Side Button 1"),  
        tr("Side Button 2"),
        tr("Right + Side 1"),
        tr("Right + Side 2"),
        tr("Side 1 + Side 2")
    };
    
    QStringList internalCombinations = {
        "Right",
        "Side1",
        "Side2", 
        "Right+Side1",
        "Right+Side2",
        "Side1+Side2"
    };
    
    QStringList dialModes = ButtonMappingHelper::getTranslatedDialModes();
    
    for (int i = 0; i < combinations.size(); ++i) {
        const QString &combination = combinations[i];
        const QString &internalCombination = internalCombinations[i];
        
        QHBoxLayout *h = new QHBoxLayout();
        
        QLabel *label = new QLabel(combination + ":", mouseDialTab);
        label->setMinimumWidth(120);
        h->addWidget(label);
        
        QComboBox *combo = new QComboBox(mouseDialTab);
        combo->addItems(dialModes);
        mouseDialMappingCombos[internalCombination] = combo;
        h->addWidget(combo);
        
        h->addStretch(); // Push everything to the left
        layout->addLayout(h);
    }
    
    // Add some spacing
    layout->addSpacing(20);
    
    // Step size information
    QLabel *stepLabel = new QLabel(tr("Mouse wheel step sizes per dial mode:"), mouseDialTab);
    stepLabel->setStyleSheet("font-weight: bold;");
    layout->addWidget(stepLabel);
    
    QLabel *stepInfo = new QLabel(
        tr("• Page Switching: 45° per wheel step (8 pages per rotation)\n"
           "• Color Presets: 60° per wheel step (6 presets per rotation)\n" 
           "• Zoom Control: 30° per wheel step (12 steps per rotation)\n"
           "• Thickness: 20° per wheel step (18 steps per rotation)\n"
           "• Tool Switching: 120° per wheel step (3 tools per rotation)\n"
           "• Pan & Scroll: 15° per wheel step (24 steps per rotation)"), mouseDialTab);
    stepInfo->setWordWrap(true);
    stepInfo->setStyleSheet("color: gray; font-size: 10px; margin: 5px 0px 15px 15px;");
    layout->addWidget(stepInfo);
    
    layout->addStretch();
    
    tabWidget->addTab(mouseDialTab, tr("Mouse Dial Control"));
}

void ControlPanelDialog::createThemeTab() {
    themeTab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(themeTab);
    
    // Custom accent color
    useCustomAccentCheckbox = new QCheckBox(tr("Use Custom Accent Color"), themeTab);
    layout->addWidget(useCustomAccentCheckbox);
    
    QLabel *accentColorLabel = new QLabel(tr("Accent Color:"), themeTab);
    accentColorButton = new QPushButton(themeTab);
    accentColorButton->setFixedSize(100, 30);
    connect(accentColorButton, &QPushButton::clicked, this, &ControlPanelDialog::chooseAccentColor);
    
    QHBoxLayout *accentColorLayout = new QHBoxLayout();
    accentColorLayout->addWidget(accentColorLabel);
    accentColorLayout->addWidget(accentColorButton);
    accentColorLayout->addStretch();
    layout->addLayout(accentColorLayout);
    
    QLabel *accentColorNote = new QLabel(tr("When enabled, use a custom accent color instead of the system accent color for the toolbar, dial, and tab selection."));
    accentColorNote->setWordWrap(true);
    accentColorNote->setStyleSheet("color: gray; font-size: 10px;");
    layout->addWidget(accentColorNote);
    
    // Enable/disable accent color button based on checkbox
    connect(useCustomAccentCheckbox, &QCheckBox::toggled, accentColorButton, &QPushButton::setEnabled);
    connect(useCustomAccentCheckbox, &QCheckBox::toggled, accentColorLabel, &QLabel::setEnabled);
    
    // Color palette preference
    useBrighterPaletteCheckbox = new QCheckBox(tr("Use Brighter Color Palette"), themeTab);
    layout->addWidget(useBrighterPaletteCheckbox);
    
    QLabel *paletteNote = new QLabel(tr("When enabled, use brighter colors (good for dark PDF backgrounds). When disabled, use darker colors (good for light PDF backgrounds). This setting is independent of the UI theme."));
    paletteNote->setWordWrap(true);
    paletteNote->setStyleSheet("color: gray; font-size: 10px;");
    layout->addWidget(paletteNote);
    
    layout->addStretch();
    
    tabWidget->addTab(themeTab, tr("Theme"));
}

void ControlPanelDialog::chooseAccentColor() {
    QColor chosen = QColorDialog::getColor(selectedAccentColor, this, tr("Select Accent Color"));
    if (chosen.isValid()) {
        selectedAccentColor = chosen;
        accentColorButton->setStyleSheet(QString("background-color: %1").arg(selectedAccentColor.name()));
    }
}

void ControlPanelDialog::addKeyboardMapping() {
    // Step 1: Capture key sequence
    KeyCaptureDialog captureDialog(this);
    if (captureDialog.exec() != QDialog::Accepted) {
        return;
    }
    
    QString keySequence = captureDialog.getCapturedKeySequence();
    if (keySequence.isEmpty()) {
        return;
    }
    
    // Check if key sequence already exists
    if (mainWindowRef && mainWindowRef->getKeyboardMappings().contains(keySequence)) {
        QMessageBox::warning(this, tr("Key Already Mapped"), 
            tr("The key sequence '%1' is already mapped. Please choose a different key combination.").arg(keySequence));
        return;
    }
    
    // Step 2: Choose action
    QStringList actions = ButtonMappingHelper::getTranslatedActions();
    bool ok;
    QString selectedAction = QInputDialog::getItem(this, tr("Select Action"), 
        tr("Choose the action to perform when '%1' is pressed:").arg(keySequence), 
        actions, 0, false, &ok);
    
    if (!ok || selectedAction.isEmpty()) {
        return;
    }
    
    // Convert display name to internal key
    QString internalKey = ButtonMappingHelper::displayToInternalKey(selectedAction, false);
    
    // Add the mapping
    if (mainWindowRef) {
        mainWindowRef->addKeyboardMapping(keySequence, internalKey);
        
        // Update table
        int row = keyboardTable->rowCount();
        keyboardTable->insertRow(row);
        keyboardTable->setItem(row, 0, new QTableWidgetItem(keySequence));
        keyboardTable->setItem(row, 1, new QTableWidgetItem(selectedAction));
    }
}

void ControlPanelDialog::removeKeyboardMapping() {
    int currentRow = keyboardTable->currentRow();
    if (currentRow < 0) {
        QMessageBox::information(this, tr("No Selection"), tr("Please select a mapping to remove."));
        return;
    }
    
    QTableWidgetItem *keyItem = keyboardTable->item(currentRow, 0);
    if (!keyItem) return;
    
    QString keySequence = keyItem->text();
    
    // Confirm removal
    int ret = QMessageBox::question(this, tr("Remove Mapping"), 
        tr("Are you sure you want to remove the keyboard shortcut '%1'?").arg(keySequence),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    
    if (ret == QMessageBox::Yes) {
        // Remove from MainWindow
        if (mainWindowRef) {
            mainWindowRef->removeKeyboardMapping(keySequence);
        }
        
        // Remove from table
        keyboardTable->removeRow(currentRow);
    }
}

void ControlPanelDialog::createControllerMappingTab() {
    controllerMappingTab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(controllerMappingTab);
    
    // Instructions
    QLabel *instructionLabel = new QLabel(tr("Configure physical controller button mappings for your Joy-Con or other controller:"), controllerMappingTab);
    instructionLabel->setWordWrap(true);
    layout->addWidget(instructionLabel);
    
    QLabel *noteLabel = new QLabel(tr("Note: This maps your physical controller buttons to the logical Joy-Con functions used by the application. "
                                     "After setting up the physical mapping, you can configure what actions each logical button performs in the 'Button Mapping' tab."), controllerMappingTab);
    noteLabel->setWordWrap(true);
    noteLabel->setStyleSheet("color: gray; font-size: 10px; margin-bottom: 10px;");
    layout->addWidget(noteLabel);
    
    // Button to open controller mapping dialog
    QPushButton *openMappingButton = new QPushButton(tr("Configure Controller Mapping"), controllerMappingTab);
    openMappingButton->setMinimumHeight(40);
    connect(openMappingButton, &QPushButton::clicked, this, &ControlPanelDialog::openControllerMapping);
    layout->addWidget(openMappingButton);
    
    // Button to reconnect controller
    reconnectButton = new QPushButton(tr("Reconnect Controller"), controllerMappingTab);
    reconnectButton->setMinimumHeight(40);
    reconnectButton->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; font-weight: bold; }");
    connect(reconnectButton, &QPushButton::clicked, this, &ControlPanelDialog::reconnectController);
    layout->addWidget(reconnectButton);
    
    // Status information
    QLabel *statusLabel = new QLabel(tr("Current controller status:"), controllerMappingTab);
    statusLabel->setStyleSheet("font-weight: bold; margin-top: 20px;");
    layout->addWidget(statusLabel);
    
    // Dynamic status label
    controllerStatusLabel = new QLabel(controllerMappingTab);
    updateControllerStatus();
    layout->addWidget(controllerStatusLabel);
    
    layout->addStretch();
    
    tabWidget->addTab(controllerMappingTab, tr("Controller Mapping"));
}

void ControlPanelDialog::openControllerMapping() {
    if (!mainWindowRef) {
        QMessageBox::warning(this, tr("Error"), tr("MainWindow reference not available."));
        return;
    }
    
    SDLControllerManager *controllerManager = mainWindowRef->getControllerManager();
    if (!controllerManager) {
        QMessageBox::warning(this, tr("Controller Not Available"), 
            tr("Controller manager is not available. Please ensure a controller is connected and restart the application."));
        return;
    }
    
    if (!controllerManager->getJoystick()) {
        QMessageBox::warning(this, tr("No Controller Detected"), 
            tr("No controller is currently connected. Please connect your controller and restart the application."));
        return;
    }
    
    ControllerMappingDialog dialog(controllerManager, this);
    dialog.exec();
}

void ControlPanelDialog::reconnectController() {
    if (!mainWindowRef) {
        QMessageBox::warning(this, tr("Error"), tr("MainWindow reference not available."));
        return;
    }
    
    SDLControllerManager *controllerManager = mainWindowRef->getControllerManager();
    if (!controllerManager) {
        QMessageBox::warning(this, tr("Controller Not Available"), 
            tr("Controller manager is not available."));
        return;
    }
    
    // Show reconnecting message
    controllerStatusLabel->setText(tr("🔄 Reconnecting..."));
    controllerStatusLabel->setStyleSheet("color: orange;");
    
    // Force the UI to update immediately
    QApplication::processEvents();
    
    // Attempt to reconnect using thread-safe method
    QMetaObject::invokeMethod(controllerManager, "reconnect", Qt::BlockingQueuedConnection);
    
    // Update status after reconnection attempt
    updateControllerStatus();
    
    // Show result message
    if (controllerManager->getJoystick()) {
        // Reconnect the controller signals in MainWindow
        mainWindowRef->reconnectControllerSignals();
        
        QMessageBox::information(this, tr("Reconnection Successful"), 
            tr("Controller has been successfully reconnected!"));
    } else {
        QMessageBox::warning(this, tr("Reconnection Failed"), 
            tr("Failed to reconnect controller. Please ensure your controller is powered on and in pairing mode, then try again."));
    }
}

void ControlPanelDialog::updateControllerStatus() {
    if (!mainWindowRef || !controllerStatusLabel) return;
    
    SDLControllerManager *controllerManager = mainWindowRef->getControllerManager();
    if (!controllerManager) {
        controllerStatusLabel->setText(tr("✗ Controller manager not available"));
        controllerStatusLabel->setStyleSheet("color: red;");
        return;
    }
    
    if (controllerManager->getJoystick()) {
        controllerStatusLabel->setText(tr("✓ Controller connected"));
        controllerStatusLabel->setStyleSheet("color: green; font-weight: bold;");
    } else {
        controllerStatusLabel->setText(tr("✗ No controller detected"));
        controllerStatusLabel->setStyleSheet("color: red; font-weight: bold;");
    }
}

void ControlPanelDialog::createAboutTab() {
    aboutTab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(aboutTab);
    
    // Add some spacing at the top
    layout->addSpacing(20);
    
    // Application icon
    QLabel *iconLabel = new QLabel(aboutTab);
    QPixmap iconPixmap(":/resources/icons/mainicon.png");
    if (iconPixmap.isNull()) {
        // Fallback to file system path if resource doesn't work
        iconPixmap.load("resources/icons/mainicon.png");
    }
    
    if (!iconPixmap.isNull()) {
        // Scale the icon to a reasonable size (e.g., 128x128)
        iconPixmap = iconPixmap.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        iconLabel->setPixmap(iconPixmap);
    } else {
        // Fallback text if icon can't be loaded
        iconLabel->setText("📝");
        iconLabel->setStyleSheet("font-size: 64px;");
    }
    iconLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(iconLabel);
    
    layout->addSpacing(10);
    
    // Application name
    QLabel *appNameLabel = new QLabel(tr("SpeedyNote"), aboutTab);
    appNameLabel->setAlignment(Qt::AlignCenter);
    appNameLabel->setStyleSheet("font-size: 24px; font-weight: bold");
    layout->addWidget(appNameLabel);
    
    layout->addSpacing(5);
    
    // Version
    QLabel *versionLabel = new QLabel(tr("Version 0.12.2"), aboutTab);
    versionLabel->setAlignment(Qt::AlignCenter);
    versionLabel->setStyleSheet("font-size: 14px; color: #7f8c8d;");
    layout->addWidget(versionLabel);
    
    layout->addSpacing(15);
    
    // Description
    QLabel *descriptionLabel = new QLabel(tr("A fast and intuitive note-taking application with PDF annotation support"), aboutTab);
    descriptionLabel->setAlignment(Qt::AlignCenter);
    descriptionLabel->setWordWrap(true);
    descriptionLabel->setStyleSheet("font-size: 12px; padding: 0 20px;");
    layout->addWidget(descriptionLabel);
    
    layout->addSpacing(20);
    
    // Author information
    QLabel *authorLabel = new QLabel(tr("Developed by GitHub @alpha-liu-01 and various contributors"), aboutTab);
    authorLabel->setAlignment(Qt::AlignCenter);
    authorLabel->setStyleSheet("font-size: 12px");
    layout->addWidget(authorLabel);
    
    layout->addSpacing(10);
    
    // Copyright
    QLabel *copyrightLabel = new QLabel(tr("© 2025 SpeedyNote. All rights reserved."), aboutTab);
    copyrightLabel->setAlignment(Qt::AlignCenter);
    copyrightLabel->setStyleSheet("font-size: 10px; color: #95a5a6;");
    layout->addWidget(copyrightLabel);
    
    // Add stretch to push everything to the top
    layout->addStretch();
    
    // Built with Qt info
    QLabel *qtLabel = new QLabel(tr("Built with Qt %1").arg(QT_VERSION_STR), aboutTab);
    qtLabel->setAlignment(Qt::AlignCenter);
    qtLabel->setStyleSheet("font-size: 9px; color: #bdc3c7;");
    layout->addWidget(qtLabel);
    
    layout->addSpacing(10);
    
    tabWidget->addTab(aboutTab, tr("About"));
}

void ControlPanelDialog::createCacheTab() {
    cacheTab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(cacheTab);

    // Add some spacing at the top
    layout->addSpacing(20);

    // Title
    QLabel *titleLabel = new QLabel(tr("Cache Management"), cacheTab);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
    layout->addWidget(titleLabel);

    layout->addSpacing(10);

    // Description
    QLabel *descriptionLabel = new QLabel(
        tr("SpeedyNote uses temporary folders to work with notebook files.\n"
           "These folders are normally cleaned up when you close a notebook,\n"
           "but crashes or force-close can leave orphaned files behind."),
        cacheTab
    );
    descriptionLabel->setAlignment(Qt::AlignCenter);
    descriptionLabel->setWordWrap(true);
    descriptionLabel->setStyleSheet("font-size: 11px; color: #7f8c8d; padding: 0 20px;");
    layout->addWidget(descriptionLabel);

    layout->addSpacing(20);

    // Show cache size
    qint64 cacheSize = SpnPackageManager::getTempDirsTotalSize();
    QString cacheSizeText = QString::number(cacheSize / 1024.0 / 1024.0, 'f', 2) + " MB";
    QLabel *cacheSizeLabel = new QLabel(tr("Current cache size: %1").arg(cacheSizeText), cacheTab);
    cacheSizeLabel->setAlignment(Qt::AlignCenter);
    cacheSizeLabel->setStyleSheet("font-size: 16px; font-weight: bold;");
    layout->addWidget(cacheSizeLabel);

    layout->addSpacing(5);

    // Location info
    QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QLabel *locationLabel = new QLabel(tr("Location: %1").arg(tempPath), cacheTab);
    locationLabel->setAlignment(Qt::AlignCenter);
    locationLabel->setStyleSheet("font-size: 9px; color: #95a5a6; font-style: italic;");
    locationLabel->setWordWrap(true);
    layout->addWidget(locationLabel);

    layout->addSpacing(30);

    // Clear cache button
    QPushButton *clearCacheButton = new QPushButton(tr("Clear Cache Now"), cacheTab);
    clearCacheButton->setFixedSize(180, 40);
    clearCacheButton->setStyleSheet("font-size: 13px; font-weight: bold; padding: 8px;");

    connect(clearCacheButton, &QPushButton::clicked, [this, cacheSizeLabel]() {
        // ✅ DISK CLEANUP: Warn user to close notebooks first
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            tr("Clear Cache?"),
            tr("This will delete all temporary cache files.\n\n"
               "⚠️ WARNING: Make sure all notebooks are closed before clearing cache, "
               "otherwise you may lose unsaved changes!\n\n"
               "Continue?"),
            QMessageBox::Yes | QMessageBox::No
        );

        if (reply == QMessageBox::Yes) {
            // Clean up orphaned temp directories
            SpnPackageManager::cleanupOrphanedTempDirs();

            // Update cache size display
            qint64 newCacheSize = SpnPackageManager::getTempDirsTotalSize();
            QString newCacheSizeText = QString::number(newCacheSize / 1024.0 / 1024.0, 'f', 2) + " MB";
            cacheSizeLabel->setText(tr("Current cache size: %1").arg(newCacheSizeText));

            // Show feedback message
            QMessageBox::information(this, tr("Cache Cleared"), 
                tr("Temporary cache files have been cleared successfully."));
        }
    });

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(clearCacheButton);
    buttonLayout->addStretch();
    layout->addLayout(buttonLayout);

    layout->addSpacing(20);

    // Warning note at bottom
    QLabel *warningLabel = new QLabel(
        tr("⚠️ Only clear cache when all notebooks are closed"),
        cacheTab
    );
    warningLabel->setAlignment(Qt::AlignCenter);
    warningLabel->setStyleSheet("font-size: 11px; color: #e74c3c; font-weight: bold;");
    layout->addWidget(warningLabel);

    // Add stretch to push everything to the top
    layout->addStretch();

    tabWidget->addTab(cacheTab, tr("Cache"));
}


void ControlPanelDialog::createLanguageTab() {
    languageTab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(languageTab);
    
    // Add some spacing at the top
    layout->addSpacing(10);
    
    // Title and description
    QLabel *titleLabel = new QLabel(tr("Language Settings"), languageTab);
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold;");
    layout->addWidget(titleLabel);
    
    layout->addSpacing(10);
    
    // Use system language checkbox
    useSystemLanguageCheckbox = new QCheckBox(tr("Use System Language (Auto-detect)"), languageTab);
    layout->addWidget(useSystemLanguageCheckbox);
    
    QLabel *systemNote = new QLabel(tr("When enabled, SpeedyNote will automatically detect and use your system's language setting."), languageTab);
    systemNote->setWordWrap(true);
    systemNote->setStyleSheet("color: gray; font-size: 11px; margin-bottom: 15px;");
    layout->addWidget(systemNote);
    
    // Manual language selection
    QLabel *manualLabel = new QLabel(tr("Manual Language Override:"), languageTab);
    manualLabel->setStyleSheet("font-weight: bold;");
    layout->addWidget(manualLabel);
    
    languageCombo = new QComboBox(languageTab);
    languageCombo->addItem(tr("English"), "en");
    languageCombo->addItem(tr("Español (Spanish)"), "es");
    languageCombo->addItem(tr("Français (French)"), "fr");
    languageCombo->addItem(tr("中文 (Chinese Simplified)"), "zh");
    layout->addWidget(languageCombo);
    
    QLabel *manualNote = new QLabel(tr("Select a specific language to override the system setting. Changes take effect after restarting the application."), languageTab);
    manualNote->setWordWrap(true);
    manualNote->setStyleSheet("color: gray; font-size: 11px; margin-bottom: 15px;");
    layout->addWidget(manualNote);
    
    // Current language status
    QLabel *statusLabel = new QLabel(tr("Current Language Status:"), languageTab);
    statusLabel->setStyleSheet("font-weight: bold; margin-top: 20px;");
    layout->addWidget(statusLabel);
    
    // Show current language
    QString currentLocale = QLocale::system().name();
    QString currentLangCode = currentLocale.section('_', 0, 0);
    QString currentLangName;
    if (currentLangCode == "es") currentLangName = tr("Spanish");
    else if (currentLangCode == "fr") currentLangName = tr("French");
    else if (currentLangCode == "zh") currentLangName = tr("Chinese Simplified");
    else currentLangName = tr("English");
    
    QLabel *currentLabel = new QLabel(tr("System Language: %1 (%2)").arg(currentLangName).arg(currentLocale), languageTab);
    currentLabel->setStyleSheet("margin-left: 10px;");
    layout->addWidget(currentLabel);
    
    // Load current settings
    if (mainWindowRef) {
        QSettings settings("SpeedyNote", "App");
        bool useSystemLang = settings.value("useSystemLanguage", true).toBool();
        QString overrideLang = settings.value("languageOverride", "en").toString();
        
        useSystemLanguageCheckbox->setChecked(useSystemLang);
        languageCombo->setEnabled(!useSystemLang);
        
        // Set combo to current override language
        for (int i = 0; i < languageCombo->count(); ++i) {
            if (languageCombo->itemData(i).toString() == overrideLang) {
                languageCombo->setCurrentIndex(i);
                break;
            }
        }
    }
    
    // Connect checkbox to enable/disable combo
    connect(useSystemLanguageCheckbox, &QCheckBox::toggled, this, [this](bool checked) {
        languageCombo->setEnabled(!checked);
    });
    
    // Add stretch to push everything to the top
    layout->addStretch();
    
    tabWidget->addTab(languageTab, tr("Language"));
}

void ControlPanelDialog::createCompatibilityTab() {
    compatibilityTab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(compatibilityTab);
    
    // Add some spacing at the top
    layout->addSpacing(10);
    
    // Title and description
    QLabel *titleLabel = new QLabel(tr("Compatibility Features"), compatibilityTab);
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold;");
    layout->addWidget(titleLabel);
    
    layout->addSpacing(10);
    
    // Folder selection section
    QLabel *folderSectionLabel = new QLabel(tr("Manual Folder Selection"), compatibilityTab);
    folderSectionLabel->setStyleSheet("font-size: 14px; font-weight: bold;");
    layout->addWidget(folderSectionLabel);
    
    QLabel *folderDescriptionLabel = new QLabel(tr("This feature allows you to manually select a save folder for your notes. "
                                                  "This is only for converting old folder-based notebooks to the new .spn format."), compatibilityTab);
    folderDescriptionLabel->setWordWrap(true);
    folderDescriptionLabel->setStyleSheet("font-size: 11px; margin-bottom: 10px;");
    layout->addWidget(folderDescriptionLabel);
    
    // Folder selection button
    selectFolderCompatibilityButton = new QPushButton(tr("Select Save Folder"), compatibilityTab);
    selectFolderCompatibilityButton->setIcon(QIcon(":/resources/icons/folder.png"));
    selectFolderCompatibilityButton->setMinimumHeight(40);
    selectFolderCompatibilityButton->setStyleSheet(
        "QPushButton {"
        "    background-color: #3498db;"
        "    color: white;"
        "    border: none;"
        "    padding: 8px 16px;"
        "    border-radius: 4px;"
        "    font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "    background-color: #2980b9;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #21618c;"
        "}"
    );
    
    connect(selectFolderCompatibilityButton, &QPushButton::clicked, this, &ControlPanelDialog::selectFolderCompatibility);
    layout->addWidget(selectFolderCompatibilityButton);
    
    // Warning note
    QLabel *warningLabel = new QLabel(tr("⚠️ Note: Make sure to select a folder that is empty or an old folder-based notebook. Otherwise, data may be lost."), compatibilityTab);
    warningLabel->setWordWrap(true);
    warningLabel->setStyleSheet("color: #e67e22; font-size: 10px; font-weight: bold; margin-top: 10px; "
                               "background-color: #fef9e7; padding: 8px; border-radius: 4px; border: 1px solid #f39c12;");
    layout->addWidget(warningLabel);
    
    // Add stretch to push everything to the top
    layout->addStretch();
    
    tabWidget->addTab(compatibilityTab, tr("Compatibility"));
}

void ControlPanelDialog::selectFolderCompatibility() {
    if (!mainWindowRef) {
        QMessageBox::warning(this, tr("Error"), tr("MainWindow reference not available."));
        return;
    }
    
    // Call the existing selectFolder method from MainWindow
    bool success = mainWindowRef->selectFolder();
    
    if (success) {
        // Show a confirmation message only if successful
        QMessageBox::information(this, tr("Folder Selection"), 
            tr("Folder selection completed successfully. You can now start taking notes in the selected folder."));
    } else {
        // Show appropriate message for cancellation
        QMessageBox::information(this, tr("Folder Selection Cancelled"), 
            tr("Folder selection was cancelled. No changes were made."));
    }
}

void ControlPanelDialog::detectStylusButton(bool isButtonA) {
    // Create a simple dialog that waits for a stylus button press
    QDialog detectDialog(this);
    detectDialog.setWindowTitle(isButtonA ? tr("Detect Button A") : tr("Detect Button B"));
    detectDialog.setFixedSize(350, 200);
    
    QVBoxLayout *layout = new QVBoxLayout(&detectDialog);
    
    QLabel *instructionLabel = new QLabel(
        tr("Press and hold a stylus side button on your tablet surface.\n\n"
           "The button you press will be assigned to '%1'.\n\n"
           "Press Cancel to abort.")
            .arg(isButtonA ? tr("Button A") : tr("Button B")), 
        &detectDialog);
    instructionLabel->setAlignment(Qt::AlignCenter);
    instructionLabel->setWordWrap(true);
    layout->addWidget(instructionLabel);
    
    QLabel *statusLabel = new QLabel(tr("Waiting for stylus button press..."), &detectDialog);
    statusLabel->setAlignment(Qt::AlignCenter);
    statusLabel->setStyleSheet("font-weight: bold; color: #3498db;");
    layout->addWidget(statusLabel);
    
    QPushButton *cancelButton = new QPushButton(tr("Cancel"), &detectDialog);
    layout->addWidget(cancelButton);
    
    Qt::MouseButton detectedButton = Qt::NoButton;
    
    // Install event filter to catch tablet events
    class TabletEventFilter : public QObject {
    public:
        Qt::MouseButton &detectedButton;
        QDialog *dialog;
        QLabel *statusLabel;
        bool isButtonA;
        
        TabletEventFilter(Qt::MouseButton &btn, QDialog *dlg, QLabel *lbl, bool btnA) 
            : detectedButton(btn), dialog(dlg), statusLabel(lbl), isButtonA(btnA) {}
        
        bool eventFilter(QObject *obj, QEvent *event) override {
            if (event->type() == QEvent::TabletPress) {
                QTabletEvent *tabletEvent = static_cast<QTabletEvent*>(event);
                Qt::MouseButtons buttons = tabletEvent->buttons();
                
                // Check for side button press (not just tip)
                if (buttons & Qt::MiddleButton) {
                    detectedButton = Qt::MiddleButton;
                    statusLabel->setText(QObject::tr("Detected: Middle Button"));
                    statusLabel->setStyleSheet("font-weight: bold; color: #27ae60;");
                    QTimer::singleShot(500, dialog, &QDialog::accept);
                    return true;
                } else if (buttons & Qt::RightButton) {
                    detectedButton = Qt::RightButton;
                    statusLabel->setText(QObject::tr("Detected: Right Button"));
                    statusLabel->setStyleSheet("font-weight: bold; color: #27ae60;");
                    QTimer::singleShot(500, dialog, &QDialog::accept);
                    return true;
                }
            }
            return QObject::eventFilter(obj, event);
        }
    };
    
    TabletEventFilter *filter = new TabletEventFilter(detectedButton, &detectDialog, statusLabel, isButtonA);
    detectDialog.installEventFilter(filter);
    
    connect(cancelButton, &QPushButton::clicked, &detectDialog, &QDialog::reject);
    
    int result = detectDialog.exec();
    
    delete filter;
    
    if (result == QDialog::Accepted && detectedButton != Qt::NoButton) {
        // Update the appropriate button mapping
        if (isButtonA) {
            mainWindowRef->setStylusButtonAQt(detectedButton);
            QString buttonName = (detectedButton == Qt::MiddleButton) ? tr("Middle") : tr("Right");
            stylusButtonALabel->setText(tr("Button A (%1):").arg(buttonName));
        } else {
            mainWindowRef->setStylusButtonBQt(detectedButton);
            QString buttonName = (detectedButton == Qt::MiddleButton) ? tr("Middle") : tr("Right");
            stylusButtonBLabel->setText(tr("Button B (%1):").arg(buttonName));
        }
        
        QMessageBox::information(this, tr("Button Detected"), 
            tr("Stylus button successfully detected and assigned to %1.")
                .arg(isButtonA ? tr("Button A") : tr("Button B")));
    }
}
