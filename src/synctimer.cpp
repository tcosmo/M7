#include "synctimer.h"

#include "engraving/dom/score.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/system.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/page.h"
#include "engraving/types/fraction.h"

#include <algorithm>

using namespace mu::engraving;

namespace scoretracker {

SyncTimer::SyncTimer(QObject* parent)
    : QObject(parent)
{
}

void SyncTimer::setScore(Score* score)
{
    m_score = score;
}

void SyncTimer::setMeasureStarts(const std::vector<double>& measureStarts)
{
    m_measureStarts = measureStarts;
}

void SyncTimer::setBeatTimes(const std::vector<double>& beatTimes, int beatsPerMeasure)
{
    m_beatTimes = beatTimes;
    m_beatsPerMeasure = beatsPerMeasure;
}

void SyncTimer::setBeatTicks(const std::vector<int>& beatTicks)
{
    m_beatTicks = beatTicks;
}

void SyncTimer::setMeasureIndices(const std::vector<int>& indices)
{
    m_measureIndices = indices;
}

void SyncTimer::setTime(double seconds)
{
    m_lastTime = seconds;
    if (!m_score || m_beatTimes.empty() || m_beatTicks.empty()) return;

    // If time is before the first beat, snap cursor to the first beat
    if (seconds < m_beatTimes.front()) {
        Fraction tick = Fraction::fromTicks(m_beatTicks.front());
        int pageIndex = 0;
        muse::RectF rect = resolveCursorRect(tick, pageIndex);
        emit cursorRectChanged(rect, pageIndex);
        return;
    }

    // If time is after the last beat, keep cursor at last position
    if (seconds > m_beatTimes.back()) return;

    // Find surrounding beats via binary search
    auto it = std::upper_bound(m_beatTimes.begin(), m_beatTimes.end(), seconds);
    if (it == m_beatTimes.begin()) return;

    size_t nextIdx = static_cast<size_t>(it - m_beatTimes.begin());
    size_t prevIdx = nextIdx - 1;

    // In sync mode (m_measureIndices non-empty), check for gaps from unsynced holes
    if (!m_measureIndices.empty() && nextIdx < m_beatTimes.size()) {
        double prevBeat = m_beatTimes[prevIdx];
        double nextBeat = m_beatTimes[nextIdx];
        double gap = nextBeat - prevBeat;

        double totalSpan = m_beatTimes.back() - m_beatTimes.front();
        double avgSpacing = totalSpan / static_cast<double>(m_beatTimes.size() - 1);

        if (gap > avgSpacing * 3.0 && seconds > prevBeat + avgSpacing) return;
    }

    // Interpolate tick position between surrounding beats
    double prevTime = m_beatTimes[prevIdx];
    int prevTick = m_beatTicks[prevIdx];
    int nextTick;
    double nextTime;

    if (nextIdx < m_beatTimes.size()) {
        nextTime = m_beatTimes[nextIdx];
        nextTick = m_beatTicks[nextIdx];
    } else {
        // Past the last beat — extrapolate one beat forward
        nextTime = prevTime + 1.0;
        nextTick = prevTick + 480; // one quarter note
    }

    double t = (nextTime > prevTime) ? (seconds - prevTime) / (nextTime - prevTime) : 0.0;
    t = std::clamp(t, 0.0, 1.0);

    int interpTick = prevTick + static_cast<int>(t * (nextTick - prevTick));
    m_lastTick = interpTick;
    Fraction tick = Fraction::fromTicks(interpTick);

    int pageIndex = 0;
    muse::RectF rect = resolveCursorRect(tick, pageIndex);
    emit cursorRectChanged(rect, pageIndex);
}

void SyncTimer::refresh()
{
    setTime(m_lastTime);
}

muse::RectF SyncTimer::resolveCursorRect(const Fraction& tick, int& outPageIndex) const
{
    outPageIndex = 0;
    if (!m_score) return muse::RectF();

    // Use MuseScore's tick2measureMM to find the measure
    const Measure* measure = m_score->tick2measureMM(tick);
    if (!measure) {
        measure = m_score->tick2measureMM(Fraction(0, 1));
        if (!measure) return muse::RectF();
    }

    const System* system = measure->system();
    if (!system || !system->page() || system->staves().empty()) {
        return muse::RectF();
    }

    // Interpolate x position within the measure using segment positions
    double x = 0.0;
    Segment* s = nullptr;
    for (s = measure->first(SegmentType::ChordRest); s;) {
        Fraction t1 = s->tick();
        double x1 = s->canvasPos().x();
        double x2 = 0.0;
        Fraction t2;

        Segment* ns = s->next(SegmentType::ChordRest);
        while (ns && !ns->visible()) {
            ns = ns->next(SegmentType::ChordRest);
        }

        if (ns) {
            t2 = ns->tick();
            x2 = ns->canvasPos().x();
        } else {
            t2 = measure->endTick();
            const Segment* seg = measure->findSegment(SegmentType::EndBarLine, measure->endTick());
            if (seg) {
                x2 = seg->canvasPos().x();
            } else {
                x2 = measure->canvasPos().x() + measure->width();
            }
        }

        if (tick >= t1 && tick < t2) {
            Fraction dt = t2 - t1;
            double dx = x2 - x1;
            x = x1 + dx * (tick - t1).ticks() / dt.ticks();
            break;
        }
        s = ns;
    }

    if (!s) {
        x = measure->canvasPos().x();
    }

    const double spatium = m_score->style().spatium();
    x -= spatium;

    const double y = system->staffCanvasYpage(0) - 3 * spatium;
    const double w = 0.4 * spatium;

    // Set cursor height for whole system
    double y2 = 0.0;
    for (size_t i = 0; i < m_score->nstaves(); ++i) {
        SysStaff* ss = system->staff(i);
        if (!ss->show() || !m_score->staff(i)->show()) {
            continue;
        }
        y2 = ss->bbox().bottom();
    }

    const double h = y2 + 6 * spatium;

    // Convert from canvas coordinates to page-relative coordinates
    const Page* cursorPage = system->page();
    outPageIndex = cursorPage ? cursorPage->no() : 0;
    double pageX = cursorPage ? cursorPage->pos().x() : 0;
    double pageY = cursorPage ? cursorPage->pos().y() : 0;

    return muse::RectF(x - pageX, y - pageY, w, h);
}

} // namespace scoretracker
