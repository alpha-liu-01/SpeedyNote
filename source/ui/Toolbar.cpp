#include "Toolbar.h"
#include "../compat/qt_compat.h"
#include "launcher/KineticScrollHelper.h"
#include "widgets/ExpandableToolButton.h"
#include "subtoolbars/PenSubToolbar.h"
#include "subtoolbars/MarkerSubToolbar.h"
#include "subtoolbars/EraserSubToolbar.h"
#include "subtoolbars/HighlighterSubToolbar.h"
#include "subtoolbars/OcrSubToolbar.h"

#include <QHBoxLayout>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPalette>
#include <QPainter>
#include <QResizeEvent>
#include <QtMath>

Toolbar::Toolbar(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    connectSignals();
    updateTheme(false);
}

void Toolbar::setupUi()
{
    setFixedHeight(TOOLBAR_HEIGHT);

    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(2);

    // Outside the stretches so each pager pins to its edge instead of drifting
    // with the centred button group. Back sits on the left, where a reader
    // expects to go back from.
    m_pagerBackButton = new ActionButton(this);
    m_pagerBackButton->setThemedIcon("left_arrow");
    m_pagerBackButton->setToolTip(tr("Back to drawing tools"));
    m_pagerBackButton->hide();
    mainLayout->addWidget(m_pagerBackButton);
    connect(m_pagerBackButton, &QPushButton::clicked,
            this, [this]() { setToolbarPage(0); });

    mainLayout->addStretch(1);

    m_toolGroup = new QButtonGroup(this);
    m_toolGroup->setExclusive(true);

    // --- Pen ---
    m_penSubToolbar = new PenSubToolbar();
    m_penExpandable = new ExpandableToolButton(this);
    m_penExpandable->setThemedIcon("pen");
    m_penExpandable->toolButton()->setToolTip(tr("Pen Tool (B)"));
    m_penExpandable->setContentWidget(m_penSubToolbar);
    m_penExpandable->toolButton()->setChecked(true);
    m_penExpandable->setExpanded(true);
    m_toolGroup->addButton(m_penExpandable->toolButton());
    mainLayout->addWidget(m_penExpandable);

    // --- Marker ---
    m_markerSubToolbar = new MarkerSubToolbar();
    m_markerExpandable = new ExpandableToolButton(this);
    m_markerExpandable->setThemedIcon("marker");
    m_markerExpandable->toolButton()->setToolTip(tr("Marker Tool (M)"));
    m_markerExpandable->setContentWidget(m_markerSubToolbar);
    m_toolGroup->addButton(m_markerExpandable->toolButton());
    mainLayout->addWidget(m_markerExpandable);

    // --- Eraser ---
    m_eraserSubToolbar = new EraserSubToolbar();
    m_eraserExpandable = new ExpandableToolButton(this);
    m_eraserExpandable->setThemedIcon("eraser");
    m_eraserExpandable->toolButton()->setToolTip(tr("Eraser Tool (E)"));
    m_eraserExpandable->setContentWidget(m_eraserSubToolbar);
    m_toolGroup->addButton(m_eraserExpandable->toolButton());
    mainLayout->addWidget(m_eraserExpandable);

    // --- Straight Line Toggle ---
    m_straightLineButton = new ToggleButton(this);
    m_straightLineButton->setThemedIcon("straightLine");
    m_straightLineButton->setToolTip(tr("Straight Line Mode (/)"));
    mainLayout->addWidget(m_straightLineButton);

    // --- Lasso (no subtoolbar) ---
    m_lassoButton = new ToolButton(this);
    m_lassoButton->setThemedIcon("rope");
    m_lassoButton->setToolTip(tr("Lasso Selection Tool (L)"));
    m_toolGroup->addButton(m_lassoButton);
    mainLayout->addWidget(m_lassoButton);

    // --- Object tools (one internal ObjectSelect tool, three insert modes) ---
    // The Link tool's controls live in a floating, viewport-owned LinkObjectBar
    // anchored to the selected LinkObject, so this is a plain toggle like the
    // other two insert modes.
    m_objectImageButton = new ToolButton(this);
    m_objectImageButton->setThemedIcon("objectinsert");
    m_objectImageButton->setToolTip(tr("Image Object Tool (I)"));
    m_toolGroup->addButton(m_objectImageButton);
    mainLayout->addWidget(m_objectImageButton);

    m_objectLinkButton = new ToolButton(this);
    m_objectLinkButton->setThemedIcon("linkicon");
    m_objectLinkButton->setToolTip(tr("Link Object Tool (Ctrl+.)"));
    m_toolGroup->addButton(m_objectLinkButton);
    mainLayout->addWidget(m_objectLinkButton);

    m_objectTextButton = new ToolButton(this);
    m_objectTextButton->setThemedIcon("auto");
    m_objectTextButton->setToolTip(tr("Text Object Tool (Ctrl+T)"));
    m_toolGroup->addButton(m_objectTextButton);
    mainLayout->addWidget(m_objectTextButton);

    // --- Highlighter ---
    m_highlighterSubToolbar = new HighlighterSubToolbar();
    m_textExpandable = new ExpandableToolButton(this);
    m_textExpandable->setThemedIcon("text");
    m_textExpandable->toolButton()->setToolTip(tr("Text Highlighter Tool (T)"));
    m_textExpandable->setContentWidget(m_highlighterSubToolbar);
    m_toolGroup->addButton(m_textExpandable->toolButton());
    mainLayout->addWidget(m_textExpandable);

    m_page1Widgets = {
        m_penExpandable, m_markerExpandable, m_eraserExpandable,
        m_straightLineButton, m_lassoButton, m_objectImageButton,
        m_objectLinkButton, m_objectTextButton, m_textExpandable
    };

    // --- OCR (not in tool group, hover-to-expand) ---
    m_ocrSubToolbar = new OcrSubToolbar();
    m_ocrExpandable = new ExpandableToolButton(this);
    m_ocrExpandable->setThemedIcon("ocr");
    m_ocrExpandable->toolButton()->setToolTip(tr("OCR - Text Recognition"));
    m_ocrExpandable->setContentWidget(m_ocrSubToolbar);
    m_ocrExpandable->setHoverExpand(true);
    connect(m_ocrExpandable, &ExpandableToolButton::expandedChanged,
            this, &Toolbar::onOcrExpanded);
    mainLayout->addWidget(m_ocrExpandable);

    // --- Pan (no subtoolbar) ---
    m_panButton = new ToolButton(this);
    m_panButton->setThemedIcon("move");
    m_panButton->setToolTip(tr("Pan Tool (H)"));
    m_toolGroup->addButton(m_panButton);
    mainLayout->addWidget(m_panButton);

    // Real widgets rather than addSpacing(): a QSpacerItem cannot be hidden,
    // so the gaps would survive on page 1 as a hole where this group was.
    QWidget* undoGap = createGapWidget(16);
    mainLayout->addWidget(undoGap);

    // --- Undo / Redo ---
    m_undoButton = new ActionButton(this);
    m_undoButton->setThemedIcon("undo");
    m_undoButton->setToolTip(tr("Undo (Ctrl+Z)"));
    mainLayout->addWidget(m_undoButton);

    m_redoButton = new ActionButton(this);
    m_redoButton->setThemedIcon("redo");
    m_redoButton->setToolTip(tr("Redo (Ctrl+Shift+Z / Ctrl+Y)"));
    mainLayout->addWidget(m_redoButton);

    QWidget* touchGap = createGapWidget(8);
    mainLayout->addWidget(touchGap);

    // --- Touch gesture mode ---
    m_touchGestureButton = new ThreeStateButton(this);
    m_touchGestureButton->setThemedIcon("hand");
    m_touchGestureButton->setToolTip(tr("Touch Gesture Mode\n0: Off\n1: Y-axis scroll only\n2: Full gestures"));
    mainLayout->addWidget(m_touchGestureButton);

    m_page2Widgets = {
        m_ocrExpandable, m_panButton, undoGap,
        m_undoButton, m_redoButton, touchGap, m_touchGestureButton
    };

    mainLayout->addStretch(1);

    m_pagerNextButton = new ActionButton(this);
    m_pagerNextButton->setThemedIcon("right_arrow");
    m_pagerNextButton->setToolTip(tr("More tools"));
    m_pagerNextButton->hide();
    mainLayout->addWidget(m_pagerNextButton);
    connect(m_pagerNextButton, &QPushButton::clicked,
            this, [this]() { setToolbarPage(1); });

    applyPageVisibility();

    installSwipeFilter(this);
}

QWidget* Toolbar::createGapWidget(int width)
{
    auto* gap = new QWidget(this);
    gap->setFixedWidth(width);
    return gap;
}

void Toolbar::connectSignals()
{
    connect(m_penExpandable->toolButton(), &QPushButton::clicked, this, [this]() {
        expandToolButton(ToolType::Pen);
        emit toolSelected(ToolType::Pen);
    });
    connect(m_markerExpandable->toolButton(), &QPushButton::clicked, this, [this]() {
        expandToolButton(ToolType::Marker);
        emit toolSelected(ToolType::Marker);
    });
    connect(m_eraserExpandable->toolButton(), &QPushButton::clicked, this, [this]() {
        expandToolButton(ToolType::Eraser);
        emit toolSelected(ToolType::Eraser);
    });
    connect(m_lassoButton, &QPushButton::clicked, this, [this]() {
        expandToolButton(ToolType::Lasso);
        emit toolSelected(ToolType::Lasso);
    });
    connect(m_objectImageButton, &QPushButton::clicked, this, [this]() {
        setObjectInsertMode(DocumentViewport::ObjectInsertMode::Image);
        expandToolButton(ToolType::ObjectSelect);
        emit objectInsertModeSelected(DocumentViewport::ObjectInsertMode::Image);
        emit toolSelected(ToolType::ObjectSelect);
    });
    connect(m_objectLinkButton, &QPushButton::clicked, this, [this]() {
        setObjectInsertMode(DocumentViewport::ObjectInsertMode::Link);
        expandToolButton(ToolType::ObjectSelect);
        emit objectInsertModeSelected(DocumentViewport::ObjectInsertMode::Link);
        emit toolSelected(ToolType::ObjectSelect);
    });
    connect(m_objectTextButton, &QPushButton::clicked, this, [this]() {
        setObjectInsertMode(DocumentViewport::ObjectInsertMode::Text);
        expandToolButton(ToolType::ObjectSelect);
        emit objectInsertModeSelected(DocumentViewport::ObjectInsertMode::Text);
        emit toolSelected(ToolType::ObjectSelect);
    });
    connect(m_textExpandable->toolButton(), &QPushButton::clicked, this, [this]() {
        expandToolButton(ToolType::Highlighter);
        emit toolSelected(ToolType::Highlighter);
    });
    connect(m_panButton, &QPushButton::clicked, this, [this]() {
        expandToolButton(ToolType::Pan);
        emit toolSelected(ToolType::Pan);
    });

    connect(m_straightLineButton, &ToggleButton::toggled,
            this, &Toolbar::straightLineToggled);

    connect(m_undoButton, &QPushButton::clicked,
            this, &Toolbar::undoClicked);
    connect(m_redoButton, &QPushButton::clicked,
            this, &Toolbar::redoClicked);

    connect(m_touchGestureButton, &ThreeStateButton::stateChanged,
            this, &Toolbar::touchGestureModeChanged);
}

void Toolbar::expandToolButton(ToolType tool)
{
    if (m_currentTool == tool)
        return;

    // Sync shared state when switching between Marker/Highlighter
    SubToolbar* newSub = nullptr;
    ExpandableToolButton* newExp = expandableForTool(tool);

    switch (tool) {
        case ToolType::Pen:       newSub = m_penSubToolbar; break;
        case ToolType::Marker:    newSub = m_markerSubToolbar; break;
        case ToolType::Eraser:    newSub = m_eraserSubToolbar; break;
        case ToolType::Highlighter: newSub = m_highlighterSubToolbar; break;
        default: break;
    }

    collapseAllToolButtons();

    if (newSub) {
        newSub->syncSharedState();
    }
    if (newExp) {
        newExp->setExpanded(true);
    }

    m_currentTool = tool;

    // Synchronously, not via the queued LayoutRequest: a subtoolbar that has
    // just expanded past the available width would otherwise be laid out once
    // while still unpaged, and the layout squeezes items below their minimum
    // to make an oversized row fit, leaving the strip a sliver wide.
    updateGeometry();
    updatePagination();
}

void Toolbar::collapseAllToolButtons()
{
    m_penExpandable->setExpanded(false);
    m_markerExpandable->setExpanded(false);
    m_eraserExpandable->setExpanded(false);
    m_textExpandable->setExpanded(false);
}

void Toolbar::onOcrExpanded(bool expanded)
{
    ExpandableToolButton* toolExp = expandableForTool(m_currentTool);
    if (!toolExp) return;

    if (expanded)
        toolExp->setExpanded(false);
    else
        toolExp->setExpanded(true);

    updateGeometry();
    updatePagination();
}

ExpandableToolButton* Toolbar::expandableForTool(ToolType tool) const
{
    switch (tool) {
        case ToolType::Pen:          return m_penExpandable;
        case ToolType::Marker:       return m_markerExpandable;
        case ToolType::Eraser:       return m_eraserExpandable;
        case ToolType::Highlighter:  return m_textExpandable;
        default: return nullptr;
    }
}

void Toolbar::setCurrentTool(ToolType tool)
{
    m_toolGroup->blockSignals(true);

    ExpandableToolButton* exp = expandableForTool(tool);
    QWidget* selected = nullptr;
    if (tool == ToolType::ObjectSelect) {
        switch (m_objectInsertMode) {
        case DocumentViewport::ObjectInsertMode::Image:
            m_objectImageButton->setChecked(true);
            selected = m_objectImageButton;
            break;
        case DocumentViewport::ObjectInsertMode::Link:
            m_objectLinkButton->setChecked(true);
            selected = m_objectLinkButton;
            break;
        case DocumentViewport::ObjectInsertMode::Text:
            m_objectTextButton->setChecked(true);
            selected = m_objectTextButton;
            break;
        }
    } else if (exp) {
        exp->toolButton()->setChecked(true);
        selected = exp;
    } else if (tool == ToolType::Lasso) {
        m_lassoButton->setChecked(true);
        selected = m_lassoButton;
    } else if (tool == ToolType::Pan) {
        m_panButton->setChecked(true);
        selected = m_panButton;
    }

    m_toolGroup->blockSignals(false);

    // A shortcut can select Pan while page 1 is showing, which would check a
    // button nobody can see and leave the toolbar looking unselected.
    revealWidget(selected);

    expandToolButton(tool);
}

void Toolbar::setObjectInsertMode(DocumentViewport::ObjectInsertMode mode)
{
    m_objectInsertMode = mode;
    if (m_currentTool != ToolType::ObjectSelect)
        return;

    m_toolGroup->blockSignals(true);
    QWidget* selected = nullptr;
    switch (mode) {
    case DocumentViewport::ObjectInsertMode::Image:
        m_objectImageButton->setChecked(true);
        selected = m_objectImageButton;
        break;
    case DocumentViewport::ObjectInsertMode::Link:
        m_objectLinkButton->setChecked(true);
        selected = m_objectLinkButton;
        break;
    case DocumentViewport::ObjectInsertMode::Text:
        m_objectTextButton->setChecked(true);
        selected = m_objectTextButton;
        break;
    }
    m_toolGroup->blockSignals(false);

    revealWidget(selected);

    collapseAllToolButtons();

    updateGeometry();
    updatePagination();
}

void Toolbar::setTouchGestureMode(int mode)
{
    m_touchGestureButton->setState(mode);
}

void Toolbar::updateTheme(bool darkMode)
{
    m_darkMode = darkMode;

    QPalette sysPalette = QGuiApplication::palette();
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, sysPalette.color(QPalette::Window));
    setPalette(pal);

    m_borderColor = darkMode ? QColor(0x4d, 0x4d, 0x4d) : QColor(0xD0, 0xD0, 0xD0);

    ButtonStyles::applyToWidget(this, darkMode);

    // Update expandable tool buttons
    m_penExpandable->setDarkMode(darkMode);
    m_markerExpandable->setDarkMode(darkMode);
    m_eraserExpandable->setDarkMode(darkMode);
    m_textExpandable->setDarkMode(darkMode);
    m_ocrExpandable->setDarkMode(darkMode);

    // Update subtoolbars
    m_penSubToolbar->setDarkMode(darkMode);
    m_markerSubToolbar->setDarkMode(darkMode);
    m_eraserSubToolbar->setDarkMode(darkMode);
    m_highlighterSubToolbar->setDarkMode(darkMode);
    m_ocrSubToolbar->setDarkMode(darkMode);

    // Update plain buttons
    m_straightLineButton->setDarkMode(darkMode);
    m_lassoButton->setDarkMode(darkMode);
    m_objectImageButton->setDarkMode(darkMode);
    m_objectLinkButton->setDarkMode(darkMode);
    m_objectTextButton->setDarkMode(darkMode);
    m_panButton->setDarkMode(darkMode);
    m_undoButton->setDarkMode(darkMode);
    m_redoButton->setDarkMode(darkMode);
    m_touchGestureButton->setDarkMode(darkMode);
    m_pagerBackButton->setDarkMode(darkMode);
    m_pagerNextButton->setDarkMode(darkMode);

    update();
}

int Toolbar::groupWidth(const QVector<QWidget*>& widgets) const
{
    auto* mainLayout = qobject_cast<QHBoxLayout*>(layout());
    if (!mainLayout)
        return 0;

    int total = 0;
    int count = 0;
    for (QWidget* widget : widgets) {
        if (!widget)
            continue;
        total += widget->sizeHint().width();
        ++count;
    }
    if (count > 1)
        total += mainLayout->spacing() * (count - 1);
    return total;
}

int Toolbar::naturalContentWidth() const
{
    auto* mainLayout = qobject_cast<QHBoxLayout*>(layout());
    if (!mainLayout)
        return 0;

    const QMargins margins = mainLayout->contentsMargins();
    return margins.left() + margins.right()
        + groupWidth(m_page1Widgets)
        + mainLayout->spacing()
        + groupWidth(m_page2Widgets);
}

QSize Toolbar::minimumSizeHint() const
{
    auto* mainLayout = qobject_cast<QHBoxLayout*>(layout());
    if (!mainLayout)
        return QWidget::minimumSizeHint();

    // The inherited hint is the whole row, which pins the window wide enough
    // that it can never shrink far enough to trigger paging in the first
    // place. What the toolbar actually needs is the wider single page plus one
    // pager, since only one page and one pager are ever on screen at once.
    const QMargins margins = mainLayout->contentsMargins();
    int widest = qMax(groupWidth(m_page1Widgets), groupWidth(m_page2Widgets));
    int pager = 0;
    if (m_pagerNextButton)
        pager = m_pagerNextButton->sizeHint().width() + mainLayout->spacing();

    return QSize(margins.left() + margins.right() + widest + pager,
                 TOOLBAR_HEIGHT);
}

void Toolbar::updatePagination()
{
    if (m_updatingPagination)
        return;

    const int natural = naturalContentWidth();
    const int available = width();

    // The measurement covers both groups whatever is on screen, so hiding a
    // group cannot feed back into the decision. The exit band only keeps the
    // boundary from fluttering by a pixel during a drag-resize.
    bool paged = m_paged;
    if (!m_paged && natural > available)
        paged = true;
    else if (m_paged && natural + PAGING_HYSTERESIS <= available)
        paged = false;

    if (paged == m_paged)
        return;

    m_updatingPagination = true;
    m_paged = paged;
    if (!m_paged)
        m_currentPage = 0;
    applyPageVisibility();
    m_updatingPagination = false;
    updateGeometry();
}

void Toolbar::applyPageVisibility()
{
    for (QWidget* widget : m_page1Widgets) {
        if (widget)
            widget->setVisible(!m_paged || m_currentPage == 0);
    }
    for (QWidget* widget : m_page2Widgets) {
        if (widget)
            widget->setVisible(!m_paged || m_currentPage == 1);
    }

    if (m_pagerBackButton)
        m_pagerBackButton->setVisible(m_paged && m_currentPage == 1);
    if (m_pagerNextButton)
        m_pagerNextButton->setVisible(m_paged && m_currentPage == 0);
}

void Toolbar::setToolbarPage(int page)
{
    if (!m_paged || page == m_currentPage)
        return;

    m_updatingPagination = true;
    m_currentPage = page;
    applyPageVisibility();
    m_updatingPagination = false;
}

void Toolbar::revealWidget(QWidget* widget)
{
    if (!m_paged || !widget)
        return;

    if (m_page2Widgets.contains(widget))
        setToolbarPage(1);
    else if (m_page1Widgets.contains(widget))
        setToolbarPage(0);
}

void Toolbar::installSwipeFilter(QWidget* root)
{
    if (!root)
        return;
    root->installEventFilter(this);
    for (QObject* child : root->children()) {
        if (auto* childWidget = qobject_cast<QWidget*>(child))
            installSwipeFilter(childWidget);
    }
}

bool Toolbar::eventFilter(QObject *watched, QEvent *event)
{
    // A swipe only means something when there is a second page to reach.
    if (!m_paged)
        return QWidget::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        // Touch arrives here as a synthesized mouse press, the same way the
        // launcher lists read it. Restricting to touch keeps a mouse user who
        // drags off a button from flipping the page by accident; they have the
        // pager button.
        if (mouseEvent->button() != Qt::LeftButton
            || !KineticScrollHelper::isTouchInput(mouseEvent)) {
            break;
        }
        m_swipeStart = mapFromGlobal(SN_MOUSE_GLOBAL_POS(mouseEvent));
        m_swipeTracking = true;
        m_swipeConsumed = false;
        break;
    }
    case QEvent::MouseMove: {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (!m_swipeTracking || m_swipeConsumed
            || !(mouseEvent->buttons() & Qt::LeftButton)) {
            break;
        }
        const QPoint delta =
            mapFromGlobal(SN_MOUSE_GLOBAL_POS(mouseEvent)) - m_swipeStart;
        if (qAbs(delta.x()) < SWIPE_THRESHOLD
            || qAbs(delta.x()) <= 2 * qAbs(delta.y())) {
            break;
        }

        m_swipeConsumed = true;
        setToolbarPage(delta.x() < 0 ? 1 : 0);
        // The press landed on a button, which would otherwise fire on release.
        if (auto* button = qobject_cast<QAbstractButton*>(watched))
            button->setDown(false);
        return true;
    }
    case QEvent::MouseButtonRelease: {
        const bool consumed = m_swipeConsumed;
        m_swipeTracking = false;
        m_swipeConsumed = false;
        if (consumed)
            return true;
        break;
    }
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

void Toolbar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updatePagination();
}

bool Toolbar::event(QEvent *event)
{
    // Fires whenever a child's size hint changes, which is how an expanding
    // subtoolbar reaches the width check without every expand path calling it.
    if (event->type() == QEvent::LayoutRequest)
        updatePagination();
    return QWidget::event(event);
}

void Toolbar::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    painter.setPen(QPen(m_borderColor, 1));
    painter.drawLine(0, height() - 1, width(), height() - 1);

    QColor innerShadow = m_darkMode ? QColor(0, 0, 0, 30) : QColor(0, 0, 0, 15);
    painter.setPen(QPen(innerShadow, 1));
    painter.drawLine(0, height() - 2, width(), height() - 2);
}

void Toolbar::setUndoEnabled(bool enabled)
{
    m_undoButton->setEnabled(enabled);
}

void Toolbar::setRedoEnabled(bool enabled)
{
    m_redoButton->setEnabled(enabled);
}

void Toolbar::setStraightLineMode(bool enabled)
{
    m_straightLineButton->blockSignals(true);
    m_straightLineButton->setChecked(enabled);
    m_straightLineButton->blockSignals(false);
}

void Toolbar::onTabChanged(int newTabId, int oldTabId)
{
    // Save state for old tab across all subtoolbars
    if (oldTabId >= 0) {
        m_penSubToolbar->saveTabState(oldTabId);
        m_markerSubToolbar->saveTabState(oldTabId);
        m_highlighterSubToolbar->saveTabState(oldTabId);
        m_eraserSubToolbar->saveTabState(oldTabId);
        m_ocrSubToolbar->saveTabState(oldTabId);
    }

    // Restore state for new tab across all subtoolbars
    if (newTabId >= 0) {
        m_penSubToolbar->restoreTabState(newTabId);
        m_markerSubToolbar->restoreTabState(newTabId);
        m_highlighterSubToolbar->restoreTabState(newTabId);
        m_eraserSubToolbar->restoreTabState(newTabId);
        m_ocrSubToolbar->restoreTabState(newTabId);
    }
}

void Toolbar::clearTabState(int tabId)
{
    m_penSubToolbar->clearTabState(tabId);
    m_markerSubToolbar->clearTabState(tabId);
    m_highlighterSubToolbar->clearTabState(tabId);
    m_eraserSubToolbar->clearTabState(tabId);
    m_ocrSubToolbar->clearTabState(tabId);
}

void Toolbar::setOcrAvailable(bool available)
{
    // Always keep the expandable trigger enabled so users can access cached
    // OCR text even on platforms without an OCR engine.
    m_ocrExpandable->toolButton()->setEnabled(true);
    m_ocrSubToolbar->setOcrAvailable(available);
    if (!available) {
        m_ocrExpandable->toolButton()->setToolTip(
            tr("OCR - View cached text (engine unavailable on this platform)"));
    } else {
        m_ocrExpandable->toolButton()->setToolTip(tr("OCR - Text Recognition"));
    }
}

void Toolbar::setOcrConfidenceSupported(bool supported)
{
    m_ocrSubToolbar->setConfidenceSupported(supported);
}
