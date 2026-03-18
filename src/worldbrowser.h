#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QSlider>
#include <QTabWidget>
#include <QStackedWidget>
#include <QList>
#include <QStringList>

#include "worldmodel.h"

namespace scoretracker {

// Card shown in the left worlds sidebar
class WorldCard : public QWidget
{
    Q_OBJECT
public:
    explicit WorldCard(const World& world, QWidget* parent = nullptr);
    void setSelected(bool selected);

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    World m_world;
    bool m_selected = false;
    bool m_hovered = false;
};

// Left sidebar listing all worlds
class WorldSidebar : public QWidget
{
    Q_OBJECT
public:
    explicit WorldSidebar(QWidget* parent = nullptr);
    void setWorlds(const QList<World>& worlds);
    void setVideoWidget(QWidget* videoWidget);
    void setExpandButton(QWidget* button);
    QWidget* videoContainer() const { return m_videoContainer; }
    void setInterpretations(const QStringList& labels, int activeIndex = 0);
    void clearInterpretations();
    void showVolumeSlider(bool show);
    void setVolume(int percent);

signals:
    void worldSelected(int index);
    void interpretationSelected(int index);
    void volumeChanged(int percent);

private:
    QVBoxLayout* m_outerLayout = nullptr;
    QVBoxLayout* m_layout = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QList<WorldCard*> m_cards;
    int m_selectedIndex = -1;
    QWidget* m_videoContainer = nullptr;
    QWidget* m_interpContainer = nullptr;
    QVBoxLayout* m_interpLayout = nullptr;
    QComboBox* m_interpCombo = nullptr;
    QWidget* m_volumeContainer = nullptr;
    QSlider* m_volumeSlider = nullptr;
};

// Central pane showing sections and levels for a world
class LevelBrowser : public QWidget
{
    Q_OBJECT
public:
    explicit LevelBrowser(QWidget* parent = nullptr);
    void setWorld(const World& world);
    void setCurrentLevel(int sectionIndex, int levelIndex);
    void showLoading(bool show);

    int selectedInterpretation() const { return m_selectedInterp; }
    void setSelectedInterpretation(int index) { m_selectedInterp = index; }

signals:
    void levelSelected(int sectionIndex, int levelIndex);
    void resumeRequested();
    void interpretationSelected(int index);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void rebuild();
    void selectInterpretation(int index);

    World m_world;
    QWidget* m_content = nullptr;
    QLabel* m_loadingLabel = nullptr;
    int m_currentSection = -1;
    int m_currentLevel = -1;
    int m_selectedInterp = 0;
    QList<QPushButton*> m_playButtons;
    QList<QWidget*> m_interpCards;
    class YouTubePlayer* m_previewPlayer = nullptr;
};

// Load all world definitions from a directory of JSON files
QList<World> loadWorlds(const QString& worldsDir);

} // namespace scoretracker
