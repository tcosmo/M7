#include "partpanel.h"

#include "engraving/dom/score.h"
#include "engraving/dom/part.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/clef.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/mscore.h"
#include "engraving/rendering/iscorerenderer.h"
#include "engraving/types/fraction.h"
#include "engraving/types/types.h"
#include "engraving/style/styledef.h"

#include <QPainter>
#include <QPainterPath>

using namespace mu::engraving;
using namespace mu::engraving::rendering;

namespace scoretracker {

// ---------------------------------------------------------------------------
// Eye icon helpers
// ---------------------------------------------------------------------------

static QIcon makeEyeIcon(bool visible)
{
    int sz = 32; // render at 2x for retina
    QPixmap px(sz, sz);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);

    QColor col = visible ? Qt::white : QColor("#666");

    // Eye shape: two arcs forming an almond
    QPainterPath path;
    qreal cx = sz / 2.0, cy = sz / 2.0;
    qreal w = sz * 0.7, h = sz * 0.32;
    path.moveTo(cx - w / 2, cy);
    path.quadTo(cx, cy - h, cx + w / 2, cy);
    path.quadTo(cx, cy + h, cx - w / 2, cy);

    if (visible) {
        p.setPen(QPen(col, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        // Iris circle
        p.setBrush(col);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(cx, cy), sz * 0.1, sz * 0.1);
    } else {
        p.setPen(QPen(col, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        // Diagonal strike-through
        p.drawLine(QPointF(cx - w * 0.35, cy + h * 0.6),
                   QPointF(cx + w * 0.35, cy - h * 0.6));
    }

    px.setDevicePixelRatio(2);
    return QIcon(px);
}

// ---------------------------------------------------------------------------
// PartRow
// ---------------------------------------------------------------------------

PartRow::PartRow(Part* part, const QString& name, QWidget* parent)
    : QWidget(parent)
    , m_part(part)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // --- Header row ---
    auto* header = new QWidget(this);
    header->setFixedHeight(28);
    header->setAutoFillBackground(true);
    QPalette hpal = header->palette();
    hpal.setColor(QPalette::Window, QColor(37, 37, 37));
    header->setPalette(hpal);

    auto* hLayout = new QHBoxLayout(header);
    hLayout->setContentsMargins(4, 0, 4, 0);
    hLayout->setSpacing(4);

    // Eye button
    m_eyeButton = new QToolButton(header);
    m_eyeButton->setFixedSize(20, 20);
    m_eyeButton->setAutoRaise(true);
    m_eyeButton->setIconSize(QSize(16, 16));
    updateEyeIcon();

    connect(m_eyeButton, &QToolButton::clicked, [this]() {
        bool newState = !m_part->show();
        m_part->setShow(newState);
        updateEyeIcon();
        emit visibilityToggled();
    });

    // Name label
    m_nameLabel = new QLabel(name, header);
    m_nameLabel->setStyleSheet("color: white; font-size: 12px;");
    m_nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    // Arrow button
    m_arrowButton = new QToolButton(header);
    m_arrowButton->setFixedSize(16, 16);
    m_arrowButton->setAutoRaise(true);
    m_arrowButton->setArrowType(Qt::RightArrow);
    m_arrowButton->setStyleSheet(
        "QToolButton { color: #aaa; border: none; background: transparent; }"
        "QToolButton:pressed { color: #aaa; }"
        "QToolButton:focus { outline: none; }"
    );

    connect(m_arrowButton, &QToolButton::clicked, [this]() {
        toggleExpand();
    });

    hLayout->addWidget(m_eyeButton);
    hLayout->addWidget(m_nameLabel);
    hLayout->addWidget(m_arrowButton);

    mainLayout->addWidget(header);

    // --- Collapsible content area ---
    m_contentArea = new QWidget(this);
    m_contentArea->setAutoFillBackground(true);
    QPalette cpal = m_contentArea->palette();
    cpal.setColor(QPalette::Window, QColor(42, 42, 42));
    m_contentArea->setPalette(cpal);

    auto* contentLayout = new QVBoxLayout(m_contentArea);
    contentLayout->setContentsMargins(32, 6, 12, 6);

    // Clef selector row
    auto* clefRow = new QHBoxLayout();
    clefRow->setSpacing(6);
    auto* clefLabel = new QLabel("Clef", m_contentArea);
    clefLabel->setStyleSheet("color: #ccc; font-size: 11px;");
    m_clefCombo = new QComboBox(m_contentArea);
    m_clefCombo->setFixedWidth(160);
    m_clefCombo->addItem("Treble Clef",       static_cast<int>(ClefType::G));
    m_clefCombo->addItem("Bass Clef",         static_cast<int>(ClefType::F));
    m_clefCombo->addItem("Soprano Clef (C1)", static_cast<int>(ClefType::C1));
    m_clefCombo->addItem("Mezzo-Soprano Clef (C2)", static_cast<int>(ClefType::C2));
    m_clefCombo->addItem("Alto Clef (C3)",    static_cast<int>(ClefType::C3));
    m_clefCombo->addItem("Tenor Clef (C4)",   static_cast<int>(ClefType::C4));
    m_clefCombo->addItem("Baritone Clef (C5)",static_cast<int>(ClefType::C5));

    // Set current clef from the part's first staff
    Staff* staff = m_part->staff(0);
    ClefType currentClef = staff->clef(Fraction(0, 1));
    for (int i = 0; i < m_clefCombo->count(); ++i) {
        if (m_clefCombo->itemData(i).toInt() == static_cast<int>(currentClef)) {
            m_clefCombo->setCurrentIndex(i);
            break;
        }
    }

    connect(m_clefCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int index) {
        auto newClef = static_cast<ClefType>(m_clefCombo->itemData(index).toInt());
        Staff* st = m_part->staff(0);
        st->clefList().setClef(0, ClefTypeList(newClef, newClef));

        // Update the Clef element in the HeaderClef segment of the first measure
        Score* score = m_part->score();
        Measure* m = score->firstMeasure();
        if (m) {
            Segment* seg = m->first(SegmentType::HeaderClef);
            if (seg) {
                track_idx_t track = st->idx() * VOICES;
                EngravingItem* el = seg->element(track);
                if (el && el->isClef()) {
                    static_cast<Clef*>(el)->setClefType(ClefTypeList(newClef, newClef));
                }
            }
        }

        emit clefChanged();
    });

    clefRow->addWidget(clefLabel);
    clefRow->addWidget(m_clefCombo);
    clefRow->addStretch();
    contentLayout->addLayout(clefRow);

    m_contentArea->hide();
    mainLayout->addWidget(m_contentArea);

    // Update label color based on initial visibility
    if (!m_part->show()) {
        m_nameLabel->setStyleSheet("color: #666; font-size: 12px;");
    }
}

void PartRow::setPartVisible(bool visible)
{
    m_part->setShow(visible);
    updateEyeIcon();
}

bool PartRow::isPartVisible() const
{
    return m_part->show();
}

void PartRow::updateEyeIcon()
{
    bool vis = m_part->show();
    m_eyeButton->setIcon(makeEyeIcon(vis));
    if (m_nameLabel) {
        m_nameLabel->setStyleSheet(
            vis ? "color: white; font-size: 12px;"
                : "color: #666; font-size: 12px;");
    }
}

void PartRow::toggleExpand()
{
    m_expanded = !m_expanded;
    m_contentArea->setVisible(m_expanded);
    m_arrowButton->setArrowType(m_expanded ? Qt::DownArrow : Qt::RightArrow);
}

// ---------------------------------------------------------------------------
// PartPanel
// ---------------------------------------------------------------------------

PartPanel::PartPanel(QWidget* parent)
    : QWidget(parent)
{
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(37, 37, 37));
    pal.setColor(QPalette::Base, QColor(37, 37, 37));
    setPalette(pal);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 4, 12, 10);

    // Buttons
    auto* btnLayout = new QHBoxLayout();
    auto* btnAll = new QPushButton("Show All", this);
    auto* btnSolo = new QPushButton("Solo", this);

    btnLayout->addWidget(btnAll);
    btnLayout->addWidget(btnSolo);
    layout->addLayout(btnLayout);

    connect(btnAll, &QPushButton::clicked, this, &PartPanel::showAllParts);
    connect(btnSolo, &QPushButton::clicked, this, &PartPanel::showSoloPart);

    // Scroll area for part rows
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setStyleSheet(
        "QScrollBar:vertical { background: palette(window); border: none; width: 14px; }"
        "QScrollBar::handle:vertical { background: rgba(255,255,255,0.15); border-radius: 4px; min-height: 30px; margin: 2px 3px; }"
        "QScrollBar::handle:vertical:hover { background: rgba(255,255,255,0.25); }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
    );

    m_scrollContent = new QWidget();
    m_rowsLayout = new QVBoxLayout(m_scrollContent);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(1);
    m_rowsLayout->addStretch();

    m_scrollArea->setWidget(m_scrollContent);
    layout->addWidget(m_scrollArea);

    setFocusPolicy(Qt::ClickFocus);
}

void PartPanel::setScore(Score* score)
{
    m_score = score;
    populateList();
}

void PartPanel::setRenderer(IScoreRenderer* renderer)
{
    m_renderer = renderer;
}

int PartPanel::desiredHeight() const
{
    // Each collapsed row is 28px, plus 1px spacing between rows
    int rowCount = static_cast<int>(m_rows.size());
    int totalRowH = rowCount * 28 + std::max(0, rowCount - 1) * 1;
    // buttons (~30px) + layout margins (4 top + 10 bottom) + some padding
    return totalRowH + 30 + 14 + 10;
}

void PartPanel::populateList()
{
    // Clear existing rows
    m_rows.clear();
    m_parts.clear();

    // Remove all widgets from layout (except the stretch)
    while (m_rowsLayout->count() > 0) {
        auto* item = m_rowsLayout->takeAt(0);
        if (item->widget()) {
            delete item->widget();
        }
        delete item;
    }

    if (!m_score) {
        m_rowsLayout->addStretch();
        return;
    }

    for (Part* part : m_score->parts()) {
        m_parts.push_back(part);

        QString name = part->partName().toQString();
        if (name.isEmpty()) {
            name = part->instrumentName().toQString();
        }
        if (name.isEmpty()) {
            name = part->longName().toQString();
        }
        if (name.isEmpty()) {
            name = "Untitled Part";
        }

        auto* row = new PartRow(part, name, m_scrollContent);
        connect(row, &PartRow::visibilityToggled, this, &PartPanel::relayout);
        connect(row, &PartRow::clefChanged, this, &PartPanel::relayout);
        m_rowsLayout->addWidget(row);
        m_rows.push_back(row);
    }

    m_rowsLayout->addStretch();
}

void PartPanel::showSoloPart()
{
    if (!m_score || m_rows.empty()) return;

    // Solo the first visible row, or just the first row
    int selectedRow = 0;
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        if (m_rows[i]->isPartVisible()) {
            selectedRow = i;
            break;
        }
    }

    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        updateRow(i, i == selectedRow);
    }
    relayout();
}

void PartPanel::showAllParts()
{
    if (!m_score) return;

    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        updateRow(i, true);
    }
    relayout();
}

void PartPanel::updateRow(int index, bool visible)
{
    if (index < 0 || index >= static_cast<int>(m_rows.size())) return;
    m_rows[index]->setPartVisible(visible);
}

void PartPanel::relayout()
{
    if (!m_score || !m_renderer) return;

    // Count visible parts
    int visibleCount = 0;
    for (const auto* part : m_parts) {
        if (part->show()) ++visibleCount;
    }

    // Hide instrument names on subsequent systems when <= 2 parts visible
    auto subsVis = (visibleCount <= 2)
        ? InstrumentLabelVisibility::HIDE
        : InstrumentLabelVisibility::SHORT;
    m_score->style().set(Sid::subsSystemInstNameVisibility,
        mu::engraving::PropertyValue(int(subsVis)));

    m_renderer->layoutScore(m_score, Fraction(0, 1), Fraction(-1, 1));
    emit partsChanged();
}

} // namespace scoretracker
