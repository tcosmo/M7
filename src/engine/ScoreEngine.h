#pragma once

#include <QString>
#include <QRectF>
#include <QSizeF>
#include <vector>

class QPainter;

namespace scoretracker {

struct PartInfo {
    QString name;
    QString shortName;
    bool visible = true;
};

class ScoreEngine {
public:
    virtual ~ScoreEngine() = default;

    // Load a MusicXML file. Returns true on success.
    virtual bool loadMusicXML(const QString& path) = 0;

    // Layout configuration (call before layout())
    virtual void setPageSizeInches(double width, double height) = 0;
    virtual void setMarginsInches(double top, double bottom, double left, double right) = 0;
    virtual void setSpatiumInches(double sp) = 0;
    virtual void setLayoutMode(int mode) = 0; // 0=PAGE, 1=LINE, 2=SYSTEM
    virtual void setShowTitleFrame(bool show) = 0;

    // Perform layout (must be called after loadMusicXML and configuration)
    virtual void layout() = 0;

    // Page information (dimensions in internal coordinate units)
    virtual int pageCount() const = 0;
    virtual QSizeF pageSize(int pageIndex) const = 0;

    // DPI of the internal coordinate system.
    // MuseScore uses 1200; Verovio uses ~247 (based on tenths-of-mm).
    virtual double internalDPI() const = 0;

    // Render a single page to the given QPainter.
    // The painter origin is at the page top-left, in internal coordinate units.
    virtual void renderPage(int pageIndex, QPainter& painter) = 0;

    // Part management
    virtual int partCount() const = 0;
    virtual PartInfo partInfo(int index) const = 0;
    virtual void setPartVisible(int index, bool visible) = 0;

    // Cursor resolution: given a tick (480 ticks per quarter note),
    // return the cursor rectangle in page-relative internal coordinates.
    virtual QRectF resolveCursorRect(int tick, int& outPageIndex) const = 0;

    // Score information
    virtual int measureCount() const = 0;
    virtual double spatium() const = 0; // in internal units

    // Web-based rendering: engines that produce complex SVG (e.g. Verovio)
    // should return true and provide HTML for a QWebEngineView.
    virtual bool usesWebRendering() const { return false; }
    virtual QString renderAllPagesHtml() const { return QString(); }
};

} // namespace scoretracker
