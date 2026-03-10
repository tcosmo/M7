#pragma once

#include <QWidget>
#include <QToolButton>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSlider>
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
    void setClefControlEnabled(bool enabled);
    void setOctaveControlEnabled(bool enabled);
    void applyOriginalClef();
    void reapplyPerPartClef();

    void setPlayAlongActive(bool active);
    bool isPlayAlongActive() const { return m_playAlongActive; }
    int playAlongGmProgram() const;
    void setPlayModeVisible(bool visible);

    QString partName() const;
    int clefComboValue() const;
    int octaveComboValue() const;
    int xmlClefInt() const { return m_xmlClefInt; }
    void setClefFromSettings(int clef, int octave);
    void applyTheme();

signals:
    void visibilityToggled();
    void soloRequested();
    void clefChanged();
    void pitchModeChanged();
    void playAlongToggled();
    void playAlongInstrumentChanged(int gmProgram);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void updateEyeIcon();
    void updateEarIcon();
    void toggleExpand();
    void applyClefInternal(int clefType);
    void updateOctaveCombo();

    mu::engraving::Part* m_part = nullptr;
    QWidget* m_header = nullptr;
    QToolButton* m_eyeButton = nullptr;
    QToolButton* m_earButton = nullptr;
    QLabel* m_nameLabel = nullptr;
    QToolButton* m_arrowButton = nullptr;
    QWidget* m_contentArea = nullptr;
    QLabel* m_clefLabel = nullptr;
    QLabel* m_octaveLabel = nullptr;
    QLabel* m_sectionLabel = nullptr;
    QLabel* m_instrLabel = nullptr;
    QComboBox* m_clefCombo = nullptr;
    QComboBox* m_octaveCombo = nullptr;
    QComboBox* m_instrCombo = nullptr;
    QRadioButton* m_writtenRadio = nullptr;
    QRadioButton* m_concertRadio = nullptr;
    mu::engraving::Interval m_origTranspose;
    bool m_isTransposing = false;
    bool m_inConcertPitch = false;
    int m_xmlClefInt = -1;
    bool m_expanded = false;
    bool m_playAlongActive = false;
};

class PartPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PartPanel(QWidget* parent = nullptr);

    void setScore(mu::engraving::Score* score);
    void setRenderer(mu::engraving::rendering::IScoreRenderer* renderer);
    void setScoreFileName(const QString& fileName);
    void showOnlyParts(const QList<int>& partNumbers);
    void activatePlayAlong(int rowIndex, int gmProgram);
    int desiredHeight() const;
    void applyTheme();
    void setPlayModeActive(bool active);

signals:
    void partsChanged();
    void playAlongChanged(mu::engraving::Part* part, int gmProgram);
    void playAlongInstrChanged(int gmProgram);
    void playAlongVolumeChanged(double gain);

private slots:
    void showSoloPart();
    void showAllParts();

private:
    void populateList();
    void relayout();
    void updateRow(int index, bool visible);
    void applyGlobalSettings();
    void updateTransposingLabel();
    void loadSettings();
    void saveSettings();
    void resizeEvent(QResizeEvent* event) override;

    QString m_scoreFileName;
    mu::engraving::Score* m_score = nullptr;
    mu::engraving::rendering::IScoreRenderer* m_renderer = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_scrollContent = nullptr;
    QVBoxLayout* m_rowsLayout = nullptr;
    std::vector<PartRow*> m_rows;
    std::vector<mu::engraving::Part*> m_parts;

    QLabel* m_pitchLabel = nullptr;
    QLabel* m_transposingListLabel = nullptr;
    QString m_transposingFullText;
    QLabel* m_clefSectionLabel = nullptr;
    QRadioButton* m_globalPitchWritten = nullptr;
    QRadioButton* m_globalPitchConcert = nullptr;
    QRadioButton* m_globalPitchPerPart = nullptr;
    QRadioButton* m_globalClefWritten = nullptr;
    QRadioButton* m_globalClefPerPart = nullptr;

    bool m_playModeActive = false;
    QLabel* m_volumeLabel = nullptr;
    QSlider* m_volumeSlider = nullptr;
};

} // namespace scoretracker
