#include "./KeyCaptureDialog.h"
#include <QApplication>
#include <QKeySequence>

KeyCaptureDialog::KeyCaptureDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Capture Key Sequence"));
    setFixedSize(350, 150);
    setModal(true);
    
    // Ensure we can capture all key events including Tab
    setFocusPolicy(Qt::StrongFocus);
    
    // Create UI elements
    instructionLabel = new QLabel(tr("Press the key combination you want to use:"), this);
    instructionLabel->setWordWrap(true);
    
    capturedLabel = new QLabel(tr("(No key captured yet)"), this);
    capturedLabel->setStyleSheet("QLabel { padding: 8px; border: 1px solid #ccc; }");
    capturedLabel->setAlignment(Qt::AlignCenter);
    
    // Buttons
    clearButton = new QPushButton(tr("Clear"), this);
    okButton = new QPushButton(tr("OK"), this);
    cancelButton = new QPushButton(tr("Cancel"), this);
    
    okButton->setDefault(true);
    okButton->setEnabled(false);  // Disabled until a key is captured
    
    // Layout
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(instructionLabel);
    mainLayout->addWidget(capturedLabel);
    
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(clearButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // Connections
    connect(clearButton, &QPushButton::clicked, this, &KeyCaptureDialog::clearSequence);
    connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    
    // Set focus to capture keys immediately
    setFocus();
}

void KeyCaptureDialog::keyPressEvent(QKeyEvent *event) {
    // Ignore auto-repeat events (when user holds a key)
    if (event->isAutoRepeat()) {
        event->accept();
        return;
    }
    
    // Don't capture modifier keys alone
    if (event->key() == Qt::Key_Control || event->key() == Qt::Key_Shift ||
        event->key() == Qt::Key_Alt || event->key() == Qt::Key_Meta) {
        return;
    }
    
    // Don't capture Escape (let it close the dialog)
    if (event->key() == Qt::Key_Escape) {
        QDialog::keyPressEvent(event);
        return;
    }
    
    // Build the key combination using Qt's portable format
    // This ensures consistent cross-platform storage
    int key = event->key();
    Qt::KeyboardModifiers mods = event->modifiers();
    
    // When Shift is pressed with number keys, Qt reports the shifted symbol
    // (e.g., Shift+1 → !, Shift+2 → @). We need to convert these back to
    // the base number keys to produce "Ctrl+Shift+1" instead of "Ctrl+Shift+!"
    if (mods & Qt::ShiftModifier) {
        switch (key) {
            case Qt::Key_Exclam:      key = Qt::Key_1; break;  // ! → 1
            case Qt::Key_At:          key = Qt::Key_2; break;  // @ → 2
            case Qt::Key_NumberSign:  key = Qt::Key_3; break;  // # → 3
            case Qt::Key_Dollar:      key = Qt::Key_4; break;  // $ → 4
            case Qt::Key_Percent:     key = Qt::Key_5; break;  // % → 5
            case Qt::Key_AsciiCircum: key = Qt::Key_6; break;  // ^ → 6
            case Qt::Key_Ampersand:   key = Qt::Key_7; break;  // & → 7
            case Qt::Key_Asterisk:    key = Qt::Key_8; break;  // * → 8
            case Qt::Key_ParenLeft:   key = Qt::Key_9; break;  // ( → 9
            case Qt::Key_ParenRight:  key = Qt::Key_0; break;  // ) → 0
            default: break;
        }
    }
    
    // Combine modifiers with the key into a single value for QKeySequence
    int keyWithMods = key;
    if (mods & Qt::ControlModifier) keyWithMods |= Qt::CTRL;
    if (mods & Qt::ShiftModifier) keyWithMods |= Qt::SHIFT;
    if (mods & Qt::AltModifier) keyWithMods |= Qt::ALT;
    if (mods & Qt::MetaModifier) keyWithMods |= Qt::META;
    
    // Use QKeySequence with PortableText for consistent cross-platform format
    // This produces strings like "Ctrl+S", "Shift+F1", "Page Up" etc.
    QKeySequence seq(keyWithMods);
    capturedSequence = seq.toString(QKeySequence::PortableText);
    
    updateDisplay();
    event->accept();
}

bool KeyCaptureDialog::event(QEvent *event) {
    // Intercept Tab/Backtab before Qt's focus navigation handles them
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
        int key = keyEvent->key();
        
        // Capture Tab and Backtab as shortcuts instead of focus navigation
        if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
            keyPressEvent(keyEvent);
            return true;
        }
    }
    return QDialog::event(event);
}

void KeyCaptureDialog::clearSequence() {
    capturedSequence.clear();
    updateDisplay();
    setFocus();  // Return focus to dialog for key capture
}

void KeyCaptureDialog::updateDisplay() {
    if (capturedSequence.isEmpty()) {
        capturedLabel->setText(tr("(No key captured yet)"));
        okButton->setEnabled(false);
    } else {
        capturedLabel->setText(capturedSequence);
        okButton->setEnabled(true);
    }
} 