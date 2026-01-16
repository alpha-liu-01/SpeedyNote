#include "MarkdownNoteEntry.h"
#include "../../markdown/qmarkdowntextedit.h"
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

// Phase M.3: New constructor for LinkObject-based notes
MarkdownNoteEntry::MarkdownNoteEntry(const NoteDisplayData &data, QWidget *parent)
    : QFrame(parent), m_linkObjectId(data.linkObjectId)
{
    // Convert NoteDisplayData to internal MarkdownNoteData format
    noteData.id = data.noteId;
    noteData.title = data.title;
    noteData.content = data.content;
    noteData.color = data.color;
    noteData.highlightId = QString();  // Not used in new system
    noteData.pageNumber = -1;          // Derived from LinkObject at runtime
    
    isDarkMode = palette().color(QPalette::Window).lightness() < 128;
    setupUI();
    
    // Phase M.3: Configure link button for LinkObject navigation
    if (!m_linkObjectId.isEmpty()) {
        highlightLinkButton->setVisible(true);
        highlightLinkButton->setToolTip(tr("Jump to linked annotation"));
        // Disconnect old signal and connect new one
        disconnect(highlightLinkButton, &QPushButton::clicked, this, &MarkdownNoteEntry::onHighlightLinkClicked);
        connect(highlightLinkButton, &QPushButton::clicked, this, &MarkdownNoteEntry::onLinkObjectClicked);
    }
    
    // Set tooltip with LinkObject description if available
    if (!data.description.isEmpty()) {
        setToolTip(data.description);
    }
    
    applyStyle();
    updatePreview();
}

MarkdownNoteEntry::~MarkdownNoteEntry() = default;

void MarkdownNoteEntry::setupUI() {
    mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 8, 10, 8);
    mainLayout->setSpacing(6);
    
    // Header with title, color indicator, and buttons
    headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(6);
    
    // Color indicator (vertical bar with rounded ends)
    colorIndicator = new QFrame(this);
    colorIndicator->setObjectName("ColorIndicator");
    colorIndicator->setFixedWidth(4);
    colorIndicator->setMinimumHeight(24);
    colorIndicator->setStyleSheet(QString("background-color: %1; border-radius: 2px;")
                                  .arg(noteData.color.name()));
    
    // Title edit
    titleEdit = new QLineEdit(noteData.title.isEmpty() ? tr("Untitled Note") : noteData.title, this);
    titleEdit->setObjectName("NoteTitleEdit");
    titleEdit->setFrame(false);
    titleEdit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    titleEdit->setCursorPosition(0);
    titleEdit->deselect();
    connect(titleEdit, &QLineEdit::editingFinished, this, &MarkdownNoteEntry::onTitleEdited);
    
    // Jump to link button
    highlightLinkButton = new QPushButton("🔗", this);
    highlightLinkButton->setObjectName("NoteActionButton");
    highlightLinkButton->setFixedSize(24, 24);
    highlightLinkButton->setToolTip(tr("Jump to linked annotation"));
    highlightLinkButton->setVisible(!noteData.highlightId.isEmpty());
    highlightLinkButton->setCursor(Qt::PointingHandCursor);
    connect(highlightLinkButton, &QPushButton::clicked, this, &MarkdownNoteEntry::onHighlightLinkClicked);
    
    // Delete button
    deleteButton = new QPushButton("×", this);
    deleteButton->setObjectName("NoteDeleteButton");
    deleteButton->setFixedSize(24, 24);
    deleteButton->setToolTip(tr("Delete note"));
    deleteButton->setCursor(Qt::PointingHandCursor);
    connect(deleteButton, &QPushButton::clicked, this, &MarkdownNoteEntry::onDeleteClicked);
    
    headerLayout->addWidget(colorIndicator);
    headerLayout->addWidget(titleEdit);
    headerLayout->addWidget(highlightLinkButton);
    headerLayout->addWidget(deleteButton);
    
    // Preview label (shows in preview mode)
    previewLabel = new QLabel(this);
    previewLabel->setObjectName("NotePreviewLabel");
    previewLabel->setWordWrap(true);
    previewLabel->setTextFormat(Qt::PlainText);
    previewLabel->setMaximumHeight(60);
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
    // Styles are now primarily from QSS loaded by parent sidebar
    // Only set dynamic properties here
    QString bgColor = isDarkMode ? "#252525" : "#ffffff";
    QString borderColor = isDarkMode ? "#353535" : "#e4e7ec";
    QString textColor = isDarkMode ? "#e6e6e6" : "#1d2939";
    QString previewColor = isDarkMode ? "#909090" : "#667085";
    QString deleteHoverBg = isDarkMode ? "#4d2828" : "#ffccc7";
    
    // Card styling with rounded corners
    setStyleSheet(QString(R"(
        MarkdownNoteEntry {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 12px;
        }
        MarkdownNoteEntry:hover {
            background-color: %3;
            border-color: %4;
        }
    )").arg(bgColor, borderColor, 
            isDarkMode ? "#2a2a2a" : "#fafbfc",
            isDarkMode ? "#454545" : "#d0d5dd"));
    
    // Title edit
    titleEdit->setStyleSheet(QString(R"(
        QLineEdit {
            background: transparent;
            border: none;
            font-weight: bold;
            font-size: 14px;
            color: %1;
            padding: 2px 4px;
        }
        QLineEdit:focus {
            background-color: %2;
            border-radius: 4px;
        }
    )").arg(textColor, isDarkMode ? "#353535" : "#f2f4f7"));
    
    // Preview label
    previewLabel->setStyleSheet(QString(R"(
        QLabel {
            color: %1;
            font-size: 13px;
            padding: 4px 8px;
            background: transparent;
        }
    )").arg(previewColor));
    
    // Jump button
    highlightLinkButton->setStyleSheet(QString(R"(
        QPushButton {
            background-color: transparent;
            border: none;
            border-radius: 12px;
            font-size: 14px;
        }
        QPushButton:hover {
            background-color: %1;
        }
        QPushButton:pressed {
            background-color: %2;
        }
    )").arg(isDarkMode ? "rgba(255, 255, 255, 0.1)" : "rgba(0, 0, 0, 0.08)",
            isDarkMode ? "rgba(255, 255, 255, 0.15)" : "rgba(0, 0, 0, 0.15)"));
    
    // Delete button
    deleteButton->setStyleSheet(QString(R"(
        QPushButton {
            background-color: %1;
            border: none;
            border-radius: 12px;
            color: %2;
            font-weight: bold;
            font-size: 12px;
        }
        QPushButton:hover {
            background-color: %3;
        }
        QPushButton:pressed {
            background-color: #ff4d4f;
            color: white;
        }
    )").arg(isDarkMode ? "#3d1f1f" : "#fff1f0",
            isDarkMode ? "#ff6b6b" : "#cf1322",
            deleteHoverBg));
    
    setFrameStyle(QFrame::NoFrame);
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
        colorIndicator->setStyleSheet(QString("background-color: %1; border-radius: 2px;")
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
    colorIndicator->setStyleSheet(QString("background-color: %1; border-radius: 2px;")
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
    
    // Phase M.3: Also emit with linkObjectId for new system
    if (!m_linkObjectId.isEmpty()) {
        emit deleteWithLinkRequested(noteData.id, m_linkObjectId);
    }
}

void MarkdownNoteEntry::onPreviewClicked() {
    setPreviewMode(false); // Switch to edit mode
}

void MarkdownNoteEntry::onHighlightLinkClicked() {
    if (!noteData.highlightId.isEmpty()) {
        emit highlightLinkClicked(noteData.highlightId);
    }
}

// Phase M.3: Jump to parent LinkObject
void MarkdownNoteEntry::onLinkObjectClicked() {
    if (!m_linkObjectId.isEmpty()) {
        emit linkObjectClicked(m_linkObjectId);
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

