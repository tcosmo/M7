#include "MuseScoreEngine.h"

#include <QPainter>
#include <QFileInfo>
#include <QDebug>

#include "modularity/ioc.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/part.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/page.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/system.h"
#include "engraving/dom/staff.h"
#include "engraving/compat/scoreaccess.h"
#include "engraving/rendering/iscorerenderer.h"
#include "engraving/rendering/paintoptions.h"
#include "engraving/infrastructure/localfileinfoprovider.h"
#include "engraving/style/defaultstyle.h"
#include "engraving/style/styledef.h"
#include "engraving/types/types.h"
#include "engraving/types/fraction.h"

#include "draw/painter.h"
#include "draw/internal/qpainterprovider.h"

#include "importexport/musicxml/internal/import/importmusicxml.h"

using namespace mu::engraving;
using namespace mu::engraving::rendering;
using namespace muse::draw;

namespace scoretracker {

MuseScoreEngine::MuseScoreEngine() = default;

MuseScoreEngine::~MuseScoreEngine()
{
    delete m_score;
}

bool MuseScoreEngine::loadMusicXML(const QString& path)
{
    QFileInfo fi(path);
    if (!fi.exists()) {
        qWarning() << "MusicXML file not found:" << path;
        return false;
    }

    // Resolve the renderer from IoC
    m_renderer = muse::modularity::globalIoc()->resolve<IScoreRenderer>("scoretracker");
    if (!m_renderer) {
        qWarning() << "Failed to resolve IScoreRenderer";
        return false;
    }

    // Clean up old score
    delete m_score;
    m_score = nullptr;

    // Create a score with default style
    auto iocCtx = muse::modularity::globalCtx();
    m_score = compat::ScoreAccess::createMasterScoreWithDefaultStyle(iocCtx);
    m_score->setFileInfoProvider(
        std::make_shared<LocalFileInfoProvider>(path.toStdString()));

    // Import MusicXML
    muse::String musePath = muse::String::fromQString(path);
    Err err = mu::iex::musicxml::importMusicXml(m_score, musePath, false);
    if (err != Err::NoError) {
        qWarning() << "Failed to import MusicXML, error:" << static_cast<int>(err);
        delete m_score;
        m_score = nullptr;
        return false;
    }

    return true;
}

void MuseScoreEngine::setPageSizeInches(double width, double height)
{
    if (!m_score) return;
    m_score->style().set(Sid::pageWidth, width);
    m_score->style().set(Sid::pageHeight, height);
    m_score->style().set(Sid::pagePrintableWidth, width - 0.39 - 0.39);
}

void MuseScoreEngine::setMarginsInches(double top, double bottom, double left, double right)
{
    if (!m_score) return;
    m_score->style().set(Sid::pageOddTopMargin, top);
    m_score->style().set(Sid::pageOddBottomMargin, bottom);
    m_score->style().set(Sid::pageOddLeftMargin, left);
    m_score->style().set(Sid::pageEvenTopMargin, top);
    m_score->style().set(Sid::pageEvenBottomMargin, bottom);
    m_score->style().set(Sid::pageEvenLeftMargin, right);
}

void MuseScoreEngine::setSpatiumInches(double sp)
{
    if (!m_score) return;
    m_score->style().set(Sid::spatium, sp * 1200.0); // convert to internal units
}

void MuseScoreEngine::setLayoutMode(int mode)
{
    if (!m_score) return;
    LayoutMode lm = LayoutMode::PAGE;
    switch (mode) {
    case 1: lm = LayoutMode::LINE; break;
    case 2: lm = LayoutMode::SYSTEM; break;
    default: lm = LayoutMode::PAGE; break;
    }
    m_score->setLayoutMode(lm);
}

void MuseScoreEngine::setShowTitleFrame(bool show)
{
    if (!m_score) return;
    m_score->setShowVBox(show);
}

void MuseScoreEngine::layout()
{
    if (!m_score || !m_renderer) return;
    m_renderer->layoutScore(m_score, Fraction(0, 1), Fraction(-1, 1));
}

int MuseScoreEngine::pageCount() const
{
    if (!m_score) return 0;
    return static_cast<int>(m_score->pages().size());
}

QSizeF MuseScoreEngine::pageSize(int pageIndex) const
{
    if (!m_score) return QSizeF();
    const auto& pages = m_score->pages();
    if (pageIndex < 0 || pageIndex >= static_cast<int>(pages.size()))
        return QSizeF();
    muse::RectF bbox = pages[pageIndex]->ldata()->bbox();
    return QSizeF(bbox.width(), bbox.height());
}

void MuseScoreEngine::renderPage(int pageIndex, QPainter& painter)
{
    if (!m_score || !m_renderer) return;
    const auto& pages = m_score->pages();
    if (pageIndex < 0 || pageIndex >= static_cast<int>(pages.size())) return;

    Page* page = pages[pageIndex];
    muse::RectF bbox = page->ldata()->bbox();

    // Wrap QPainter in MuseScore's Painter. The provider does NOT own the QPainter.
    // We save/restore the QPainter state ourselves to keep things balanced.
    painter.save();
    Painter musePainter(QPainterProvider::make(&painter, false), "engine");
    musePainter.setAntialiasing(true);

    PaintOptions paintOpt;
    paintOpt.isPrinting = true;

    muse::RectF itemRect(bbox.x() - 500, bbox.y(), bbox.width() + 500, bbox.height());
    std::vector<EngravingItem*> elements = page->items(itemRect);
    for (const EngravingItem* item : elements) {
        m_renderer->paintItem(musePainter, item, paintOpt);
    }

    musePainter.endDraw();
    painter.restore();
}

int MuseScoreEngine::partCount() const
{
    if (!m_score) return 0;
    return static_cast<int>(m_score->parts().size());
}

PartInfo MuseScoreEngine::partInfo(int index) const
{
    PartInfo info;
    if (!m_score) return info;
    const auto& parts = m_score->parts();
    if (index < 0 || index >= static_cast<int>(parts.size())) return info;

    Part* part = parts[index];
    info.name = part->partName().toQString();
    const Instrument* instr = part->instrument();
    if (!instr->shortNames().empty())
        info.shortName = instr->shortNames().front().name().toQString();
    info.visible = part->show();
    return info;
}

void MuseScoreEngine::setPartVisible(int index, bool visible)
{
    if (!m_score) return;
    const auto& parts = m_score->parts();
    if (index < 0 || index >= static_cast<int>(parts.size())) return;
    parts[index]->setShow(visible);
}

QRectF MuseScoreEngine::resolveCursorRect(int tick, int& outPageIndex) const
{
    outPageIndex = 0;
    if (!m_score) return QRectF();

    Fraction frac = Fraction::fromTicks(tick);
    const Measure* measure = m_score->tick2measureMM(frac);
    if (!measure) {
        measure = m_score->tick2measureMM(Fraction(0, 1));
        if (!measure) return QRectF();
    }

    const System* system = measure->system();
    if (!system || !system->page() || system->staves().empty())
        return QRectF();

    // Interpolate x position within the measure
    double x = 0.0;
    Segment* s = nullptr;
    for (s = measure->first(SegmentType::ChordRest); s;) {
        Fraction t1 = s->tick();
        double x1 = s->canvasPos().x();
        double x2 = 0.0;
        Fraction t2;

        Segment* ns = s->next(SegmentType::ChordRest);
        while (ns && !ns->visible()) ns = ns->next(SegmentType::ChordRest);

        if (ns) {
            t2 = ns->tick();
            x2 = ns->canvasPos().x();
        } else {
            t2 = measure->endTick();
            const Segment* seg = measure->findSegment(SegmentType::EndBarLine, measure->endTick());
            x2 = seg ? seg->canvasPos().x() : measure->canvasPos().x() + measure->width();
        }

        if (frac >= t1 && frac < t2) {
            Fraction dt = t2 - t1;
            double dx = x2 - x1;
            x = x1 + dx * (frac - t1).ticks() / dt.ticks();
            break;
        }
        s = ns;
    }

    if (!s) x = measure->canvasPos().x();

    const double sp = m_score->style().spatium();
    x -= sp;

    const double y = system->staffCanvasYpage(0) - 3 * sp;
    const double w = 0.4 * sp;

    double y2 = 0.0;
    for (size_t i = 0; i < m_score->nstaves(); ++i) {
        SysStaff* ss = system->staff(i);
        if (!ss->show() || !m_score->staff(i)->show()) continue;
        y2 = ss->bbox().bottom();
    }
    const double h = y2 + 6 * sp;

    const Page* cursorPage = system->page();
    outPageIndex = cursorPage ? cursorPage->no() : 0;
    double pageX = cursorPage ? cursorPage->pos().x() : 0;
    double pageY = cursorPage ? cursorPage->pos().y() : 0;

    return QRectF(x - pageX, y - pageY, w, h);
}

int MuseScoreEngine::measureCount() const
{
    if (!m_score) return 0;
    return m_score->nmeasures();
}

double MuseScoreEngine::spatium() const
{
    if (!m_score) return 0;
    return m_score->style().spatium();
}

} // namespace scoretracker
