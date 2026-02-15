#include "syncmode.h"

#include "engraving/dom/masterscore.h"
#include "engraving/dom/part.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/rest.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/mscore.h"
#include "engraving/dom/stafflines.h"
#include "engraving/rendering/iscorerenderer.h"
#include "engraving/types/fraction.h"

#include <QDebug>

using namespace mu::engraving;
using namespace mu::engraving::rendering;

namespace scoretracker {

SyncMode::SyncMode(QObject* parent)
    : QObject(parent)
{
}

void SyncMode::enter(MasterScore* score, IScoreRenderer* renderer)
{
    if (m_active) return;

    m_score = score;
    m_renderer = renderer;

    // Derive beats per measure from each measure's time signature
    m_beatsPerMeasure.clear();
    m_beats.clear();

    int globalBeat = 0;
    int measureIdx = 0;
    for (Measure* m = m_score->firstMeasure(); m; m = m->nextMeasure(), ++measureIdx) {
        int bpm = m->timesig().numerator();
        m_beatsPerMeasure.push_back(bpm);

        for (int b = 0; b < bpm; ++b, ++globalBeat) {
            BeatSync bs;
            bs.beatIndex = globalBeat;
            bs.measureIndex = measureIdx;
            bs.beatInMeasure = b;
            m_beats.push_back(bs);
        }
    }

    createSyncPart();
    m_active = true;
    emit entered();
}

void SyncMode::exit()
{
    if (!m_active) return;

    removeSyncPart();
    m_beats.clear();
    m_beatsPerMeasure.clear();
    m_active = false;
    m_syncPart = nullptr;
    m_syncStaff = nullptr;
    emit exited();
}

void SyncMode::recordTap(double audioTime)
{
    int idx = nextUnsyncedBeat();
    if (idx < 0) return;

    m_beats[idx].tappedTime = audioTime;
    m_beats[idx].timeOffset = 0.0;
    m_beats[idx].synced = true;

    // Advance to the next immediate beat
    m_nextInputOverride = (idx + 1 < static_cast<int>(m_beats.size())) ? idx + 1 : -1;

    emit beatSynced(idx);
}

void SyncMode::adjustBeat(int beatIndex, double timeDelta)
{
    if (beatIndex < 0 || beatIndex >= static_cast<int>(m_beats.size())) return;
    if (!m_beats[beatIndex].synced) return;

    m_beats[beatIndex].timeOffset += timeDelta;
    emit beatAdjusted(beatIndex);
}

void SyncMode::unsyncBeat(int beatIndex)
{
    if (beatIndex < 0 || beatIndex >= static_cast<int>(m_beats.size())) return;
    if (!m_beats[beatIndex].synced) return;

    m_beats[beatIndex].synced = false;
    m_beats[beatIndex].tappedTime = 0.0;
    m_beats[beatIndex].timeOffset = 0.0;
    emit beatUnsynced(beatIndex);
}

void SyncMode::setNextUnsyncedFrom(int beatIndex)
{
    if (beatIndex < 0 || beatIndex >= static_cast<int>(m_beats.size())) return;

    m_nextInputOverride = beatIndex;
}

int SyncMode::nextUnsyncedBeat() const
{
    if (m_nextInputOverride >= 0
        && m_nextInputOverride < static_cast<int>(m_beats.size())) {
        return m_nextInputOverride;
    }
    for (size_t i = 0; i < m_beats.size(); ++i) {
        if (!m_beats[i].synced) return static_cast<int>(i);
    }
    return -1;
}

int SyncMode::syncedCount() const
{
    int count = 0;
    for (const auto& b : m_beats) {
        if (b.synced) ++count;
    }
    return count;
}

int SyncMode::beatsInMeasure(int measureIdx) const
{
    if (measureIdx < 0 || measureIdx >= static_cast<int>(m_beatsPerMeasure.size())) return 0;
    return m_beatsPerMeasure[measureIdx];
}

int SyncMode::syncStaffIdx() const
{
    if (!m_syncStaff || !m_score) return -1;
    return static_cast<int>(m_syncStaff->idx());
}

int SyncMode::activeBeatForTime(double time) const
{
    int result = -1;
    for (size_t i = 0; i < m_beats.size(); ++i) {
        if (m_beats[i].synced && m_beats[i].effectiveTime() <= time) {
            result = static_cast<int>(i);
        }
    }
    // Only show if there's a synced beat ahead — means we're reviewing,
    // not at the input frontier
    if (result >= 0) {
        for (size_t i = result + 1; i < m_beats.size(); ++i) {
            if (m_beats[i].synced) return result;
        }
        return -1;
    }
    return result;
}

void SyncMode::createSyncPart()
{
    if (!m_score) return;

    m_syncPart = new Part(m_score);
    m_syncPart->setPartName(u"Sync");
    m_syncPart->instrument()->setLongName(muse::String(u"Sync"));
    m_syncPart->instrument()->setShortName(muse::String(u"Sync"));
    m_syncPart->setShow(true);

    m_syncStaff = Factory::createStaff(m_syncPart);
    m_syncStaff->setHideWhenEmpty(mu::engraving::AutoOnOff::OFF);

    m_score->appendPart(m_syncPart);
    m_score->insertStaff(m_syncStaff, 0);

    // Expand segment element lists and create MStaff objects for the new staff
    staff_idx_t absIdx = m_syncStaff->idx();
    for (Measure* m = m_score->firstMeasure(); m; m = m->nextMeasure()) {
        m->insertStaves(absIdx, absIdx + 1);

        MStaff* ms = new MStaff;
        ms->setLines(Factory::createStaffLines(m));
        ms->lines()->setTrack(absIdx * VOICES);
        ms->lines()->setParent(m);
        ms->lines()->setVisible(true);
        m->insertMStaff(ms, absIdx);
    }

    populateRests();

    m_renderer->layoutScore(m_score, Fraction(0, 1), Fraction(-1, 1));
}

void SyncMode::removeSyncPart()
{
    if (!m_score || !m_syncPart) return;

    staff_idx_t absIdx = m_syncStaff->idx();

    // Remove MStaff objects and shrink segment element lists for the removed staff
    for (Measure* m = m_score->firstMeasure(); m; m = m->nextMeasure()) {
        m->removeMStaff(nullptr, absIdx);
        m->removeStaves(absIdx, absIdx + 1);
    }

    m_score->removeStaff(m_syncStaff);
    m_score->removePart(m_syncPart);

    m_renderer->layoutScore(m_score, Fraction(0, 1), Fraction(-1, 1));
}

void SyncMode::populateRests()
{
    if (!m_score || !m_syncStaff) return;

    track_idx_t track = staff2track(m_syncStaff->idx());

    int beatIdx = 0;
    int measureIdx = 0;
    for (Measure* measure = m_score->firstMeasure();
         measure && beatIdx < static_cast<int>(m_beats.size());
         measure = measure->nextMeasure(), ++measureIdx) {

        int bpm = (measureIdx < static_cast<int>(m_beatsPerMeasure.size()))
                  ? m_beatsPerMeasure[measureIdx] : 0;

        for (int b = 0; b < bpm && beatIdx < static_cast<int>(m_beats.size()); ++b, ++beatIdx) {
            Fraction beatTick = measure->tick() + Fraction(b, 4);
            Segment* seg = measure->getSegment(SegmentType::ChordRest, beatTick);

            Rest* rest = Factory::createRest(seg);
            rest->setDurationType(DurationType::V_QUARTER);
            rest->setTicks(Fraction(1, 4));
            rest->setTrack(track);
            seg->add(rest);
        }
    }
}

QJsonObject SyncMode::toJson() const
{
    QJsonObject obj;

    // Check if all measures have the same beat count
    bool uniform = true;
    int firstBpm = m_beatsPerMeasure.empty() ? 0 : m_beatsPerMeasure[0];
    for (int bpm : m_beatsPerMeasure) {
        if (bpm != firstBpm) { uniform = false; break; }
    }

    if (uniform && firstBpm > 0) {
        obj["beats_per_measure"] = firstBpm;
    } else {
        obj["beats_per_measure"] = QJsonValue(); // null
    }

    // Group beats by measure
    QJsonArray measuresArr;
    int beatIdx = 0;
    for (size_t mi = 0; mi < m_beatsPerMeasure.size(); ++mi) {
        QJsonObject measureObj;
        QJsonArray beatsArr;

        int bpm = m_beatsPerMeasure[mi];
        for (int b = 0; b < bpm && beatIdx < static_cast<int>(m_beats.size()); ++b, ++beatIdx) {
            if (m_beats[beatIdx].synced) {
                QJsonObject beatObj;
                beatObj["beat"] = b;
                beatObj["time"] = m_beats[beatIdx].effectiveTime();
                beatsArr.append(beatObj);
            }
        }

        measureObj["beats"] = beatsArr;
        measuresArr.append(measureObj);
    }

    obj["measures"] = measuresArr;
    return obj;
}

void SyncMode::fromJson(const QJsonObject& obj)
{
    QJsonArray measuresArr = obj.value("measures").toArray();

    int globalBeat = 0;
    for (int mi = 0; mi < measuresArr.size() && globalBeat < static_cast<int>(m_beats.size()); ++mi) {
        QJsonArray beatsArr = measuresArr[mi].toObject().value("beats").toArray();
        for (const auto& val : beatsArr) {
            QJsonObject beatObj = val.toObject();
            int beatInMeasure = beatObj.value("beat").toInt(-1);

            // Find the matching beat in this measure
            for (int bi = globalBeat;
                 bi < static_cast<int>(m_beats.size()) && m_beats[bi].measureIndex == mi;
                 ++bi) {
                if (m_beats[bi].beatInMeasure == beatInMeasure) {
                    m_beats[bi].tappedTime = beatObj.value("time").toDouble();
                    m_beats[bi].synced = true;
                    break;
                }
            }
        }

        // Advance globalBeat past this measure
        int bpm = (mi < static_cast<int>(m_beatsPerMeasure.size())) ? m_beatsPerMeasure[mi] : 0;
        globalBeat += bpm;
    }
}

} // namespace scoretracker
