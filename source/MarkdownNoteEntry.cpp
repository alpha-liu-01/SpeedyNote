#include "MarkdownNoteEntry.h"
#include "../markdown/qmarkdowntextedit.h"
#include <QTextDocument>
#include <QApplication>
#include <QPalette>
#include <QDebug>
#include <QTimer>

MarkdownNoteEntry::MarkdownNoteEntry(const MarkdownNoteData &data, QWidget *parent)
    : QFrame(parent), noteData(data)
{
    isDarkMode = palette().color(QPalette::Window).lightness() < 128;
    setupUI();
    applyStyle();
    updatePreview();
}

MarkdownNoteEntry::~MarkdownNoteEntry() = default;

void MarkdownNoteEntry::setupUI() {
    mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(4);
    
    // Header with title, color indicator, and buttons
    headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(4);
    
    // Color indicator (vertical bar)
    colorIndicator = new QFrame(this);
    colorIndicator->setFixedWidth(4);
    colorIndicator->setMinimumHeight(20);
    colorIndicator->setStyleSheet(QString("background-color: %1; border-radius: 0px;")
                                  .arg(noteData.color.name()));
    
    // Title edit
    titleEdit = new QLineEdit(noteData.title.isEmpty() ? tr("Untitled Note") : noteData.title, this);
    titleEdit->setFrame(false);
    titleEdit->setStyleSheet("font-weight: bold; background: transparent; padding-left: 2px;");
    titleEdit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter); // Left-align title text
    // Ensure cursor starts at beginning to show the start of text
    titleEdit->setCursorPosition(0);
    titleEdit->deselect();
    connect(titleEdit, &QLineEdit::editingFinished, this, &MarkdownNoteEntry::onTitleEdited);
    
    // Highlight link button (if linked to a highlight)
    highlightLinkButton = new QPushButton("🔗", this);
    highlightLinkButton->setFixedSize(20, 20);
    highlightLinkButton->setToolTip(tr("Jump to linked highlight"));
    highlightLinkButton->setVisible(!noteData.highlightId.isEmpty());
    highlightLinkButton->setStyleSheet(R"(
        QPushButton {
            background-color: transparent;
            border: none;
            font-size: 14px;
        }
        QPushButton:hover {
            background-color: rgba(100, 100, 100, 0.2);
            border-radius: 3px;
        }
    )");
    connect(highlightLinkButton, &QPushButton::clicked, this, &MarkdownNoteEntry::onHighlightLinkClicked);
    
    // Delete button
    deleteButton = new QPushButton("×", this);
    deleteButton->setFixedSize(20, 20);
    deleteButton->setToolTip(tr("Delete note"));
    deleteButton->setStyleSheet(R"(
        QPushButton {
            background-color: #ff4444;
            color: white;
            border: none;
            border-radius: 10px;
            font-weight: bold;
            font-size: 12px;
        }
        QPushButton:hover {
            background-color: #ff6666;
        }
        QPushButton:pressed {
            background-color: #cc2222;
        }
    )");
    connect(deleteButton, &QPushButton::clicked, this, &MarkdownNoteEntry::onDeleteClicked);
    
    headerLayout->addWidget(colorIndicator);
    headerLayout->addWidget(titleEdit);
    headerLayout->addWidget(highlightLinkButton);
    headerLayout->addWidget(deleteButton);
    
    // Preview label (shows in preview mode)
    previewLabel = new QLabel(this);
    previewLabel->setWordWrap(true);
    previewLabel->setTextFormat(Qt::PlainText);
    previewLabel->setMaximumHeight(60);
    previewLabel->setStyleSheet("padding: 4px; background: transparent;");
    previewLabel->setCursor(Qt::PointingHandCursor);
    previewLabel->installEventFilter(this);
    
    // Full editor (shows in edit mode)
    editor = new QMarkdownTextEdit(this);
    editor->setPlainText(noteData.content);
    editor->setMinimumHeight(150);
    editor->setMaximumHeight(300);
    editor->hide(); // Start in preview mode
    connect(editor, &QMarkdownTextEdit::textChanged, this, &MarkdownNoteEntry::onContentChanged);
    
    // Install event filter on editor to detect focus loss
    editor->installEventFilter(this);
    
    mainLayout->addLayout(headerLayout);
    mainLayout->addWidget(previewLabel);
    mainLayout->addWidget(editor);
}

void MarkdownNoteEntry::applyStyle() {
    QString bgColor = isDarkMode ? "#2b2b2b" : "#f5f5f5";
    QString borderColor = isDarkMode ? "#555555" : "#dddddd";
    
    setStyleSheet(QString(R"(
        MarkdownNoteEntry {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 0px;
        }
    )").arg(bgColor, borderColor));
    
    setFrameStyle(QFrame::StyledPanel | QFrame::Raised);
}

void MarkdownNoteEntry::updatePreview() {
    if (noteData.content.isEmpty()) {
        previewLabel->setText(tr("(empty note)"));
        previewLabel->setStyleSheet("padding: 4px; color: gray; font-style: italic;");
    } else {
        // Show first 100 characters of content
        QString preview = noteData.content.left(100);
        if (noteData.content.length() > 100) {
            preview += "...";
        }
        previewLabel->setText(preview);
        previewLabel->setStyleSheet("padding: 4px;");
    }
}

void MarkdownNoteEntry::setNoteData(const MarkdownNoteData &data) {
    // ✅ OPTIMIZATION: Only update fields that have actually changed
    // This avoids expensive QMarkdownTextEdit re-parsing when content is the same
    
    bool titleChanged = (noteData.title != data.title);
    bool contentChanged = (noteData.content != data.content);
    bool colorChanged = (noteData.color != data.color);
    bool highlightLinkChanged = (noteData.highlightId != data.highlightId);
    
    // Update the stored data
    noteData = data;
    
    // Only update UI elements that have changed
    if (titleChanged) {
        titleEdit->setText(data.title.isEmpty() ? tr("Untitled Note") : data.title);
        titleEdit->setCursorPosition(0);
        titleEdit->deselect();
    }
    
    if (contentChanged) {
        // This is the expensive operation - only do it when content actually changed
        editor->setPlainText(data.content);
        updatePreview();
    }
    
    if (colorChanged) {
        colorIndicator->setStyleSheet(QString("background-color: %1; border-radius: 0px;")
                                      .arg(data.color.name()));
    }
    
    if (highlightLinkChanged) {
        highlightLinkButton->setVisible(!data.highlightId.isEmpty());
    }
}

QString MarkdownNoteEntry::getTitle() const {
    return titleEdit->text();
}

void MarkdownNoteEntry::setTitle(const QString &title) {
    titleEdit->setText(title);
    noteData.title = title;
}

QString MarkdownNoteEntry::getContent() const {
    return editor->toPlainText();
}

void MarkdownNoteEntry::setContent(const QString &content) {
    editor->setPlainText(content);
    noteData.content = content;
    updatePreview();
}

void MarkdownNoteEntry::setColor(const QColor &color) {
    noteData.color = color;
    colorIndicator->setStyleSheet(QString("background-color: %1; border-radius: 0px;")
                                  .arg(color.name()));
}

void MarkdownNoteEntry::setPreviewMode(bool preview) {
    if (previewMode == preview) return;
    
    previewMode = preview;
    
    if (preview) {
        // Save content before hiding editor
        noteData.content = editor->toPlainText();
        updatePreview();
        editor->hide();
        previewLabel->show();
    } else {
        // Show full editor
        previewLabel->hide();
        editor->show();
        editor->setFocus();
        emit editRequested(noteData.id);
    }
}

void MarkdownNoteEntry::onTitleEdited() {
    QString newTitle = titleEdit->text();
    if (newTitle != noteData.title) {
        noteData.title = newTitle;
        emit titleChanged(noteData.id, newTitle);
        emit contentChanged(noteData.id);
    }
}

void MarkdownNoteEntry::onDeleteClicked() {
    emit deleteRequested(noteData.id);
}

void MarkdownNoteEntry::onPreviewClicked() {
    setPreviewMode(false); // Switch to edit mode
}

void MarkdownNoteEntry::onHighlightLinkClicked() {
    if (!noteData.highlightId.isEmpty()) {
        emit highlightLinkClicked(noteData.highlightId);
    }
}

void MarkdownNoteEntry::onContentChanged() {
    noteData.content = editor->toPlainText();
    updatePreview();
    emit contentChanged(noteData.id);
}

bool MarkdownNoteEntry::eventFilter(QObject *obj, QEvent *event) {
    if (obj == previewLabel && event->type() == QEvent::MouseButtonPress) {
        onPreviewClicked();
        return true;
    }
    
    // Handle editor focus out - return to preview mode when clicking elsewhere
    if (obj == editor && event->type() == QEvent::FocusOut) {
        // Only switch to preview if focus is going to something outside this widget
        QWidget *focusWidget = QApplication::focusWidget();
        if (focusWidget != titleEdit && !editor->isAncestorOf(focusWidget)) {
            // Give a small delay to allow the click to be processed
            QTimer::singleShot(100, this, [this]() {
                if (!editor->hasFocus() && !titleEdit->hasFocus()) {
                    setPreviewMode(true);
                }
            });
        }
    }
    
    return QFrame::eventFilter(obj, event);
}

