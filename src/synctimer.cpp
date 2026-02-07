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

void SyncTimer::setTime(double seconds)
{
    if (!m_score || m_measureStarts.empty()) return;

    int measureIndex = findMeasureIndex(seconds);
    int pageIndex = 0;
    muse::RectF rect = resolveCursorRect(measureIndex, seconds, pageIndex);
    m_lastMeasureIndex = measureIndex;
    emit cursorRectChanged(rect, pageIndex);
}

int SyncTimer::findMeasureIndex(double seconds) const
{
    // Binary search for the measure containing this time
    auto it = std::upper_bound(m_measureStarts.begin(), m_measureStarts.end(), seconds);
    if (it == m_measureStarts.begin()) return 0;
    return static_cast<int>(std::distance(m_measureStarts.begin(), it) - 1);
}

muse::RectF SyncTimer::resolveCursorRect(int measureIndex, double seconds, int& outPageIndex) const
{
    outPageIndex = 0;
    if (!m_score) return muse::RectF();

    // Convert measure index to a tick position
    // Each measure in 3/4 time has 3 quarter notes = 3 * 480 ticks (at standard MIDI resolution)
    // MuseScore uses Fraction for ticks: 1 quarter note = Fraction(1,4)
    // A 3/4 measure spans Fraction(3,4)

    // Calculate the tick at the start of this measure
    Fraction measureTick = Fraction(measureIndex * m_beatsPerMeasure, 4);

    // Find how far we are within this measure (fractional position 0..1)
    double measureStartTime = (measureIndex < static_cast<int>(m_measureStarts.size()))
        ? m_measureStarts[measureIndex] : seconds;
    double measureEndTime = (measureIndex + 1 < static_cast<int>(m_measureStarts.size()))
        ? m_measureStarts[measureIndex + 1]
        : measureStartTime + 2.0; // fallback
    double fraction = 0.0;
    if (measureEndTime > measureStartTime) {
        fraction = (seconds - measureStartTime) / (measureEndTime - measureStartTime);
        fraction = std::clamp(fraction, 0.0, 1.0);
    }

    // Calculate the tick within this measure
    Fraction withinMeasure = Fraction(m_beatsPerMeasure, 4) * Fraction(static_cast<int>(fraction * 1000), 1000);
    Fraction tick = measureTick + withinMeasure;

    // Use MuseScore's tick2measureMM to find the measure
    const Measure* measure = m_score->tick2measureMM(tick);
    if (!measure) {
        // Try the first measure as fallback
        measure = m_score->tick2measureMM(Fraction(0, 1));
        if (!measure) return muse::RectF();
    }

    const System* system = measure->system();
    if (!system || !system->page() || system->staves().empty()) {
        return muse::RectF();
    }

    // Interpolate x position within the measure using segment positions
    // (Ported from PlaybackCursor::resolveCursorRectByTick)
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
        // Fallback: use the start of the measure
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
