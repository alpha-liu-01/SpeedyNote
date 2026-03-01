#include "ThicknessPresetButton.h"

#include <cmath>
#include <QPainter>
#include <QMouseEvent>
#include <QPalette>
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>

// Android/iOS keyboard fix (BUG-A001)
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
#include <QGuiApplication>
#include <QInputMethod>
#include <QTimer>
#endif

// ============================================================================
// ThicknessEditDialog
// ============================================================================

ThicknessEditDialog::ThicknessEditDialog(qreal currentThickness,
                                         qreal minThickness,
                                         qreal maxThickness,
                                         QWidget* parent)
    : QDialog(parent)
    , m_minThickness(minThickness)
    , m_maxThickness(maxThickness)
{
    setWindowTitle(tr("Edit Thickness"));
    setModal(true);
    setFixedWidth(300);
    
    auto* layout = new QVBoxLayout(this);
    
    // Label
    auto* label = new QLabel(tr("Thickness (pt):"), this);
    layout->addWidget(label);
    
    // Slider + SpinBox row
    auto* controlLayout = new QHBoxLayout();
    
    // Slider (integer values, will be scaled)
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(static_cast<int>(minThickness * 10), 
                       static_cast<int>(maxThickness * 10));
    m_slider->setValue(static_cast<int>(currentThickness * 10));
    controlLayout->addWidget(m_slider, 1);
    
    // SpinBox
    m_spinBox = new QDoubleSpinBox(this);
    m_spinBox->setRange(minThickness, maxThickness);
    m_spinBox->setSingleStep(0.5);
    m_spinBox->setDecimals(1);
    m_spinBox->setValue(currentThickness);
    m_spinBox->setSuffix(" pt");
    controlLayout->addWidget(m_spinBox);
    
    layout->addLayout(controlLayout);
    
    // Connect slider and spinbox
    connect(m_slider, &QSlider::valueChanged, this, &ThicknessEditDialog::onSliderChanged);
    connect(m_spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), 
            this, &ThicknessEditDialog::onSpinBoxChanged);
    
    // Buttons
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);
}

qreal ThicknessEditDialog::thickness() const
{
    return m_spinBox->value();
}

void ThicknessEditDialog::onSliderChanged(int value)
{
    // Slider uses integer values * 10 for 0.1 precision
    qreal thickness = value / 10.0;
    
    // Block signals to prevent feedback loop
    m_spinBox->blockSignals(true);
    m_spinBox->setValue(thickness);
    m_spinBox->blockSignals(false);
}

void ThicknessEditDialog::onSpinBoxChanged(double value)
{
    // Block signals to prevent feedback loop
    m_slider->blockSignals(true);
    m_slider->setValue(static_cast<int>(value * 10));
    m_slider->blockSignals(false);
}

void ThicknessEditDialog::done(int result)
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // BUG-A001 Fix: Defer dialog close. See ControlPanelDialog.cpp for full explanation.
    static bool isDeferring = false;
    if (isDeferring) {
        QDialog::done(result);
        return;
    }
    
    if (QWidget* focused = QApplication::focusWidget()) {
        focused->clearFocus();
    }
    if (QGuiApplication::inputMethod()) {
        QGuiApplication::inputMethod()->hide();
        QGuiApplication::inputMethod()->commit();
    }
    
    isDeferring = true;
    int savedResult = result;
    QTimer::singleShot(150, this, [this, savedResult]() {
        isDeferring = false;
        QDialog::done(savedResult);
    });
    return;
#else
    QDialog::done(result);
#endif
}

// ============================================================================
// ThicknessPresetButton
// ============================================================================

ThicknessPresetButton::ThicknessPresetButton(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(BUTTON_SIZE, BUTTON_SIZE);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    
    // Set tooltip
    setToolTip(tr("Click to select, click again to edit"));
}

qreal ThicknessPresetButton::thickness() const
{
    return m_thickness;
}

void ThicknessPresetButton::setThickness(qreal thickness)
{
    if (!qFuzzyCompare(m_thickness, thickness)) {
        m_thickness = thickness;
        update();
        emit thicknessChanged(m_thickness);
    }
}

bool ThicknessPresetButton::isSelected() const
{
    return m_selected;
}

void ThicknessPresetButton::setSelected(bool selected)
{
    if (m_selected != selected) {
        m_selected = selected;
        update();
        emit selectedChanged(m_selected);
    }
}

QColor ThicknessPresetButton::lineColor() const
{
    return m_lineColor;
}

void ThicknessPresetButton::setLineColor(const QColor& color)
{
    if (m_lineColor != color) {
        m_lineColor = color;
        update();
    }
}

QSize ThicknessPresetButton::sizeHint() const
{
    return QSize(BUTTON_SIZE, BUTTON_SIZE);
}

QSize ThicknessPresetButton::minimumSizeHint() const
{
    return QSize(BUTTON_SIZE, BUTTON_SIZE);
}

void ThicknessPresetButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Background rounded rect (blends with parent ExpandableToolButton)
    const QColor bgColor = isDarkMode() ? Qt::black : Qt::white;
    painter.setPen(Qt::NoPen);
    painter.setBrush(bgColor);
    painter.drawRoundedRect(QRectF(rect()), BORDER_RADIUS, BORDER_RADIUS);

    // Border (only when selected)
    if (m_selected) {
        const qreal bw = BORDER_WIDTH_SELECTED / 2.0;
        QRectF outerRect = QRectF(rect()).adjusted(bw, bw, -bw, -bw);
        QPen borderPen(borderColor());
        borderPen.setWidthF(BORDER_WIDTH_SELECTED);
        painter.setPen(borderPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(outerRect, BORDER_RADIUS, BORDER_RADIUS);
    }

    // Centered dot preview (diameter represents thickness)
    const qreal dotDiameter = displayLineWidth();
    const QPointF center(BUTTON_SIZE / 2.0, BUTTON_SIZE / 2.0);

    painter.setPen(Qt::NoPen);
    painter.setBrush(adjustedLineColor());
    painter.drawEllipse(center, dotDiameter / 2.0, dotDiameter / 2.0);
}

void ThicknessPresetButton::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        update();
    }
    QWidget::mousePressEvent(event);
}

void ThicknessPresetButton::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_pressed) {
        m_pressed = false;
        update();
        
        // Check if release is within button bounds
        if (rect().contains(event->pos())) {
            // Capture selection state BEFORE clicked() might change it via signal handler
            bool wasSelected = m_selected;
            
            emit clicked();
            
            // If was already selected BEFORE this click, emit edit request
            // This ensures clicking an unselected button only selects it (no dialog)
            if (wasSelected) {
                emit editRequested();
            }
        }
    }
    QWidget::mouseReleaseEvent(event);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void ThicknessPresetButton::enterEvent(QEnterEvent* event)
#else
void ThicknessPresetButton::enterEvent(QEvent* event)
#endif
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void ThicknessPresetButton::leaveEvent(QEvent* event)
{
    m_hovered = false;
    m_pressed = false;  // Cancel press if mouse leaves
    update();
    QWidget::leaveEvent(event);
}

bool ThicknessPresetButton::isDarkMode() const
{
    // Detect dark mode by checking the window background luminance
    const QPalette& pal = QApplication::palette();
    const QColor windowColor = pal.color(QPalette::Window);
    
    // Calculate relative luminance (simplified)
    const qreal luminance = 0.299 * windowColor.redF() 
                          + 0.587 * windowColor.greenF() 
                          + 0.114 * windowColor.blueF();
    
    return luminance < 0.5;
}

QColor ThicknessPresetButton::borderColor() const
{
    if (m_selected) {
        return m_lineColor;
    } else {
        return Qt::transparent;
    }
}

QColor ThicknessPresetButton::adjustedLineColor() const
{
    QColor color = m_lineColor;
    
    if (m_pressed) {
        // Darken when pressed
        color = color.darker(120);
    } else if (m_hovered && !m_selected) {
        // Slightly brighten on hover (only if not selected)
        color = color.lighter(110);
    }
    
    return color;
}

qreal ThicknessPresetButton::displayLineWidth() const
{
    // Scale the thickness to fit visually within the button
    // Map thickness range (e.g., 0.5 - 50.0) to display range (1.0 - 12.0)
    // Using logarithmic scale for better visual representation
    
    const qreal minThickness = 0.5;
    const qreal maxThickness = 50.0;
    
    // Clamp thickness to valid range
    qreal t = qBound(minThickness, m_thickness, maxThickness);
    
    // Logarithmic mapping for better visual distribution
    // log(t/min) / log(max/min) gives 0..1
    qreal normalized = std::log(t / minThickness) / std::log(maxThickness / minThickness);
    
    // Map to display range
    return MIN_DISPLAY_WIDTH + normalized * (MAX_DISPLAY_WIDTH - MIN_DISPLAY_WIDTH);
}

