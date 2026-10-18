// SPDX-License-Identifier: GPL-3.0-or-later
// Independent geometry regression. No plugin or application window is needed.
// The build includes the repository's unmodified native headers. The
// comparisons below call the real VectorLayer::renderStroke, including its
// four-step Catmull-Rom subdivision and pressure-dependent polygon outline.
#include "rounding.h"
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QSet>
#include "layers/VectorLayer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

// Frozen reference implementation of the earlier quadratic smoother.
// In particular, it splits the input chord into <=6 px pieces BEFORE fitting.
class LegacyRounder
{
public:
    struct Sample { QPointF pos; qreal pressure; };
    void begin(QPointF pos, qreal pressure) { m_anchor = m_control = {pos, pressure}; }
    template<class Sink> void append(QPointF pos, qreal pressure, Sink sink)
    {
        if (!std::isfinite(pos.x()) || !std::isfinite(pos.y()) || !std::isfinite(pressure)) return;
        const Sample previous = m_control;
        const qreal length = distance(previous.pos, pos);
        if (length < 0.0001) return;
        const int steps = int(std::clamp(std::ceil(length / 6.0), 1.0, 4096.0));
        for (int i = 1; i <= steps; ++i) {
            const qreal t = qreal(i) / steps;
            const Sample next {previous.pos + (pos - previous.pos) * t,
                                 previous.pressure + (pressure - previous.pressure) * t};
            const Sample midpoint {(m_control.pos + next.pos) * 0.5,
                                    (m_control.pressure + next.pressure) * 0.5};
            curve(m_anchor, m_control, midpoint, sink);
            m_anchor = midpoint;
            m_control = next;
        }
    }
    template<class Sink> void finish(Sink sink)
    {
        curve(m_anchor, m_control, m_control, sink);
        m_anchor = m_control;
    }
private:
    static qreal distance(QPointF a, QPointF b) { return std::hypot(a.x() - b.x(), a.y() - b.y()); }
    template<class Sink> static void curve(Sample a, Sample b, Sample c, Sink sink)
    {
        const qreal length = distance(a.pos, b.pos) + distance(b.pos, c.pos);
        if (length < 0.0001) return;
        const qreal maxDerivative = 2.0 * std::max(distance(a.pos, b.pos), distance(b.pos, c.pos));
        const int steps = int(std::clamp(std::ceil(maxDerivative), 1.0, 4096.0));
        for (int i = 1; i <= steps; ++i) {
            const qreal t = qreal(i) / steps, u = 1.0 - t;
            sink(a.pos * (u * u) + b.pos * (2.0 * u * t) + c.pos * (t * t),
                 a.pressure * (u * u) + b.pressure * (2.0 * u * t) + c.pressure * (t * t));
        }
    }
    Sample m_anchor {}, m_control {};
};

using Path = QVector<StrokePoint>;
static constexpr double Pi = 3.14159265358979323846;
static int failures = 0;
static QJsonArray checks;

// Qt's bundled clang64 build uses libc++; do not invoke a Qt exported method
// whose ABI mentions std::initializer_list from this MinGW/libstdc++ executable.
static QJsonObject object(std::initializer_list<std::pair<QString, QJsonValue>> entries)
{
    QJsonObject result;
    for (const auto& entry : entries) result.insert(entry.first, entry.second);
    return result;
}

static double distance(QPointF a, QPointF b)
{ return std::hypot(a.x() - b.x(), a.y() - b.y()); }

static void check(bool pass, const char* label)
{
    checks.append(object({{"check", label}, {"pass", pass}}));
    std::printf("%s: %s\n", pass ? "PASS" : "FAIL", label);
    if (!pass) ++failures;
}

template<class Rounder> static Path rounded(const Path& input)
{
    if (input.isEmpty()) return {};
    Path result { input.first() };
    Rounder rounder;
    rounder.begin(input.first().pos, input.first().pressure);
    auto sink = [&](QPointF pos, qreal pressure) { result.append({pos, pressure, 0}); };
    for (int i = 1; i < input.size(); ++i)
        rounder.append(input[i].pos, input[i].pressure, sink);
    rounder.finish(sink);
    return result;
}

// Exact position/pressure decimation in DocumentViewport::addPointToStroke,
// MIN_SCREEN_DISTANCE=1.5, with zoom=1. Coordinates here are screen pixels.
// No forced final point: the viewport itself also skips a sub-threshold point.
static Path nativeDecimation(const Path& input)
{
    Path result;
    for (const auto& p : input) {
        if (!result.isEmpty() && distance(result.last().pos, p.pos) < 1.5) {
            result.last().pressure = std::max(result.last().pressure, p.pressure);
            continue;
        }
        result.append(p);
    }
    return result;
}

static VectorStroke stroke(const Path& path, QColor color = QColor("#ff755d"), double width = 3.0)
{
    VectorStroke s;
    s.points = path;
    s.baseThickness = width;
    s.color = color;
    s.updateBoundingBox();
    return s;
}

// Recover the native renderer's interpolated centerline from its left/right
// outline pairs. This uses public buildStrokePolygon, avoiding a test copy of
// the private Catmull-Rom routine. Both edge points use the same exact center.
static Path nativeRenderedCenter(const Path& path)
{
    const auto polygon = VectorLayer::buildStrokePolygon(stroke(path));
    if (polygon.isSinglePoint) return path;
    const int n = int(polygon.polygon.size()) / 2;
    Path center;
    center.reserve(n);
    for (int i = 0; i < n; ++i)
        center.append({(polygon.polygon[i] + polygon.polygon[2 * n - 1 - i]) * 0.5, 1.0, 0});
    return center;
}

static std::vector<double> lengths(const Path& path)
{
    std::vector<double> arc(path.size(), 0.0);
    for (int i = 1; i < path.size(); ++i)
        arc[i] = arc[i - 1] + distance(path[i - 1].pos, path[i].pos);
    return arc;
}

// Equal physical spacing makes the rendered-center metric independent of the
// native Catmull-Rom parameter step, which is not an equal-distance sampling.
static Path resample(const Path& path, double spacing)
{
    if (path.size() < 2) return path;
    const auto arc = lengths(path);
    Path result {path.first()};
    int i = 1;
    for (double s = spacing; s < arc.back(); s += spacing) {
        while (i < path.size() - 1 && arc[i] < s) ++i;
        const double d = arc[i] - arc[i - 1];
        if (d < 1e-9) continue;
        const double t = (s - arc[i - 1]) / d;
        result.append({path[i - 1].pos * (1.0 - t) + path[i].pos * t, 1.0, 0});
    }
    result.append(path.last());
    return result;
}

static QJsonObject metrics(const Path& path, double trim, double radius = 0.0)
{
    const auto arc = lengths(path);
    QJsonObject result = object({{"points", int(path.size())}});
    if (path.isEmpty()) return result;
    double maxStep = 0, maxTurn = 0, maxTurnJump = 0, maxCurvature = 0;
    double maxCurvatureJump = 0, radialSq = 0, radialMax = 0;
    double previousTurn = 0, previousCurvature = 0;
    int radialN = 0;
    bool havePrevious = false;
    std::vector<double> curvatures;
    for (int i = 1; i < path.size(); ++i)
        maxStep = std::max(maxStep, arc[i] - arc[i - 1]);
    for (int i = 1; i + 1 < path.size(); ++i) {
        if (arc[i] < trim || arc.back() - arc[i] < trim) continue;
        const QPointF a = path[i].pos - path[i - 1].pos;
        const QPointF b = path[i + 1].pos - path[i].pos;
        const double la = distance(a, {}), lb = distance(b, {});
        if (la < 1e-8 || lb < 1e-8) continue;
        const double turn = std::atan2(a.x() * b.y() - a.y() * b.x(), QPointF::dotProduct(a, b));
        const double curvature = turn / ((la + lb) * 0.5);
        maxTurn = std::max(maxTurn, std::abs(turn));
        maxCurvature = std::max(maxCurvature, std::abs(curvature));
        curvatures.push_back(std::abs(curvature));
        if (havePrevious) {
            maxTurnJump = std::max(maxTurnJump, std::abs(turn - previousTurn));
            maxCurvatureJump = std::max(maxCurvatureJump, std::abs(curvature - previousCurvature));
        }
        previousTurn = turn;
        previousCurvature = curvature;
        havePrevious = true;
        if (radius > 0) {
            const double deviation = std::abs(distance(path[i].pos, {}) - radius);
            radialSq += deviation * deviation;
            radialMax = std::max(radialMax, deviation);
            ++radialN;
        }
    }
    std::sort(curvatures.begin(), curvatures.end());
    const double p95 = curvatures.empty() ? 0.0 : curvatures[size_t((curvatures.size() - 1) * 0.95)];
    result["path_length_px"] = arc.back();
    result["max_step_px"] = maxStep;
    result["max_local_turn_deg"] = maxTurn * 180.0 / Pi;
    result["max_adjacent_turn_jump_deg"] = maxTurnJump * 180.0 / Pi;
    result["max_curvature_per_px"] = maxCurvature;
    result["p95_curvature_per_px"] = p95;
    result["max_adjacent_curvature_jump_per_px"] = maxCurvatureJump;
    if (radius > 0) {
        result["ideal_curvature_per_px"] = 1.0 / radius;
        result["peak_curvature_over_ideal"] = maxCurvature * radius;
        result["radial_rms_error_px"] = radialN ? std::sqrt(radialSq / radialN) : 0;
        result["radial_max_error_px"] = radialMax;
    }
    return result;
}

static Path circle(double radius, double spacing, bool uneven = false)
{
    Path input;
    const double start = -2.95, end = 0.65;
    int n = 0;
    for (double angle = start; angle < end;) {
        input.append({QPointF(radius * std::cos(angle), radius * std::sin(angle)), 1.0, 0});
        const double factors[] = {0.22, 1.0, 0.55, 0.88, 0.35};
        angle += spacing / radius * (uneven ? factors[n++ % 5] : 1.0);
    }
    input.append({QPointF(radius * std::cos(end), radius * std::sin(end)), 1.0, 0});
    return input;
}

static Path spiral(double spacing)
{
    Path input;
    const double end = 5.6 * Pi;
    for (double t = 0.0; t < end;) {
        const double radius = 25.0 + 7.0 * t;
        input.append({QPointF(radius * std::cos(t), radius * std::sin(t)), 1.0, 0});
        t += spacing / std::hypot(radius, 7.0);
    }
    const double radius = 25.0 + 7.0 * end;
    input.append({QPointF(radius * std::cos(end), radius * std::sin(end)), 1.0, 0});
    return input;
}

static QJsonObject comparison(const Path& input, double trim, double radius = 0.0)
{
    const auto oldPath = rounded<LegacyRounder>(input), newPath = rounded<InkRounder>(input);
    const auto oldStored = nativeDecimation(oldPath), newStored = nativeDecimation(newPath);
    const auto oldRendered = resample(nativeRenderedCenter(oldStored), 1.5);
    const auto newRendered = resample(nativeRenderedCenter(newStored), 1.5);
    auto stages = [&](const Path& output, const Path& stored, const Path& rendered) {
        return object({{"rounder_output", metrics(output, trim, radius)},
                            {"native_decimated", metrics(stored, trim, radius)},
                            {"native_rendered_equal_1_5px", metrics(rendered, trim, radius)}});
    };
    return object({{"input_points", int(input.size())}, {"exclude_endpoint_length_px", trim},
        {"legacy", stages(oldPath, oldStored, oldRendered)},
        {"new", stages(newPath, newStored, newRendered)}});
}

static double metric(const QJsonObject& c, const char* algorithm, const char* stage, const char* name)
{ return c[algorithm].toObject()[stage].toObject()[name].toDouble(); }

static void drawNative(QPainter& painter, const Path& path, const QRectF& world, const QRectF& pane)
{
    const double scale = std::min(pane.width() / world.width(), pane.height() / world.height());
    painter.save();
    painter.setClipRect(pane);
    painter.translate(pane.center());
    painter.scale(scale, scale);
    painter.translate(-world.center());
    // Native plot uses 3px width in original screen space, magnified together
    // with the stroke. The underlying captured output was decimated BEFORE
    // this display magnification, exactly as when zooming a completed stroke.
    VectorLayer::renderStroke(painter, stroke(path));
    painter.restore();
}

static void drawInput(QPainter& painter, const Path& input, const QRectF& world, const QRectF& pane)
{
    const double scale = std::min(pane.width() / world.width(), pane.height() / world.height());
    painter.save();
    painter.setClipRect(pane);
    painter.translate(pane.center());
    painter.scale(scale, scale);
    painter.translate(-world.center());
    painter.setPen(QPen(QColor("#9ca8b4"), 1.0 / scale));
    painter.setBrush(Qt::white);
    for (const auto& p : input) painter.drawEllipse(p.pos, 3.5 / scale, 3.5 / scale);
    painter.restore();
}

static bool comparisonImage(const QString& path, const Path& input, bool arc, const QJsonObject& report)
{
    QImage image(1800, arc ? 680 : 1160, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor("#f5f7fa"));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QColor("#172635"));
    painter.setFont(QFont("Segoe UI", 23, QFont::Bold));
    painter.drawText(QRectF(45, 25, 1720, 55), Qt::AlignLeft | Qt::AlignVCenter,
        arc ? "Sparse circular arc: same 25 px input samples" : "Sparse spiral: same 20 px input samples");
    painter.setFont(QFont("Segoe UI", 12));
    painter.drawText(QRectF(45, 84, 1720, 50), Qt::AlignLeft | Qt::AlignVCenter,
        "1.5 px native point decimation + unmodified VectorLayer::renderStroke; grey circles = original samples");
    const QRectF world = arc ? QRectF(-75, -130, 150, 62) : QRectF(-155, -155, 310, 310);
    const Path outputs[] = {nativeDecimation(rounded<LegacyRounder>(input)), nativeDecimation(rounded<InkRounder>(input))};
    const QString labels[] = {"PREVIOUS: small fillets between flat chords", "UPDATED: fit the real samples as one smooth arc"};
    const char* algorithms[] = {"legacy", "new"};
    for (int i = 0; i < 2; ++i) {
        const QRectF card(35 + i * 885, 155, 850, image.height() - 195);
        painter.setPen(QPen(QColor("#dce3eb"), 1));
        painter.setBrush(Qt::white);
        painter.drawRoundedRect(card, 15, 15);
        painter.setFont(QFont("Segoe UI", 15, QFont::Bold));
        painter.setPen(QColor("#26394b"));
        painter.drawText(card.adjusted(22, 13, -22, -card.height() + 62), Qt::AlignLeft | Qt::AlignVCenter, labels[i]);
        const QRectF pane = card.adjusted(24, 80, -24, -100);
        drawNative(painter, outputs[i], world, pane);
        drawInput(painter, input, world, pane);
        const auto m = report[algorithms[i]].toObject()["native_decimated"].toObject();
        painter.setFont(QFont("Segoe UI", 12));
        painter.setPen(QColor("#4b5f71"));
        const QString text = QString("Peak local turn after native decimation: %1 deg\nMaximum adjacent turn change: %2 deg")
            .arg(m["max_local_turn_deg"].toDouble(), 0, 'f', 2)
            .arg(m["max_adjacent_turn_jump_deg"].toDouble(), 0, 'f', 2);
        painter.drawText(card.adjusted(24, card.height() - 84, -24, -12), Qt::AlignLeft | Qt::AlignVCenter, text);
    }
    painter.end();
    return image.save(path);
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    const QString outputDir = QCoreApplication::applicationDirPath() + "/test-results";
    if (!QDir(outputDir).exists()) return 2;
    QJsonObject cases;

    for (double spacing : {10.0, 17.0, 25.0}) {
        const auto input = circle(120.0, spacing);
        const auto report = comparison(input, 2.0 * spacing, 120.0);
        cases[QString("circle_%1px").arg(spacing)] = report;
        const double oldPeak = metric(report, "legacy", "native_decimated", "max_curvature_per_px");
        const double newPeak = metric(report, "new", "native_decimated", "max_curvature_per_px");
        const double oldJump = metric(report, "legacy", "native_decimated", "max_adjacent_curvature_jump_per_px");
        const double newJump = metric(report, "new", "native_decimated", "max_adjacent_curvature_jump_per_px");
        const double oldRendered = metric(report, "legacy", "native_rendered_equal_1_5px", "max_curvature_per_px");
        const double newRendered = metric(report, "new", "native_rendered_equal_1_5px", "max_curvature_per_px");
        const std::string label = "circle " + std::to_string(int(spacing)) + "px: native curvature peaks and jumps decrease";
        check(newPeak < oldPeak * 0.65 && newJump < oldJump * 0.60, label.c_str());
        check(newRendered < oldRendered * 0.70, "native final rendered circle also has smaller curvature peaks");
        check(metric(report, "new", "native_decimated", "radial_max_error_px") < spacing * spacing / (8.0 * 120.0) + 0.15,
              "circle shape error remains inside the expected midpoint-spline sagitta");
        check(metric(report, "new", "rounder_output", "max_step_px") <= 1.00001,
              "circle emitted samples stay within one screen pixel");
        if (spacing == 25.0)
            check(comparisonImage(QDir(outputDir).filePath("arc-crease-comparison.png"), input, true, report), "save native arc comparison PNG");
    }

    const auto spiralInput = spiral(20.0);
    const auto spiralReport = comparison(spiralInput, 45.0);
    cases["spiral_20px"] = spiralReport;
    check(metric(spiralReport, "new", "native_decimated", "max_curvature_per_px") <
          metric(spiralReport, "legacy", "native_decimated", "max_curvature_per_px") * 0.70,
          "spiral native curvature peak decreases");
    check(comparisonImage(QDir(outputDir).filePath("spiral-crease-comparison.png"), spiralInput, false, spiralReport), "save native spiral comparison PNG");

    const auto unevenInput = circle(120.0, 25.0, true);
    const auto uneven = comparison(unevenInput, 50.0, 120.0);
    cases["nonuniform_circle_5_5_to_25px"] = uneven;
    // Highly nonuniform spacing is deliberately retained as a harder case:
    // the spline is tangent-continuous, but curvature need not be constant.
    // Require a non-regressing worst peak and a useful typical improvement;
    // do not hide the remaining peak by relaxing the reported measurement.
    check(metric(uneven, "new", "native_decimated", "max_curvature_per_px") <=
          metric(uneven, "legacy", "native_decimated", "max_curvature_per_px") &&
          metric(uneven, "new", "native_decimated", "p95_curvature_per_px") <
          metric(uneven, "legacy", "native_decimated", "p95_curvature_per_px") * 0.75,
          "highly nonuniform spacing preserves worst peak and reduces typical curvature spikes");
    check(metric(uneven, "new", "rounder_output", "max_step_px") <= 1.00001,
          "nonuniform samples retain one-pixel maximum output spacing");

    Path noise;
    for (int i = 0; i <= 180; ++i)
        noise.append({QPointF(i * 0.8, (i % 2 ? 1.0 : -1.0)), 0.25 + i * 0.003, 0});
    const auto noisyOutput = rounded<InkRounder>(noise);
    double squared = 0;
    int samples = 0;
    for (const auto& p : noisyOutput) if (p.pos.x() > 5 && p.pos.x() < 139) {
        squared += p.pos.y() * p.pos.y();
        ++samples;
    }
    const double noiseRms = std::sqrt(squared / samples);
    cases["dense_alternating_noise_1px"] = object({{"new_rms_y_px", noiseRms}, {"input_rms_y_px", 1.0},
        {"native_decimated", metrics(nativeDecimation(noisyOutput), 5.0)}});
    check(noiseRms < 0.65, "dense plus/minus one-pixel alternating noise stays suppressed");

    const Path straight {{{0, 0}, 0.2, 0}, {{1, 0}, 0.3, 0}, {{21, 0}, 0.6, 0},
                         {{22, 0}, 0.7, 0}, {{67, 0}, 0.8, 0}, {{68, 0}, 1.0, 0}};
    const auto straightOutput = rounded<InkRounder>(straight);
    bool straightPass = true, pressurePass = true;
    for (const auto& p : straightOutput) {
        straightPass &= std::abs(p.pos.y()) < 1e-10 && p.pos.x() >= -1e-10 && p.pos.x() <= 68.00001;
        pressurePass &= std::isfinite(p.pressure) && p.pressure >= 0.199999 && p.pressure <= 1.000001;
    }
    check(straightPass && distance(straightOutput.last().pos, straight.last().pos) < 1e-10,
          "nonuniform straight line stays straight and flushes to exact endpoint");
    check(pressurePass, "pressure interpolation stays within the real sample range");
    check(metrics(straightOutput, 0)["max_step_px"].toDouble() <= 1.00001,
          "asymmetric one-to-forty-five-pixel straight segments keep one-pixel spacing");
    cases["nonuniform_straight"] = metrics(straightOutput, 0);

    const Path dot {{{8.25, 13.5}, 0.7, 0}, {{8.25, 13.5}, 0.7, 0}};
    const auto dotOutput = rounded<InkRounder>(dot);
    check(dotOutput.size() == 1 && dotOutput.first().pos == dot.first().pos, "stationary dot emits no artificial segment");

    const Path hook {{{0, 0}, 1, 0}, {{30, 0}, 1, 0}, {{60, 0}, 1, 0},
                     {{60, 35}, 1, 0}, {{60, 50}, 1, 0}, {{56, 53}, 1, 0}, {{53, 50}, 1, 0}};
    const auto hookOutput = rounded<InkRounder>(hook);
    double cornerDistance = 1e9, maxY = 0;
    bool contained = true;
    for (const auto& p : hookOutput) {
        cornerDistance = std::min(cornerDistance, distance(p.pos, QPointF(60, 0)));
        maxY = std::max(maxY, p.pos.y());
        contained &= p.pos.x() >= -1e-9 && p.pos.x() <= 60.000001 && p.pos.y() >= -1e-9 && p.pos.y() <= 53.000001;
    }
    check(cornerDistance <= 1.1 && maxY > 51.5 && contained && hookOutput.last().pos == hook.last().pos,
          "right angle and short hook keep their local shape without overshoot");
    cases["right_angle_short_hook"] = object({{"corner_min_distance_px", cornerDistance}, {"maximum_y_px", maxY},
        {"output", metrics(hookOutput, 0)}});

    auto cornerGap = [](double degrees) {
        const double angle = degrees * Pi / 180.0;
        const Path input {{{-40, 0}, 1, 0}, {{0, 0}, 1, 0},
                          {{40 * std::cos(angle), 40 * std::sin(angle)}, 1, 0}};
        double closest = 1e9;
        for (const auto& p : rounded<InkRounder>(input))
            closest = std::min(closest, distance(p.pos, {}));
        return closest;
    };
    QJsonArray angleTransitions;
    bool continuous = true;
    for (double boundary : {45.0, 60.0, 75.0}) {
        const double before = cornerGap(boundary - 0.01), after = cornerGap(boundary + 0.01);
        const double gapChange = std::abs(after - before);
        continuous &= gapChange < 0.05;
        angleTransitions.append(object({{"angle_deg", boundary}, {"before_gap_px", before},
                                       {"after_gap_px", after}, {"gap_change_px", gapChange}}));
    }
    check(continuous, "corner preservation changes continuously across 45, 60, and 75 degrees");
    cases["corner_strength_continuity"] = angleTransitions;

    InkRounder abnormal;
    Path bounded {{{0, 0}, 0.5, 0}};
    abnormal.begin({0, 0}, 0.5);
    auto sink = [&](QPointF p, qreal pressure) { bounded.append({p, pressure, 0}); };
    abnormal.append({std::numeric_limits<double>::quiet_NaN(), 0}, 0.5, sink);
    abnormal.append({std::numeric_limits<double>::infinity(), 0}, 0.5, sink);
    abnormal.append({1e9, 0}, 0.5, sink);
    abnormal.append({-1e9, 0}, 0.5, sink);
    abnormal.append({20, 0}, std::numeric_limits<double>::quiet_NaN(), sink);
    abnormal.append({20, 0}, 0.7, sink);
    abnormal.finish(sink);
    bool finite = true;
    for (const auto& p : bounded)
        finite &= std::isfinite(p.pos.x()) && std::isfinite(p.pos.y()) && std::isfinite(p.pressure) && p.pos.x() >= 0 && p.pos.x() <= 20;
    check(finite && bounded.size() <= 64 && bounded.last().pos == QPointF(20, 0),
          "nonfinite and billion-pixel packets are rejected without contaminating the next valid point");
    const int count = int(bounded.size());
    abnormal.finish(sink);
    check(bounded.size() == count, "repeated finish is idempotent");
    cases["rejected_abnormal_packets"] = object({{"points_after_valid_recovery", count}, {"finite_and_bounded", finite}});

    // Finite but implausibly large packets below the rejection limit can use
    // the deliberate subdivision cap. Test bounded work, not one-pixel spacing.
    InkRounder large;
    int generated = 0;
    large.begin({0, 0}, 1);
    large.append({1e6, 0}, 1, [&](QPointF, qreal) { ++generated; });
    large.finish([&](QPointF, qreal) { ++generated; });
    check(generated <= 8192, "very large finite packet does bounded work under the subdivision cap");
    cases["large_finite_packet_work_cap"] = object({{"generated_points", generated}, {"note", "Subdivision cap intentionally supersedes one-pixel spacing for implausible huge packets."}});

    QJsonObject report = object({{"passed", failures == 0}, {"failures", failures},
        {"native_renderer", "Unmodified reference VectorLayer::renderStroke and buildStrokePolygon"},
        {"native_decimation_screen_px", 1.5}, {"capture_zoom", 1.0},
        {"metrics_note", "Curvature = signed local turn / mean neighboring chord length. Endpoint transition lengths are excluded. Final renderer centerline is recovered from native outline pairs and resampled every 1.5 px. Synthetic inputs isolate geometry; actual hardware timing/pressure requires an application check."},
        {"cases", cases}, {"checks", checks}});
    QFile file(QDir(outputDir).filePath("curve-regression.json"));
    if (!file.open(QIODevice::WriteOnly)) return 2;
    file.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    file.close();
    std::printf("\nCurve regression: %d failed checks. Output: %s\n", failures, qPrintable(outputDir));
    return failures ? 1 : 0;
}
