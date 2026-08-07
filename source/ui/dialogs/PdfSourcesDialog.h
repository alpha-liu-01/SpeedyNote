#pragma once

#include <QDialog>

class Document;
class QTreeWidget;
class QPushButton;

class PdfSourcesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PdfSourcesDialog(Document* document, QWidget* parent = nullptr);

signals:
    void sourcesChanged();

private slots:
    void refreshRows();
    void locateSelectedSource();
    void retrySelectedSource();
    void showSelectedSourceInFolder();
    void locateSourcesInFolder();
    void updateButtonStates();

private:
    QString selectedSourceId() const;
    void handleLocatedPath(const QString& sourceId, const QString& path);
    QString choosePdfFile(const QString& startPath);

    Document* m_document = nullptr;
    QTreeWidget* m_sources = nullptr;
    QPushButton* m_locateButton = nullptr;
    QPushButton* m_retryButton = nullptr;
    QPushButton* m_showFolderButton = nullptr;
    QPushButton* m_locateFolderButton = nullptr;
};
