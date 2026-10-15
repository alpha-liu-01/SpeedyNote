// SPDX-License-Identifier: GPL-3.0-or-later
// Uses the installed Qt runtime and the same mouse-source rejection as SpeedyNote.
#include <QApplication>
#include <QEventPoint>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QPushButton>
#include <QMouseEvent>
#include <QLineF>
#include <QPointingDevice>
#include <QSettings>
#include <QStandardPaths>
#include <QTouchEvent>
#include <QTabletEvent>
#include <QToolButton>
#include <QWidget>
#include <QWindow>
#include <QtGui/private/qeventpoint_p.h>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

static bool restoreNeeded = false;
static QVariant originalGesture, originalBrush, originalRound;
static void restoreSettings()
{
    if (!restoreNeeded) return;
    QSettings settings(QStringLiteral("SpeedyNote"), QStringLiteral("App"));
    const auto restore = [&settings](const QString &key, const QVariant &value) {
        if (value.isValid()) settings.setValue(key, value);
        else settings.remove(key);
    };
    restore(QStringLiteral("touchGestureMode"), originalGesture);
    restore(QStringLiteral("touchInk/brushMode"), originalBrush);
    restore(QStringLiteral("touchInk/roundMode"), originalRound);
    settings.sync();
    restoreNeeded = false;
}

static void require(bool result, const char *message)
{
    if (!result) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

class DocumentViewport final : public QWidget
{
    Q_OBJECT
public:
    int starts = 0;
    int moves = 0;
    int ends = 0;
    bool drawing = false;
    QPointF last;
    QList<Qt::MouseEventSource> sources;
    int touches = 0, tabletStarts = 0, tabletEnds = 0;
    QList<qreal> pressures;
    QList<QPointF> tabletPositions;
    QList<QPointF> mousePositions;

protected:
    void tabletEvent(QTabletEvent *event) override
    {
        if (event->type() == QEvent::TabletPress) {
            require(!drawing, "tablet press must not overlap another stroke");
            ++tabletStarts;
            drawing = true;
        } else if (event->type() == QEvent::TabletRelease) {
            require(drawing, "tablet release follows press");
            ++tabletEnds;
            drawing = false;
        } else require(drawing, "tablet move follows press");
        if (event->type() != QEvent::TabletRelease) {
            require(std::isfinite(event->pressure()) && event->pressure() >= 0.1 &&
                    event->pressure() <= 1, "finite bounded ink pressure");
            pressures.append(event->pressure());
            tabletPositions.append(event->position());
        }
        last = event->position();
        event->accept();
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (reject(event)) return;
        ++starts;
        drawing = true;
        last = event->position();
        mousePositions.append(last);
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (reject(event)) return;
        require(drawing, "move must follow press");
        ++moves;
        last = event->position();
        mousePositions.append(last);
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (reject(event)) return;
        require(drawing, "release must follow press");
        ++ends;
        drawing = false;
        last = event->position();
    }
    bool event(QEvent *event) override
    {
        if (event->type() >= QEvent::TouchBegin && event->type() <= QEvent::TouchEnd) {
            ++touches;
            event->accept();
            return true;
        }
        return QWidget::event(event);
    }
private:
    bool reject(QMouseEvent *event)
    {
        if (event->source() == Qt::MouseEventSynthesizedBySystem ||
            event->source() == Qt::MouseEventSynthesizedByQt) {
            event->ignore();
            return true;
        }
        sources.append(event->source());
        event->accept();
        return false;
    }
};

class ExpandableToolButton final : public QWidget
{
    Q_OBJECT
public:
    QPushButton *pen = new QPushButton(this);
    ExpandableToolButton() { pen->setCheckable(true); pen->setChecked(true); }
};

// Exercise the real plugin's integration through the same Qt class and signal
// contracts exposed by the installed application; no internal plugin methods.
class Toolbar final : public QWidget
{
    Q_OBJECT
public:
    int mode = -1;
    ExpandableToolButton *pen = new ExpandableToolButton;
    ExpandableToolButton *marker = new ExpandableToolButton;
    Toolbar() {
        resize(1100, 44);
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(4, 4, 4, 4);
        row->addWidget(pen);
        marker->pen->setChecked(false);
        row->addWidget(marker);
        connect(this, &Toolbar::touchGestureModeChanged, this, [this](int value) {
            mode = value;
            QSettings settings(QStringLiteral("SpeedyNote"), QStringLiteral("App"));
            settings.setValue(QStringLiteral("touchGestureMode"), value);
        });
    }
signals:
    void touchGestureModeChanged(int mode);
};

class TouchButton final : public QWidget
{
public:
    int received = 0;
protected:
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::TouchBegin) {
            ++received;
            event->accept();
            return true;
        }
        return QWidget::event(event);
    }
};

static QEventPoint point(int id, QEventPoint::State state, QPointF position)
{
    return QMutableEventPoint::withTimeStamp(10, id, state, position, position,
                                            position + QPointF(100, 200));
}

static void touch(QWidget &widget, const QPointingDevice &device, QEvent::Type type,
                  const QList<QEventPoint> &points, quint64 timestamp = 10)
{
    QTouchEvent event(type, &device, Qt::NoModifier, points);
    event.setTimestamp(timestamp);
    QApplication::sendEvent(&widget, &event);
}

int main(int argc, char **argv)
{
    std::setbuf(stdout, nullptr);
    // An already-open handle still identifies the real HKCU after a process-local
    // override, letting us verify that QSettings writes go to the copy's profile.
    HKEY realUser = nullptr;
    require(RegOpenKeyExW(HKEY_CURRENT_USER, L"Software", 0, KEY_READ, &realUser) == ERROR_SUCCESS,
            "open original settings view for isolation verification");
    QApplication app(argc, argv);
    const bool enabled = app.property("speedynoteTouchInkEnabled").toBool();
    const bool expectEnabled = argc > 1 && QByteArray(argv[1]) == "--enabled";
    require(enabled == expectEnabled, "generic plugin load state");
    app.setOrganizationName(QStringLiteral("SpeedyNote"));
    app.setApplicationName(QStringLiteral("App"));
    if (enabled) {
        require(app.applicationName() == QStringLiteral("SpeedyNote-TouchInk"),
                "application data namespace is isolated after SpeedyNote resets its name");
        QSettings settings(QStringLiteral("SpeedyNote"), QStringLiteral("App"));
        settings.setValue(QStringLiteral("touchInkIsolationSelfTest"), 271828);
        settings.sync();
        require(settings.status() == QSettings::NoError, "isolated settings are writable");
        HKEY isolatedKey = nullptr;
        require(RegOpenKeyExW(realUser, L"SpeedyNoteTouchInk\\Profile\\Software\\SpeedyNote\\App",
                    0, KEY_READ, &isolatedKey) == ERROR_SUCCESS, "copy's settings key exists");
        DWORD marker = 0, bytes = sizeof(marker);
        require(RegQueryValueExW(isolatedKey, L"touchInkIsolationSelfTest", nullptr, nullptr,
                    reinterpret_cast<BYTE *>(&marker), &bytes) == ERROR_SUCCESS && marker == 271828,
                "settings actually land in the separate profile");
        RegCloseKey(isolatedKey);
        HKEY originalKey = nullptr;
        if (RegOpenKeyExW(realUser, L"SpeedyNote\\App", 0, KEY_READ, &originalKey) == ERROR_SUCCESS) {
            bytes = sizeof(marker);
            require(RegQueryValueExW(originalKey, L"touchInkIsolationSelfTest", nullptr, nullptr,
                        reinterpret_cast<BYTE *>(&marker), &bytes) == ERROR_FILE_NOT_FOUND,
                    "original settings are untouched");
            RegCloseKey(originalKey);
        }
        settings.remove(QStringLiteral("touchInkIsolationSelfTest"));
        settings.sync();
        require(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                    .contains(QStringLiteral("SpeedyNote-TouchInk")), "notebook library has separate data path");
    }
    RegCloseKey(realUser);
    const QPointingDevice device(QStringLiteral("test touch"), 123,
        QInputDevice::DeviceType::TouchScreen, QPointingDevice::PointerType::Finger,
        QInputDevice::Capability::Position, 10, 0);
    const QPointingDevice touchpad(QStringLiteral("test trackpad"), 124,
        QInputDevice::DeviceType::TouchPad, QPointingDevice::PointerType::Finger,
        QInputDevice::Capability::Position, 10, 0);
    using State = QEventPoint::State;

    Toolbar toolbar;
    QToolButton *gestures = nullptr, *brush = nullptr, *round = nullptr;
    if (enabled) {
        QSettings settings(QStringLiteral("SpeedyNote"), QStringLiteral("App"));
        originalGesture = settings.value(QStringLiteral("touchGestureMode"));
        originalBrush = settings.value(QStringLiteral("touchInk/brushMode"));
        originalRound = settings.value(QStringLiteral("touchInk/roundMode"));
        restoreNeeded = true;
        std::atexit(restoreSettings);
        const bool savedBrush = settings.value(QStringLiteral("touchInk/brushMode"), false).toBool();
        const bool savedGesture = settings.value(QStringLiteral("touchGestureMode"), 0).toInt() != 0;
        const bool savedRound = settings.value(QStringLiteral("touchInk/roundMode"), true).toBool();
        toolbar.show();
        app.processEvents();
        gestures = toolbar.findChild<QToolButton *>(QStringLiteral("touchInkGestureToggle"));
        brush = toolbar.findChild<QToolButton *>(QStringLiteral("touchInkBrushToggle"));
        round = toolbar.findChild<QToolButton *>(QStringLiteral("touchInkRoundToggle"));
        require(gestures && brush && round, "all three persistent toolbar controls installed");
        require(gestures->isChecked() == savedGesture && brush->isChecked() == savedBrush &&
                round->isChecked() == savedRound,
                "toggle states restore from saved settings");
        require(gestures->parentWidget()->pos().x() == 4 &&
                toolbar.layout()->contentsMargins().left() >= 254 &&
                round->geometry().left() > brush->geometry().right(),
                "controls reserve the left side without breaking native pagination");
        if (gestures->isChecked()) gestures->click();
        if (brush->isChecked()) brush->click();
        if (round->isChecked()) round->click();
        require(toolbar.mode == 0, "gesture-off reaches native toolbar signal");
    }

    DocumentViewport canvas;
    canvas.setAttribute(Qt::WA_AcceptTouchEvents, true);
    canvas.resize(800, 600);
    touch(canvas, device, QEvent::TouchBegin, {point(1, State::Pressed, {10.5, 20.25})});
    touch(canvas, device, QEvent::TouchUpdate, {point(1, State::Updated, {30.75, 40.5})});
    touch(canvas, device, QEvent::TouchEnd, {point(1, State::Released, {50.25, 60.75})});
    if (!enabled) {
        require(canvas.starts == 0, "baseline does not draw from touch");
        std::puts("PASS: original behavior rejects touch drawing");
        return 0;
    }
    std::printf("Single-stroke events: press=%d move=%d release=%d\n",
                canvas.starts, canvas.moves, canvas.ends);
    require(canvas.starts == 1 && canvas.moves == 1 && canvas.ends == 1 && !canvas.drawing,
            "one continuous stroke ends on lift");
    require(canvas.last == QPointF(50.25, 60.75), "fractional touch coordinates are preserved");
    for (const auto source : canvas.sources)
        require(source == Qt::MouseEventSynthesizedByApplication, "events pass actual source rejection");

    touch(canvas, device, QEvent::TouchBegin, {point(1, State::Pressed, {15, 25})});
    // A packet can contain only the new contact. The old contact is still down.
    touch(canvas, device, QEvent::TouchUpdate, {point(2, State::Pressed, {100, 100})});
    require(canvas.starts == 2 && canvas.ends == 2 && !canvas.drawing,
            "second contact safely ends the stroke even with a partial touch packet");
    touch(canvas, device, QEvent::TouchUpdate, {point(2, State::Released, {100, 100})});
    touch(canvas, device, QEvent::TouchUpdate, {point(1, State::Updated, {50, 50})});
    require(canvas.starts == 2 && canvas.moves == 1, "no accidental stroke after palm contact lifts");
    touch(canvas, device, QEvent::TouchEnd, {point(1, State::Released, {50, 50})});

    touch(canvas, device, QEvent::TouchBegin, {point(1, State::Pressed, {10, 20})});
    touch(canvas, device, QEvent::TouchCancel, {});
    require(canvas.starts == 3 && canvas.ends == 3 && !canvas.drawing, "touch cancel cannot leave mouse down");
    touch(canvas, device, QEvent::TouchBegin, {point(1, State::Pressed, {10, 20})});
    QHideEvent hide;
    QApplication::sendEvent(&canvas, &hide);
    require(canvas.starts == 4 && canvas.ends == 4 && !canvas.drawing, "hiding canvas safely ends a stroke");

    touch(canvas, device, QEvent::TouchBegin,
          {point(1, State::Pressed, {10, 20}), point(2, State::Pressed, {50, 60})});
    touch(canvas, device, QEvent::TouchEnd,
          {point(1, State::Released, {10, 20}), point(2, State::Released, {50, 60})});
    require(canvas.starts == 4, "two-contact start does not draw");

    TouchButton button;
    button.setAttribute(Qt::WA_AcceptTouchEvents, true);
    touch(button, device, QEvent::TouchBegin, {point(1, State::Pressed, {10, 20})});
    require(button.received == 1, "non-canvas touch controls remain interactive");
    QWidget overlay(&canvas);
    overlay.setGeometry(0, 0, 100, 100);
    overlay.show();
    touch(canvas, device, QEvent::TouchBegin, {point(1, State::Pressed, {10, 20})});
    touch(canvas, device, QEvent::TouchUpdate, {point(1, State::Updated, {200, 200})});
    touch(canvas, device, QEvent::TouchEnd, {point(1, State::Released, {200, 200})});
    require(canvas.starts == 4, "touch starting on floating child control does not ink");
    overlay.hide();
    touch(canvas, touchpad, QEvent::TouchBegin, {point(1, State::Pressed, {10, 20})});
    touch(canvas, touchpad, QEvent::TouchEnd, {point(1, State::Released, {10, 20})});
    require(canvas.starts == 4, "touchpad does not become a drawing device");

    QMouseEvent press(QEvent::MouseButtonPress, {10, 20}, {110, 220}, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, {10, 20}, {110, 220}, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &press);
    QApplication::sendEvent(&canvas, &release);
    require(canvas.starts == 5 && canvas.ends == 5, "real mouse still works");

    const int beforeTouches = canvas.touches;
    gestures->click();
    require(toolbar.mode == 2, "gesture-on enables native Full pan and pinch mode");
    touch(canvas, device, QEvent::TouchBegin, {point(1, State::Pressed, {10, 20})});
    touch(canvas, device, QEvent::TouchUpdate,
        {point(1, State::Updated, {20, 30}), point(2, State::Pressed, {80, 90})});
    touch(canvas, device, QEvent::TouchEnd,
        {point(1, State::Released, {20, 30}), point(2, State::Released, {80, 90})});
    require(canvas.starts == 5 && canvas.tabletStarts == 0 && canvas.touches == beforeTouches + 3,
            "gesture mode forwards single- and two-contact packets without ink");
    gestures->click();
    brush->click();
    canvas.pressures.clear();
    canvas.tabletPositions.clear();
    // Slow horizontal, fast horizontal, and a sharp turn; release is stationary.
    touch(canvas, device, QEvent::TouchBegin, {point(1, State::Pressed, {10, 150})}, 100);
    for (int i = 1; i <= 15; ++i)
        touch(canvas, device, QEvent::TouchUpdate, {point(1, State::Updated, {10.0 + 4 * i, 150})}, 100 + 30 * i);
    qreal slowPressure = canvas.pressures.last();
    for (int i = 1; i <= 15; ++i)
        touch(canvas, device, QEvent::TouchUpdate, {point(1, State::Updated, {70.0 + 12 * i, 150})}, 550 + 5 * i);
    qreal fastPressure = canvas.pressures.last();
    touch(canvas, device, QEvent::TouchUpdate, {point(1, State::Updated, {250, 155})}, 655);
    touch(canvas, device, QEvent::TouchEnd, {point(1, State::Released, {250, 155})}, 665);
    require(canvas.tabletStarts == 1 && canvas.tabletEnds == 1 && !canvas.drawing,
            "brush creates exactly one pressure-aware stroke");
    require(slowPressure > fastPressure + 0.15, "slow writing is visibly thicker than fast writing");
    require(canvas.pressures.first() < slowPressure && canvas.pressures.last() <= 0.121,
            "light start and tapered stationary lift");
    require(canvas.tabletPositions.last() == QPointF(250, 155), "taper ends at the actual lift location");
    require(canvas.starts == 5, "brush does not send duplicate mouse strokes");
    std::printf("Brush pressures: start=%.3f slow=%.3f fast=%.3f lift=%.3f\n",
        double(canvas.pressures.first()), double(slowPressure), double(fastPressure), double(canvas.pressures.last()));

    // Marker/object tools retain their original immediate mouse-based behavior.
    toolbar.pen->pen->setChecked(false);
    touch(canvas, device, QEvent::TouchBegin, {point(1, State::Pressed, {10, 20})});
    touch(canvas, device, QEvent::TouchEnd, {point(1, State::Released, {10, 20})});
    require(canvas.starts == 6 && canvas.ends == 6 && canvas.tabletStarts == 1,
            "non-pen tools preserve normal input in brush mode");
    toolbar.pen->pen->setChecked(true);
    touch(canvas, device, QEvent::TouchBegin, {point(1, State::Pressed, {10, 20})});
    touch(canvas, device, QEvent::TouchCancel, {});
    require(canvas.tabletStarts == 2 && canvas.tabletEnds == 2, "brush cancel ends the tablet stroke");

    brush->click();
    touch(canvas, device, QEvent::TouchBegin, {point(1, State::Pressed, {10, 20})});
    gestures->click();
    touch(canvas, device, QEvent::TouchUpdate, {point(1, State::Updated, {60, 20})});
    touch(canvas, device, QEvent::TouchEnd, {point(1, State::Released, {60, 20})});
    require(canvas.starts == 7 && canvas.ends == 7 && !canvas.drawing,
            "switching modes mid-stroke releases and suppresses the unfinished sequence");
    gestures->click();
    touch(canvas, device, QEvent::TouchBegin, {point(1, State::Pressed, {10, 20})});
    touch(canvas, device, QEvent::TouchEnd, {point(1, State::Released, {10, 20})});
    require(canvas.starts == 8 && canvas.ends == 8, "normal writing resumes after both toggles are off");
    brush->click();
    touch(canvas, device, QEvent::TouchBegin, {point(1, State::Pressed, {180, 250})}, 1000);
    for (int i = 1; i <= 240; ++i)
        touch(canvas, device, QEvent::TouchUpdate,
            {point(1, State::Updated, {180.0 + i * 1.37, 250 + 22 * std::sin(i * 0.14)})}, 1000 + i * 8);
    touch(canvas, device, QEvent::TouchEnd,
        {point(1, State::Released, {508.8, 250 + 22 * std::sin(33.6)})}, 2930);
    require(canvas.tabletStarts == 3 && canvas.tabletEnds == 3 && !canvas.drawing,
            "long curved stroke with fractional positions terminates without numerical stalls");

    brush->click();
    round->click();
    auto noisyLine = [&] {
        canvas.mousePositions.clear();
        touch(canvas, device, QEvent::TouchBegin, {point(1, State::Pressed, {100, 300})});
        for (int i = 1; i <= 80; ++i)
            touch(canvas, device, QEvent::TouchUpdate,
                {point(1, State::Updated, {100.0 + i * 4, 300.0 + (i % 2 ? 1 : -1)})});
        touch(canvas, device, QEvent::TouchEnd, {point(1, State::Released, {423, 300})});
        return canvas.mousePositions;
    };
    const auto rounded = noisyLine();
    require(rounded.first() == QPointF(100, 300) && rounded.last() == QPointF(423, 300),
            "rounding preserves actual start and moving lift positions");
    qreal squaredNoise = 0;
    int noiseSamples = 0;
    for (int i = 1; i < rounded.size(); ++i) {
        const auto p = rounded[i];
        require(p.x() >= 100 && p.x() <= 423 && p.y() >= 299 && p.y() <= 301,
                "rounded curve cannot ring outside input bounds");
        require(std::hypot(p.x() - rounded[i-1].x(), p.y() - rounded[i-1].y()) <= 1.01,
                "rounded trajectory has dense bounded output spacing");
        if (p.x() > 110 && p.x() < 410) { squaredNoise += (p.y()-300)*(p.y()-300); ++noiseSamples; }
    }
    const qreal noiseRms = std::sqrt(squaredNoise / noiseSamples);
    require(noiseRms < 0.65, "rounding suppresses visible alternating sampling wobble");
    round->click();
    const auto raw = noisyLine();
    require(raw.size() == 81 && raw[1] == QPointF(104, 301),
            "rounding off restores original coordinates without extra interpolation");
    std::printf("Rounding: raw=%lld rounded=%lld samples, noise RMS=%.3f (input=1.000)\n",
        static_cast<long long>(raw.size()), static_cast<long long>(rounded.size()), double(noiseRms));

    round->click();
    canvas.mousePositions.clear();
    touch(canvas, device, QEvent::TouchBegin, {point(1, State::Pressed, {100, 100})});
    touch(canvas, device, QEvent::TouchUpdate, {point(1, State::Updated, {160, 100})});
    touch(canvas, device, QEvent::TouchUpdate, {point(1, State::Updated, {160, 160})});
    touch(canvas, device, QEvent::TouchEnd, {point(1, State::Released, {152, 152})});
    qreal closestCorner = 100;
    for (auto p : canvas.mousePositions)
        closestCorner = std::min(closestCorner, std::hypot(p.x()-160, p.y()-100));
    require(closestCorner > 0.2 && closestCorner < 3 &&
            canvas.mousePositions.last() == QPointF(152, 152),
            "softens a corner locally and keeps the short final hook");

    const int tapMoves = canvas.moves;
    touch(canvas, device, QEvent::TouchBegin, {point(1, State::Pressed, {50, 50})});
    touch(canvas, device, QEvent::TouchEnd, {point(1, State::Released, {50, 50})});
    require(canvas.moves == tapMoves && !canvas.drawing, "rounded tap remains a single dot");
    toolbar.pen->pen->setChecked(false);
    toolbar.marker->pen->setChecked(true);
    require(noisyLine().size() > 81, "rounding also supports the marker tool");
    toolbar.marker->pen->setChecked(false);
    require(noisyLine().size() == 81, "eraser and object tools retain immediate original input");
    toolbar.pen->pen->setChecked(true);
    brush->click();
    canvas.pressures.clear();
    canvas.tabletPositions.clear();
    touch(canvas, device, QEvent::TouchBegin, {point(1, State::Pressed, {180, 250})}, 4000);
    for (int i = 1; i <= 240; ++i)
        touch(canvas, device, QEvent::TouchUpdate,
            {point(1, State::Updated, {180.0 + i * 1.37, 250 + 22 * std::sin(i * 0.14)})}, 4000 + i * 8);
    const QPointF lift {508.8, 250 + 22 * std::sin(33.6)};
    touch(canvas, device, QEvent::TouchEnd, {point(1, State::Released, lift)}, 5930);
    require(canvas.tabletStarts == 4 && canvas.tabletEnds == 4 && !canvas.drawing &&
            QLineF(canvas.tabletPositions.last(), lift).length() < 0.00001 &&
            canvas.pressures.last() <= 0.121,
            "rounding and brush cooperate without losing stationary lift taper");
    brush->click();

    // A real coarse touch arc must keep the same rounded geometry with nib
    // mode on. The nib's partial tail-buffer points used to become artificial
    // curve knots and concentrated each bend into just a few pixels.
    const auto coarseArc = [&] {
        const QPointF center(300, 210);
        const auto pos = [center](int i) {
            const qreal angle = i * 0.24;
            return center + QPointF(100 * std::cos(angle), 100 * std::sin(angle));
        };
        touch(canvas, device, QEvent::TouchBegin, {point(1, State::Pressed, pos(0))}, 7000);
        for (int i = 1; i <= 25; ++i)
            touch(canvas, device, QEvent::TouchUpdate,
                {point(1, State::Updated, pos(i))}, 7000 + i * 16);
        // The lift moves to a new sample rather than repeating the last update.
        touch(canvas, device, QEvent::TouchEnd, {point(1, State::Released, pos(26))}, 7416);
        return pos(26);
    };
    canvas.mousePositions.clear();
    const QPointF coarseLift = coarseArc();
    const auto coarseReference = canvas.mousePositions;
    brush->click();
    canvas.tabletPositions.clear();
    canvas.pressures.clear();
    coarseArc();
    qreal worstDistance = 0;
    for (const auto p : canvas.tabletPositions) {
        qreal closest = 1e9;
        for (int i = 1; i < coarseReference.size(); ++i) {
            const QPointF a = coarseReference[i-1], delta = coarseReference[i] - a;
            const qreal squared = QPointF::dotProduct(delta, delta);
            const qreal t = squared > 0 ? qBound(0.0, QPointF::dotProduct(p-a, delta) / squared, 1.0) : 0;
            closest = qMin(closest, QLineF(p, a + delta * t).length());
        }
        worstDistance = qMax(worstDistance, closest);
    }
    require(worstDistance < 0.01 && canvas.tabletPositions.size() > 500 &&
            QLineF(canvas.tabletPositions.last(), coarseLift).length() < 0.00001 &&
            canvas.pressures.last() <= 0.121 && !canvas.drawing,
            "coarse arcs keep the same rounded geometry with nib mode and a moving lift");
    std::printf("Coarse arc + nib: maximum deviation from rounded trajectory=%.6f pixels\n",
                double(worstDistance));
    brush->click();
    touch(canvas, device, QEvent::TouchBegin, {point(1, State::Pressed, {40, 40})});
    touch(canvas, device, QEvent::TouchUpdate, {point(1, State::Updated, {50, 50})});
    round->click();
    const int movesAfterToggle = canvas.moves;
    require(!canvas.drawing && canvas.mousePositions.last() == QPointF(50, 50),
            "switching rounding flushes to the previous touch location");
    touch(canvas, device, QEvent::TouchUpdate, {point(1, State::Updated, {60, 60})});
    touch(canvas, device, QEvent::TouchEnd, {point(1, State::Released, {60, 60})});
    require(!canvas.drawing && canvas.moves == movesAfterToggle,
            "changing rounding mid-stroke blocks remaining touch until lift");
    round->click();
    gestures->click();
    const int noInkStarts = canvas.starts;
    touch(canvas, device, QEvent::TouchBegin, {point(1, State::Pressed, {40, 40})});
    touch(canvas, device, QEvent::TouchEnd, {point(1, State::Released, {60, 60})});
    require(canvas.starts == noInkStarts, "gesture mode still excludes ink when rounding is on");
    gestures->click();
    QSettings settings(QStringLiteral("SpeedyNote"), QStringLiteral("App"));
    require(settings.value(QStringLiteral("touchInk/roundMode")).toBool(), "rounding state is persisted");
    if (originalGesture.isValid()) settings.setValue(QStringLiteral("touchGestureMode"), originalGesture);
    else settings.remove(QStringLiteral("touchGestureMode"));
    if (originalBrush.isValid()) settings.setValue(QStringLiteral("touchInk/brushMode"), originalBrush);
    else settings.remove(QStringLiteral("touchInk/brushMode"));
    if (originalRound.isValid()) settings.setValue(QStringLiteral("touchInk/roundMode"), originalRound);
    else settings.remove(QStringLiteral("touchInk/roundMode"));
    settings.sync();
    restoreNeeded = false;
    std::puts("PASS: left toolbar, saved toggles, native gesture forwarding, variable pen pressure,");
    std::puts("      stationary lift taper, non-pen tools, cancellation, and live mode transitions");
    std::puts("PASS: plugin load, settings/data isolation, continuous inking, subpixel coordinates,");
    std::puts("      contact tracking, cancel/hide cleanup, multitouch, controls, trackpad, and mouse");
    std::puts("PASS: rounding reduces jitter, bounds sample spacing, preserves hooks and dots,");
    std::puts("      supports marker/nib, restores raw input when off, and saves its toggle");
    return 0;
}

#include "input-tests.moc"
