#include "scorewidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QGestureEvent>
#include <QPinchGesture>
#include <QDebug>

#include "draw/painter.h"
#include "draw/internal/qpainterprovider.h"
#include "engraving/rendering/iscorerenderer.h"
#include "engraving/rendering/paintoptions.h"
#include "engraving/dom/score.h"
#include "engraving/dom/page.h"

using namespace mu::engraving;
using namespace mu::engraving::rendering;
using namespace muse::draw;

namespace scoretracker {

static const int PAGE_GAP = 10; // pixels between pages

// --- ScoreCanvas ---

ScoreCanvas::ScoreCanvas(QWidget* parent)
    : QWidget(parent)
{
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(200, 200, 200));
    setPalette(pal);
}

void ScoreCanvas::setScore(Score* score)
{
    m_score = score;
    updateCanvasSize();
    update();
}

void ScoreCanvas::setRenderer(IScoreRenderer* renderer)
{
    m_renderer = renderer;
    update();
}

void ScoreCanvas::setCursorRect(const muse::RectF& rect, int pageIndex)
{
    if (pageIndex < 0 || rect.isNull()) {
        m_cursorRect = muse::RectF();
        update();
        return;
    }
    m_cursorPageIndex = pageIndex;
    m_cursorRect = mapToRenderCoords(rect, pageIndex);
    update();
}

void ScoreCanvas::setZoom(double zoom)
{
    m_zoom = zoom;
    updateCanvasSize();
    update();
}

double ScoreCanvas::scale() const
{
    double dpi = logicalDpiX();
    return m_zoom * dpi / 1200.0; // MuseScore uses 1200 DPI internally
}

QRect ScoreCanvas::cursorWidgetRect() const
{
    if (m_cursorRect.isNull()) return QRect();

    double s = scale();

    return QRect(
        static_cast<int>(m_cursorRect.x() * s),
        static_cast<int>(m_cursorRect.y() * s),
        static_cast<int>(m_cursorRect.width() * s) + 10,
        static_cast<int>(m_cursorRect.height() * s) + 10
    );
}

QRect ScoreCanvas::pageWidgetRect(int pageIndex) const
{
    if (!m_score || pageIndex < 0 || pageIndex >= static_cast<int>(m_score->pages().size()))
        return QRect();

    const auto& pages = m_score->pages();
    double s = scale();

    double yOffset = PAGE_GAP / 2.0; // in widget coords
    for (int i = 0; i < pageIndex; ++i) {
        muse::RectF bbox = pages[i]->ldata()->bbox();
        yOffset += bbox.height() * s + PAGE_GAP;
    }

    muse::RectF bbox = pages[pageIndex]->ldata()->bbox();
    double pageW = bbox.width() * s;
    double pageH = bbox.height() * s;
    double xOffset = (width() - pageW) / 2.0;
    if (xOffset < 0) xOffset = 0;

    return QRect(
        static_cast<int>(xOffset),
        static_cast<int>(yOffset),
        static_cast<int>(pageW),
        static_cast<int>(pageH)
    );
}

void ScoreCanvas::updateCanvasSize()
{
    if (!m_score || !m_renderer) return;

    double s = scale();
    const auto& pages = m_score->pages();

    double totalHeight = 0;
    double maxWidth = 0;

    for (const Page* page : pages) {
        muse::RectF bbox = page->ldata()->bbox();
        maxWidth = std::max(maxWidth, bbox.width() * s);
        totalHeight += bbox.height() * s + PAGE_GAP;
    }

    int w = static_cast<int>(maxWidth) + 20;
    int h = static_cast<int>(totalHeight) + 20;
    setMinimumSize(0, 0);
    resize(w, h);
}

double ScoreCanvas::maxPageWidthScore() const
{
    if (!m_score) return 0;
    double maxW = 0;
    for (const Page* page : m_score->pages()) {
        maxW = std::max(maxW, page->ldata()->bbox().width());
    }
    return maxW;
}

muse::RectF ScoreCanvas::mapToRenderCoords(const muse::RectF& pageRelRect, int pageIndex) const
{
    if (!m_score || pageRelRect.isNull()) return pageRelRect;

    const auto& pages = m_score->pages();
    if (pages.empty() || pageIndex < 0 || pageIndex >= static_cast<int>(pages.size()))
        return pageRelRect;

    double s = scale();
    muse::RectF pageBBox = pages[pageIndex]->ldata()->bbox();

    // Compute our y-offset for this page in score coordinates
    double yOffsetScore = PAGE_GAP / (2.0 * s);
    for (int i = 0; i < pageIndex; ++i) {
        muse::RectF bbox = pages[i]->ldata()->bbox();
        yOffsetScore += bbox.height() + PAGE_GAP / s;
    }

    // Center page horizontally in our layout
    double xOffsetScore = (width() / s - pageBBox.width()) / 2.0;
    if (xOffsetScore < 0) xOffsetScore = 0;

    return muse::RectF(
        xOffsetScore + pageRelRect.x(),
        yOffsetScore + pageRelRect.y(),
        pageRelRect.width(),
        pageRelRect.height()
    );
}

void ScoreCanvas::paintEvent(QPaintEvent* event)
{
    if (!m_score || !m_renderer) {
        QWidget::paintEvent(event);
        return;
    }

    QPainter qp(this);
    qp.setRenderHint(QPainter::Antialiasing, true);
    qp.setRenderHint(QPainter::TextAntialiasing, true);

    double s = scale();
    const auto& pages = m_score->pages();
    QRect clipRect = event->rect();

    // Apply base scale once, then work in score coordinates
    qp.scale(s, s);

    Painter painter(QPainterProvider::make(&qp, false), "scorewidget");
    painter.setAntialiasing(true);

    PaintOptions paintOpt;
    paintOpt.isPrinting = true;
    double yOffsetScore = PAGE_GAP / (2.0 * s); // gap in score coords

    for (size_t pi = 0; pi < pages.size(); ++pi) {
        Page* page = pages[pi];
        muse::RectF bbox = page->ldata()->bbox();

        double pageScreenH = bbox.height() * s;
        double pageScreenW = bbox.width() * s;
        double pageScreenY = yOffsetScore * s;

        // Center page horizontally
        double xOffsetScreen = (width() - pageScreenW) / 2.0;
        if (xOffsetScreen < 0) xOffsetScreen = 0;
        double xOffsetScore = xOffsetScreen / s;

        QRectF pageScreenRect(xOffsetScreen, pageScreenY, pageScreenW, pageScreenH);

        // Skip pages not visible in the clip rect
        if (pageScreenRect.bottom() < clipRect.top()) {
            yOffsetScore += bbox.height() + PAGE_GAP / s;
            continue;
        }
        if (pageScreenRect.top() > clipRect.bottom()) {
            break;
        }

        // Draw page background (white rectangle in score coords)
        painter.fillRect(muse::RectF(xOffsetScore, yOffsetScore, bbox.width(), bbox.height()),
                         muse::draw::Color::WHITE);

        // Translate to page position and draw elements
        painter.save();
        painter.translate(muse::PointF(xOffsetScore, yOffsetScore));

        // Expand bbox leftward to include instrument names in the margin
        muse::RectF itemRect(bbox.x() - 500, bbox.y(), bbox.width() + 500, bbox.height());
        std::vector<EngravingItem*> elements = page->items(itemRect);
        for (const EngravingItem* item : elements) {
            m_renderer->paintItem(painter, item, paintOpt);
        }

        painter.restore();
        yOffsetScore += bbox.height() + PAGE_GAP / s;
    }

    painter.endDraw();

    // Draw cursor overlay (using raw QPainter after Painter is done)
    if (!m_cursorRect.isNull()) {
        QColor cursorColor(50, 100, 255, 120);
        // qp state is already scaled, but endDraw may have reset it
        // Use a fresh QPainter approach
        QPainter overlay(this);
        overlay.fillRect(
            QRectF(m_cursorRect.x() * s, m_cursorRect.y() * s,
                   m_cursorRect.width() * s, m_cursorRect.height() * s),
            cursorColor
        );
    }
}

// --- ScoreWidget ---

static constexpr double ZOOM_MIN = 0.5;
static constexpr double ZOOM_MAX = 4.0;
static constexpr double ZOOM_STEP = 0.1;
static constexpr double ZOOM_DEFAULT = 1.5;

ScoreWidget::ScoreWidget(QWidget* parent)
    : QScrollArea(parent)
{
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(200, 200, 200));
    setPalette(pal);
    setAutoFillBackground(true);

    m_canvas = new ScoreCanvas(this);
    setWidget(m_canvas);
    setWidgetResizable(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    grabGesture(Qt::PinchGesture);
    m_canvas->setZoom(ZOOM_DEFAULT);
}

void ScoreWidget::setScore(Score* score)
{
    m_canvas->setScore(score);
}

void ScoreWidget::setRenderer(IScoreRenderer* renderer)
{
    m_canvas->setRenderer(renderer);
}

void ScoreWidget::setZoom(double zoom)
{
    applyZoom(zoom);
}

double ScoreWidget::zoom() const
{
    return m_canvas->zoom();
}

void ScoreWidget::zoomIn()
{
    applyZoom(m_canvas->zoom() + ZOOM_STEP);
}

void ScoreWidget::zoomOut()
{
    applyZoom(m_canvas->zoom() - ZOOM_STEP);
}

void ScoreWidget::zoomToFit()
{
    double maxW = m_canvas->maxPageWidthScore();
    if (maxW <= 0) return;

    double dpi = m_canvas->logicalDpiX();
    int vpWidth = width() - 20 - m_overlayWidth; // use full scroll area width (incl. scrollbar area)
    if (vpWidth < 100) vpWidth = 100;

    // zoom * dpi / 1200 = scale, and we want: maxW * scale = vpWidth
    // so zoom = vpWidth * 1200 / (maxW * dpi)
    double fitZoom = (vpWidth * 1200.0) / (maxW * dpi);
    applyZoom(fitZoom);
}

void ScoreWidget::setOverlayWidth(int width)
{
    m_overlayWidth = width;
}

void ScoreWidget::scrollToTop()
{
    horizontalScrollBar()->setValue(0);
    verticalScrollBar()->setValue(0);
}

void ScoreWidget::applyZoom(double newZoom)
{
    newZoom = std::clamp(newZoom, ZOOM_MIN, ZOOM_MAX);
    if (qFuzzyCompare(newZoom, m_canvas->zoom())) return;
    m_canvas->setZoom(newZoom);
    verticalScrollBar()->setSingleStep(60);
    emit zoomChanged(newZoom);
}

bool ScoreWidget::event(QEvent* event)
{
    if (event->type() == QEvent::Gesture) {
        auto* ge = static_cast<QGestureEvent*>(event);
        if (auto* pinch = static_cast<QPinchGesture*>(ge->gesture(Qt::PinchGesture))) {
            if (pinch->changeFlags() & QPinchGesture::ScaleFactorChanged) {
                double factor = pinch->scaleFactor();
                applyZoom(m_canvas->zoom() * factor);
            }
            return true;
        }
    }
    return QScrollArea::event(event);
}

void ScoreWidget::setCursorRect(const muse::RectF& rect, int pageIndex)
{
    int prevPage = m_canvas->cursorPageIndex();
    m_canvas->setCursorRect(rect, pageIndex);

    // Auto-scroll on page break so cursor stays visible
    if (pageIndex >= 0 && pageIndex != prevPage) {
        ensureCursorVisible();
    }
}

void ScoreWidget::resizeEvent(QResizeEvent* event)
{
    QScrollArea::resizeEvent(event);
    zoomToFit();
}

void ScoreWidget::ensureCursorVisible()
{
    QRect pr = m_canvas->pageWidgetRect(m_canvas->cursorPageIndex());
    if (pr.isNull()) return;

    // Scroll so the top of the current page is at the top of the viewport
    verticalScrollBar()->setValue(pr.top());

    // Center page horizontally
    int vpW = viewport()->width();
    int targetX = pr.left() - (vpW - pr.width()) / 2;
    if (targetX < 0) targetX = 0;
    horizontalScrollBar()->setValue(targetX);
}

} // namespace scoretracker
