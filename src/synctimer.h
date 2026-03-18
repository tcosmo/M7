#pragma once

#include <QObject>
#include <vector>

#include <QRectF>

#ifdef USE_MUSESCORE
#include "engraving/types/fraction.h"
#endif

#ifdef USE_MUSESCORE
namespace mu::engraving {
class Score;
}
#endif

namespace scoretracker {
class ScoreEngine;
}

namespace scoretracker {

class SyncTimer : public QObject
{
    Q_OBJECT

public:
    explicit SyncTimer(QObject* parent = nullptr);

#ifdef USE_MUSESCORE
    void setScore(mu::engraving::Score* score);
#endif
    void setEngine(scoretracker::ScoreEngine* engine);
    void setMeasureStarts(const std::vector<double>& measureStarts);
    void setBeatTimes(const std::vector<double>& beatTimes, int beatsPerMeasure);
    void setBeatTicks(const std::vector<int>& beatTicks);
    void setMeasureIndices(const std::vector<int>& indices);
    int currentTick() const { return m_lastTick; }
    int beatsPerMeasure() const { return m_beatsPerMeasure; }
    const std::vector<double>& beatTimes() const { return m_beatTimes; }
    const std::vector<double>& measureStarts() const { return m_measureStarts; }
    const std::vector<int>& beatTicks() const { return m_beatTicks; }
    const std::vector<int>& measureIndices() const { return m_measureIndices; }

public slots:
    void setTime(double seconds);
    void refresh();

signals:
    void cursorRectChanged(const QRectF& rect, int pageIndex);

private:
#ifdef USE_MUSESCORE
    QRectF resolveCursorRect(const mu::engraving::Fraction& tick, int& outPageIndex) const;
#endif

#ifdef USE_MUSESCORE
    mu::engraving::Score* m_score = nullptr;
#endif
    scoretracker::ScoreEngine* m_engine = nullptr;
    std::vector<double> m_measureStarts;
    std::vector<double> m_beatTimes;
    std::vector<int> m_beatTicks;        // tick position for each entry in m_beatTimes
    std::vector<int> m_measureIndices;   // real score measure index for each entry in m_measureStarts
    int m_beatsPerMeasure = 3;
    double m_lastTime = 0.0;
    int m_lastTick = 0;
};

} // namespace scoretracker
