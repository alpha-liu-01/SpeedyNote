#pragma once

#include "../../objects/TextBoxObject.h"

#include <QWidget>
#include <QPushButton>
#include <QButtonGroup>
#include <QSlider>
#include <QLabel>
#include <QColor>

class ColorPresetButton;

class QMarkdownTextEdit;

class FloatingTextEditor : public QWidget {
    Q_OBJECT
public:
    explicit FloatingTextEditor(QWidget* parent = nullptr);

    void setTarget(TextBoxObject* obj);
    TextBoxObject* target() const { return m_target; }
    void closeEditor();
    void setDarkMode(bool dark);

signals:
    void repaintRequested();
    void editorClosed(const QString& objectId,
                      const QString& oldText, const QString& newText,
                      int oldAlignment, int newAlignment,
                      int oldOpacity, int newOpacity,
                      const QColor& oldFontColor, const QColor& newFontColor);

private:
    void buildUi();
    void updateAlignIcons();
    void onTextChanged();
    void onAlignmentChanged(int id);
    void onOpacityChanged(int value);
    void onColorButtonClicked();

    bool eventFilter(QObject* obj, QEvent* event) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void paintEvent(QPaintEvent* e) override;

    static constexpr int TITLE_BAR_HEIGHT = 30;
    static constexpr int TOOLBAR_HEIGHT = 28;

    QMarkdownTextEdit* m_editor = nullptr;
    QPushButton* m_alignLeft = nullptr;
    QPushButton* m_alignCenter = nullptr;
    QPushButton* m_alignRight = nullptr;
    QButtonGroup* m_alignGroup = nullptr;
    ColorPresetButton* m_colorButton = nullptr;
    QSlider* m_opacitySlider = nullptr;
    QLabel* m_opacityLabel = nullptr;
    QPushButton* m_closeButton = nullptr;
    QLabel* m_titleLabel = nullptr;

    TextBoxObject* m_target = nullptr;
    QString m_targetId;
    QString m_originalText;
    TextAlignment m_originalAlignment = TextAlignment::Left;
    int m_originalOpacity = 180;
    QColor m_originalFontColor = QColor(60, 60, 60);
    bool m_darkMode = false;

    QPoint m_dragOffset;
    bool m_dragging = false;
};
