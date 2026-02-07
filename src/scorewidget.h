#pragma once

#include <QWidget>
#include <QScrollArea>
#include <memory>

#include "types/geometry.h"

namespace mu::engraving {
class Score;
namespace rendering {
class IScoreRenderer;
}
}

namespace scoretracker {

class ScoreCanvas : public QWidget
{
    Q_OBJECT

public:
    explicit ScoreCanvas(QWidget* parent = nullptr);

    void setScore(mu::engraving::Score* score);
    void setRenderer(mu::engraving::rendering::IScoreRenderer* renderer);
    void setCursorRect(const muse::RectF& rect, int pageIndex);
    void setZoom(double zoom);
    double zoom() const { return m_zoom; }
    double scale() const;

    QRect cursorWidgetRect() const;
    QRect pageWidgetRect(int pageIndex) const;
    int cursorPageIndex() const { return m_cursorPageIndex; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void updateCanvasSize();
    muse::RectF mapToRenderCoords(const muse::RectF& pageRelRect, int pageIndex) const;

    mu::engraving::Score* m_score = nullptr;
    mu::engraving::rendering::IScoreRenderer* m_renderer = nullptr;
    muse::RectF m_cursorRect;
    int m_cursorPageIndex = 0;
    double m_zoom = 1.0;
};

class ScoreWidget : public QScrollArea
{
    Q_OBJECT

public:
    explicit ScoreWidget(QWidget* parent = nullptr);

    void setScore(mu::engraving::Score* score);
    void setRenderer(mu::engraving::rendering::IScoreRenderer* renderer);
    void setZoom(double zoom);
    double zoom() const;
    void setFollowCursor(bool follow);

public slots:
    void setCursorRect(const muse::RectF& rect, int pageIndex);
    void ensureCursorVisible();

protected:
    void wheelEvent(QWheelEvent* event) override;

private:
    ScoreCanvas* m_canvas = nullptr;
    bool m_followCursor = true;
};

} // namespace scoretracker
