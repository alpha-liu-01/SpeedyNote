#include "DebugOverlay.h"
#include "../core/DocumentViewport.h"
#include "../core/Document.h"
#include <QPainter>
#include <QMouseEvent>
#include <QFontMetrics>

// ============================================================================
// Constructor & Destructor
// ============================================================================

DebugOverlay::DebugOverlay(QWidget* parent)
    : QWidget(parent)
{
    // Make overlay transparent to mouse events except where we draw
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_TranslucentBackground);
    
    // Start hidden by default - MainWindow will show if debug mode enabled
    setVisible(false);
    
    // Set up update timer (30 FPS default)
    m_updateTimer.setInterval(33);
    connect(&m_updateTimer, &QTimer::timeout, this, &DebugOverlay::updateInfo);
    
    // Set up font
    m_font.setFamily("Consolas");  // Monospace for alignment
    m_font.setPointSize(10);
    
    // Initial size (will auto-resize based on content)
    setMinimumSize(200, 80);
    resize(350, 150);
}

DebugOverlay::~DebugOverlay() = default;

// ============================================================================
// Viewport Connection
// ============================================================================

void DebugOverlay::setViewport(DocumentViewport* viewport)
{
    m_viewport = viewport;
    
    if (isVisible()) {
        updateInfo();  // Refresh immediately when viewport changes
    }
}

// ============================================================================
// Toggle & Visibility
// ============================================================================

void DebugOverlay::toggle()
{
    if (isVisible()) {
        hide();
    } else {
        show();
    }
}

void DebugOverlay::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    m_updateTimer.start();
    updateInfo();  // Immediate update
    emit shown();
}

void DebugOverlay::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    m_updateTimer.stop();
    emit hidden();
}

// ============================================================================
// Extensibility
// ============================================================================

void DebugOverlay::addSection(const QString& name, std::function<QString()> generator)
{
    // Check if section already exists
    for (auto& section : m_customSections) {
        if (section.name == name) {
            section.generator = std::move(generator);
            return;
        }
    }
    
    m_customSections.push_back({name, std::move(generator), true});
}

void DebugOverlay::removeSection(const QString& name)
{
    m_customSections.erase(
        std::remove_if(m_customSections.begin(), m_customSections.end(),
            [&name](const DebugSection& s) { return s.name == name; }),
        m_customSections.end()
    );
}

void DebugOverlay::setSectionEnabled(const QString& name, bool enabled)
{
    for (auto& section : m_customSections) {
        if (section.name == name) {
            section.enabled = enabled;
            return;
        }
    }
}

void DebugOverlay::clearCustomSections()
{
    m_customSections.clear();
}

// ============================================================================
// Configuration
// ============================================================================

void DebugOverlay::setUpdateInterval(int ms)
{
    m_updateTimer.setInterval(ms);
}

void DebugOverlay::setBackgroundOpacity(int alpha)
{
    m_backgroundOpacity = qBound(0, alpha, 255);
    update();
}

// ============================================================================
// Update & Rendering
// ============================================================================

void DebugOverlay::updateInfo()
{
    if (!m_viewport) {
        m_cachedText = "No viewport connected";
        update();
        return;
    }
    
    Document* doc = m_viewport->document();
    if (!doc) {
        m_cachedText = "No document loaded";
        update();
        return;
    }
    
    // Generate text based on document mode
    if (doc->isEdgeless()) {
        m_cachedText = generateEdgelessInfo();
    } else {
        m_cachedText = generatePagedInfo();
    }
    
    // Append custom sections
    QString customText = generateCustomSections();
    if (!customText.isEmpty()) {
        m_cachedText += "\n" + customText;
    }
    
    // Auto-resize based on content
    QFontMetrics fm(m_font);
    QRect textRect = fm.boundingRect(
        QRect(0, 0, 500, 500),
        Qt::AlignLeft | Qt::TextWordWrap,
        m_cachedText
    );
    
    int newWidth = textRect.width() + 20;   // 10px padding each side
    int newHeight = textRect.height() + 20;
    
    if (newWidth != width() || newHeight != height()) {
        resize(newWidth, newHeight);
    }
    
    update();
}

QString DebugOverlay::generateEdgelessInfo() const
{
    if (!m_viewport || !m_viewport->document()) {
        return QString();
    }
    
    Document* doc = m_viewport->document();
    
    return QString(
        "Edgeless Canvas | Tiles: %1\n"
        "Zoom: %2% | Pan: (%3, %4)\n"
        "Tool: %5%6 | Undo:%7 Redo:%8\n"
        "Paint Rate: %9"
    )
    .arg(doc->tileCount())
    .arg(m_viewport->zoomLevel() * 100, 0, 'f', 0)
    .arg(m_viewport->panOffset().x(), 0, 'f', 1)
    .arg(m_viewport->panOffset().y(), 0, 'f', 1)
    .arg(toolName())
    .arg(m_viewport->isHardwareEraserActive() ? " (HW Eraser)" : "")
    .arg(m_viewport->canUndo() ? "Y" : "N")
    .arg(m_viewport->canRedo() ? "Y" : "N")
    .arg(m_viewport->isBenchmarking() 
         ? QString("%1 Hz").arg(m_viewport->getPaintRate()) 
         : "OFF");
}

QString DebugOverlay::generatePagedInfo() const
{
    if (!m_viewport || !m_viewport->document()) {
        return QString();
    }
    
    Document* doc = m_viewport->document();
    QSizeF contentSize = m_viewport->totalContentSize();
    
    return QString(
        "Document: %1 | Pages: %2 | Current: %3\n"
        "Zoom: %4% | Pan: (%5, %6)\n"
        "Layout: %7 | Content: %8x%9\n"
        "Tool: %10%11 | Undo:%12 Redo:%13\n"
        "Paint Rate: %14 [P=Pen, E=Eraser, B=Benchmark]"
    )
    .arg(doc->displayName())
    .arg(doc->pageCount())
    .arg(m_viewport->currentPageIndex() + 1)
    .arg(m_viewport->zoomLevel() * 100, 0, 'f', 0)
    .arg(m_viewport->panOffset().x(), 0, 'f', 1)
    .arg(m_viewport->panOffset().y(), 0, 'f', 1)
    .arg(m_viewport->layoutMode() == LayoutMode::SingleColumn ? "Single Column" : "Two Column")
    .arg(contentSize.width(), 0, 'f', 0)
    .arg(contentSize.height(), 0, 'f', 0)
    .arg(toolName())
    .arg(m_viewport->isHardwareEraserActive() ? " (HW Eraser)" : "")
    .arg(m_viewport->canUndo() ? "Y" : "N")
    .arg(m_viewport->canRedo() ? "Y" : "N")
    .arg(m_viewport->isBenchmarking() 
         ? QString("%1 Hz").arg(m_viewport->getPaintRate()) 
         : "OFF (press F10)");
}

QString DebugOverlay::generateCustomSections() const
{
    QString result;
    
    for (const auto& section : m_customSections) {
        if (section.enabled && section.generator) {
            QString text = section.generator();
            if (!text.isEmpty()) {
                if (!result.isEmpty()) {
                    result += "\n";
                }
                result += text;
            }
        }
    }
    
    return result;
}

QString DebugOverlay::toolName() const
{
    if (!m_viewport) return "N/A";
    
    switch (m_viewport->currentTool()) {
        case ToolType::Pen:         return "Pen";
        case ToolType::Marker:      return "Marker";
        case ToolType::Eraser:      return "Eraser";
        case ToolType::Highlighter: return "Highlighter";
        case ToolType::Lasso:       return "Lasso";
        case ToolType::ObjectSelect: {
            // Phase C.4: Show sub-modes for ObjectSelect tool
            QString insertMode = (m_viewport->objectInsertMode() == DocumentViewport::ObjectInsertMode::Image) 
                                 ? "Img" : "Link";
            QString actionMode = (m_viewport->objectActionMode() == DocumentViewport::ObjectActionMode::Create) 
                                 ? "Create" : "Select";
            return QString("Object[%1/%2]").arg(insertMode, actionMode);
        }
        default:                    return "Unknown";
    }
}

void DebugOverlay::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Draw semi-transparent background with rounded corners
    QColor bgColor(0, 0, 0, m_backgroundOpacity);
    painter.setPen(Qt::NoPen);
    painter.setBrush(bgColor);
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 6, 6);
    
    // Draw border
    painter.setPen(QColor(80, 80, 80));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 6, 6);
    
    // Draw text
    painter.setPen(Qt::white);
    painter.setFont(m_font);
    painter.drawText(rect().adjusted(10, 10, -10, -10), 
                     Qt::AlignTop | Qt::AlignLeft, 
                     m_cachedText);
}

// ============================================================================
// Drag Support
// ============================================================================

void DebugOverlay::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void DebugOverlay::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging) {
        QPoint newPos = mapToParent(event->pos() - m_dragOffset);
        
        // Keep within parent bounds
        if (parentWidget()) {
            QRect parentRect = parentWidget()->rect();
            newPos.setX(qBound(0, newPos.x(), parentRect.width() - width()));
            newPos.setY(qBound(0, newPos.y(), parentRect.height() - height()));
        }
        
        move(newPos);
    }
}

void DebugOverlay::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        setCursor(Qt::ArrowCursor);
    }
}
