#pragma once

#include <QWidget>
#include <QToolButton>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <vector>

namespace mu::engraving {
class Score;
class Part;
namespace rendering {
class IScoreRenderer;
}
}

namespace scoretracker {

class PartRow : public QWidget
{
    Q_OBJECT

public:
    explicit PartRow(mu::engraving::Part* part, const QString& name, QWidget* parent = nullptr);

    void setPartVisible(bool visible);
    bool isPartVisible() const;
    mu::engraving::Part* part() const { return m_part; }

signals:
    void visibilityToggled();
    void clefChanged();

private:
    void updateEyeIcon();
    void toggleExpand();

    mu::engraving::Part* m_part = nullptr;
    QToolButton* m_eyeButton = nullptr;
    QLabel* m_nameLabel = nullptr;
    QToolButton* m_arrowButton = nullptr;
    QWidget* m_contentArea = nullptr;
    QComboBox* m_clefCombo = nullptr;
    bool m_expanded = false;
};

class PartPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PartPanel(QWidget* parent = nullptr);

    void setScore(mu::engraving::Score* score);
    void setRenderer(mu::engraving::rendering::IScoreRenderer* renderer);
    int desiredHeight() const;

signals:
    void partsChanged();

private slots:
    void showSoloPart();
    void showAllParts();

private:
    void populateList();
    void relayout();
    void updateRow(int index, bool visible);

    mu::engraving::Score* m_score = nullptr;
    mu::engraving::rendering::IScoreRenderer* m_renderer = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_scrollContent = nullptr;
    QVBoxLayout* m_rowsLayout = nullptr;
    std::vector<PartRow*> m_rows;
    std::vector<mu::engraving::Part*> m_parts;
};

} // namespace scoretracker
