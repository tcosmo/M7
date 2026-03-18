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

#ifdef USE_MUSESCORE
#include "engraving/dom/interval.h"

namespace mu::engraving {
class Score;
class Part;
namespace rendering {
class IScoreRenderer;
}
}
#endif

namespace scoretracker {

class PartRow : public QWidget
{
    Q_OBJECT

public:
#ifdef USE_MUSESCORE
    explicit PartRow(mu::engraving::Part* part, const QString& name, QWidget* parent = nullptr);
#endif

#ifdef USE_MUSESCORE
    void setPartVisible(bool visible);
    bool isPartVisible() const;
    mu::engraving::Part* part() const { return m_part; }
#endif
    bool isTransposing() const { return m_isTransposing; }

#ifdef USE_MUSESCORE
    void applyPitchMode(bool concert);
#endif
    void setPitchControlEnabled(bool enabled);
    void setClefControlEnabled(bool enabled);
    void setOctaveControlEnabled(bool enabled);
#ifdef USE_MUSESCORE
    void applyOriginalClef();
    void reapplyPerPartClef();
#endif

    void setPlayAlongActive(bool active);
    bool isPlayAlongActive() const { return m_playAlongActive; }
    int playAlongGmProgram() const;
    void setPlayModeVisible(bool visible);

#ifdef USE_MUSESCORE
    QString partName() const;
#endif
    int clefComboValue() const;
    int octaveComboValue() const;
    int xmlClefInt() const { return m_xmlClefInt; }
#ifdef USE_MUSESCORE
    void setClefFromSettings(int clef, int octave);
#endif
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
#ifdef USE_MUSESCORE
    void updateEyeIcon();
#endif
    void updateEarIcon();
    void toggleExpand();
#ifdef USE_MUSESCORE
    void applyClefInternal(int clefType);
    void updateOctaveCombo();
#endif

#ifdef USE_MUSESCORE
    mu::engraving::Part* m_part = nullptr;
#endif
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
#ifdef USE_MUSESCORE
    mu::engraving::Interval m_origTranspose;
#endif
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

#ifdef USE_MUSESCORE
    void setScore(mu::engraving::Score* score);
    void setRenderer(mu::engraving::rendering::IScoreRenderer* renderer);
#endif
    void setScoreFileName(const QString& fileName);
#ifdef USE_MUSESCORE
    void showOnlyParts(const QList<int>& partNumbers);
    void activatePlayAlong(int rowIndex, int gmProgram);
#endif
    int desiredHeight() const;
    void applyTheme();
#ifdef USE_MUSESCORE
    void setPlayModeActive(bool active);
#endif

signals:
    void partsChanged();
#ifdef USE_MUSESCORE
    void playAlongChanged(mu::engraving::Part* part, int gmProgram);
#endif
    void playAlongInstrChanged(int gmProgram);
    void playAlongVolumeChanged(double gain);

private slots:
#ifdef USE_MUSESCORE
    void showSoloPart();
    void showAllParts();
#endif

private:
#ifdef USE_MUSESCORE
    void populateList();
    void relayout();
    void updateRow(int index, bool visible);
    void applyGlobalSettings();
#endif
    void updateTransposingLabel();
#ifdef USE_MUSESCORE
    void loadSettings();
    void saveSettings();
#endif
    void resizeEvent(QResizeEvent* event) override;

    QString m_scoreFileName;
#ifdef USE_MUSESCORE
    mu::engraving::Score* m_score = nullptr;
    mu::engraving::rendering::IScoreRenderer* m_renderer = nullptr;
#endif
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_scrollContent = nullptr;
    QVBoxLayout* m_rowsLayout = nullptr;
    std::vector<PartRow*> m_rows;
#ifdef USE_MUSESCORE
    std::vector<mu::engraving::Part*> m_parts;
#endif

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
