#include "MarkdownNoteEntry.h"
#include "../../markdown/qmarkdowntextedit.h"
#include <QTextDocument>
#include <QApplication>
#include <QPalette>
#include <QDebug>
#include <QDesktopServices>
#include <QMouseEvent>
#include <QResizeEvent>
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
    
    // Edit button (switches to edit mode)
    editButton = new QPushButton(this);
    editButton->setObjectName("NoteEditButton");
    editButton->setFixedSize(24, 24);
    editButton->setToolTip(tr("Edit note"));
    editButton->setCursor(Qt::PointingHandCursor);
    editButton->setIcon(QIcon(isDarkMode
        ? QStringLiteral(":/resources/icons/edit_reversed.png")
        : QStringLiteral(":/resources/icons/edit.png")));
    editButton->setIconSize(QSize(16, 16));
    connect(editButton, &QPushButton::clicked, this, &MarkdownNoteEntry::onPreviewClicked);

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
    headerLayout->addWidget(editButton);
    headerLayout->addWidget(highlightLinkButton);
    headerLayout->addWidget(deleteButton);
    
    // Rendered markdown preview (shows in preview mode)
    previewBrowser = new QTextBrowser(this);
    previewBrowser->setObjectName("NotePreviewBrowser");
    previewBrowser->setReadOnly(true);
    previewBrowser->setOpenExternalLinks(false);
    previewBrowser->setOpenLinks(false);
    connect(previewBrowser, &QTextBrowser::anchorClicked, this, [](const QUrl &url) {
        QDesktopServices::openUrl(url);
    });
    previewBrowser->setFrameShape(QFrame::NoFrame);
    previewBrowser->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    previewBrowser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    previewBrowser->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    previewBrowser->installEventFilter(this);
    
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
    mainLayout->addWidget(previewBrowser);
    mainLayout->addWidget(editor);
}

void MarkdownNoteEntry::applyStyle() {
    // Styles are now primarily from QSS loaded by parent sidebar
    // Only set dynamic properties here
    // Unified gray colors: dark #2a2e32/#3a3e42/#4d4d4d, light #F5F5F5/#E8E8E8/#D0D0D0
    QString bgColor = isDarkMode ? "#2a2e32" : "#ffffff";
    QString borderColor = isDarkMode ? "#4d4d4d" : "#D0D0D0";
    QString textColor = isDarkMode ? "#e6e6e6" : "#1d2939";
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
            isDarkMode ? "#3a3e42" : "#F5F5F5",
            isDarkMode ? "#4d4d4d" : "#D0D0D0"));
    
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
    )").arg(textColor, isDarkMode ? "#3a3e42" : "#F5F5F5"));
    
    // Edit button
    editButton->setStyleSheet(QString(R"(
        QPushButton {
            background-color: transparent;
            border: none;
            border-radius: 12px;
        }
        QPushButton:hover {
            background-color: %1;
        }
        QPushButton:pressed {
            background-color: %2;
        }
    )").arg(isDarkMode ? "rgba(255, 255, 255, 0.1)" : "rgba(0, 0, 0, 0.08)",
            isDarkMode ? "rgba(255, 255, 255, 0.15)" : "rgba(0, 0, 0, 0.15)"));

    // Preview browser
    previewBrowser->setStyleSheet(QString(R"(
        QTextBrowser {
            color: %1;
            font-size: 13px;
            padding: 4px 8px;
            background: transparent;
            border: none;
        }
    )").arg(textColor));
    
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
        previewBrowser->setHtml(
            QStringLiteral("<i style='color:gray'>") + tr("(empty note)") + QStringLiteral("</i>"));
    } else {
        previewBrowser->document()->setMarkdown(noteData.content);
    }
    adjustPreviewHeight();
}

void MarkdownNoteEntry::adjustPreviewHeight() {
    const int maxPreviewHeight = 200;

    int viewportWidth = previewBrowser->viewport()->width();
    if (viewportWidth <= 0) {
        if (previewBrowser->isVisible()) {
            QTimer::singleShot(0, this, &MarkdownNoteEntry::adjustPreviewHeight);
        }
        return;
    }

    previewBrowser->document()->setTextWidth(viewportWidth);
    int docHeight = static_cast<int>(previewBrowser->document()->size().height())
                    + (previewBrowser->height() - previewBrowser->viewport()->height())
                    + 2;
    int target = qBound(20, docHeight, maxPreviewHeight);
    if (previewBrowser->height() != target) {
        m_adjustingHeight = true;
        previewBrowser->setFixedHeight(target);
        m_adjustingHeight = false;
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
        noteData.content = editor->toPlainText();
        editor->hide();
        previewBrowser->show();
        editButton->setVisible(true);
        updatePreview();
    } else {
        // Show full editor
        previewBrowser->hide();
        editButton->setVisible(false);
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
    if (obj == previewBrowser) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::MiddleButton) {
                onPreviewClicked();
                return true;
            }
        }
        if (event->type() == QEvent::Resize && !m_adjustingHeight) {
            auto *re = static_cast<QResizeEvent *>(event);
            if (re->size().width() != re->oldSize().width()) {
                QTimer::singleShot(0, this, &MarkdownNoteEntry::adjustPreviewHeight);
            }
        }
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

