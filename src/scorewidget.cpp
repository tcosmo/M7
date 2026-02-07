#include "scorewidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QScrollBar>
#include <QWheelEvent>
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

    setMinimumSize(
        static_cast<int>(maxWidth) + 20,
        static_cast<int>(totalHeight) + 20
    );
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

ScoreWidget::ScoreWidget(QWidget* parent)
    : QScrollArea(parent)
{
    m_canvas = new ScoreCanvas(this);
    setWidget(m_canvas);
    setWidgetResizable(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // Snap back on any scroll when follow is on (catches momentum scrolling)
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        if (m_followCursor) {
            ensureCursorVisible();
        }
    });
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        if (m_followCursor) {
            ensureCursorVisible();
        }
    });
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
    m_canvas->setZoom(zoom);
}

double ScoreWidget::zoom() const
{
    return m_canvas->zoom();
}

void ScoreWidget::setFollowCursor(bool follow)
{
    m_followCursor = follow;
    if (m_followCursor) {
        ensureCursorVisible();
    }
}

void ScoreWidget::wheelEvent(QWheelEvent* event)
{
    QScrollArea::wheelEvent(event);
    // When following, let the scroll happen visually but snap back
    if (m_followCursor) {
        ensureCursorVisible();
    }
}

void ScoreWidget::setCursorRect(const muse::RectF& rect, int pageIndex)
{
    int prevPage = m_canvas->cursorPageIndex();
    m_canvas->setCursorRect(rect, pageIndex);

    // When following, auto-scroll on page change
    if (m_followCursor && pageIndex != prevPage) {
        ensureCursorVisible();
    }
}

void ScoreWidget::ensureCursorVisible()
{
    QRect cr = m_canvas->cursorWidgetRect();
    if (cr.isNull()) return;

    int vpH = viewport()->height();
    int vpW = viewport()->width();

    // Position cursor in the upper third of the viewport
    int targetY = cr.top() - vpH / 4;
    if (targetY < 0) targetY = 0;

    // Center horizontally on the page containing the cursor
    QRect pr = m_canvas->pageWidgetRect(m_canvas->cursorPageIndex());
    int targetX = 0;
    if (!pr.isNull()) {
        targetX = pr.left() - (vpW - pr.width()) / 2;
        if (targetX < 0) targetX = 0;
    }

    verticalScrollBar()->setValue(targetY);
    horizontalScrollBar()->setValue(targetX);
}

} // namespace scoretracker
