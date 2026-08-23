#include "DebugOverlay.h"
#include "../core/DocumentViewport.h"
#include "../core/Document.h"
#include <QPainter>
#include <QMouseEvent>
#include <QFontMetrics>
#include <QStringList>

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
    m_updateTimer.setInterval(DefaultUpdateIntervalMs);
    connect(&m_updateTimer, &QTimer::timeout, this, &DebugOverlay::updateInfo);
    
    // Set up font. The style hint matters on Android, where Consolas does not
    // exist and a proportional fallback would break the column alignment the
    // perf readout relies on.
    m_font.setFamily("Consolas");  // Monospace for alignment
    m_font.setStyleHint(QFont::Monospace);
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
    applyPerfModeHygiene(m_viewport && m_viewport->isBenchmarking());
    
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
    
    samplePerfStats();
    
    // Generate text based on document mode
    if (doc->isEdgeless()) {
        m_cachedText = generateEdgelessInfo();
    } else {
        m_cachedText = generatePagedInfo();
    }
    
    m_cachedText += "\n" + generatePerfInfo();
    
    // Append custom sections
    QString customText = generateCustomSections();
    if (!customText.isEmpty()) {
        m_cachedText += "\n" + customText;
    }
    
    // Auto-resize based on content. Skipped while measuring: resizing this
    // overlay repaints the viewport region underneath it, which would inject
    // frames into the statistics being collected.
    if (!m_sizeFrozen) {
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
    }
    
    update();
}

// ============================================================================
// Performance HUD
// ============================================================================

void DebugOverlay::samplePerfStats()
{
    if (!m_viewport || !m_viewport->isBenchmarking()) {
        return;
    }
    
    using Bucket = ViewportPerfMonitor::Bucket;
    
    auto latch = [](HeldStats& held, const ViewportPerfMonitor::Stats& current) {
        if (current.isValid()) {
            held.stats = current;
            held.valid = true;
            held.since.restart();
        }
    };
    
    latch(m_heldPan, m_viewport->perfStats(Bucket::PanZoom));
    latch(m_heldComposite, m_viewport->perfStats(Bucket::Composite));
    latch(m_heldPartial, m_viewport->perfStats(Bucket::Partial));
}

void DebugOverlay::applyPerfModeHygiene(bool perfActive)
{
    if (perfActive == m_perfModeActive) {
        return;
    }
    m_perfModeActive = perfActive;
    
    if (perfActive) {
        // This overlay overlaps the viewport as a sibling widget. While it is
        // translucent, Qt has to repaint the viewport underneath on every
        // overlay update, so the overlay would be manufacturing the very
        // frames it is trying to measure. Going opaque removes that, and a
        // frozen size removes the resize-triggered repaints as well.
        setAttribute(Qt::WA_TranslucentBackground, false);
        setAttribute(Qt::WA_OpaquePaintEvent, true);
        m_updateTimer.setInterval(PerfUpdateIntervalMs);
        m_sizeFrozen = true;
        resize(perfModeSize());
        
        m_heldPan = HeldStats{};
        m_heldComposite = HeldStats{};
        m_heldPartial = HeldStats{};
    } else {
        setAttribute(Qt::WA_OpaquePaintEvent, false);
        setAttribute(Qt::WA_TranslucentBackground, true);
        m_updateTimer.setInterval(DefaultUpdateIntervalMs);
        m_sizeFrozen = false;
    }
    
    update();
}

QSize DebugOverlay::perfModeSize() const
{
    // Measured from a worst-case layout rather than the live text, so the
    // overlay can be sized once on entry and never resized while measuring.
    static const QString widest = QStringLiteral(
        "Document: WWWWWWWWWWWWWWWWWWWWWWWWWWWW | Pages: 9999 | Current: 9999\n"
        "Zoom: 9999% | Pan: (-99999.9, -99999.9)\n"
        "Layout: Single Column | Content: 999999x999999\n"
        "Tool: Object[Text/Create] (HW Eraser) | Undo:Y Redo:Y\n"
        "Pan:     9999.9 fps | paint 9999.9ms avg, 9999.9 p95, 9999.9 max (99s ago)\n"
        "Compose: 9999.9 fps | paint 9999.9ms avg, 9999.9 p95, 9999.9 max (99s ago)\n"
        "Partial: 9999.9 fps | paint 9999.9ms avg, 9999.9 p95, 9999.9 max (99s ago)\n"
        "Fill:    99999 Mpix/s | 99.99 MP/frame\n"
        "Blit:    store 999.99ms | heap 999.99ms | 999.99x | 99999 frames\n"
        "Verdict: PRESENT-BOUND - paint 9999.9 of 9999.9ms, residual 9999.9ms\n"
        "Surface: 99999x99999 phys @9.99dpr | vp 99999x99999 | 999.9Hz | tier Capped");
    
    QFontMetrics fm(m_font);
    const QRect r = fm.boundingRect(QRect(0, 0, 1200, 1200),
                                    Qt::AlignLeft | Qt::TextWordWrap,
                                    widest);
    QSize size(r.width() + 20, r.height() + 20);
    
    // Keep it on screen. A tablet in portrait is narrower than the worst-case
    // line, and an overlay hanging off the edge would hide the readout.
    if (const QWidget* parent = parentWidget()) {
        size.setWidth(qMin(size.width(), parent->width() - 20));
        size.setHeight(qMin(size.height(), parent->height() - 20));
    }
    return size;
}

QString DebugOverlay::generatePerfInfo() const
{
    if (!m_viewport) {
        return QString();
    }
    
    if (!m_viewport->isBenchmarking()) {
        return QStringLiteral("Perf: OFF (press F10)");
    }
    
    auto formatBucket = [](const char* label, const HeldStats& held) {
        if (!held.valid) {
            return QStringLiteral("%1 -").arg(QLatin1String(label), -8);
        }
        
        const ViewportPerfMonitor::Stats& s = held.stats;
        QString line = QStringLiteral("%1 %2 fps | paint %3ms avg, %4 p95, %5 max")
                           .arg(QLatin1String(label), -8)
                           .arg(s.fps, 0, 'f', 1)
                           .arg(s.meanMs, 0, 'f', 1)
                           .arg(s.p95Ms, 0, 'f', 1)
                           .arg(s.maxMs, 0, 'f', 1);
        
        // Anything older than the statistics window is a stale reading being
        // held for legibility, not a live measurement.
        const qint64 ageMs = held.since.isValid() ? held.since.elapsed() : 0;
        if (ageMs > ViewportPerfMonitor::WindowMs) {
            line += QStringLiteral(" (%1s ago)").arg(ageMs / 1000);
        }
        return line;
    };
    
    QStringList lines;
    lines << formatBucket("Pan:", m_heldPan);
    lines << formatBucket("Compose:", m_heldComposite);
    lines << formatBucket("Partial:", m_heldPartial);
    
    // The expensive full-viewport work is what we care about; prefer gesture
    // pan frames and fall back to full re-composites.
    const HeldStats& primary = m_heldPan.valid ? m_heldPan : m_heldComposite;
    if (primary.valid) {
        const ViewportPerfMonitor::Stats& s = primary.stats;
        lines << QStringLiteral("Fill:    %1 Mpix/s | %2 MP/frame")
                     .arg(s.mpixPerSec, 0, 'f', 0)
                     .arg(s.mpixPerFrame, 0, 'f', 2);
        
        // If paintEvent accounts for most of the frame period, rasterization
        // is the bottleneck. If it does not, the time is going somewhere
        // outside our paint code - backing store flush, event delivery, vsync.
        lines << QStringLiteral("Verdict: %1 - paint %2 of %3ms, residual %4ms")
                     .arg(s.isPaintBound() ? QStringLiteral("PAINT-BOUND")
                                           : QStringLiteral("PRESENT-BOUND"))
                     .arg(s.meanMs, 0, 'f', 1)
                     .arg(s.meanIntervalMs, 0, 'f', 1)
                     .arg(s.residualMs, 0, 'f', 1);
    } else {
        lines << QStringLiteral("Fill:    - (pan or scroll to collect samples)");
    }
    
    // DIAGNOSTIC (see DocumentViewport::BlitProbe): the same cached-frame blit
    // timed into the backing store and into heap memory. A large ratio means the
    // backing store's memory is what costs, not the rasterizer.
    const DocumentViewport::BlitProbe blit = m_viewport->blitProbe();
    if (blit.frames > 0) {
        lines << QStringLiteral("Blit:    store %1ms | heap %2ms | %3x | %4 frames")
                     .arg(blit.storeMs, 0, 'f', 2)
                     .arg(blit.heapMs, 0, 'f', 2)
                     .arg(blit.heapMs > 0.0 ? blit.storeMs / blit.heapMs : 0.0, 0, 'f', 2)
                     .arg(blit.frames);
    } else {
        lines << QStringLiteral("Blit:    - (touch-pan to collect samples)");
    }
    
    // Physical viewport pixels are what the rasterizer actually fills, and the
    // panel refresh rate is the ceiling any fps figure is measured against.
    const DocumentViewport::PerfContext ctx = m_viewport->perfContext();
    lines << QStringLiteral("Surface: %1x%2 phys @%3dpr | vp %4x%5 | %6Hz | tier %7")
                 .arg(ctx.viewportPhysical.width())
                 .arg(ctx.viewportPhysical.height())
                 .arg(ctx.devicePixelRatio, 0, 'f', 2)
                 .arg(ctx.viewportLogical.width())
                 .arg(ctx.viewportLogical.height())
                 .arg(ctx.screenRefreshRate, 0, 'f', 1)
                 .arg(ctx.strokeCacheTier);
    
    return lines.join(QLatin1Char('\n'));
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
        "Tool: %5%6 | Undo:%7 Redo:%8"
    )
    .arg(doc->tileCount())
    .arg(m_viewport->zoomLevel() * 100, 0, 'f', 0)
    .arg(m_viewport->panOffset().x(), 0, 'f', 1)
    .arg(m_viewport->panOffset().y(), 0, 'f', 1)
    .arg(toolName())
    .arg(m_viewport->isHardwareEraserActive() ? " (HW Eraser)" : "")
    .arg(m_viewport->canUndo() ? "Y" : "N")
    .arg(m_viewport->canRedo() ? "Y" : "N");
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
        "Tool: %10%11 | Undo:%12 Redo:%13"
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
    .arg(m_viewport->canRedo() ? "Y" : "N");
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
            QString insertMode;
            switch (m_viewport->objectInsertMode()) {
            case DocumentViewport::ObjectInsertMode::Image: insertMode = "Img"; break;
            case DocumentViewport::ObjectInsertMode::Link:  insertMode = "Link"; break;
            case DocumentViewport::ObjectInsertMode::Text:  insertMode = "Text"; break;
            }
            QString actionMode = (m_viewport->objectActionMode() == DocumentViewport::ObjectActionMode::Create) 
                                 ? "Create" : "Select";
            return QString("Object[%1/%2]").arg(insertMode, actionMode);
        }
        case ToolType::Pan:         return "Pan";
        default:                    return "Unknown";
    }
}

void DebugOverlay::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    
    if (m_perfModeActive) {
        // Fully opaque square fill. WA_OpaquePaintEvent is set while measuring,
        // so every pixel must be written - rounded corners would leave the
        // corner pixels undefined. The squared-off look is the price of not
        // forcing a viewport repaint on every overlay update.
        painter.fillRect(rect(), QColor(0, 0, 0));
        painter.setPen(QColor(80, 80, 80));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    } else {
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
    }
    
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
