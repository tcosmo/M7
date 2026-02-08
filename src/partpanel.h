#pragma once

#include <QWidget>
#include <QToolButton>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QEvent>
#include <vector>

#include "engraving/dom/interval.h"

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
    bool isTransposing() const { return m_isTransposing; }

    void applyPitchMode(bool concert);
    void setPitchControlEnabled(bool enabled);
    void applySimplifiedClef();
    void restoreOriginalClef();
    void setClefControlEnabled(bool enabled);

signals:
    void visibilityToggled();
    void clefChanged();
    void pitchModeChanged();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void updateEyeIcon();
    void toggleExpand();
    void applyClefInternal(int clefType);
    int computeBestClef() const;

    mu::engraving::Part* m_part = nullptr;
    QToolButton* m_eyeButton = nullptr;
    QLabel* m_nameLabel = nullptr;
    QToolButton* m_arrowButton = nullptr;
    QWidget* m_contentArea = nullptr;
    QComboBox* m_clefCombo = nullptr;
    QRadioButton* m_writtenRadio = nullptr;
    QRadioButton* m_concertRadio = nullptr;
    mu::engraving::Interval m_origTranspose;
    bool m_isTransposing = false;
    bool m_inConcertPitch = false;
    int m_origClefInt = -1;
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
    void applyGlobalSettings();
    void updateTransposingLabel();
    void resizeEvent(QResizeEvent* event) override;

    mu::engraving::Score* m_score = nullptr;
    mu::engraving::rendering::IScoreRenderer* m_renderer = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_scrollContent = nullptr;
    QVBoxLayout* m_rowsLayout = nullptr;
    std::vector<PartRow*> m_rows;
    std::vector<mu::engraving::Part*> m_parts;

    QLabel* m_transposingListLabel = nullptr;
    QString m_transposingFullText;
    QRadioButton* m_globalPitchWritten = nullptr;
    QRadioButton* m_globalPitchConcert = nullptr;
    QRadioButton* m_globalPitchPerPart = nullptr;
    QRadioButton* m_globalClefWritten = nullptr;
    QRadioButton* m_globalClefSimplified = nullptr;
    QRadioButton* m_globalClefPerPart = nullptr;
};

} // namespace scoretracker
