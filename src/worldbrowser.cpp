#include "worldbrowser.h"
#include "theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QPushButton>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QDir>

namespace scoretracker {

// ---------------------------------------------------------------------------
// WorldCard
// ---------------------------------------------------------------------------

WorldCard::WorldCard(const World& world, QWidget* parent)
    : QWidget(parent), m_world(world)
{
    setFixedHeight(80);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QString("%1 — %2").arg(world.composer, world.title));
}

void WorldCard::setSelected(bool selected)
{
    m_selected = selected;
    update();
}

void WorldCard::mousePressEvent(QMouseEvent*)
{
    emit clicked();
}

void WorldCard::enterEvent(QEnterEvent*)
{
    m_hovered = true;
    update();
}

void WorldCard::leaveEvent(QEvent*)
{
    m_hovered = false;
    update();
}

void WorldCard::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Background
    QColor bg = m_selected ? QColor(0x40, 0x44, 0x4b)
               : m_hovered ? QColor(0x36, 0x39, 0x3f)
               : QColor(0x2f, 0x31, 0x36);
    p.setBrush(bg);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(rect().adjusted(4, 2, -4, -2), 8, 8);

    // Selection indicator — orange bar on the left
    if (m_selected) {
        p.setBrush(Theme::accent());
        p.drawRoundedRect(QRect(4, 8, 4, height() - 16), 2, 2);
    }

    // Cover thumbnail
    int imgSize = 56;
    int imgX = 16;
    int imgY = (height() - imgSize) / 2;
    if (!m_world.cover.isNull()) {
        QPixmap scaled = m_world.cover.scaled(
            imgSize * 2, imgSize * 2, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        scaled = scaled.copy((scaled.width() - imgSize * 2) / 2, (scaled.height() - imgSize * 2) / 2, imgSize * 2, imgSize * 2);
        scaled.setDevicePixelRatio(2);

        // Rounded clip
        QPainterPath clipPath;
        clipPath.addRoundedRect(QRectF(imgX, imgY, imgSize, imgSize), 6, 6);
        p.setClipPath(clipPath);
        p.drawPixmap(imgX, imgY, scaled);
        p.setClipping(false);
    }

    // Text
    int textX = imgX + imgSize + 10;
    int textW = width() - textX - 12;

    p.setPen(Theme::textPrimary());
    QFont titleFont = font();
    titleFont.setPointSize(11);
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.drawText(QRect(textX, imgY + 2, textW, 20), Qt::AlignLeft | Qt::AlignVCenter,
               p.fontMetrics().elidedText(m_world.title, Qt::ElideRight, textW));

    p.setPen(Theme::textSecondary());
    QFont subFont = font();
    subFont.setPointSize(10);
    p.setFont(subFont);
    p.drawText(QRect(textX, imgY + 22, textW, 16), Qt::AlignLeft | Qt::AlignVCenter,
               m_world.composer);

    p.setPen(Theme::textHint());
    QFont catFont = font();
    catFont.setPointSize(9);
    p.setFont(catFont);
    p.drawText(QRect(textX, imgY + 38, textW, 14), Qt::AlignLeft | Qt::AlignVCenter,
               m_world.catalogue);
}

// ---------------------------------------------------------------------------
// WorldSidebar
// ---------------------------------------------------------------------------

WorldSidebar::WorldSidebar(QWidget* parent)
    : QWidget(parent)
{
    setFixedWidth(240);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Theme::surfaceBg());
    setPalette(pal);

    m_outerLayout = new QVBoxLayout(this);
    m_outerLayout->setContentsMargins(0, 0, 0, 0);
    m_outerLayout->setSpacing(0);

    // Header
    auto* header = new QLabel("  Worlds", this);
    QFont hf = header->font();
    hf.setPointSize(13);
    hf.setBold(true);
    header->setFont(hf);
    header->setFixedHeight(40);
    header->setStyleSheet(QString("color: %1; padding-left: 12px;").arg(Theme::textPrimary().name()));
    m_outerLayout->addWidget(header);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setStyleSheet(Theme::scrollBarStyleStr());

    auto* container = new QWidget();
    m_layout = new QVBoxLayout(container);
    m_layout->setContentsMargins(4, 0, 4, 8);
    m_layout->setSpacing(4);
    m_layout->addStretch();

    m_scrollArea->setWidget(container);
    m_outerLayout->addWidget(m_scrollArea, 1);

    // Video container — initially empty, filled by setVideoWidget
    m_videoContainer = new QWidget(this);
    m_videoContainer->setFixedHeight(0);
    m_outerLayout->addWidget(m_videoContainer);

    // Interpretation list — below the video
    m_interpContainer = new QWidget(this);
    m_interpLayout = new QVBoxLayout(m_interpContainer);
    m_interpLayout->setContentsMargins(8, 4, 8, 8);
    m_interpLayout->setSpacing(2);
    m_interpContainer->hide();
    m_outerLayout->addWidget(m_interpContainer);

    // Volume slider — below interpretations
    m_volumeContainer = new QWidget(this);
    auto* volLayout = new QHBoxLayout(m_volumeContainer);
    volLayout->setContentsMargins(8, 4, 8, 8);
    volLayout->setSpacing(6);
    auto* volLabel = new QLabel("Vol");
    volLabel->setStyleSheet(QString("color: %1; font-size: 10px;").arg(Theme::textSecondary().name()));
    volLayout->addWidget(volLabel);
    m_volumeSlider = new QSlider(Qt::Horizontal);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(80);
    connect(m_volumeSlider, &QSlider::valueChanged,
            this, &WorldSidebar::volumeChanged);
    volLayout->addWidget(m_volumeSlider);
    m_volumeContainer->hide();
    m_outerLayout->addWidget(m_volumeContainer);
}

void WorldSidebar::setVideoWidget(QWidget* videoWidget)
{
    if (!videoWidget) return;
    videoWidget->setParent(m_videoContainer);
    videoWidget->setFixedSize(240, 200);
    m_videoContainer->setFixedHeight(200);
    auto* vl = new QVBoxLayout(m_videoContainer);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->addWidget(videoWidget);
}

void WorldSidebar::setInterpretations(const QStringList& labels, int activeIndex)
{
    clearInterpretations();
    if (labels.isEmpty()) return;

    auto* header = new QLabel("Interpretation");
    QFont hf = header->font();
    hf.setPointSize(10);
    hf.setBold(true);
    header->setFont(hf);
    header->setStyleSheet(QString("color: %1;").arg(Theme::textSecondary().name()));
    m_interpLayout->addWidget(header);

    m_interpCombo = new QComboBox();
    m_interpCombo->addItems(labels);
    m_interpCombo->setCurrentIndex(activeIndex);
    connect(m_interpCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &WorldSidebar::interpretationSelected);
    m_interpLayout->addWidget(m_interpCombo);

    m_interpContainer->show();
}

void WorldSidebar::clearInterpretations()
{
    m_interpCombo = nullptr;
    while (m_interpLayout->count() > 0) {
        auto* item = m_interpLayout->takeAt(0);
        if (item->widget()) delete item->widget();
        delete item;
    }
    m_interpContainer->hide();
}

void WorldSidebar::showVolumeSlider(bool show)
{
    m_volumeContainer->setVisible(show);
}

void WorldSidebar::setWorlds(const QList<World>& worlds)
{
    // Clear old cards
    for (auto* card : m_cards) card->deleteLater();
    m_cards.clear();

    for (int i = 0; i < worlds.size(); ++i) {
        auto* card = new WorldCard(worlds[i]);
        connect(card, &WorldCard::clicked, this, [this, i]() {
            m_selectedIndex = i;
            for (int j = 0; j < m_cards.size(); ++j)
                m_cards[j]->setSelected(j == i);
            emit worldSelected(i);
        });
        m_layout->insertWidget(m_layout->count() - 1, card); // before stretch
        m_cards.append(card);
    }

    // Auto-select first
    if (!m_cards.isEmpty()) {
        m_cards[0]->setSelected(true);
        m_selectedIndex = 0;
        emit worldSelected(0);
    }
}

// ---------------------------------------------------------------------------
// LevelBrowser
// ---------------------------------------------------------------------------

LevelBrowser::LevelBrowser(QWidget* parent)
    : QScrollArea(parent)
{
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);

    QPalette pal = palette();
    pal.setColor(QPalette::Window, Theme::scoreBg());
    setPalette(pal);
    setAutoFillBackground(true);

    setStyleSheet(Theme::scrollBarStyleStr());

    m_content = new QWidget();
    setWidget(m_content);
}

void LevelBrowser::setWorld(const World& world)
{
    m_world = world;
    rebuild();
}

void LevelBrowser::setCurrentLevel(int sectionIndex, int levelIndex)
{
    m_currentSection = sectionIndex;
    m_currentLevel = levelIndex;
    rebuild();
}

void LevelBrowser::showLoading(bool show)
{
    if (show) {
        if (!m_loadingLabel) {
            m_loadingLabel = new QLabel("Loading...", this);
            m_loadingLabel->setAlignment(Qt::AlignCenter);
            QFont f = m_loadingLabel->font();
            f.setPointSize(16);
            m_loadingLabel->setFont(f);
            m_loadingLabel->setStyleSheet(QString(
                "color: %1; background: %2; border-radius: 8px; padding: 20px;"
            ).arg(Theme::textPrimary().name(), Theme::scoreBg().name()));
        }
        m_loadingLabel->setGeometry(rect());
        m_loadingLabel->raise();
        m_loadingLabel->show();
    } else {
        if (m_loadingLabel) {
            m_loadingLabel->hide();
        }
    }
}

void LevelBrowser::rebuild()
{
    // Delete old content
    delete m_content;
    m_content = new QWidget();
    m_playButtons.clear();
    setWidget(m_content);

    auto* layout = new QVBoxLayout(m_content);
    layout->setContentsMargins(32, 24, 32, 24);
    layout->setSpacing(6);

    // World header — cover + title
    auto* headerRow = new QWidget();
    auto* headerLayout = new QHBoxLayout(headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(20);

    if (!m_world.cover.isNull()) {
        auto* coverLabel = new QLabel();
        QPixmap scaled = m_world.cover.scaled(120 * 2, 120 * 2, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        scaled = scaled.copy((scaled.width() - 120*2)/2, (scaled.height() - 120*2)/2, 120*2, 120*2);
        scaled.setDevicePixelRatio(2);
        coverLabel->setPixmap(scaled);
        coverLabel->setFixedSize(120, 120);
        coverLabel->setStyleSheet("border-radius: 8px;");
        headerLayout->addWidget(coverLabel, 0, Qt::AlignTop);
    }

    auto* titleBlock = new QWidget();
    auto* titleLayout = new QVBoxLayout(titleBlock);
    titleLayout->setContentsMargins(0, 8, 0, 0);
    titleLayout->setSpacing(4);

    auto* composerLabel = new QLabel(m_world.composer);
    composerLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(Theme::textHint().name()));
    titleLayout->addWidget(composerLabel);

    auto* titleLabel = new QLabel(m_world.title);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(22);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setStyleSheet(QString("color: %1;").arg(Theme::textPrimary().name()));
    titleLabel->setWordWrap(true);
    titleLayout->addWidget(titleLabel);

    auto* catLabel = new QLabel(m_world.catalogue);
    catLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(Theme::textSecondary().name()));
    titleLayout->addWidget(catLabel);
    titleLayout->addStretch();

    headerLayout->addWidget(titleBlock, 1);
    layout->addWidget(headerRow);
    layout->addSpacing(20);

    // "Levels" heading
    auto* levelsLabel = new QLabel("Levels");
    QFont lf = levelsLabel->font();
    lf.setPointSize(16);
    lf.setBold(true);
    levelsLabel->setFont(lf);
    levelsLabel->setStyleSheet(QString("color: %1;").arg(Theme::textPrimary().name()));
    layout->addWidget(levelsLabel);
    layout->addSpacing(8);

    // Sections
    for (int si = 0; si < m_world.sections.size(); ++si) {
        const auto& section = m_world.sections[si];

        // Section header
        auto* sectionLabel = new QLabel(section.title);
        QFont sf = sectionLabel->font();
        sf.setPointSize(14);
        sf.setBold(true);
        sectionLabel->setFont(sf);
        sectionLabel->setStyleSheet(QString("color: %1; padding-top: 8px;").arg(Theme::textPrimary().name()));
        layout->addWidget(sectionLabel);

        if (section.levels.isEmpty()) {
            auto* comingSoon = new QLabel("Coming soon...");
            comingSoon->setStyleSheet(QString("color: %1; font-size: 11px; font-style: italic; padding-left: 8px;")
                .arg(Theme::textDisabled().name()));
            layout->addWidget(comingSoon);
        }

        // Level cards
        for (int li = 0; li < section.levels.size(); ++li) {
            const auto& level = section.levels[li];
            bool isCurrent = (si == m_currentSection && li == m_currentLevel);

            auto* card = new QWidget();
            card->setObjectName("levelCard");
            card->setFixedHeight(56);
            card->setMaximumWidth(520);
            card->setCursor(Qt::PointingHandCursor);
            if (isCurrent) {
                card->setStyleSheet(QString(
                    "#levelCard { background: %1; border: 2px solid %2; border-radius: 8px; }"
                ).arg(Theme::inputBg().name(), Theme::accent().name()));
            } else {
                card->setStyleSheet(QString(
                    "#levelCard { background: %1; border-radius: 8px; }"
                    "#levelCard:hover { background: %2; }"
                ).arg(Theme::panelBg().name(), Theme::inputBg().name()));
            }

            auto* cardLayout = new QHBoxLayout(card);
            cardLayout->setContentsMargins(16, 0, 16, 0);

            // Playing indicator (always present to keep alignment)
            auto* dot = new QLabel("\u25B6");
            dot->setFixedWidth(16);
            dot->setStyleSheet(QString("color: %1; font-size: 10px;")
                .arg(isCurrent ? Theme::accent().name() : "transparent"));
            cardLayout->addWidget(dot);

            auto* nameLabel = new QLabel(level.title);
            nameLabel->setStyleSheet(QString("color: %1; font-size: 13px; font-weight: bold;")
                .arg(isCurrent ? Theme::accent().name() : Theme::textPrimary().name()));
            cardLayout->addWidget(nameLabel);

            if (!level.description.isEmpty()) {
                auto* descLabel = new QLabel(level.description);
                descLabel->setStyleSheet(QString("color: %1; font-size: 11px;")
                    .arg(Theme::textHint().name()));
                cardLayout->addWidget(descLabel);
            }

            cardLayout->addSpacing(16);

            auto* playBtn = new QPushButton(isCurrent ? "Resume" : "Play");
            playBtn->setFixedSize(72, 32);
            playBtn->setStyleSheet(QString(
                "QPushButton { background: %1; color: white; border-radius: 6px; font-size: 12px; font-weight: bold; }"
                "QPushButton:hover { background: %2; }"
            ).arg(Theme::accent().name(), Theme::accentHover().name()));
            cardLayout->addWidget(playBtn);
            m_playButtons.append(playBtn);

            if (isCurrent) {
                // "Resume" navigates back to score view
                connect(playBtn, &QPushButton::clicked, this, [this]() {
                    emit resumeRequested();
                });
            } else {
                connect(playBtn, &QPushButton::clicked, this, [this, si, li]() {
                    emit levelSelected(si, li);
                });
            }

            layout->addWidget(card);
        }

        layout->addSpacing(8);
    }

    layout->addStretch();
}

// ---------------------------------------------------------------------------
// World loading from JSON
// ---------------------------------------------------------------------------

QList<World> loadWorlds(const QString& worldsDir)
{
    QList<World> worlds;
    QDir dir(worldsDir);
    QStringList jsonFiles = dir.entryList({"*.json"}, QDir::Files, QDir::Name);

    for (const auto& fileName : jsonFiles) {
        QString filePath = dir.absoluteFilePath(fileName);
        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly)) continue;

        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject()) continue;
        QJsonObject obj = doc.object();

        World world;
        world.id = obj["id"].toString();
        world.title = obj["title"].toString();
        world.composer = obj["composer"].toString();
        world.catalogue = obj["catalogue"].toString();

        // Resolve paths relative to the JSON file
        QDir jsonDir = QFileInfo(filePath).absoluteDir();

        QString coverRel = obj["cover"].toString();
        if (!coverRel.isEmpty()) {
            world.coverPath = jsonDir.absoluteFilePath(coverRel);
            world.cover = QPixmap(world.coverPath);
        }

        QJsonArray sectionsArr = obj["sections"].toArray();
        for (const auto& sv : sectionsArr) {
            QJsonObject so = sv.toObject();
            Section section;
            section.id = so["id"].toString();
            section.title = so["title"].toString();
            if (so.contains("score"))
                section.scorePath = jsonDir.absoluteFilePath(so["score"].toString());
            if (so.contains("sources"))
                section.sourcesPath = jsonDir.absoluteFilePath(so["sources"].toString());
            if (so.contains("beats"))
                section.beatsPath = jsonDir.absoluteFilePath(so["beats"].toString());

            QJsonArray levelsArr = so["levels"].toArray();
            for (const auto& lv : levelsArr) {
                QJsonObject lo = lv.toObject();
                Level level;
                level.id = lo["id"].toString();
                level.title = lo["title"].toString();
                level.description = lo["description"].toString();
                level.playPart = lo["playPart"].toInt(-1);
                level.gmProgram = lo["gmProgram"].toInt(34);
                QJsonArray partsArr = lo["parts"].toArray();
                for (const auto& pv : partsArr)
                    level.parts.append(pv.toInt());
                section.levels.append(level);
            }

            world.sections.append(section);
        }

        worlds.append(world);
    }

    return worlds;
}

} // namespace scoretracker
