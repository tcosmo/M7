#pragma once

#include <QString>
#include <QList>
#include <QPixmap>

namespace scoretracker {

struct VoiceConfig {
    int playPart = -1;      // 1-based part to activate for play-along
    int gmProgram = 34;     // GM instrument
    QString soundfont;      // soundfont filename, empty = default
    QString keys = "all";   // "left", "right", "all"
};

struct Level {
    QString id;
    QString title;
    QString description;
    QList<int> parts;       // 1-based part numbers to show
    int playPart = -1;      // 1-based part to activate for play-along (single-voice)
    int gmProgram = 34;     // GM instrument (default: Electric Bass pick)
    QString soundfont;      // soundfont filename (e.g. "MS Basic.sf3"), empty = default
    QList<VoiceConfig> voices; // multi-voice config (if non-empty, overrides playPart/gmProgram/soundfont)
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
    int order = 0;          // sort order (lower = first)
    QList<Section> sections;
};

} // namespace scoretracker
