#pragma once

#include "ScoreEngine.h"
#include <memory>

namespace mu::engraving {
class MasterScore;
class Score;
namespace rendering {
class IScoreRenderer;
}
}

namespace scoretracker {

class MuseScoreEngine : public ScoreEngine {
public:
    MuseScoreEngine();
    ~MuseScoreEngine() override;

    bool loadMusicXML(const QString& path) override;
    void setPageSizeInches(double width, double height) override;
    void setMarginsInches(double top, double bottom, double left, double right) override;
    void setSpatiumInches(double sp) override;
    void setLayoutMode(int mode) override;
    void setShowTitleFrame(bool show) override;
    void layout() override;

    int pageCount() const override;
    QSizeF pageSize(int pageIndex) const override;
    double internalDPI() const override { return 1200.0; }
    void renderPage(int pageIndex, QPainter& painter) override;

    int partCount() const override;
    PartInfo partInfo(int index) const override;
    void setPartVisible(int index, bool visible) override;

    QRectF resolveCursorRect(int tick, int& outPageIndex) const override;
    int measureCount() const override;
    double spatium() const override;

    // Access to underlying MuseScore objects for features that need them
    // (SyncMode, PlayAlongSynth, PartPanel advanced features, etc.)
    mu::engraving::MasterScore* score() const { return m_score; }
    mu::engraving::rendering::IScoreRenderer* renderer() const { return m_renderer.get(); }

private:
    mu::engraving::MasterScore* m_score = nullptr;
    std::shared_ptr<mu::engraving::rendering::IScoreRenderer> m_renderer;
};

} // namespace scoretracker
