#ifndef PAGERANGESELECTDIALOG_H
#define PAGERANGESELECTDIALOG_H

#include <QDialog>
#include <QList>
#include <QString>

class QLineEdit;
class QLabel;

/**
 * @brief Small dialog to select pages by a range expression (Plan C).
 *
 * Accepts a comma-separated list of 1-based page numbers and ranges, e.g.
 * "3-7, 12". Parses to a sorted, deduplicated list of 0-based indices clamped
 * to the valid page count. Retrieve the result via selectedIndices() after
 * exec() returns QDialog::Accepted.
 */
class PageRangeSelectDialog : public QDialog {
    Q_OBJECT

public:
    /**
     * @param pageCount Total number of pages in the document (>= 1).
     * @param parent Parent widget.
     */
    explicit PageRangeSelectDialog(int pageCount,
                                   QWidget* parent = nullptr,
                                   const QString& title = QString(),
                                   const QString& sourceName = QString(),
                                   const QString& defaultRange = QString());

    /**
     * @brief The parsed 0-based page indices (sorted, unique, in-range).
     */
    QList<int> selectedIndices() const { return m_indices; }

    /**
     * @brief Parse a range expression into sorted, unique 0-based indices.
     * @param text Range expression (e.g. "3-7, 12"). "all" selects every page.
     * @param pageCount Total pages; out-of-range values are dropped.
     */
    static QList<int> parseRange(const QString& text, int pageCount);

private slots:
    void onAccept();

private:
    int m_pageCount = 0;
    QLineEdit* m_input = nullptr;
    QLabel* m_errorLabel = nullptr;
    QList<int> m_indices;
};

#endif // PAGERANGESELECTDIALOG_H
