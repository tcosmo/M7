#pragma once

#include "ScoreEngine.h"
#include <QDomDocument>
#include <map>
#include <set>

namespace vrv {
class Toolkit;
}

namespace scoretracker {

class VerovioEngine : public ScoreEngine {
public:
    VerovioEngine();
    ~VerovioEngine() override;

    bool loadMusicXML(const QString& path) override;
    void setPageSizeInches(double width, double height) override;
    void setMarginsInches(double top, double bottom, double left, double right) override;
    void setSpatiumInches(double sp) override;
    void setLayoutMode(int mode) override;
    void setShowTitleFrame(bool show) override;
    void layout() override;

    int pageCount() const override;
    QSizeF pageSize(int pageIndex) const override;
    double internalDPI() const override;
    void renderPage(int pageIndex, QPainter& painter) override;

    int partCount() const override;
    PartInfo partInfo(int index) const override;
    void setPartVisible(int index, bool visible) override;

    // Set page width directly in Verovio units (default 2100 ≈ A4 width)
    void setPageWidthDirect(int vrvUnits) { m_pageWidthOverride = vrvUnits; }

    // Load with specific parts in a specific order (1-based part numbers)
    void selectParts(const QList<int>& partNumbers);

    QRectF resolveCursorRect(int tick, int& outPageIndex) const override;
    int measureCount() const override;
    double spatium() const override;

    bool usesWebRendering() const override { return true; }
    QString renderAllPagesHtml() const override;

    // Get the element ID closest to the given tick (for cursor positioning)
    QString elementIdAtTick(int tick) const;
    // Get the timemap as a JS array string for embedding in web view
    QString timemapAsJs() const;

    // Access the SVGs rendered during renderAllPagesHtml (for getNotesForPart)
    const QStringList& lastRenderedSvgs() const { return m_renderedSvgs; }

    // Note information for play-along
    struct NoteInfo {
        QString elementId;
        int pitch;
        int midiTime = -1;
        int midiDur = 0;
        bool tiedBack = false;
    };
    // Extract notes for a part from pre-rendered SVGs (avoids ID regeneration).
    // renderedSvgs should be the same SVGs used for HTML display.
    std::vector<NoteInfo> getNotesForPart(int partIndex, const QStringList& renderedSvgs) const;

private:
    void applyOptions();
    void reloadWithVisibleParts();
    void buildTimemap();
    void parseParts(const QString& musicXml);
    QString filterMusicXML(const QString& xml, const QList<int>& partNumbers) const;

    vrv::Toolkit* m_toolkit = nullptr;
    QString m_musicXmlPath;
    QString m_musicXmlData; // raw XML content for part filtering

    double m_pageWidthIn = 8.5;
    int m_pageWidthOverride = 0; // direct Verovio units, bypasses DPI calc
    double m_pageHeightIn = 11.0;
    double m_marginTop = 0.39;
    double m_marginBottom = 0.79;
    double m_marginLeft = 0.39;
    double m_marginRight = 0.39;
    double m_spatiumIn = 0.046;
    int m_layoutMode = 0;
    bool m_showTitle = true;
    int m_scale = 40; // Verovio scale percentage

    // Part information parsed from MusicXML
    struct InternalPart {
        QString id;
        QString name;
        QString shortName;
        bool visible = true;
    };
    std::vector<InternalPart> m_parts;

    // Cached rendered SVGs (from renderAllPagesHtml)
    mutable QStringList m_renderedSvgs;

    // Currently selected parts (1-based, in display order)
    QList<int> m_selectedParts;

    // Timemap for cursor resolution
    struct TimemapEntry {
        double qstamp;       // quarter-note position
        QString elementId;   // XML element ID
    };
    mutable std::vector<TimemapEntry> m_timemap;
    int m_measCount = 0;
};

} // namespace scoretracker
