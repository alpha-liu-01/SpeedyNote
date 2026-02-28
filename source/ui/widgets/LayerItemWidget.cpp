#include "LayerItemWidget.h"

#include <QPainter>
#include <QMouseEvent>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QPalette>
#include <QApplication>

// ============================================================================
// Constructor
// ============================================================================

LayerItemWidget::LayerItemWidget(int layerIndex, QWidget* parent)
    : QWidget(parent)
    , m_layerIndex(layerIndex)
    , m_name(QString("Layer %1").arg(layerIndex + 1))
{
    setFixedHeight(ITEM_HEIGHT);
    setMinimumWidth(150);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    
    // Load icons (assume visible.png exists, user will provide)
    updateVisibilityIcon();
    
    // Create inline edit widget (hidden by default)
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->hide();
    m_nameEdit->setFrame(false);
    connect(m_nameEdit, &QLineEdit::editingFinished, 
            this, &LayerItemWidget::onEditingFinished);
}

// ============================================================================
// Public Methods
// ============================================================================

void LayerItemWidget::setLayerName(const QString& name)
{
    if (m_name != name) {
        m_name = name;
        update();
    }
}

QString LayerItemWidget::layerName() const
{
    return m_name;
}

void LayerItemWidget::setLayerVisible(bool visible)
{
    if (m_visible != visible) {
        m_visible = visible;
        update();
    }
}

void LayerItemWidget::setSelected(bool selected)
{
    if (m_selected != selected) {
        m_selected = selected;
        update();
        emit selectionToggled(m_layerIndex, m_selected);
    }
}

void LayerItemWidget::setActive(bool active)
{
    if (m_active != active) {
        m_active = active;
        update();
    }
}

void LayerItemWidget::setDarkMode(bool dark)
{
    if (m_darkMode != dark) {
        m_darkMode = dark;
        updateVisibilityIcon();
        update();
    }
}

void LayerItemWidget::startEditing()
{
    if (m_editing) return;
    
    m_editing = true;
    
    // Position the edit widget over the name area
    QRect nameRect = nameAreaRect();
    m_nameEdit->setGeometry(nameRect);
    
    // Set font to match display font (size 11, bold if active)
    QFont editFont = m_nameEdit->font();
    editFont.setPointSize(11);
    editFont.setBold(m_active);
    m_nameEdit->setFont(editFont);
    
    m_nameEdit->setText(m_name);
    m_nameEdit->selectAll();
    m_nameEdit->show();
    m_nameEdit->setFocus();
}

QSize LayerItemWidget::sizeHint() const
{
    return QSize(200, ITEM_HEIGHT);
}

QSize LayerItemWidget::minimumSizeHint() const
{
    return QSize(150, ITEM_HEIGHT);
}

// ============================================================================
// Paint Event
// ============================================================================

void LayerItemWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    
    // Draw background
    QColor bgColor = backgroundColor();
    if (m_pressed && m_pressedArea == 2) {
        bgColor = bgColor.darker(110);
    } else if (m_hovered && !m_active) {
        bgColor = bgColor.lighter(105);
    }
    
    painter.setPen(Qt::NoPen);
    painter.setBrush(bgColor);
    painter.drawRoundedRect(rect(), 6, 6);
    
    // Draw visibility button
    QRect visRect = visibilityButtonRect();
    {
        QColor visBg = m_darkMode ? QColor(60, 60, 60) : QColor(220, 220, 220);
        if (m_pressed && m_pressedArea == 0) {
            visBg = visBg.darker(120);
        } else if (m_hovered && visRect.contains(mapFromGlobal(QCursor::pos()))) {
            visBg = visBg.lighter(110);
        }
        
        painter.setBrush(visBg);
        painter.drawEllipse(visRect);
        
        // Draw eye icon
        QIcon& icon = m_visible ? m_visibleIcon : m_notVisibleIcon;
        if (!icon.isNull()) {
            int iconX = visRect.x() + (visRect.width() - ICON_SIZE) / 2;
            int iconY = visRect.y() + (visRect.height() - ICON_SIZE) / 2;
            icon.paint(&painter, iconX, iconY, ICON_SIZE, ICON_SIZE);
        }
    }
    
    // Draw selection toggle (checkbox style)
    QRect selRect = selectionToggleRect();
    {
        QColor selBg = m_selected 
            ? (m_darkMode ? QColor(70, 130, 180) : QColor(100, 149, 237))
            : (m_darkMode ? QColor(50, 50, 50) : QColor(200, 200, 200));
        
        if (m_pressed && m_pressedArea == 1) {
            selBg = selBg.darker(120);
        }
        
        painter.setBrush(selBg);
        painter.setPen(m_darkMode ? QColor(80, 80, 80) : QColor(180, 180, 180));
        painter.drawRoundedRect(selRect, 4, 4);
        
        // Draw checkmark if selected
        if (m_selected) {
            painter.setPen(QPen(Qt::white, 2));
            int cx = selRect.center().x();
            int cy = selRect.center().y();
            painter.drawLine(cx - 5, cy, cx - 2, cy + 4);
            painter.drawLine(cx - 2, cy + 4, cx + 5, cy - 3);
        }
    }
    
    // Draw layer name (skip if editing)
    if (!m_editing) {
        QRect nameRect = nameAreaRect();
        
        QColor textColor = m_darkMode ? QColor(230, 230, 230) : QColor(30, 30, 30);
        if (!m_visible) {
            textColor.setAlpha(128);  // Dim text for hidden layers
        }
        
        painter.setPen(textColor);
        QFont font = painter.font();
        font.setPointSize(11);
        if (m_active) {
            font.setBold(true);
        }
        painter.setFont(font);
        
        painter.drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft, m_name);
    }
}

// ============================================================================
// Mouse Events
// ============================================================================

void LayerItemWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        
        // Determine which area was pressed
        QPoint pos = event->pos();
        if (visibilityButtonRect().contains(pos)) {
            m_pressedArea = 0;
        } else if (selectionToggleRect().contains(pos)) {
            m_pressedArea = 1;
        } else {
            m_pressedArea = 2;  // Name area
        }
        
        update();
    }
    QWidget::mousePressEvent(event);
}

void LayerItemWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_pressed) {
        m_pressed = false;
        
        QPoint pos = event->pos();
        
        // Check which area was released in
        if (m_pressedArea == 0 && visibilityButtonRect().contains(pos)) {
            // Visibility toggle
            onVisibilityClicked();
        } else if (m_pressedArea == 1 && selectionToggleRect().contains(pos)) {
            // Selection toggle
            onSelectionClicked();
        } else if (m_pressedArea == 2 && nameAreaRect().contains(pos)) {
            // Name area - select as active layer
            emit clicked(m_layerIndex);
        }
        
        m_pressedArea = -1;
        update();
    }
    QWidget::mouseReleaseEvent(event);
}

void LayerItemWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        QPoint pos = event->pos();
        
        // Double-click on name area starts editing
        if (nameAreaRect().contains(pos)) {
            emit editRequested(m_layerIndex);
            startEditing();
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void LayerItemWidget::enterEvent(QEnterEvent* event)
#else
void LayerItemWidget::enterEvent(QEvent* event)
#endif
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void LayerItemWidget::leaveEvent(QEvent* event)
{
    m_hovered = false;
    m_pressed = false;
    m_pressedArea = -1;
    update();
    QWidget::leaveEvent(event);
}

// ============================================================================
// Slots
// ============================================================================

void LayerItemWidget::onVisibilityClicked()
{
    m_visible = !m_visible;
    update();
    emit visibilityToggled(m_layerIndex, m_visible);
}

void LayerItemWidget::onSelectionClicked()
{
    m_selected = !m_selected;
    update();
    emit selectionToggled(m_layerIndex, m_selected);
}

void LayerItemWidget::onEditingFinished()
{
    if (!m_editing) return;
    
    QString newName = m_nameEdit->text().trimmed();
    
    // Don't allow empty names
    if (newName.isEmpty()) {
        newName = QString("Layer %1").arg(m_layerIndex + 1);
    }
    
    m_nameEdit->hide();
    m_editing = false;
    
    if (newName != m_name) {
        m_name = newName;
        emit nameChanged(m_layerIndex, m_name);
    }
    
    update();
}

// ============================================================================
// Private Helpers
// ============================================================================

void LayerItemWidget::updateVisibilityIcon()
{
    QString visiblePath = m_darkMode
        ? ":/resources/icons/visible_reversed.png"
        : ":/resources/icons/visible.png";
    QString notVisiblePath = m_darkMode
        ? ":/resources/icons/notvisible_reversed.png"
        : ":/resources/icons/notvisible.png";
    
    m_visibleIcon = QIcon(visiblePath);
    m_notVisibleIcon = QIcon(notVisiblePath);
}

QColor LayerItemWidget::backgroundColor() const
{
    if (m_active) {
        // Phase L.4: Active layer - use consistent app highlight colors (desaturated)
        if (isDarkMode()) {
            return QColor(45, 70, 100);  // Desaturated steel blue for dark mode
        } else {
            return QColor(210, 230, 250);  // Desaturated cornflower blue for light mode
        }
    } else {
        // Normal layer - unified gray colors: dark #2a2e32, light #F5F5F5
        if (isDarkMode()) {
            return QColor(0x2a, 0x2e, 0x32);  // Unified dark primary #2a2e32
        } else {
            return QColor(0xF5, 0xF5, 0xF5);  // Unified light primary #F5F5F5
        }
    }
}

bool LayerItemWidget::isDarkMode() const
{
    // Return the explicitly set dark mode value
    // (setDarkMode is always called by LayerPanel, so m_darkMode is reliable)
    return m_darkMode;
}

QRect LayerItemWidget::visibilityButtonRect() const
{
    int y = (ITEM_HEIGHT - BUTTON_SIZE) / 2;
    return QRect(PADDING, y, BUTTON_SIZE, BUTTON_SIZE);
}

QRect LayerItemWidget::selectionToggleRect() const
{
    // Phase L.4: Moved to the tail (right end) of the entry
    int x = width() - PADDING - TOGGLE_SIZE;
    int y = (ITEM_HEIGHT - TOGGLE_SIZE) / 2;
    return QRect(x, y, TOGGLE_SIZE, TOGGLE_SIZE);
}

QRect LayerItemWidget::nameAreaRect() const
{
    // Name area is now between visibility button and selection toggle
    int x = PADDING + BUTTON_SIZE + PADDING;
    int y = 0;
    int w = width() - x - PADDING - TOGGLE_SIZE - PADDING;
    return QRect(x, y, w, ITEM_HEIGHT);
}
