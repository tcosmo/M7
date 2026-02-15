#pragma once

#include <QObject>
#include <vector>

#include "types/geometry.h"

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
    void setMeasureIndices(const std::vector<int>& indices);
    int beatsPerMeasure() const { return m_beatsPerMeasure; }
    const std::vector<double>& beatTimes() const { return m_beatTimes; }
    const std::vector<double>& measureStarts() const { return m_measureStarts; }
    const std::vector<int>& measureIndices() const { return m_measureIndices; }

public slots:
    void setTime(double seconds);
    void refresh();

signals:
    void cursorRectChanged(const muse::RectF& rect, int pageIndex);

private:
    int findMeasureIndex(double seconds) const;
    muse::RectF resolveCursorRect(int measureIndex, double seconds, int& outPageIndex) const;

    mu::engraving::Score* m_score = nullptr;
    std::vector<double> m_measureStarts;
    std::vector<double> m_beatTimes;
    std::vector<int> m_measureIndices; // real score measure index for each entry in m_measureStarts
    int m_beatsPerMeasure = 3;
    int m_lastMeasureIndex = -1;
    double m_lastTime = 0.0;
};

} // namespace scoretracker
