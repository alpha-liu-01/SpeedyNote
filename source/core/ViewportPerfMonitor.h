#pragma once

// ============================================================================
// ViewportPerfMonitor - Low-overhead paint instrumentation for DocumentViewport
// ============================================================================
// Records per-frame paint duration, painted area and which paint path ran, so
// the debug overlay can distinguish three very different workloads that all
// used to show up as a single "paints per second" number:
//
//   - panning/zooming    (whole viewport, gesture fast path, expensive)
//   - full re-composite  (whole viewport, PDF + stroke caches + overlays)
//   - live stroke        (tiny dirty rect, hundreds per second, cheap)
//
// Counting paintEvent calls conflates these: drawing produces ~240 tiny paints
// per second while a pan produces ~10 full-screen paints per second. Reporting
// them per bucket - together with the time each frame actually took - is what
// makes the numbers comparable across devices and resolutions.
//
// Overhead when enabled is two QElapsedTimer::nsecsElapsed() calls plus one
// write into a fixed ring buffer; there is no allocation on the paint path.
// When disabled the cost is a single branch.
//
// This header is intentionally dependency-free (no QObject, no signals) so it
// can be embedded by value and compiled into release builds.
// ============================================================================

#include <QElapsedTimer>
#include <QRect>
#include <QSize>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cstdint>

/**
 * @brief Rolling-window paint statistics for a single viewport.
 *
 * Usage: enable it, then wrap each paintEvent body in a FrameSampler and tag
 * the sampler with the path that was taken. Query with stats().
 */
class ViewportPerfMonitor {
public:
    /** @brief Which branch of DocumentViewport::paintEvent produced the frame. */
    enum class FramePath : quint8 {
        Full = 0,           ///< Normal layered re-composite
        GesturePan,         ///< Gesture fast path: cached frame shifted
        GestureZoom,        ///< Gesture fast path: cached frame scaled
        SelectionTransform, ///< Selection drag/resize fast path
        ObjectDrag          ///< Object drag fast path
    };

    /** @brief Frame groupings that stats() can be queried for. */
    enum class Bucket : quint8 {
        PanZoom = 0, ///< Gesture pan/zoom fast-path frames
        Composite,   ///< Non-gesture frames covering more than half the viewport
        Partial,     ///< Non-gesture frames covering half the viewport or less
        All          ///< Every recorded frame (must stay last: see BucketCount)
    };

    /** @brief Aggregated statistics over the rolling window. */
    struct Stats {
        int   frames         = 0;   ///< Frames matched in the window
        qreal fps            = 0.0; ///< Derived from mean active interval
        qreal meanMs         = 0.0; ///< Mean time inside paintEvent
        qreal p95Ms          = 0.0; ///< 95th percentile paint time
        qreal maxMs          = 0.0; ///< Worst paint time
        qreal meanIntervalMs = 0.0; ///< Mean gap between consecutive frame starts
        qreal residualMs     = 0.0; ///< meanIntervalMs - meanMs (flush + event loop)
        qreal mpixPerSec     = 0.0; ///< Fill rate in megapixels/second
        qreal mpixPerFrame   = 0.0; ///< Mean painted megapixels per frame

        bool isValid() const { return frames > 0; }
        /** @brief True when paintEvent accounts for most of the frame period. */
        bool isPaintBound() const { return meanIntervalMs > 0.0 && meanMs >= 0.7 * meanIntervalMs; }
    };

    /** @brief Length of the rolling statistics window. */
    static constexpr qint64 WindowMs = 1000;

    /**
     * @brief Gaps longer than this are treated as idle, not as slow frames.
     *
     * Without this, pausing between two pans would be averaged in as one
     * enormous frame interval and the reported fps would be meaningless.
     */
    static constexpr qint64 MaxActiveGapMs = 250;

    // ===== Control =====

    void setEnabled(bool enabled)
    {
        if (m_enabled == enabled) {
            return;
        }
        m_enabled = enabled;
        reset();
        if (enabled) {
            m_timer.start();
        }
    }

    bool isEnabled() const { return m_enabled; }

    /** @brief Discard all recorded samples. */
    void reset()
    {
        m_count = 0;
        m_head = 0;
        m_lastStartNs = -1;
        m_lastStartByBucket.fill(-1);
    }

    // ===== Query =====

    /**
     * @brief Aggregate the samples in the rolling window for one bucket.
     *
     * Safe to call at any rate; cost is a scan of at most RingSize samples.
     */
    Stats stats(Bucket bucket) const
    {
        Stats out;
        if (!m_enabled || m_count == 0) {
            return out;
        }

        const qint64 now = nowNs();
        const qint64 windowNs = WindowMs * 1000000LL;
        const qint32 maxGapUs = static_cast<qint32>(MaxActiveGapMs * 1000LL);
        const bool wantAll = (bucket == Bucket::All);

        // Durations are collected so a percentile can be taken. Stack-only.
        std::array<qint32, RingSize> durations{};
        int n = 0;

        qreal durSumUs = 0.0;
        qreal areaSumKpx = 0.0;
        qint32 maxDurUs = 0;

        qreal gapSumUs = 0.0;
        int gapCount = 0;

        for (quint32 i = 0; i < m_count; ++i) {
            const Sample& s = m_ring[i];
            if (now - s.startNs > windowNs) {
                continue;
            }
            if (!wantAll && s.bucket != bucket) {
                continue;
            }

            durations[n++] = s.durUs;
            durSumUs += s.durUs;
            areaSumKpx += s.areaKpx;
            maxDurUs = std::max(maxDurUs, s.durUs);

            // A frame rate is only meaningful between two frames of the same
            // kind, and only while the viewport was continuously busy.
            const qint32 gap = wantAll ? s.gapAllUs : s.gapUs;
            if (gap > 0 && gap < maxGapUs) {
                gapSumUs += gap;
                ++gapCount;
            }
        }

        if (n == 0) {
            return out;
        }

        out.frames = n;
        out.meanMs = (durSumUs / n) / 1000.0;
        out.maxMs = maxDurUs / 1000.0;
        out.mpixPerFrame = (areaSumKpx / n) / 1000.0;

        // p95 via partial sort - cheap at these sample counts.
        const int idx = std::min(n - 1, static_cast<int>(n * 0.95));
        std::nth_element(durations.begin(), durations.begin() + idx, durations.begin() + n);
        out.p95Ms = durations[idx] / 1000.0;

        if (gapCount > 0) {
            out.meanIntervalMs = (gapSumUs / gapCount) / 1000.0;
            if (out.meanIntervalMs > 0.0) {
                out.fps = 1000.0 / out.meanIntervalMs;
            }
            out.residualMs = std::max(0.0, out.meanIntervalMs - out.meanMs);
            out.mpixPerSec = out.mpixPerFrame * out.fps;
        }

        return out;
    }

    // ===== Frame recording =====

    /**
     * @brief Scope guard that times one paintEvent.
     *
     * Declare this *before* the QPainter in paintEvent so the painter is
     * destroyed first and its pending work is flushed before the duration is
     * taken. Because it records from the destructor, every early return out of
     * paintEvent is measured without needing an exit hook on each branch.
     */
    class FrameSampler {
    public:
        FrameSampler(ViewportPerfMonitor& monitor,
                     const QRect& dirtyRect,
                     const QSize& viewportSize,
                     qreal devicePixelRatio)
            : m_monitor(monitor.m_enabled ? &monitor : nullptr)
        {
            if (!m_monitor) {
                return;
            }

            const double dprSq = devicePixelRatio * devicePixelRatio;
            m_areaPhysPx = static_cast<quint64>(
                std::max(0.0, dirtyRect.width() * static_cast<double>(dirtyRect.height()) * dprSq));
            m_viewportAreaPhysPx = static_cast<quint64>(
                std::max(0.0, viewportSize.width() * static_cast<double>(viewportSize.height()) * dprSq));

            m_startNs = m_monitor->nowNs();
        }

        ~FrameSampler()
        {
            if (!m_monitor) {
                return;
            }
            m_monitor->recordFrame(m_startNs,
                                   m_monitor->nowNs() - m_startNs,
                                   m_areaPhysPx,
                                   m_viewportAreaPhysPx,
                                   m_path);
        }

        FrameSampler(const FrameSampler&) = delete;
        FrameSampler& operator=(const FrameSampler&) = delete;

        /** @brief Tag which paintEvent branch handled this frame. */
        void setPath(FramePath path) { m_path = path; }

    private:
        ViewportPerfMonitor* m_monitor = nullptr;
        qint64 m_startNs = 0;
        quint64 m_areaPhysPx = 0;
        quint64 m_viewportAreaPhysPx = 0;
        FramePath m_path = FramePath::Full;
    };

private:
    /** @brief Number of frames retained. ~1s at 240Hz, ~25s at 10Hz. */
    static constexpr quint32 RingSize = 256;

    /** @brief Number of concrete buckets a sample can be assigned to. */
    static constexpr std::size_t BucketCount = static_cast<std::size_t>(Bucket::All);

    struct Sample {
        qint64 startNs = 0;    ///< Monotonic start time of the frame
        qint32 durUs = 0;      ///< Time spent inside paintEvent
        qint32 gapUs = -1;     ///< Since previous frame in the same bucket
        qint32 gapAllUs = -1;  ///< Since previous frame of any bucket
        quint32 areaKpx = 0;   ///< Painted physical pixels / 1000
        Bucket bucket = Bucket::Partial;
        FramePath path = FramePath::Full;
    };

    qint64 nowNs() const { return m_timer.isValid() ? m_timer.nsecsElapsed() : 0; }

    static qint32 gapMicros(qint64 startNs, qint64 previousStartNs)
    {
        if (previousStartNs < 0) {
            return -1;
        }
        return static_cast<qint32>(std::min<qint64>((startNs - previousStartNs) / 1000, INT32_MAX));
    }

    void recordFrame(qint64 startNs,
                     qint64 durationNs,
                     quint64 areaPhysPx,
                     quint64 viewportAreaPhysPx,
                     FramePath path)
    {
        const quint64 coverPct = (viewportAreaPhysPx > 0)
                                     ? std::min<quint64>(100, areaPhysPx * 100 / viewportAreaPhysPx)
                                     : 0;

        Sample s;
        s.startNs = startNs;
        s.durUs = static_cast<qint32>(std::min<qint64>(durationNs / 1000, INT32_MAX));
        s.areaKpx = static_cast<quint32>(std::min<quint64>(areaPhysPx / 1000, UINT32_MAX));
        s.path = path;
        s.bucket = (path == FramePath::GesturePan || path == FramePath::GestureZoom)
                       ? Bucket::PanZoom
                       : ((coverPct > 50) ? Bucket::Composite : Bucket::Partial);

        const auto bucketIdx = static_cast<std::size_t>(s.bucket);
        s.gapUs = gapMicros(startNs, m_lastStartByBucket[bucketIdx]);
        s.gapAllUs = gapMicros(startNs, m_lastStartNs);

        m_ring[m_head] = s;
        m_head = (m_head + 1) % RingSize;
        if (m_count < RingSize) {
            ++m_count;
        }
        m_lastStartNs = startNs;
        m_lastStartByBucket[bucketIdx] = startNs;
    }

    bool m_enabled = false;
    QElapsedTimer m_timer;
    std::array<Sample, RingSize> m_ring{};
    quint32 m_head = 0;
    quint32 m_count = 0;
    qint64 m_lastStartNs = -1;
    std::array<qint64, BucketCount> m_lastStartByBucket{{-1, -1, -1}};
};
