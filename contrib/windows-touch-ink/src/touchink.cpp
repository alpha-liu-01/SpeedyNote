// SPDX-License-Identifier: GPL-3.0-or-later
// Optional Qt plugin for the unmodified SpeedyNote 1.6.3 executable.
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QElapsedTimer>
#include <QGenericPlugin>
#include <QHBoxLayout>
#include <QAbstractButton>
#include <QMouseEvent>
#include <QPointer>
#include <QPointingDevice>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QTabletEvent>
#include <QTimer>
#include <QToolButton>
#include <QTouchEvent>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <cstring>
#include "rounding.h"
#include "imageinsert.h"
#include "launcherimages.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

static bool isolateInstanceName()
{
    // v1.6.3 hardcodes this UTF-8 name for BOTH QSharedMemory and QLocalServer.
    // Change only the matching read-only string in our own process before main
    // performs its instance check. The executable on disk stays byte-identical.
    // Never write to executable sections or to another process.
    static constexpr char original[] = "SpeedyNote_SingleInstance";
    static constexpr char isolated[] = "SpeedyNote_TouchInk______";
    static_assert(sizeof(original) == sizeof(isolated));
    auto *base = reinterpret_cast<unsigned char *>(GetModuleHandleW(nullptr));
    if (!base) return false;
    auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    auto *section = IMAGE_FIRST_SECTION(nt);
    bool changed = false;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (!(section->Characteristics & IMAGE_SCN_MEM_READ) ||
            (section->Characteristics & (IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE))) continue;
        const auto size = section->Misc.VirtualSize;
        if (size < sizeof(original) || section->VirtualAddress + size > nt->OptionalHeader.SizeOfImage) continue;
        for (size_t offset = 0; offset + sizeof(original) <= size; ++offset) {
            auto *location = base + section->VirtualAddress + offset;
            if (std::memcmp(location, original, sizeof(original)) != 0) continue;
            DWORD previous = 0, ignored = 0;
            if (!VirtualProtect(location, sizeof(original), PAGE_READWRITE, &previous)) return false;
            std::memcpy(location, isolated, sizeof(isolated));
            const bool restored = VirtualProtect(location, sizeof(original), previous, &ignored) != 0;
            if (!restored) return false;
            changed = true;
        }
    }
    return changed;
}

static bool isolateSettings()
{
    // SpeedyNote explicitly uses native QSettings("SpeedyNote", "App");
    // setDefaultFormat(IniFormat) does not affect that constructor. Use Windows'
    // process-local registry redirection to keep its writes away from the original.
    // The override applies only inside this process and ends when it exits.
    HKEY profile = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\SpeedyNoteTouchInk\\Profile", 0,
            nullptr, 0, KEY_ALL_ACCESS, nullptr, &profile, nullptr) != ERROR_SUCCESS)
        return false;

    // Retain the real Windows theme and known-folder locations in the isolated
    // view; no original SpeedyNote settings or notebook library are copied.
    const wchar_t *paths[] = {
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders",
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders"
    };
    for (const wchar_t *path : paths) {
        HKEY source = nullptr, destination = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &source) == ERROR_SUCCESS) {
            if (RegCreateKeyExW(profile, path, 0, nullptr, 0, KEY_ALL_ACCESS,
                                nullptr, &destination, nullptr) == ERROR_SUCCESS) {
                RegCopyTreeW(source, nullptr, destination);
                RegCloseKey(destination);
            }
            RegCloseKey(source);
        }
    }
    if (RegOverridePredefKey(HKEY_CURRENT_USER, profile) != ERROR_SUCCESS) {
        RegCloseKey(profile);
        return false;
    }
    // Windows retains its own reference for this process-local mapping.
    RegCloseKey(profile);
    return true;
}

class TouchInkFilter final : public QObject
{
    Q_OBJECT
public:
    explicit TouchInkFilter(QObject *parent) : QObject(parent),
        m_pen(QStringLiteral("TouchInk simulated nib"), 0x54494e4b,
              QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
              QInputDevice::Capability::Position | QInputDevice::Capability::Pressure,
              1, 1)
    {
        QSettings settings(QStringLiteral("SpeedyNote"), QStringLiteral("App"));
        // The old adapter always inked regardless of the native gesture setting.
        // Keep that initial behavior when upgrading, then remember both toggles.
        if (!settings.contains(QStringLiteral("touchInk/modesVersion"))) {
            settings.setValue(QStringLiteral("touchGestureMode"), 0);
            settings.setValue(QStringLiteral("touchInk/modesVersion"), 2);
        }
        m_gestures = settings.value(QStringLiteral("touchGestureMode"), 0).toInt() != 0;
        m_brush = settings.value(QStringLiteral("touchInk/brushMode"), false).toBool();
        m_round = settings.value(QStringLiteral("touchInk/roundMode"), true).toBool();
        m_clock.start();
    }

private slots:
    void installPendingLauncher()
    {
        if (m_pendingLauncher && !m_pendingLauncher->property("touchInkLauncherImagesInstalled").toBool())
            new LauncherImageImportController(m_pendingLauncher);
        m_pendingLauncher.clear();
    }

    void installPendingControls()
    {
        if (m_pendingToolbar) installControls(m_pendingToolbar);
        m_pendingToolbar.clear();
    }

    void nativeGestureChanged(int mode)
    {
        if (m_drawing) {
            finishStroke(false);
            m_blocked = true;
        }
        m_gestures = mode != 0;
        updateButtons();
    }

protected:
    bool eventFilter(QObject *receiver, QEvent *event) override
    {
        if (receiver->inherits("Launcher") && event->type() == QEvent::Show) {
            m_pendingLauncher = qobject_cast<QWidget *>(receiver);
            QMetaObject::invokeMethod(this, "installPendingLauncher", Qt::QueuedConnection);
        }
        if (receiver->inherits("Toolbar") && event->type() == QEvent::Show) {
            m_pendingToolbar = qobject_cast<QWidget *>(receiver);
            QMetaObject::invokeMethod(this, "installPendingControls", Qt::QueuedConnection);
        }
        if (m_target && ((receiver == m_target &&
                (event->type() == QEvent::Hide || event->type() == QEvent::Close ||
                 event->type() == QEvent::WindowDeactivate)) ||
                event->type() == QEvent::ApplicationDeactivate)) {
            finishStroke(false);
            resetSequence();
        }

        const auto type = event->type();
        if (type != QEvent::TouchBegin && type != QEvent::TouchUpdate &&
            type != QEvent::TouchEnd && type != QEvent::TouchCancel)
            return false;

        // Only the notebook canvas changes behavior. Buttons, dialogs, text
        // editors, the launcher, and real mouse/stylus events keep their behavior.
        if (!receiver->inherits("DocumentViewport"))
            return false;
        auto *canvas = qobject_cast<QWidget *>(receiver);
        if (!canvas)
            return false;
        auto *touch = static_cast<QTouchEvent *>(event);
        if (touch->pointingDevice() &&
            touch->pointingDevice()->type() == QInputDevice::DeviceType::TouchPad)
            return false;

        // An owned sequence must finish here, even if its mode was toggled while
        // a contact was down. Fresh gesture sequences go to the native handler.
        if (m_gestures && !m_target)
            return false;

        if (type == QEvent::TouchBegin) {
            if (!touch->points().isEmpty() &&
                canvas->childAt(touch->points().first().position().toPoint()))
                return false;
            finishStroke(false);
            resetSequence();
            if (m_gestures)
                return false;
            m_target = canvas;
        } else if (m_target != canvas) {
            // A touch which began over a child widget still belongs to it.
            return false;
        }

        m_modifiers = touch->modifiers();
        m_timestamp = touch->timestamp();
        for (const auto &point : touch->points()) {
            if (point.state() == QEventPoint::State::Released)
                m_ids.remove(point.id());
            else
                m_ids.insert(point.id());
        }

        // A second contact ends inking and blocks this whole sequence. Do not
        // start another stroke when that contact lifts; wait for a fresh touch.
        if (m_ids.size() > 1) {
            finishStroke(false);
            m_blocked = true;
        }

        if (!m_blocked && type != QEvent::TouchCancel) {
            for (const auto &point : touch->points()) {
                if (!m_drawing && type == QEvent::TouchBegin &&
                    point.state() != QEventPoint::State::Released && m_ids.size() == 1) {
                    m_primaryId = point.id();
                    m_local = point.position();
                    m_global = point.globalPosition();
                    m_drawing = true;
                    // Pressure is useful only for Pen. Preserve immediate input
                    // for marker, eraser, lasso, text, and object manipulation.
                    m_brushStroke = m_brush && (!m_penTool || m_penTool->isChecked());
                    m_roundStroke = m_round && (!m_penTool || m_penTool->isChecked() ||
                                                (m_markerTool && m_markerTool->isChecked()));
                    canvas->setFocus(Qt::MouseFocusReason);
                    beginInk();
                } else if (m_drawing && point.id() == m_primaryId) {
                    const bool moved = point.position() != m_local;
                    const QPointF previous = m_local;
                    m_local = point.position();
                    m_global = point.globalPosition();
                    if (moved && m_brushStroke)
                        appendBrush(previous);
                    // The native release handler ignores the lift position.
                    // Include it in the rounded trajectory before flushing.
                    if (moved && !m_brushStroke && m_roundStroke &&
                        point.state() == QEventPoint::State::Released)
                        sendMouse(QEvent::MouseMove);
                    if (point.state() == QEventPoint::State::Released)
                        finishStroke(true);
                    else if (moved && !m_brushStroke)
                        sendMouse(QEvent::MouseMove);
                }
            }
        }

        if (type == QEvent::TouchEnd || type == QEvent::TouchCancel) {
            finishStroke(type != QEvent::TouchCancel);
            resetSequence();
        }
        touch->accept();
        return true;
    }

private:
    void installControls(QWidget *toolbar)
    {
        if (toolbar->property("touchInkControlsInstalled").toBool()) return;
        auto *layout = qobject_cast<QHBoxLayout *>(toolbar->layout());
        if (!layout) return;
        m_toolbar = toolbar;
        // Reserve margin rather than adding an unmeasured layout item: native
        // pagination includes these margins in both its width and minimum hint.
        auto *panel = new QWidget(toolbar);
        panel->setObjectName(QStringLiteral("touchInkModes"));
        auto *row = new QHBoxLayout(panel);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);
        auto makeButton = [panel, row](const char *name) {
            auto *button = new QToolButton(panel);
            button->setObjectName(QString::fromLatin1(name));
            button->setCheckable(true);
            button->setFixedSize(80, 34);
            button->setFocusPolicy(Qt::NoFocus);
            button->setStyleSheet(QStringLiteral(
                "QToolButton { border: 1px solid palette(mid); border-radius: 6px;"
                " padding: 2px; font-size: 12px; background: palette(button);"
                " color: palette(button-text); }"
                "QToolButton:hover { border-color: #367bd5; }"
                "QToolButton:checked { background: #286bc4; color: white; border-color: #286bc4; }"));
            row->addWidget(button);
            return button;
        };
        m_gestureButton = makeButton("touchInkGestureToggle");
        m_brushButton = makeButton("touchInkBrushToggle");
        m_roundButton = makeButton("touchInkRoundToggle");
        auto *imageButton = makeButton("touchInkInsertImage");
        imageButton->setFixedWidth(100);
        new ImageInsertController(toolbar, imageButton);
        m_gestureButton->setToolTip(QStringLiteral("手势模式：开时单指移动、双指缩放，电容笔不写字；关时电容笔书写。"));
        m_brushButton->setToolTip(QStringLiteral("笔锋模式：选钢笔后，按速度和转折模拟轻起笔、顿笔和收锋。关闭恢复普通书写。"));
        m_roundButton->setToolTip(QStringLiteral("圆润模式：沿整段弧线连续平滑，减少边沿细小折痕，保留明显转折和短勾。可与笔锋叠加；关闭恢复原来的轨迹。"));
        panel->setFixedSize(352, 34);
        const QMargins margins = layout->contentsMargins();
        layout->setContentsMargins(margins.left() + panel->width() + 6,
                                  margins.top(), margins.right(), margins.bottom());
        panel->move(margins.left(), (toolbar->height() - panel->height()) / 2);
        panel->show();
        panel->raise();
        toolbar->setProperty("touchInkControlsInstalled", true);
        // The first two expandable tools are Pen and Marker in v1.6.3.
        for (auto *widget : toolbar->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
            if (widget->inherits("ExpandableToolButton")) {
                if (!m_penTool) m_penTool = widget->findChild<QAbstractButton *>();
                else { m_markerTool = widget->findChild<QAbstractButton *>(); break; }
            }
        }
        connect(toolbar, SIGNAL(touchGestureModeChanged(int)),
                this, SLOT(nativeGestureChanged(int)));
        connect(m_gestureButton, &QToolButton::toggled, this, [this](bool enabled) {
            // Invoke the existing signal so MainWindow updates every native
            // gesture setting and saves it through its normal path.
            QMetaObject::invokeMethod(m_toolbar, "touchGestureModeChanged", Qt::DirectConnection,
                                      Q_ARG(int, enabled ? 2 : 0));
        });
        connect(m_brushButton, &QToolButton::toggled, this, [this](bool enabled) {
            if (m_drawing) {
                finishStroke(false);
                m_blocked = true;
            }
            m_brush = enabled;
            QSettings settings(QStringLiteral("SpeedyNote"), QStringLiteral("App"));
            settings.setValue(QStringLiteral("touchInk/brushMode"), enabled);
            updateButtons();
        });
        connect(m_roundButton, &QToolButton::toggled, this, [this](bool enabled) {
            if (m_drawing) {
                finishStroke(false);
                m_blocked = true;
            }
            m_round = enabled;
            QSettings settings(QStringLiteral("SpeedyNote"), QStringLiteral("App"));
            settings.setValue(QStringLiteral("touchInk/roundMode"), enabled);
            updateButtons();
        });
        QMetaObject::invokeMethod(toolbar, "touchGestureModeChanged", Qt::DirectConnection,
                                  Q_ARG(int, m_gestures ? 2 : 0));
        updateButtons();
        toolbar->updateGeometry();
    }

    void updateButtons()
    {
        if (!m_gestureButton || !m_brushButton || !m_roundButton) return;
        QSignalBlocker gestures(m_gestureButton), brush(m_brushButton), round(m_roundButton);
        m_gestureButton->setChecked(m_gestures);
        m_brushButton->setChecked(m_brush);
        m_roundButton->setChecked(m_round);
        m_gestureButton->setText(m_gestures ? QStringLiteral("手势：开") : QStringLiteral("手势：关"));
        m_brushButton->setText(m_brush ? QStringLiteral("笔锋：开") : QStringLiteral("笔锋：关"));
        m_roundButton->setText(m_round ? QStringLiteral("圆润：开") : QStringLiteral("圆润：关"));
        m_gestureButton->setAccessibleName(QStringLiteral("手势模式"));
        m_brushButton->setAccessibleName(QStringLiteral("笔锋模式"));
        m_roundButton->setAccessibleName(QStringLiteral("圆润模式"));
    }

    void sendRounded(QPointF position, qreal pressure)
    {
        if (m_brushStroke) appendNib(position, pressure);
        else sendMouseDirect(QEvent::MouseMove, position);
    }

    void sendTabletDirect(QEvent::Type type, QPointF position, qreal pressure)
    {
        if (!m_target) return;
        const QPointF global = m_global + position - m_local;
        const auto button = type == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton;
        const auto buttons = type == QEvent::TabletRelease ? Qt::NoButton : Qt::LeftButton;
        QTabletEvent tablet(type, &m_pen, position, global, pressure, 0, 0, 0, 0, 0,
                            m_modifiers, button, buttons);
        tablet.setTimestamp(m_timestamp);
        QCoreApplication::sendEvent(m_target, &tablet);
    }

    void beginInk()
    {
        m_pending.clear();
        m_pendingLength = m_travel = m_speed = 0;
        m_pressure = 0.24;
        m_lastSent = m_local;
        m_lastSentPressure = m_pressure;
        m_direction = {};
        m_previousTime = inputTime();
        m_rounder.begin(m_local, m_brushStroke ? m_pressure : 1.0);
        if (m_brushStroke) sendTabletDirect(QEvent::TabletPress, m_local, m_pressure);
        else sendMouse(QEvent::MouseButtonPress);
    }

    quint64 inputTime() const { return m_timestamp ? m_timestamp : quint64(m_clock.elapsed()); }
    struct NibPoint { QPointF pos; qreal pressure; };

    void appendBrush(QPointF previous)
    {
        const QPointF delta = m_local - previous;
        const qreal distance = std::hypot(delta.x(), delta.y());
        if (distance < 0.001) return;
        const quint64 now = inputTime();
        const qreal dt = now > m_previousTime ? qBound<quint64>(1, now - m_previousTime, 100) : 8;
        m_previousTime = now;
        m_speed += (distance / dt - m_speed) * (1.0 - std::exp(-dt / 24.0));
        qreal turn = 0;
        if (!m_direction.isNull()) {
            const qreal priorLength = std::hypot(m_direction.x(), m_direction.y());
            turn = (1.0 - qBound(-1.0, QPointF::dotProduct(delta, m_direction) /
                                       (distance * priorLength), 1.0)) * 0.5;
        }
        m_direction = delta;
        m_travel += distance;
        const qreal body = qBound(0.28, 0.34 + 0.60 / (1.0 + m_speed * 1.8) + turn * 0.18, 0.98);
        const qreal target = 0.24 + (body - 0.24) * qMin(1.0, m_travel / 9.0);
        m_pressure += (target - m_pressure) * (1.0 - std::exp(-dt / 14.0));
        // Fit the REAL touch knots before the nib's five-pixel tail buffer
        // splits them. Fitting those artificial collinear points would again
        // leave flat sections and short bends when both modes are enabled.
        if (m_roundStroke)
            m_rounder.append(m_local, m_pressure,
                [this](QPointF p, qreal v) { sendRounded(p, v); });
        else appendNib(m_local, m_pressure);
    }

    void appendNib(QPointF position, qreal pressure)
    {
        const QPointF previous = m_pending.isEmpty() ? m_lastSent : m_pending.last().pos;
        const qreal distance = std::hypot(position.x() - previous.x(), position.y() - previous.y());
        if (distance < 0.001) return;
        m_pending.append({position, pressure});
        m_pendingLength += distance;
        // Keep at most five logical pixels for a true tapered lift, even if
        // TouchEnd has exactly the same position as the preceding update. This
        // introduces no timer or growing input queue and preserves corners.
        while (m_pendingLength > 5.001 && !m_pending.isEmpty()) {
            const NibPoint next = m_pending.first();
            const QPointF segment = next.pos - m_lastSent;
            const qreal length = std::hypot(segment.x(), segment.y());
            if (length < 0.001) { m_pending.removeFirst(); continue; }
            const qreal step = qMin(length, m_pendingLength - 5.0);
            if (step < 0.001) break;
            const qreal t = step / length;
            m_lastSent += segment * t;
            m_lastSentPressure += (next.pressure - m_lastSentPressure) * t;
            sendTabletDirect(QEvent::TabletMove, m_lastSent, m_lastSentPressure);
            m_pendingLength = qMax(0.0, m_pendingLength - step);
            if (t >= 0.999999) m_pending.removeFirst();
        }
    }

    void flushBrush(bool taper)
    {
        const qreal tail = m_pendingLength;
        qreal advanced = 0;
        while (!m_pending.isEmpty()) {
            const NibPoint next = m_pending.takeFirst();
            const QPointF start = m_lastSent;
            const qreal startPressure = m_lastSentPressure;
            const QPointF segment = next.pos - start;
            const qreal length = std::hypot(segment.x(), segment.y());
            const int steps = qMax(1, int(std::ceil(length / 1.25)));
            for (int i = 1; i <= steps; ++i) {
                const qreal t = qreal(i) / steps;
                const qreal p = startPressure + (next.pressure - startPressure) * t;
                const qreal progress = tail > 0 ? qBound(0.0, (advanced + length * t) / tail, 1.0) : 0;
                const qreal eased = progress * progress * (3.0 - 2.0 * progress);
                const qreal finalPressure = taper ? p + (0.12 - p) * eased : p;
                sendTabletDirect(QEvent::TabletMove, start + segment * t, finalPressure);
            }
            advanced += length;
            m_lastSent = next.pos;
            m_lastSentPressure = next.pressure;
        }
        m_pendingLength = 0;
    }

    void sendMouse(QEvent::Type type)
    {
        if (m_roundStroke && type == QEvent::MouseMove)
            m_rounder.append(m_local, 1.0, [this](QPointF p, qreal v) { sendRounded(p, v); });
        else sendMouseDirect(type, m_local);
    }

    void sendMouseDirect(QEvent::Type type, QPointF position)
    {
        if (!m_target)
            return;
        const QPointF windowPos = m_target->mapTo(m_target->window(), position);
        const QPointF global = m_global + position - m_local;
        const auto button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
        const auto buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
        // SpeedyNote accepts application-generated mouse events. It deliberately
        // rejects the system/Qt-generated mouse events produced by normal touch.
        QMouseEvent mouse(type, position, windowPos, global, button, buttons,
                          m_modifiers, Qt::MouseEventSynthesizedByApplication);
        mouse.setTimestamp(m_timestamp);
        QCoreApplication::sendEvent(m_target, &mouse);
    }

    void finishStroke(bool taper = false)
    {
        if (!m_drawing)
            return;
        m_drawing = false;
        if (m_roundStroke)
            m_rounder.finish([this](QPointF p, qreal v) { sendRounded(p, v); });
        if (m_brushStroke) flushBrush(taper);
        if (m_brushStroke) sendTabletDirect(QEvent::TabletRelease, m_local, 0);
        else sendMouse(QEvent::MouseButtonRelease);
        m_primaryId = -1;
    }

    void resetSequence()
    {
        m_target.clear();
        m_ids.clear();
        m_blocked = false;
        m_primaryId = -1;
    }

    QPointer<QWidget> m_target;
    QPointer<QWidget> m_toolbar;
    QPointer<QWidget> m_pendingToolbar;
    QPointer<QWidget> m_pendingLauncher;
    QPointer<QAbstractButton> m_penTool, m_markerTool;
    QPointer<QToolButton> m_gestureButton, m_brushButton, m_roundButton;
    InkRounder m_rounder;
    QPointingDevice m_pen;
    QElapsedTimer m_clock;
    QList<NibPoint> m_pending;
    QPointF m_lastSent, m_direction;
    qreal m_pressure = 0.24, m_lastSentPressure = 0.24;
    qreal m_pendingLength = 0, m_travel = 0, m_speed = 0;
    quint64 m_previousTime = 0;
    bool m_gestures = false, m_brush = false, m_brushStroke = false;
    bool m_round = true, m_roundStroke = false;
    QSet<int> m_ids;
    QPointF m_local;
    QPointF m_global;
    Qt::KeyboardModifiers m_modifiers = Qt::NoModifier;
    quint64 m_timestamp = 0;
    int m_primaryId = -1;
    bool m_drawing = false;
    bool m_blocked = false;
};

class TouchInkPlugin final : public QGenericPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QGenericPluginFactoryInterface" FILE "touchink.json")
public:
    QObject *create(const QString &key, const QString &) override
    {
        if (key.compare(QStringLiteral("speedynotetouchink"), Qt::CaseInsensitive) != 0)
            return nullptr;
        if (!isolateSettings())
            qFatal("Cannot isolate settings for SpeedyNote TouchInk; startup stopped.");
        const bool isolatedInstance = isolateInstanceName();
        // The generic-plugin factory owns the returned driver. Giving it the
        // application as a QObject parent would delete it twice on shutdown.
        auto *filter = new TouchInkFilter(nullptr);
        // SpeedyNote sets its application name after QApplication construction.
        // Keep a separate QStandardPaths data/cache namespace for this edition.
        const auto isolateName = [] {
            const QString name = QStringLiteral("SpeedyNote-TouchInk");
            if (QCoreApplication::applicationName() != name)
                QCoreApplication::setApplicationName(name);
        };
        QObject::connect(QCoreApplication::instance(), &QCoreApplication::applicationNameChanged,
                         filter, isolateName);
        isolateName();
        QCoreApplication::instance()->installEventFilter(filter);
        QCoreApplication::instance()->setProperty("speedynoteTouchInkEnabled", true);
        QCoreApplication::instance()->setProperty("speedynoteTouchInkInstanceIsolated", isolatedInstance);
        return filter;
    }
};

#include "touchink.moc"
