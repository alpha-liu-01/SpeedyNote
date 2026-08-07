#include "PageRangeSelectDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

PageRangeSelectDialog::PageRangeSelectDialog(int pageCount,
                                             QWidget* parent,
                                             const QString& title,
                                             const QString& sourceName,
                                             const QString& defaultRange)
    : QDialog(parent)
    , m_pageCount(pageCount)
{
    setWindowTitle(title.isEmpty() ? tr("Select Pages by Range") : title);
    setModal(true);

    QVBoxLayout* layout = new QVBoxLayout(this);

    QLabel* prompt = new QLabel(
        sourceName.isEmpty()
            ? tr("Enter page numbers and/or ranges:")
            : tr("Choose pages from %1:").arg(sourceName),
        this);
    layout->addWidget(prompt);

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tr("e.g. 3-7, 12"));
    m_input->setText(defaultRange);
    m_input->selectAll();
    layout->addWidget(m_input);

    QLabel* hint = new QLabel(tr("Valid pages: 1 to %1").arg(qMax(1, m_pageCount)), this);
    QFont hintFont = hint->font();
    hintFont.setPointSizeF(hintFont.pointSizeF() * 0.9);
    hint->setFont(hintFont);
    layout->addWidget(hint);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setStyleSheet("color: #C0392B;");
    m_errorLabel->setVisible(false);
    layout->addWidget(m_errorLabel);

    QHBoxLayout* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    QPushButton* cancelButton = new QPushButton(tr("Cancel"), this);
    QPushButton* okButton = new QPushButton(tr("OK"), this);
    okButton->setDefault(true);
    buttons->addWidget(cancelButton);
    buttons->addWidget(okButton);
    layout->addLayout(buttons);

    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(okButton, &QPushButton::clicked, this, &PageRangeSelectDialog::onAccept);
    connect(m_input, &QLineEdit::returnPressed, this, &PageRangeSelectDialog::onAccept);
}

void PageRangeSelectDialog::onAccept()
{
    m_indices = parseRange(m_input->text(), m_pageCount);
    if (m_indices.isEmpty()) {
        m_errorLabel->setText(tr("No valid pages in that range."));
        m_errorLabel->setVisible(true);
        return;
    }
    accept();
}

QList<int> PageRangeSelectDialog::parseRange(const QString& text, int pageCount)
{
    QList<int> result;
    if (pageCount <= 0) {
        return result;
    }

    const QString range = text.trimmed().toLower();

    QSet<int> seen;  // 0-based indices, deduped

    if (range == QLatin1String("all")) {
        for (int i = 0; i < pageCount; ++i) {
            result.append(i);
        }
        return result;
    }

    const QStringList parts = range.split(',', Qt::SkipEmptyParts);
    static const QRegularExpression rangePattern(QStringLiteral("^\\s*(\\d+)\\s*-\\s*(\\d+)\\s*$"));
    static const QRegularExpression singlePattern(QStringLiteral("^\\s*(\\d+)\\s*$"));

    for (const QString& part : parts) {
        const QRegularExpressionMatch rangeMatch = rangePattern.match(part);
        if (rangeMatch.hasMatch()) {
            int start = rangeMatch.captured(1).toInt();
            int end = rangeMatch.captured(2).toInt();
            if (start > end) {
                std::swap(start, end);
            }
            for (int p = start; p <= end; ++p) {
                if (p >= 1 && p <= pageCount) {
                    seen.insert(p - 1);  // 1-based input -> 0-based index
                }
            }
            continue;
        }

        const QRegularExpressionMatch singleMatch = singlePattern.match(part);
        if (singleMatch.hasMatch()) {
            const int p = singleMatch.captured(1).toInt();
            if (p >= 1 && p <= pageCount) {
                seen.insert(p - 1);
            }
        }
        // Unparseable parts are ignored.
    }

    result = QList<int>(seen.begin(), seen.end());
    std::sort(result.begin(), result.end());
    return result;
}
