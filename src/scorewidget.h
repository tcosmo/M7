#pragma once

#include <QWidget>
#include <QScrollArea>
#include <QGestureEvent>
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
    double maxPageWidthScore() const;

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

class TriggerLineOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit TriggerLineOverlay(QWidget* parent = nullptr);
    void setTriggerFraction(double fraction);
    void setVisible(bool visible);
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    double m_fraction = 0.60;
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
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void scrollToTop();
    void setOverlayWidth(int width);
    void applyTheme();

    void setAutoScrollEnabled(bool enabled);
    void setAutoScrollTrigger(double trigger);
    void setAutoScrollTarget(double target);
    void setShowTriggerLine(bool show);
    void setCursorAnchor(int anchor); // 0=Top, 1=Center, 2=Bottom

signals:
    void zoomChanged(double zoom);

public slots:
    void setCursorRect(const muse::RectF& rect, int pageIndex);

protected:
    bool event(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void ensureCursorVisible();
    void applyZoom(double newZoom);

    ScoreCanvas* m_canvas = nullptr;
    TriggerLineOverlay* m_triggerOverlay = nullptr;
    int m_overlayWidth = 0;
    bool m_autoScroll = true;
    double m_scrollTrigger = 0.60;
    double m_scrollTarget = 0.0;
    int m_cursorAnchor = 1; // 0=Top, 1=Center, 2=Bottom
};

} // namespace scoretracker
