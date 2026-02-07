#pragma once

#include <QDockWidget>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <vector>

namespace mu::engraving {
class Score;
class Part;
namespace rendering {
class IScoreRenderer;
}
}

namespace scoretracker {

class PartPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit PartPanel(QWidget* parent = nullptr);

    void setScore(mu::engraving::Score* score);
    void setRenderer(mu::engraving::rendering::IScoreRenderer* renderer);

signals:
    void partsChanged();

private slots:
    void onItemChanged(QListWidgetItem* item);
    void showFullScore();
    void showSoloPart();
    void showAllParts();

private:
    void populateList();
    void relayout();

    mu::engraving::Score* m_score = nullptr;
    mu::engraving::rendering::IScoreRenderer* m_renderer = nullptr;
    QListWidget* m_listWidget = nullptr;
    std::vector<mu::engraving::Part*> m_parts;
};

} // namespace scoretracker
