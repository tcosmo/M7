#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <vector>

namespace mu::engraving {
class MasterScore;
class Part;
class Staff;
namespace rendering {
class IScoreRenderer;
}
}

namespace scoretracker {

struct BeatSync {
    int beatIndex = 0;       // global beat index across the whole score
    int measureIndex = 0;    // which measure this beat belongs to
    int beatInMeasure = 0;   // beat position within the measure (0-based)
    double tappedTime = 0.0;
    double timeOffset = 0.0;
    bool synced = false;

    double effectiveTime() const { return tappedTime + timeOffset; }
};

class SyncMode : public QObject
{
    Q_OBJECT

public:
    explicit SyncMode(QObject* parent = nullptr);

    void enter(mu::engraving::MasterScore* score,
               mu::engraving::rendering::IScoreRenderer* renderer);
    void exit();
    bool isActive() const { return m_active; }

    void recordTap(double audioTime);
    void adjustBeat(int beatIndex, double timeDelta);
    void unsyncBeat(int beatIndex);
    void setNextUnsyncedFrom(int beatIndex);
    void clearNextInputOverride();

    int nextUnsyncedBeat() const;
    int syncedCount() const;
    int totalBeats() const { return static_cast<int>(m_beats.size()); }
    int totalMeasures() const { return static_cast<int>(m_beatsPerMeasure.size()); }
    const std::vector<BeatSync>& beats() const { return m_beats; }
    const std::vector<int>& beatsPerMeasure() const { return m_beatsPerMeasure; }
    int beatsInMeasure(int measureIdx) const;

    int syncStaffIdx() const;
    int activeBeatForTime(double time) const;

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& obj);

signals:
    void beatSynced(int beatIndex);
    void beatAdjusted(int beatIndex);
    void beatUnsynced(int beatIndex);
    void entered();
    void exited();

private:
    void createSyncPart();
    void removeSyncPart();
    void populateRests();

    mu::engraving::MasterScore* m_score = nullptr;
    mu::engraving::rendering::IScoreRenderer* m_renderer = nullptr;
    mu::engraving::Part* m_syncPart = nullptr;
    mu::engraving::Staff* m_syncStaff = nullptr;

    std::vector<BeatSync> m_beats;
    std::vector<int> m_beatsPerMeasure;  // per-measure beat counts
    bool m_active = false;
    int m_nextInputOverride = -1;  // if >= 0, forces nextUnsyncedBeat to return this
};

} // namespace scoretracker
