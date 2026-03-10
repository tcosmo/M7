#pragma once

#include <QString>
#include <QList>
#include <QPixmap>

namespace scoretracker {

struct Level {
    QString id;
    QString title;
    QString description;
    QList<int> parts;       // 1-based part numbers to show
    int playPart = -1;      // 1-based part to activate for play-along
    int gmProgram = 34;     // GM instrument (default: Electric Bass pick)
};

struct Section {
    QString id;
    QString title;
    QString scorePath;      // absolute path to MusicXML
    QString sourcesPath;    // absolute path to sources.json
    QString beatsPath;      // absolute path to beatdata.json
    QList<Level> levels;
};

struct World {
    QString id;
    QString title;
    QString composer;
    QString catalogue;      // e.g. "BWV 243"
    QString coverPath;      // absolute path to cover image
    QPixmap cover;          // loaded pixmap
    QList<Section> sections;
};

} // namespace scoretracker
