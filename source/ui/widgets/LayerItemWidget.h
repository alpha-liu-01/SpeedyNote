#ifndef LAYERITEMWIDGET_H
#define LAYERITEMWIDGET_H

#include <QWidget>
#include <QIcon>

class QLabel;
class QLineEdit;

/**
 * @brief A touch-friendly layer item widget for the LayerPanel.
 * 
 * Each layer item displays:
 * - Visibility toggle button (36×36, eye icon)
 * - Selection toggle (checkbox-style, for batch operations)
 * - Layer name (click to select as active, double-click to rename)
 * 
 * Visual states:
 * - Normal: Standard background
 * - Active: Highlighted background (this is the layer being drawn on)
 * - Selected: Checkbox checked (for merge/batch operations)
 * - Hidden: Eye icon crossed out
 * 
 * Height: 48px for comfortable touch targets
 * 
 * Part of Phase L.1 - LayerPanel Touch-Friendly Restyle
 */
class LayerItemWidget : public QWidget {
    Q_OBJECT

public:
    explicit LayerItemWidget(int layerIndex, QWidget* parent = nullptr);
    
    /**
     * @brief Get the layer index this widget represents.
     */
    int layerIndex() const { return m_layerIndex; }
    
    /**
     * @brief Update the layer index (after reordering).
     */
    void setLayerIndex(int index) { m_layerIndex = index; }
    
    /**
     * @brief Set the layer name displayed.
     */
    void setLayerName(const QString& name);
    
    /**
     * @brief Get the current layer name.
     */
    QString layerName() const;
    
    /**
     * @brief Set the visibility state.
     * @param visible True if layer is visible.
     */
    void setLayerVisible(bool visible);
    
    /**
     * @brief Get the visibility state.
     */
    bool isLayerVisible() const { return m_visible; }
    
    /**
     * @brief Set the selection state (checkbox for batch operations).
     * @param selected True if selected for batch operation.
     */
    void setSelected(bool selected);
    
    /**
     * @brief Get the selection state.
     */
    bool isSelected() const { return m_selected; }
    
    /**
     * @brief Set whether this is the active layer (currently being drawn on).
     * @param active True if this is the active layer.
     */
    void setActive(bool active);
    
    /**
     * @brief Get whether this is the active layer.
     */
    bool isActive() const { return m_active; }
    
    /**
     * @brief Set dark mode for theming.
     */
    void setDarkMode(bool dark);
    
    /**
     * @brief Start inline editing of the layer name.
     */
    void startEditing();
    
    /**
     * @brief Get recommended size.
     */
    QSize sizeHint() const override;
    
    /**
     * @brief Get minimum size.
     */
    QSize minimumSizeHint() const override;

signals:
    /**
     * @brief Emitted when visibility is toggled.
     * @param index The layer index.
     * @param visible The new visibility state.
     */
    void visibilityToggled(int index, bool visible);
    
    /**
     * @brief Emitted when selection checkbox is toggled.
     * @param index The layer index.
     * @param selected The new selection state.
     */
    void selectionToggled(int index, bool selected);
    
    /**
     * @brief Emitted when the item is clicked (single click).
     * @param index The layer index.
     */
    void clicked(int index);
    
    /**
     * @brief Emitted when rename is requested (double-click).
     * @param index The layer index.
     */
    void editRequested(int index);
    
    /**
     * @brief Emitted when the layer name is changed.
     * @param index The layer index.
     * @param newName The new name.
     */
    void nameChanged(int index, const QString& newName);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent* event) override;
#else
    void enterEvent(QEvent* event) override;
#endif
    void leaveEvent(QEvent* event) override;

private slots:
    void onVisibilityClicked();
    void onSelectionClicked();
    void onEditingFinished();

private:
    void updateVisibilityIcon();
    QColor backgroundColor() const;
    bool isDarkMode() const;
    
    // Layout areas (for hit testing)
    QRect visibilityButtonRect() const;
    QRect selectionToggleRect() const;
    QRect nameAreaRect() const;

    int m_layerIndex;
    QString m_name;
    bool m_visible = true;
    bool m_selected = false;
    bool m_active = false;
    bool m_darkMode = false;
    
    // Interaction state
    bool m_hovered = false;
    bool m_pressed = false;
    int m_pressedArea = -1;  // 0=visibility, 1=selection, 2=name
    
    // Icons
    QIcon m_visibleIcon;
    QIcon m_notVisibleIcon;
    
    // Inline editing
    QLineEdit* m_nameEdit = nullptr;
    bool m_editing = false;
    
    // Layout constants
    static constexpr int ITEM_HEIGHT = 48;
    static constexpr int BUTTON_SIZE = 36;
    static constexpr int TOGGLE_SIZE = 28;
    static constexpr int ICON_SIZE = 20;
    static constexpr int PADDING = 6;
};

#endif // LAYERITEMWIDGET_H
