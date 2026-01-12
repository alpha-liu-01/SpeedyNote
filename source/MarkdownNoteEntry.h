#ifndef MARKDOWNNOTEENTRY_H
#define MARKDOWNNOTEENTRY_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QString>
#include <QColor>
#include <QFrame>
#include <QJsonObject>

class QMarkdownTextEdit;

// Structure to store markdown note data
struct MarkdownNoteData {
    QString id;                  // Unique ID for this note
    QString highlightId;         // ID of the associated highlight (empty if none)
    int pageNumber;              // Page number (0-based)
    QString title;               // Note title
    QString content;             // Markdown content
    QColor color;                // Color indicator (matches highlight color)
    
    // Serialization helpers
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["highlightId"] = highlightId;
        obj["pageNumber"] = pageNumber;
        obj["title"] = title;
        obj["content"] = content;
        obj["color"] = color.name(QColor::HexArgb);
        return obj;
    }
    
    static MarkdownNoteData fromJson(const QJsonObject &obj) {
        MarkdownNoteData note;
        note.id = obj["id"].toString();
        note.highlightId = obj["highlightId"].toString();
        note.pageNumber = obj["pageNumber"].toInt();
        note.title = obj["title"].toString();
        note.content = obj["content"].toString();
        note.color = QColor(obj["color"].toString());
        return note;
    }
};

// Individual markdown note entry widget (shows in sidebar)
class MarkdownNoteEntry : public QFrame {
    Q_OBJECT

public:
    explicit MarkdownNoteEntry(const MarkdownNoteData &data, QWidget *parent = nullptr);
    ~MarkdownNoteEntry();
    
    // Data access
    QString getNoteId() const { return noteData.id; }
    QString getHighlightId() const { return noteData.highlightId; }
    MarkdownNoteData getNoteData() const { return noteData; }
    void setNoteData(const MarkdownNoteData &data);
    
    // Content management
    QString getTitle() const;
    void setTitle(const QString &title);
    QString getContent() const;
    void setContent(const QString &content);
    QColor getColor() const { return noteData.color; }
    void setColor(const QColor &color);
    
    // UI state
    void setPreviewMode(bool preview);
    bool isPreviewMode() const { return previewMode; }

signals:
    void editRequested(const QString &noteId);
    void deleteRequested(const QString &noteId);
    void contentChanged(const QString &noteId);
    void titleChanged(const QString &noteId, const QString &newTitle);
    void highlightLinkClicked(const QString &highlightId);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onTitleEdited();
    void onDeleteClicked();
    void onPreviewClicked();
    void onHighlightLinkClicked();
    void onContentChanged();

private:
    void setupUI();
    void applyStyle();
    void updatePreview();
    
    MarkdownNoteData noteData;
    
    // UI components
    QVBoxLayout *mainLayout;
    QHBoxLayout *headerLayout;
    QLineEdit *titleEdit;
    QPushButton *deleteButton;
    QPushButton *highlightLinkButton;
    QFrame *colorIndicator;
    QLabel *previewLabel;
    QMarkdownTextEdit *editor;
    
    bool previewMode = true;
    bool isDarkMode = false;
};

#endif // MARKDOWNNOTEENTRY_H

