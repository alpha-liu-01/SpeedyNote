#ifndef OCRSUBTOOLBAR_H
#define OCRSUBTOOLBAR_H

#include "SubToolbar.h"
#include <QHash>

class QPushButton;
class QLabel;
class QTimer;

class OcrSubToolbar : public SubToolbar {
    Q_OBJECT

public:
    explicit OcrSubToolbar(QWidget* parent = nullptr);

    void refreshFromSettings() override;
    void restoreTabState(int tabId) override;
    void saveTabState(int tabId) override;
    void clearTabState(int tabId) override;

    void setOcrAvailable(bool available);
    void setStatusText(const QString& text);
    void clearStatusAfterDelay(int ms = 5000);
    bool isShowTextEnabled() const;

signals:
    void scanPageClicked();
    void scanAllClicked();
    void autoOcrToggled(bool enabled);
    void showTextToggled(bool enabled);

private:
    void createWidgets();
    void setupConnections();

    QPushButton* m_scanPageButton = nullptr;
    QPushButton* m_scanAllButton = nullptr;
    QPushButton* m_autoOcrButton = nullptr;
    QPushButton* m_showTextButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QTimer* m_statusClearTimer = nullptr;

    struct TabState {
        bool autoOcrEnabled = false;
        bool showTextEnabled = false;
        bool initialized = false;
    };
    QHash<int, TabState> m_tabStates;
};

#endif // OCRSUBTOOLBAR_H
