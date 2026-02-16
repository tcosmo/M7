#pragma once

#include <QObject>
#include <vector>

#include "types/geometry.h"
#include "engraving/types/fraction.h"

namespace mu::engraving {
class Score;
}

namespace scoretracker {

class SyncTimer : public QObject
{
    Q_OBJECT

public:
    explicit SyncTimer(QObject* parent = nullptr);

    void setScore(mu::engraving::Score* score);
    void setMeasureStarts(const std::vector<double>& measureStarts);
    void setBeatTimes(const std::vector<double>& beatTimes, int beatsPerMeasure);
    void setBeatTicks(const std::vector<int>& beatTicks);
    void setMeasureIndices(const std::vector<int>& indices);
    int beatsPerMeasure() const { return m_beatsPerMeasure; }
    const std::vector<double>& beatTimes() const { return m_beatTimes; }
    const std::vector<double>& measureStarts() const { return m_measureStarts; }
    const std::vector<int>& beatTicks() const { return m_beatTicks; }
    const std::vector<int>& measureIndices() const { return m_measureIndices; }

public slots:
    void setTime(double seconds);
    void refresh();

signals:
    void cursorRectChanged(const muse::RectF& rect, int pageIndex);

private:
    muse::RectF resolveCursorRect(const mu::engraving::Fraction& tick, int& outPageIndex) const;

    mu::engraving::Score* m_score = nullptr;
    std::vector<double> m_measureStarts;
    std::vector<double> m_beatTimes;
    std::vector<int> m_beatTicks;        // tick position for each entry in m_beatTimes
    std::vector<int> m_measureIndices;   // real score measure index for each entry in m_measureStarts
    int m_beatsPerMeasure = 3;
    double m_lastTime = 0.0;
};

} // namespace scoretracker
