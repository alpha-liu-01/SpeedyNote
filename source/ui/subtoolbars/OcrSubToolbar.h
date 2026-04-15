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
    void setDarkMode(bool darkMode) override;

    void setOcrAvailable(bool available);
    void setStatusText(const QString& text);
    void clearStatusAfterDelay(int ms = 5000);
    bool isShowTextEnabled() const;
    bool isConfidenceEnabled() const;
    bool isSnapToGridEnabled() const;
    void setSnapToGridChecked(bool checked);

signals:
    void scanPageClicked();
    void scanAllClicked();
    void autoOcrToggled(bool enabled);
    void showTextToggled(bool enabled);
    void confidenceToggled(bool enabled);
    void snapToGridToggled(bool enabled);

private:
    void createWidgets();
    void setupConnections();
    void updateIcons();
    void applyButtonStyle();

    QPushButton* m_scanPageButton = nullptr;
    QPushButton* m_scanAllButton = nullptr;
    QPushButton* m_autoOcrButton = nullptr;
    QPushButton* m_showTextButton = nullptr;
    QPushButton* m_confidenceButton = nullptr;
    QPushButton* m_snapButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QTimer* m_statusClearTimer = nullptr;

    bool m_darkMode = false;

    struct TabState {
        bool autoOcrEnabled = false;
        bool showTextEnabled = false;
        bool confidenceEnabled = false;
        bool snapToGridEnabled = false;
        bool initialized = false;
    };
    QHash<int, TabState> m_tabStates;

    static constexpr int BUTTON_SIZE = 28;
    static constexpr int ICON_SIZE = 18;
};

#endif // OCRSUBTOOLBAR_H
