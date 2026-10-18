// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QPointF>
#include <algorithm>
#include <cmath>

// Streaming quadratic B-spline over the REAL touch samples. Do not subdivide
// straight input chords before fitting: doing that leaves long flat sections
// and concentrates all turning into tiny fillets (visible creases on an arc).
// Dense output is generated only AFTER fitting. Consecutive curves share a
// tangent and stay in the input control triangle. Sharp, intentional corners
// receive a small local fillet instead of cutting across the written shape.
// Constant memory, no timer. The tip trails by half the latest input segment;
// finish() flushes to the exact endpoint. Pressure follows the same geometry.
class InkRounder
{
public:
    struct Sample { QPointF pos; qreal pressure; };
    void begin(QPointF pos, qreal pressure) { m_anchor = m_control = {pos, pressure}; }

    template<class Sink> void append(QPointF pos, qreal pressure, Sink sink)
    {
        if (!std::isfinite(pos.x()) || !std::isfinite(pos.y()) || !std::isfinite(pressure) ||
            std::abs(pos.x()) > 1e7 || std::abs(pos.y()) > 1e7) return;
        if (distance(m_control.pos, pos) < 0.0001) return;
        const Sample next {pos, pressure};
        const Sample midpoint = mix(m_control, next, 0.5);
        Sample a = m_anchor, c = midpoint;
        const QPointF incoming = m_control.pos - a.pos;
        const QPointF outgoing = c.pos - m_control.pos;
        const qreal inLength = distance(a.pos, m_control.pos);
        const qreal outLength = distance(m_control.pos, c.pos);
        // Protect written right angles and short hooks. Gentle arc samples use
        // the entire chord, so the tangent turns throughout the curve rather
        // than repeatedly alternating between a straight and a tiny corner.
        if (inLength > 0.0001 && outLength > 0.0001) {
            const qreal cosine = QPointF::dotProduct(incoming, outgoing) / (inLength * outLength);
            // Fade from full-arc fitting at 45 degrees to local rounding at
            // 75 degrees, avoiding a sudden strength change near a threshold.
            const qreal t = std::clamp((0.7071067812 - cosine) / 0.4482877361, 0.0, 1.0);
            const qreal blend = t * t * (3.0 - 2.0 * t);
            const qreal inExtent = inLength + (std::min(inLength, 3.0) - inLength) * blend;
            const qreal outExtent = outLength + (std::min(outLength, 3.0) - outLength) * blend;
            a = mix(m_control, a, inExtent / inLength);
            c = mix(m_control, c, outExtent / outLength);
            line(m_anchor, a, sink);
        }
        curve(a, m_control, c, sink);
        line(c, midpoint, sink);
        m_anchor = midpoint;
        m_control = next;
    }

    template<class Sink> void finish(Sink sink)
    {
        curve(m_anchor, m_control, m_control, sink);
        m_anchor = m_control;
    }

private:
    static qreal distance(QPointF a, QPointF b) { return std::hypot(a.x() - b.x(), a.y() - b.y()); }
    static Sample mix(Sample a, Sample b, qreal t)
    { return {a.pos + (b.pos - a.pos) * t, a.pressure + (b.pressure - a.pressure) * t}; }
    template<class Sink> static void line(Sample a, Sample b, Sink sink)
    {
        const qreal length = distance(a.pos, b.pos);
        if (length < 0.0001) return;
        const int steps = int(std::clamp(std::ceil(length), 1.0, 4096.0));
        for (int i = 1; i <= steps; ++i) {
            const Sample p = mix(a, b, qreal(i) / steps);
            sink(p.pos, p.pressure);
        }
    }
    template<class Sink> static void curve(Sample a, Sample b, Sample c, Sink sink)
    {
        const qreal length = distance(a.pos, b.pos) + distance(b.pos, c.pos);
        if (length < 0.0001) return;
        // Bound the quadratic's derivative, rather than only its arc length:
        // a short asymmetric segment can otherwise have a two-pixel last step.
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
