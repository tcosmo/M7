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
#include <QRegularExpression>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QScrollBar>
#include <QTimer>
#include <algorithm>
#include <cmath>

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
    setFixedWidth(220);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Theme::surfaceBg());
    setPalette(pal);

    m_outerLayout = new QVBoxLayout(this);
    m_outerLayout->setContentsMargins(0, 0, 0, 0);
    m_outerLayout->setSpacing(0);

    // Header
    auto* header = new QLabel("  PlayBach", this);
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
    m_interpContainer->setMaximumWidth(220);
    m_interpLayout = new QVBoxLayout(m_interpContainer);
    m_interpLayout->setContentsMargins(8, 4, 8, 8);
    m_interpLayout->setSpacing(2);
    m_interpContainer->hide();
    m_outerLayout->addWidget(m_interpContainer);

    // Volume slider — below interpretations
    m_volumeContainer = new QWidget(this);
    m_volumeContainer->setMaximumWidth(220);
    auto* volLayout = new QHBoxLayout(m_volumeContainer);
    volLayout->setContentsMargins(8, 4, 8, 8);
    volLayout->setSpacing(6);
    auto* volLabel = new QLabel("Vol");
    volLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(Theme::textSecondary().name()));
    volLayout->addWidget(volLabel);
    m_volumeSlider = new QSlider(Qt::Horizontal);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(80);
    m_volumeSlider->setToolTip("Volume: 80%");
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int val) {
        m_volumeSlider->setToolTip(QString("Volume: %1%").arg(val));
        emit volumeChanged(val);
    });
    volLayout->addWidget(m_volumeSlider);
    m_volumeContainer->hide();
    m_outerLayout->addWidget(m_volumeContainer);
}

void WorldSidebar::setVideoWidget(QWidget* videoWidget)
{
    if (!videoWidget) return;
    videoWidget->setParent(m_videoContainer);
    videoWidget->setFixedSize(220, 200);
    videoWidget->setMinimumSize(0, 0);
    videoWidget->setMaximumWidth(220);
    m_videoContainer->setFixedSize(220, 200);
    m_videoContainer->setMaximumWidth(220);
    auto* vl = new QVBoxLayout(m_videoContainer);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->addWidget(videoWidget);
}

void WorldSidebar::setExpandButton(QWidget* button)
{
    if (!button) return;
    // Insert just above the video container
    int idx = m_outerLayout->indexOf(m_videoContainer);
    if (idx >= 0)
        m_outerLayout->insertWidget(idx, button);
    else
        m_outerLayout->addWidget(button);
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

void WorldSidebar::setVolume(int percent)
{
    if (m_volumeSlider) {
        m_volumeSlider->blockSignals(true);
        m_volumeSlider->setValue(percent);
        m_volumeSlider->blockSignals(false);
        emit volumeChanged(percent);
    }
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
    : QWidget(parent)
{
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Theme::contentBg());
    setPalette(pal);
    setAutoFillBackground(true);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    m_content = new QWidget();
    rootLayout->addWidget(m_content);
}

void LevelBrowser::setWorld(const World& world)
{
    m_world = world;
    m_currentSection = -1;
    m_currentLevel = -1;
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

// Star rating widget that draws 3 stars with partial fill
class StarRatingWidget : public QWidget
{
public:
    StarRatingWidget(double rating, QColor fillColor, QColor emptyColor, QWidget* parent = nullptr)
        : QWidget(parent), m_rating(rating), m_fill(fillColor), m_empty(emptyColor)
    {
        setFixedSize(60, 16);
        setAttribute(Qt::WA_TranslucentBackground);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        int starSize = 14;
        int spacing = 4;
        int totalW = 3 * starSize + 2 * spacing;
        int x0 = (width() - totalW) / 2;
        int y0 = (height() - starSize) / 2;

        for (int i = 0; i < 3; ++i) {
            int sx = x0 + i * (starSize + spacing);
            double fill = std::clamp(m_rating - i, 0.0, 1.0);
            drawStar(p, sx, y0, starSize, fill);
        }
    }
private:
    void drawStar(QPainter& p, int x, int y, int size, double fill) {
        // Build star polygon
        QPainterPath star;
        double cx = x + size / 2.0, cy = y + size / 2.0;
        double outer = size / 2.0, inner = outer * 0.38;
        for (int i = 0; i < 10; ++i) {
            double angle = M_PI / 2.0 + i * M_PI / 5.0;
            double r = (i % 2 == 0) ? outer : inner;
            double px = cx + r * cos(angle);
            double py = cy - r * sin(angle);
            if (i == 0) star.moveTo(px, py);
            else star.lineTo(px, py);
        }
        star.closeSubpath();

        // Draw empty star background
        p.setPen(Qt::NoPen);
        p.setBrush(m_empty);
        p.drawPath(star);

        if (fill > 0.0) {
            // Clip to left portion for partial fill
            p.save();
            QRectF clipRect(x, y, size * fill, size);
            p.setClipRect(clipRect);
            p.setBrush(m_fill);
            p.drawPath(star);
            p.restore();
        }
    }
    double m_rating;
    QColor m_fill, m_empty;
};

void LevelBrowser::rebuild()
{
    // Delete old content
    delete m_content;
    m_content = new QWidget();
    m_playButtons.clear();
    layout()->addWidget(m_content);

    m_content->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* layout = new QVBoxLayout(m_content);
    layout->setContentsMargins(16, 24, 32, 24);
    layout->setSpacing(6);

    // World header — cover + title block side by side
    int coverSize = 180;
    auto* headerRow = new QWidget();
    auto* headerLayout = new QHBoxLayout(headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(24);

    if (!m_world.cover.isNull()) {
        auto* coverLabel = new QLabel();
        QPixmap scaled = m_world.cover.scaled(coverSize * 2, coverSize * 2,
            Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        scaled = scaled.copy((scaled.width() - coverSize*2)/2,
            (scaled.height() - coverSize*2)/2, coverSize*2, coverSize*2);
        scaled.setDevicePixelRatio(2);
        coverLabel->setPixmap(scaled);
        coverLabel->setFixedSize(coverSize, coverSize);
        coverLabel->setStyleSheet("border-radius: 10px;");
        coverLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        headerLayout->addWidget(coverLabel, 0, Qt::AlignTop);
    }

    auto* titleBlock = new QWidget();
    auto* titleLayout = new QVBoxLayout(titleBlock);
    titleLayout->setContentsMargins(0, 4, 0, 0);
    titleLayout->setSpacing(4);

    auto* composerLabel = new QLabel(m_world.composer);
    composerLabel->setStyleSheet(QString("color: %1; font-size: 13px;").arg(Theme::textHint().name()));
    titleLayout->addWidget(composerLabel);

    auto* titleLabel = new QLabel(m_world.title);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(26);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setStyleSheet(QString("color: %1;").arg(Theme::textPrimary().name()));
    titleLabel->setWordWrap(true);
    titleLayout->addWidget(titleLabel);

    auto* catLabel = new QLabel(m_world.catalogue);
    catLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(Theme::textSecondary().name()));
    titleLayout->addWidget(catLabel);

    // Description tucked under the catalogue
    if (!m_world.description.isEmpty()) {
        titleLayout->addSpacing(8);
        auto* descLabel = new QLabel(m_world.description);
        descLabel->setWordWrap(true);
        descLabel->setMaximumWidth(600);
        descLabel->setStyleSheet(QString("color: %1; font-size: 12px;")
            .arg(Theme::textHint().name()));
        titleLayout->addWidget(descLabel);
    }

    headerLayout->addWidget(titleBlock, 1);
    layout->addWidget(headerRow);

    // --- Interpretations ---
    m_interpCards.clear();
    if (!m_world.interpretations.isEmpty()) {
        if (m_selectedInterp >= m_world.interpretations.size())
            m_selectedInterp = 0;

        layout->addSpacing(16);
        auto* interpLabel = new QLabel("Interpretations");
        QFont ilf = interpLabel->font();
        ilf.setPointSize(18);
        ilf.setBold(true);
        interpLabel->setFont(ilf);
        interpLabel->setStyleSheet(QString("color: %1;").arg(Theme::textPrimary().name()));
        layout->addWidget(interpLabel);
        layout->addSpacing(4);

        auto* interpScroll = new QScrollArea();
        interpScroll->setWidgetResizable(true);
        interpScroll->setFrameShape(QFrame::NoFrame);
        interpScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        interpScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        interpScroll->setFixedHeight(152);
        interpScroll->setStyleSheet("background: transparent;");

        auto* interpRow = new QWidget();
        auto* interpRowLayout = new QHBoxLayout(interpRow);
        interpRowLayout->setContentsMargins(0, 0, 0, 0);
        interpRowLayout->setSpacing(12);

        auto* nam = new QNetworkAccessManager(this);
        int thumbW = 180, thumbH = 101; // 16:9
        int selThumbW = 216, selThumbH = 122;

        for (int idx = 0; idx < m_world.interpretations.size(); ++idx) {
            const auto& interp = m_world.interpretations[idx];
            bool sel = (idx == m_selectedInterp);
            int tw = sel ? selThumbW : thumbW;
            int th = sel ? selThumbH : thumbH;

            auto* card = new QWidget();
            card->setObjectName("interpCard");
            card->setCursor(Qt::PointingHandCursor);
            card->setFixedSize(tw + 6, th + 24); // +6 for border space

            auto* cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(0, 0, 0, 0);
            cardLayout->setSpacing(3);

            auto* thumbLabel = new QLabel();
            thumbLabel->setObjectName("interpThumb");
            thumbLabel->setFixedSize(tw, th);
            thumbLabel->setAlignment(Qt::AlignCenter);
            if (sel) {
                thumbLabel->setStyleSheet(QString(
                    "background: %1; border-radius: 6px; border: 3px solid %2;"
                ).arg(Theme::panelBg().name(), Theme::accent().name()));
            } else {
                thumbLabel->setStyleSheet(QString(
                    "background: %1; border-radius: 6px; border: 3px solid transparent;"
                ).arg(Theme::panelBg().name()));
            }
            cardLayout->addWidget(thumbLabel);

            auto* nameLabel = new QLabel(interp.label);
            nameLabel->setObjectName("interpName");
            nameLabel->setAlignment(Qt::AlignCenter);
            nameLabel->setStyleSheet(QString("color: %1; font-size: 12px;")
                .arg(sel ? Theme::accent().name() : Theme::textSecondary().name()));
            nameLabel->setMaximumWidth(tw);
            QFontMetrics fm(nameLabel->font());
            nameLabel->setText(fm.elidedText(interp.label, Qt::ElideRight, tw));
            cardLayout->addWidget(nameLabel);

            m_interpCards.append(card);
            interpRowLayout->addWidget(card);

            // Click to select
            auto* clickBtn = new QPushButton(card);
            clickBtn->setGeometry(0, 0, tw + 6, th + 24);
            clickBtn->setStyleSheet("background: transparent; border: none;");
            clickBtn->setCursor(Qt::PointingHandCursor);
            clickBtn->setFocusPolicy(Qt::NoFocus);
            connect(clickBtn, &QPushButton::clicked, this, [this, idx]() {
                selectInterpretation(idx);
            });

            // Download thumbnail
            if (!interp.videoId.isEmpty()) {
                QString thumbUrl = QString("https://img.youtube.com/vi/%1/mqdefault.jpg").arg(interp.videoId);
                QNetworkReply* reply = nam->get(QNetworkRequest(QUrl(thumbUrl)));
                connect(reply, &QNetworkReply::finished, thumbLabel, [reply, thumbLabel, tw, th]() {
                    reply->deleteLater();
                    if (reply->error() != QNetworkReply::NoError) return;
                    QPixmap pix;
                    pix.loadFromData(reply->readAll());
                    if (pix.isNull()) return;
                    QPixmap scaled = pix.scaled(tw * 2, th * 2,
                        Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                    scaled = scaled.copy((scaled.width() - tw*2)/2,
                        (scaled.height() - th*2)/2, tw*2, th*2);
                    scaled.setDevicePixelRatio(2);
                    thumbLabel->setPixmap(scaled);
                });
            }
        }

        interpRowLayout->addStretch();
        interpScroll->setWidget(interpRow);
        layout->addWidget(interpScroll);

        // Scroll to make the selected interpretation visible
        if (m_selectedInterp >= 0 && m_selectedInterp < m_interpCards.size()) {
            QWidget* targetCard = m_interpCards[m_selectedInterp];
            QTimer::singleShot(0, targetCard, [interpScroll, targetCard]() {
                interpScroll->ensureWidgetVisible(targetCard, 20, 0);
            });
        }
    }

    // --- Custom tab bar ---
    struct TabDef { QString title; QString subtitle; };
    TabDef tabDefs[] = {
        { "Levels",   "One movement, one mission" },
        { "Campaign", "Play the entire work, no interruption" },
        { "Sandbox",  "Pick any part, build your own level" },
    };

    auto* tabBarRow = new QHBoxLayout();
    tabBarRow->setContentsMargins(0, 0, 0, 0);
    tabBarRow->setSpacing(0);

    auto* stack = new QStackedWidget();
    QList<QWidget*> tabButtons;

    for (int t = 0; t < 3; ++t) {
        auto* btn = new QWidget();
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(52);
        auto* btnLayout = new QVBoxLayout(btn);
        btnLayout->setContentsMargins(0, 8, 0, 6);
        btnLayout->setSpacing(2);

        auto* titleLbl = new QLabel(tabDefs[t].title);
        titleLbl->setAlignment(Qt::AlignCenter);
        QFont tf = titleLbl->font();
        tf.setPointSize(15);
        tf.setBold(true);
        titleLbl->setFont(tf);
        titleLbl->setStyleSheet(QString("color: %1;").arg(
            t == 0 ? Theme::accent().name() : Theme::textHint().name()));
        titleLbl->setObjectName("tabTitle");
        btnLayout->addWidget(titleLbl);

        auto* subLbl = new QLabel(tabDefs[t].subtitle);
        subLbl->setAlignment(Qt::AlignCenter);
        subLbl->setStyleSheet(QString("color: %1; font-size: 11px;").arg(Theme::textHint().name()));
        subLbl->setObjectName("tabSub");
        btnLayout->addWidget(subLbl);

        tabButtons.append(btn);
        tabBarRow->addWidget(btn, 1);
    }

    // Bottom accent line (only under selected tab)
    auto updateTabStyles = [tabButtons, stack](int sel) {
        for (int i = 0; i < tabButtons.size(); ++i) {
            auto* w = tabButtons[i];
            auto* title = w->findChild<QLabel*>("tabTitle");
            auto* sub = w->findChild<QLabel*>("tabSub");
            if (i == sel) {
                w->setStyleSheet(QString("border-bottom: 2px solid %1;").arg(Theme::accent().name()));
                if (title) title->setStyleSheet(QString("color: %1;").arg(Theme::accent().name()));
                if (sub) sub->setStyleSheet(QString("color: %1; font-size: 11px;").arg(Theme::textSecondary().name()));
            } else {
                w->setStyleSheet("border-bottom: 2px solid transparent;");
                if (title) title->setStyleSheet(QString("color: %1;").arg(Theme::textHint().name()));
                if (sub) sub->setStyleSheet(QString("color: %1; font-size: 11px;").arg(Theme::textHint().name()));
            }
        }
        stack->setCurrentIndex(sel);
    };

    // Click handling via transparent overlay buttons
    for (int t = 0; t < 3; ++t) {
        auto* clickBtn = new QPushButton(tabButtons[t]);
        clickBtn->setFixedSize(9999, 52);
        clickBtn->setStyleSheet("background: transparent; border: none;");
        clickBtn->setCursor(Qt::PointingHandCursor);
        clickBtn->setFocusPolicy(Qt::NoFocus);
        connect(clickBtn, &QPushButton::clicked, this, [updateTabStyles, t]() {
            updateTabStyles(t);
        });
    }

    layout->addLayout(tabBarRow);

    // --- Tab 1: Levels ---
    auto* levelsScroll = new QScrollArea();
    levelsScroll->setWidgetResizable(true);
    levelsScroll->setFrameShape(QFrame::NoFrame);
    levelsScroll->setStyleSheet(
        QString("QScrollArea { background: %1; } %2")
        .arg(Theme::contentBg().name(), Theme::scrollBarStyleStr()));
    auto* levelsWidget = new QWidget();
    levelsWidget->setStyleSheet(QString("background: %1;").arg(Theme::contentBg().name()));
    auto* levelsLayout = new QVBoxLayout(levelsWidget);
    levelsLayout->setContentsMargins(0, 4, 0, 12);
    levelsLayout->setSpacing(0);

    int levelNum = 0;
    for (int si = 0; si < m_world.sections.size(); ++si) {
        const auto& section = m_world.sections[si];

        auto* sectionLabel = new QLabel(section.title);
        QFont sf = sectionLabel->font();
        sf.setPointSize(14);
        sf.setBold(true);
        sectionLabel->setFont(sf);
        sectionLabel->setStyleSheet(QString("color: %1; padding-top: 8px;").arg(Theme::textPrimary().name()));
        levelsLayout->addWidget(sectionLabel);

        if (section.levels.isEmpty()) {
            auto* comingSoon = new QLabel("Coming soon...");
            comingSoon->setStyleSheet(QString("color: %1; font-size: 11px; font-style: italic; padding-left: 8px;")
                .arg(Theme::textDisabled().name()));
            levelsLayout->addWidget(comingSoon);
        }

        // Column header row
        if (!section.levels.isEmpty()) {
            auto* headerRow = new QWidget();
            headerRow->setFixedHeight(28);
            auto* hl = new QHBoxLayout(headerRow);
            hl->setContentsMargins(12, 0, 12, 0);
            hl->setSpacing(0);
            QString hdrStyle = QString("color: %1; font-size: 10px; font-weight: bold;").arg(Theme::textHint().name());
            auto* hNum = new QLabel("#"); hNum->setFixedWidth(32); hNum->setStyleSheet(hdrStyle);
            auto* hTitle = new QLabel("Title"); hTitle->setStyleSheet(hdrStyle);
            auto* hInstr = new QLabel("Instruments"); hInstr->setFixedWidth(75); hInstr->setAlignment(Qt::AlignCenter); hInstr->setStyleSheet(hdrStyle);
            auto* hDiff = new QLabel("Difficulty"); hDiff->setFixedWidth(70); hDiff->setAlignment(Qt::AlignCenter); hDiff->setStyleSheet(hdrStyle);
            auto* hMyPlays = new QLabel("My Plays"); hMyPlays->setFixedWidth(60); hMyPlays->setAlignment(Qt::AlignCenter); hMyPlays->setStyleSheet(hdrStyle);
            auto* hTotal = new QLabel("Total Plays"); hTotal->setFixedWidth(75); hTotal->setAlignment(Qt::AlignCenter); hTotal->setStyleSheet(hdrStyle);
            auto* hAcc = new QLabel("Accuracy"); hAcc->setFixedWidth(65); hAcc->setAlignment(Qt::AlignCenter); hAcc->setStyleSheet(hdrStyle);
            auto* hPlay = new QLabel(""); hPlay->setFixedWidth(80);
            hl->addWidget(hNum); hl->addWidget(hTitle, 1); hl->addWidget(hInstr); hl->addWidget(hDiff); hl->addWidget(hMyPlays); hl->addWidget(hTotal); hl->addWidget(hAcc); hl->addWidget(hPlay);
            levelsLayout->addWidget(headerRow);

            // Thin separator
            auto* sep = new QWidget();
            sep->setFixedHeight(1);
            sep->setStyleSheet(QString("background: %1;").arg(Theme::inputBg().name()));
            levelsLayout->addWidget(sep);
        }

        for (int li = 0; li < section.levels.size(); ++li) {
            const auto& level = section.levels[li];
            bool isCurrent = (si == m_currentSection && li == m_currentLevel);
            ++levelNum;

            auto* row = new QWidget();
            row->setObjectName("levelRow");
            row->setFixedHeight(32);
            row->setCursor(Qt::PointingHandCursor);
            if (isCurrent) {
                row->setStyleSheet(QString(
                    "#levelRow { background: %1; border-radius: 4px; }"
                    "#levelRow QLabel { background: transparent; }"
                ).arg(Theme::inputBg().name()));
            } else {
                row->setStyleSheet(QString(
                    "#levelRow { background: transparent; border-radius: 4px; }"
                    "#levelRow:hover { background: %1; }"
                    "#levelRow QLabel { background: transparent; }"
                ).arg(Theme::panelBg().name()));
            }

            auto* rl = new QHBoxLayout(row);
            rl->setContentsMargins(12, 0, 12, 0);
            rl->setSpacing(0);

            // # column
            auto* numLabel = new QLabel(QString::number(levelNum));
            numLabel->setFixedWidth(32);
            numLabel->setStyleSheet(QString("color: %1; font-size: 12px;")
                .arg(isCurrent ? Theme::accent().name() : Theme::textHint().name()));
            rl->addWidget(numLabel);

            // Title
            auto* titleLabel = new QLabel(level.title);
            titleLabel->setStyleSheet(QString("color: %1; font-size: 13px; font-weight: bold;")
                .arg(isCurrent ? Theme::accent().name() : Theme::textPrimary().name()));
            rl->addWidget(titleLabel, 1);

            // Instruments count
            int nInstr = level.voices.isEmpty() ? 1 : level.voices.size();
            auto* instrLabel = new QLabel(QString::number(nInstr));
            instrLabel->setFixedWidth(75);
            instrLabel->setAlignment(Qt::AlignCenter);
            instrLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(Theme::textHint().name()));
            rl->addWidget(instrLabel);

            // Difficulty stars (custom painted)
            auto* starsW = new StarRatingWidget(level.difficulty,
                Theme::accent(), Theme::inputBg());
            starsW->setFixedWidth(70);
            rl->addWidget(starsW);

            // My plays (placeholder — will come from player database)
            auto* myPlaysLabel = new QLabel("0");
            myPlaysLabel->setFixedWidth(60);
            myPlaysLabel->setAlignment(Qt::AlignCenter);
            myPlaysLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(Theme::textHint().name()));
            rl->addWidget(myPlaysLabel);

            // Total plays on platform (placeholder — will come from server)
            auto* totalPlaysLabel = new QLabel("0");
            totalPlaysLabel->setFixedWidth(75);
            totalPlaysLabel->setAlignment(Qt::AlignCenter);
            totalPlaysLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(Theme::textHint().name()));
            rl->addWidget(totalPlaysLabel);

            // Accuracy (placeholder — will come from player database)
            auto* accLabel = new QLabel("—");
            accLabel->setFixedWidth(65);
            accLabel->setAlignment(Qt::AlignCenter);
            accLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(Theme::textHint().name()));
            rl->addWidget(accLabel);

            // Play button — always says "Play", always goes through levelSelected
            auto* playBtn = new QPushButton("Play");
            playBtn->setFixedSize(76, 28);
            playBtn->setStyleSheet(QString(
                "QPushButton { background: %1; color: white; border-radius: 14px; font-size: 11px; font-weight: bold; }"
                "QPushButton:hover { background: %2; }"
            ).arg(Theme::accent().name(), Theme::accentHover().name()));
            rl->addWidget(playBtn);
            m_playButtons.append(playBtn);

            // Both row click and button click trigger levelSelected
            connect(playBtn, &QPushButton::clicked, this, [this, si, li]() {
                emit levelSelected(si, li);
            });

            // Store indices on the row for click handling
            row->setProperty("si", si);
            row->setProperty("li", li);
            row->installEventFilter(this);

            levelsLayout->addWidget(row);
        }

        levelsLayout->addSpacing(8);
    }
    levelsLayout->addStretch();
    levelsScroll->setWidget(levelsWidget);
    stack->addWidget(levelsScroll);

    // --- Tab 2: Campaign ---
    auto* campaignWidget = new QWidget();
    auto* campaignLayout = new QVBoxLayout(campaignWidget);
    campaignLayout->setContentsMargins(0, 24, 0, 12);
    campaignLayout->setSpacing(12);
    auto* campaignSoon = new QLabel("Coming soon...");
    campaignSoon->setStyleSheet(QString("color: %1; font-size: 13px; font-style: italic;")
        .arg(Theme::textDisabled().name()));
    campaignSoon->setAlignment(Qt::AlignCenter);
    campaignLayout->addWidget(campaignSoon);
    campaignLayout->addStretch();
    stack->addWidget(campaignWidget);

    // --- Tab 3: Sandbox ---
    auto* sandboxWidget = new QWidget();
    auto* sandboxLayout = new QVBoxLayout(sandboxWidget);
    sandboxLayout->setContentsMargins(0, 24, 0, 12);
    sandboxLayout->setSpacing(12);
    auto* sandboxSoon = new QLabel("Coming soon...");
    sandboxSoon->setStyleSheet(QString("color: %1; font-size: 13px; font-style: italic;")
        .arg(Theme::textDisabled().name()));
    sandboxSoon->setAlignment(Qt::AlignCenter);
    sandboxLayout->addWidget(sandboxSoon);
    sandboxLayout->addStretch();
    stack->addWidget(sandboxWidget);

    // Select first tab
    updateTabStyles(0);
    layout->addWidget(stack, 1);
}

bool LevelBrowser::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        auto* w = qobject_cast<QWidget*>(obj);
        if (w && w->objectName() == "levelRow") {
            int si = w->property("si").toInt();
            int li = w->property("li").toInt();
            emit levelSelected(si, li);
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void LevelBrowser::selectInterpretation(int index)
{
    if (index < 0 || index >= m_world.interpretations.size()) return;
    m_selectedInterp = index;
    emit interpretationSelected(index);
    rebuild(); // refresh to update selection visuals
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
        world.description = obj["description"].toString();
        world.order = obj.value("order").toInt(0);

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
                level.difficulty = lo.value("difficulty").toDouble(0);
                level.playPart = lo["playPart"].toInt(-1);
                level.gmProgram = lo["gmProgram"].toInt(34);
                level.soundfont = lo["soundfont"].toString();
                QJsonArray partsArr = lo["parts"].toArray();
                for (const auto& pv : partsArr)
                    level.parts.append(pv.toInt());
                // Multi-voice config
                if (lo.contains("voices")) {
                    QJsonArray voicesArr = lo["voices"].toArray();
                    for (const auto& vv : voicesArr) {
                        QJsonObject vo = vv.toObject();
                        VoiceConfig vc;
                        vc.playPart = vo["playPart"].toInt(-1);
                        vc.gmProgram = vo["gmProgram"].toInt(34);
                        vc.soundfont = vo["soundfont"].toString();
                        vc.keys = vo.value("keys").toString("all");
                        level.voices.append(vc);
                    }
                }
                section.levels.append(level);
            }

            world.sections.append(section);
        }

        // Load interpretations from first section's sources.json
        for (const auto& sec : world.sections) {
            if (sec.sourcesPath.isEmpty()) continue;
            QFile sf(sec.sourcesPath);
            if (!sf.open(QIODevice::ReadOnly)) continue;
            QJsonDocument sdoc = QJsonDocument::fromJson(sf.readAll());
            if (!sdoc.isObject()) break;
            QJsonObject sobj = sdoc.object();
            QJsonValue ytVal = sobj.value("youtube");
            if (ytVal.isArray()) {
                for (const auto& yv : ytVal.toArray()) {
                    QJsonObject yo = yv.toObject();
                    Interpretation interp;
                    interp.label = yo.value("label").toString("YouTube");
                    interp.youtubeUrl = yo.value("url").toString();
                    // Extract video ID from URL
                    QRegularExpression re("(?:v=|youtu\\.be/)([\\w-]{11})");
                    auto match = re.match(interp.youtubeUrl);
                    if (match.hasMatch())
                        interp.videoId = match.captured(1);
                    world.interpretations.append(interp);
                }
            }
            break; // only first section with sources
        }

        worlds.append(world);
    }

    std::sort(worlds.begin(), worlds.end(), [](const World& a, const World& b) {
        return a.order < b.order;
    });

    return worlds;
}

} // namespace scoretracker
