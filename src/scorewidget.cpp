#include "scorewidget.h"
#include "syncmode.h"
#include "theme.h"

#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QMouseEvent>
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
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/system.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/mscore.h"

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

void ScoreCanvas::setCursorVisible(bool visible)
{
    m_cursorVisible = visible;
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

    double s = scale();
    const auto& pages = m_score->pages();
    QRect clipRect = event->rect();

    // Score rendering in its own scope so qp is destroyed before overlay painters
    {
        QPainter qp(this);
        qp.setRenderHint(QPainter::Antialiasing, true);
        qp.setRenderHint(QPainter::TextAntialiasing, true);
        qp.scale(s, s);

        Painter painter(QPainterProvider::make(&qp, false), "scorewidget");
        painter.setAntialiasing(true);

        PaintOptions paintOpt;
        paintOpt.isPrinting = true;
        double yOffsetScore = PAGE_GAP / (2.0 * s);

        for (size_t pi = 0; pi < pages.size(); ++pi) {
            Page* page = pages[pi];
            muse::RectF bbox = page->ldata()->bbox();

            double pageScreenH = bbox.height() * s;
            double pageScreenW = bbox.width() * s;
            double pageScreenY = yOffsetScore * s;

            double xOffsetScreen = (width() - pageScreenW) / 2.0;
            if (xOffsetScreen < 0) xOffsetScreen = 0;
            double xOffsetScore = xOffsetScreen / s;

            QRectF pageScreenRect(xOffsetScreen, pageScreenY, pageScreenW, pageScreenH);

            if (pageScreenRect.bottom() < clipRect.top()) {
                yOffsetScore += bbox.height() + PAGE_GAP / s;
                continue;
            }
            if (pageScreenRect.top() > clipRect.bottom()) {
                break;
            }

            painter.fillRect(muse::RectF(xOffsetScore, yOffsetScore, bbox.width(), bbox.height()),
                             muse::draw::Color::WHITE);

            painter.save();
            painter.translate(muse::PointF(xOffsetScore, yOffsetScore));

            muse::RectF itemRect(bbox.x() - 500, bbox.y(), bbox.width() + 500, bbox.height());
            std::vector<EngravingItem*> elements = page->items(itemRect);
            for (const EngravingItem* item : elements) {
                m_renderer->paintItem(painter, item, paintOpt);
            }

            painter.restore();
            yOffsetScore += bbox.height() + PAGE_GAP / s;
        }

        painter.endDraw();
    }

    // Draw cursor overlay
    if (!m_cursorRect.isNull() && m_cursorVisible) {
        QPainter overlay(this);
        overlay.fillRect(
            QRectF(m_cursorRect.x() * s, m_cursorRect.y() * s,
                   m_cursorRect.width() * s, m_cursorRect.height() * s),
            QColor(50, 100, 255, 120)
        );
    }

    // Draw sync dots (fixed pixel size, independent of zoom)
    if (m_syncMode && m_syncMode->isActive()) {
        QPainter dotPainter(this);
        dotPainter.resetTransform();
        paintSyncDots(dotPainter);
    }
}

void ScoreCanvas::setSyncMode(scoretracker::SyncMode* syncMode)
{
    m_syncMode = syncMode;
    m_playbackTime = -1.0;
    update();
}

QPoint ScoreCanvas::dotWidgetPos(int beatIndex) const
{
    for (const auto& dot : m_dotInfos) {
        if (dot.beatIndex == beatIndex) {
            return dot.center.toPoint();
        }
    }
    return QPoint(-1, -1);
}

void ScoreCanvas::setPlaybackTime(double time)
{
    m_playbackTime = time;
    if (m_syncMode && m_syncMode->isActive()) {
        update();
    }
}

void ScoreCanvas::setPlaying(bool playing)
{
    m_playing = playing;
    update();
}

void ScoreCanvas::setLastTappedBeat(int beatIndex)
{
    m_lastTappedBeat = beatIndex;
}

void ScoreCanvas::clearLastTappedBeat()
{
    m_lastTappedBeat = -1;
}

void ScoreCanvas::setSelectedBeat(int beatIndex)
{
    if (m_selectedBeatIndex == beatIndex) return;
    m_selectedBeatIndex = beatIndex;
    update();
}

void ScoreCanvas::paintSyncDots(QPainter& painter)
{
    if (!m_score || !m_syncMode) return;

    painter.setRenderHint(QPainter::Antialiasing, true);
    m_dotInfos.clear();

    int syncStaff = m_syncMode->syncStaffIdx();
    if (syncStaff < 0) return;

    double s = scale();
    double spatium = m_score->style().spatium();
    const auto& beats = m_syncMode->beats();
    const auto& bpmVec = m_syncMode->beatsPerMeasure();
    int nextUnsynced = m_syncMode->nextUnsyncedBeat();

    // Dot placed below the 5-line staff: top + 4sp (staff height) + 2sp (padding)
    const double dotYOffset = 6.0 * spatium;
    const double radius = 0.9 * spatium * s;

    int beatIdx = 0;
    int measureIdx = 0;
    const auto& pages = m_score->pages();

    for (Measure* measure = m_score->firstMeasure();
         measure && beatIdx < static_cast<int>(beats.size());
         measure = measure->nextMeasure(), ++measureIdx) {

        int bpm = (measureIdx < static_cast<int>(bpmVec.size())) ? bpmVec[measureIdx] : 0;
        for (int b = 0; b < bpm && beatIdx < static_cast<int>(beats.size()); ++b, ++beatIdx) {
            Fraction beatTick = measure->tick() + Fraction(b, 4);
            Segment* seg = measure->findSegment(SegmentType::ChordRest, beatTick);
            if (!seg) continue;

            const System* system = seg->system();
            if (!system || !system->page()) continue;

            // Get segment x in canvas coords, convert to page-relative
            double canvasX = seg->canvasPos().x();
            const Page* page = system->page();
            int pageIndex = page->no();
            double pageX = page->pos().x();
            double pageY = page->pos().y();

            // Y position: below the sync staff
            double staffY = system->staffCanvasYpage(syncStaff);
            double dotY = staffY + dotYOffset - pageY;
            double dotX = canvasX - pageX + 0.5 * spatium;

            // Map to render coordinates (same as mapToRenderCoords)
            double yOffsetScore = PAGE_GAP / (2.0 * s);
            for (int pi = 0; pi < pageIndex; ++pi) {
                muse::RectF bbox = pages[pi]->ldata()->bbox();
                yOffsetScore += bbox.height() + PAGE_GAP / s;
            }
            muse::RectF pageBBox = pages[pageIndex]->ldata()->bbox();
            double xOffsetScore = (width() / s - pageBBox.width()) / 2.0;
            if (xOffsetScore < 0) xOffsetScore = 0;

            double renderX = (xOffsetScore + dotX) * s;
            double renderY = (yOffsetScore + dotY) * s;

            // System top: first staff Y minus minimal padding (just above the staff lines)
            double sysTopY = system->staffCanvasYpage(0) - 1.0 * spatium - pageY;
            double renderSysTop = (yOffsetScore + sysTopY) * s;

            DotInfo dot;
            dot.beatIndex = beatIdx;
            dot.center = QPointF(renderX, renderY);
            dot.radius = radius;
            dot.systemTop = renderSysTop;
            m_dotInfos.push_back(dot);

            double pw = 0.3 * spatium * s; // pen width in score-proportional pixels

            if (beatIdx == nextUnsynced) {
                // Next inputtable: always blue outline, no fill
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(QColor(30, 120, 255), pw));
                painter.drawEllipse(dot.center, radius, radius);
            } else if (beats[beatIdx].synced) {
                painter.setBrush(QColor(30, 120, 255));
                painter.setPen(Qt::NoPen);
                painter.drawEllipse(dot.center, radius, radius);
            } else {
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(QColor(120, 120, 120, 100), pw * 0.5));
                painter.drawEllipse(dot.center, radius, radius);
            }

            // Orange dot below: selected beat or active beat during playback
            bool showOrangeDot = false;
            if (beatIdx == m_selectedBeatIndex) {
                showOrangeDot = true;
            } else if (m_playing && m_lastTappedBeat < 0
                       && beats[beatIdx].synced && m_playbackTime >= 0
                       && beats[beatIdx].effectiveTime() <= m_playbackTime) {
                bool isActive = true;
                for (int j = beatIdx + 1; j < static_cast<int>(beats.size()); ++j) {
                    if (beats[j].synced) {
                        if (beats[j].effectiveTime() <= m_playbackTime)
                            isActive = false;
                        break;
                    }
                }
                showOrangeDot = isActive;
            }
            if (showOrangeDot) {
                double smallRadius = radius * 0.45;
                QPointF belowCenter(dot.center.x(), dot.center.y() + radius + smallRadius + pw * 2);
                painter.setBrush(QColor(255, 180, 0));
                painter.setPen(Qt::NoPen);
                painter.drawEllipse(belowCenter, smallRadius, smallRadius);
            }

        }
    }
}

int ScoreCanvas::hitTestDot(const QPoint& pos) const
{
    for (const auto& dot : m_dotInfos) {
        double dx = pos.x() - dot.center.x();
        double dy = pos.y() - dot.center.y();
        if (dx * dx + dy * dy <= (dot.radius + 4) * (dot.radius + 4)) {
            return dot.beatIndex;
        }
    }
    return -1;
}

void ScoreCanvas::mousePressEvent(QMouseEvent* event)
{
    if (m_syncMode && m_syncMode->isActive() && event->button() == Qt::LeftButton) {
        int hit = hitTestDot(event->pos());
        if (hit >= 0) {
            const auto& beat = m_syncMode->beats()[hit];
            bool forceNextToTap = (event->modifiers() & Qt::ShiftModifier);

            // Double-click detection
            if (m_lastClickBeat == hit && m_lastClickTimer.isValid()
                && m_lastClickTimer.elapsed() < 400) {
                forceNextToTap = true;
            }
            m_lastClickBeat = hit;
            m_lastClickTimer.restart();

            if (forceNextToTap || !beat.synced) {
                // Double-click, shift-click, or unsynced: set as next-to-tap
                m_syncMode->setNextUnsyncedFrom(hit);
                m_selectedBeatIndex = -1;
            } else {
                // Single click on synced: select (orange dot below)
                m_selectedBeatIndex = hit;
            }
            emit beatClicked(hit);
            update();
            return;
        }
        // Click outside dots: deselect
        if (m_selectedBeatIndex >= 0) {
            m_selectedBeatIndex = -1;
            update();
        }
    }
    QWidget::mousePressEvent(event);
}

void ScoreCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (m_syncMode && m_syncMode->isActive()) {
        int hit = hitTestDot(event->pos());
        setCursor(hit >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
    QWidget::mouseMoveEvent(event);
}


// --- TriggerLineOverlay ---

TriggerLineOverlay::TriggerLineOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    hide();
}

void TriggerLineOverlay::setTriggerFraction(double fraction)
{
    m_fraction = fraction;
    if (isVisible()) update();
}

void TriggerLineOverlay::setVisible(bool visible)
{
    QWidget::setVisible(visible);
    if (visible) {
        raise();
        update();
    }
}

void TriggerLineOverlay::paintEvent(QPaintEvent*)
{
    int y = static_cast<int>(height() * m_fraction);
    QPainter p(this);
    p.setPen(QPen(QColor(255, 0, 0, 160), 1, Qt::DashLine));
    p.drawLine(0, y, width(), y);
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

    // Match scrollbar to sidebar style
    verticalScrollBar()->setStyleSheet(Theme::scrollBarStyleStr());

    m_triggerOverlay = new TriggerLineOverlay(this);

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

void ScoreWidget::applyTheme()
{
    verticalScrollBar()->setStyleSheet(Theme::scrollBarStyleStr());
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
    m_canvas->setCursorRect(rect, pageIndex);

    if (pageIndex >= 0 && !rect.isNull()) {
        ensureCursorVisible();
    }
}

void ScoreWidget::resizeEvent(QResizeEvent* event)
{
    QScrollArea::resizeEvent(event);
    m_triggerOverlay->setGeometry(viewport()->geometry());
    m_triggerOverlay->raise();
    zoomToFit();
}

void ScoreWidget::setAutoScrollEnabled(bool enabled)
{
    m_autoScroll = enabled;
}

void ScoreWidget::setAutoScrollTrigger(double trigger)
{
    m_scrollTrigger = trigger;
    m_triggerOverlay->setTriggerFraction(trigger);
}

void ScoreWidget::setAutoScrollTarget(double target)
{
    m_scrollTarget = target;
}

void ScoreWidget::setShowTriggerLine(bool show)
{
    m_triggerOverlay->setVisible(show);
}

void ScoreWidget::setCursorAnchor(int anchor)
{
    m_cursorAnchor = anchor;
}

void ScoreWidget::setCursorVisible(bool visible)
{
    m_canvas->setCursorVisible(visible);
}

void ScoreWidget::setSyncMode(scoretracker::SyncMode* syncMode)
{
    m_canvas->setSyncMode(syncMode);
    if (syncMode) {
        connect(m_canvas, &ScoreCanvas::beatClicked, this, &ScoreWidget::beatClicked,
                Qt::UniqueConnection);
        connect(m_canvas, &ScoreCanvas::beatDoubleClicked, this, &ScoreWidget::beatDoubleClicked,
                Qt::UniqueConnection);
    }
}

void ScoreWidget::setPlaybackTime(double time)
{
    m_canvas->setPlaybackTime(time);
}

void ScoreWidget::setPlaying(bool playing)
{
    m_canvas->setPlaying(playing);
}

void ScoreWidget::setLastTappedBeat(int beatIndex)
{
    m_canvas->setLastTappedBeat(beatIndex);
}

void ScoreWidget::clearLastTappedBeat()
{
    m_canvas->clearLastTappedBeat();
}

void ScoreWidget::setSelectedBeat(int beatIndex)
{
    m_canvas->setSelectedBeat(beatIndex);
}

int ScoreWidget::selectedBeatIndex() const
{
    return m_canvas->selectedBeatIndex();
}

void ScoreWidget::ensureBeatVisible(int beatIndex)
{
    QPoint pos = m_canvas->dotWidgetPos(beatIndex);
    if (pos.x() < 0) return;

    // Find the DotInfo for this beat to get systemTop
    double sysTop = -1;
    for (const auto& dot : m_canvas->dotInfos()) {
        if (dot.beatIndex == beatIndex) {
            sysTop = dot.systemTop;
            break;
        }
    }

    int margin = 40;
    int vpH = viewport()->height();
    int vpW = viewport()->width();
    int scrollY = verticalScrollBar()->value();
    int scrollX = horizontalScrollBar()->value();

    // Vertical: try to show the full system (from systemTop to dot)
    if (sysTop >= 0) {
        int sysTopInVp = static_cast<int>(sysTop) - scrollY;
        int dotInVpY = pos.y() - scrollY;
        if (sysTopInVp < margin || dotInVpY > vpH - margin) {
            // Scroll so system top is near the top of viewport
            verticalScrollBar()->setValue(static_cast<int>(sysTop) - margin);
        }
    } else {
        int dotInVpY = pos.y() - scrollY;
        if (dotInVpY > vpH - margin || dotInVpY < margin) {
            verticalScrollBar()->setValue(pos.y() - vpH / 3);
        }
    }

    int effectiveW = vpW - m_overlayWidth;
    int dotInVpX = pos.x() - scrollX;
    if (dotInVpX > effectiveW - margin) {
        horizontalScrollBar()->setValue(pos.x() - effectiveW + margin);
    } else if (dotInVpX < margin) {
        horizontalScrollBar()->setValue(pos.x() - margin);
    }
}


void ScoreWidget::ensureCursorVisible()
{
    if (!m_autoScroll) return;

    QRect cr = m_canvas->cursorWidgetRect();
    if (cr.isNull()) return;

    int vpH = viewport()->height();
    int vpW = viewport()->width();
    int margin = 40; // pixels of breathing room

    // Pick cursor reference point based on anchor setting
    int cursorRef;
    switch (m_cursorAnchor) {
    case 0:  cursorRef = cr.top(); break;
    case 2:  cursorRef = cr.bottom(); break;
    default: cursorRef = cr.top() + cr.height() / 2; break;
    }

    // Vertical: keep cursor in the upper portion of the viewport.
    int scrollY = verticalScrollBar()->value();
    int triggerY = static_cast<int>(vpH * m_scrollTrigger);
    int targetY = static_cast<int>(vpH * m_scrollTarget);
    int cursorInVP = cursorRef - scrollY;

    if (cursorInVP < margin) {
        verticalScrollBar()->setValue(cr.top() - margin);
    } else if (cursorInVP > triggerY) {
        verticalScrollBar()->setValue(cr.top() - targetY);
    }

    // Horizontal: scroll if cursor is outside the visible area
    int scrollX = horizontalScrollBar()->value();
    int effectiveW = vpW - m_overlayWidth;
    if (cr.left() < scrollX + margin) {
        horizontalScrollBar()->setValue(cr.left() - margin);
    } else if (cr.right() > scrollX + effectiveW - margin) {
        horizontalScrollBar()->setValue(cr.right() - effectiveW + margin);
    }
}

} // namespace scoretracker
