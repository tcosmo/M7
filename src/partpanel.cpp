#include "partpanel.h"

#include "engraving/dom/score.h"
#include "engraving/dom/part.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/clef.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/mscore.h"
#include "engraving/dom/note.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/keysig.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/keylist.h"
#include "engraving/dom/factory.h"
#include "engraving/editing/transpose.h"
#include "engraving/rendering/iscorerenderer.h"
#include "engraving/types/fraction.h"
#include "engraving/types/types.h"
#include "engraving/style/styledef.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QResizeEvent>

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
// Radio button style (shared)
// ---------------------------------------------------------------------------

static const char* radioStyleStr =
    "QRadioButton { color: #ccc; font-size: 11px; spacing: 4px; }"
    "QRadioButton::indicator { width: 9px; height: 9px; }"
    "QRadioButton::indicator::unchecked { border: 1px solid #888; border-radius: 5px; background: transparent; }"
    "QRadioButton::indicator::checked { border: 1px solid #4a9eff; border-radius: 5px; background: #4a9eff; }"
    "QRadioButton:disabled { color: #555; }"
    "QRadioButton::indicator:disabled { border-color: #444; background: transparent; }"
    "QRadioButton::indicator::checked:disabled { border-color: #555; background: #555; }";

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

    hLayout->addWidget(m_eyeButton);
    hLayout->addWidget(m_nameLabel);
    hLayout->addWidget(m_arrowButton);

    header->installEventFilter(this);
    m_nameLabel->installEventFilter(this);
    m_arrowButton->installEventFilter(this);
    header->setCursor(Qt::PointingHandCursor);

    mainLayout->addWidget(header);

    // --- Collapsible content area ---
    m_contentArea = new QWidget(this);
    m_contentArea->setAutoFillBackground(true);
    QPalette cpal = m_contentArea->palette();
    cpal.setColor(QPalette::Window, QColor(42, 42, 42));
    m_contentArea->setPalette(cpal);

    auto* contentLayout = new QVBoxLayout(m_contentArea);
    contentLayout->setContentsMargins(32, 6, 12, 6);
    contentLayout->setSpacing(2);

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
        applyClefInternal(static_cast<int>(newClef));
        emit clefChanged();
    });

    clefRow->addWidget(clefLabel);
    clefRow->addWidget(m_clefCombo);
    clefRow->addStretch();
    contentLayout->addLayout(clefRow);

    // Transposing instrument section
    Interval transpose = m_part->instrument()->transpose();
    m_isTransposing = !transpose.isZero();
    if (m_isTransposing) {
        m_origTranspose = transpose;
    }

    auto* transposeSection = new QWidget(m_contentArea);
    auto* transposeSectionLayout = new QVBoxLayout(transposeSection);
    transposeSectionLayout->setContentsMargins(0, 0, 0, 0);
    transposeSectionLayout->setSpacing(4);

    auto* sectionLabel = new QLabel("Transposing Instrument", transposeSection);
    sectionLabel->setStyleSheet("color: #ccc; font-size: 12px;");
    transposeSectionLayout->addWidget(sectionLabel);

    auto* radioRow = new QHBoxLayout();
    radioRow->setContentsMargins(12, 0, 0, 0);
    radioRow->setSpacing(12);

    m_writtenRadio = new QRadioButton("Written", transposeSection);
    m_writtenRadio->setStyleSheet(radioStyleStr);
    m_writtenRadio->setChecked(true);
    m_concertRadio = new QRadioButton("Concert", transposeSection);
    m_concertRadio->setStyleSheet(radioStyleStr);

    radioRow->addWidget(m_writtenRadio);
    radioRow->addWidget(m_concertRadio);
    radioRow->addStretch();
    transposeSectionLayout->addLayout(radioRow);

    if (!m_isTransposing) {
        sectionLabel->setStyleSheet("color: #555; font-size: 12px;");
        m_writtenRadio->setEnabled(false);
        m_concertRadio->setEnabled(false);
    }

    connect(m_concertRadio, &QRadioButton::toggled, [this](bool concert) {
        if (!m_isTransposing) return;
        applyPitchMode(concert);
        emit pitchModeChanged();
    });

    contentLayout->addWidget(transposeSection);

    m_contentArea->hide();
    mainLayout->addWidget(m_contentArea);

    // Update label color based on initial visibility
    if (!m_part->show()) {
        m_nameLabel->setStyleSheet("color: #666; font-size: 12px;");
    }
}

void PartRow::applyPitchMode(bool concert)
{
    if (!m_isTransposing) return;
    if (concert == m_inConcertPitch) return;
    m_inConcertPitch = concert;

    Staff* st = m_part->staff(0);
    Score* score = m_part->score();
    Instrument* inst = m_part->instrument();

    staff_idx_t staffIdx = st->idx();
    track_idx_t startTrack = staffIdx * VOICES;
    track_idx_t endTrack = startTrack + VOICES;

    if (concert) {
        inst->setTranspose(Interval(0, 0));

        for (Segment* seg = score->firstSegment(SegmentType::ChordRest);
             seg; seg = seg->next1(SegmentType::ChordRest)) {
            for (track_idx_t track = startTrack; track < endTrack; ++track) {
                EngravingItem* e = seg->element(track);
                if (!e || !e->isChord()) continue;
                Chord* chord = static_cast<Chord*>(e);
                for (Note* n : chord->notes())
                    n->setTpc2(n->tpc1());
                for (Chord* g : chord->graceNotes())
                    for (Note* n : g->notes())
                        n->setTpc2(n->tpc1());
            }
        }
    } else {
        inst->setTranspose(m_origTranspose);

        Interval flipped = m_origTranspose;
        flipped.flip();
        for (Segment* seg = score->firstSegment(SegmentType::ChordRest);
             seg; seg = seg->next1(SegmentType::ChordRest)) {
            for (track_idx_t track = startTrack; track < endTrack; ++track) {
                EngravingItem* e = seg->element(track);
                if (!e || !e->isChord()) continue;
                Chord* chord = static_cast<Chord*>(e);
                for (Note* n : chord->notes())
                    n->setTpc2(Transpose::transposeTpc(n->tpc1(), flipped, false));
                for (Chord* g : chord->graceNotes())
                    for (Note* n : g->notes())
                        n->setTpc2(Transpose::transposeTpc(n->tpc1(), flipped, false));
            }
        }
    }

    // --- Update key signature ---
    // concert mode: written->concert = transpose UP by orig interval
    // written mode: concert->written = transpose DOWN (flipped)
    Interval keyInterval = m_origTranspose;
    if (!concert) keyInterval.flip();

    // Update existing entries in the KeyList
    KeyList* kl = st->keyList();
    bool hadEntries = !kl->empty();
    for (auto& [tick, kse] : *kl) {
        Key newKey = Transpose::transposeKey(kse.key(), keyInterval);
        kse.setKey(newKey);
    }

    // If KeyList was empty (C major default), insert a transposed entry
    if (!hadEntries) {
        KeySigEvent kse;
        Key newKey = Transpose::transposeKey(Key::C, keyInterval);
        kse.setConcertKey(newKey);
        kl->setKey(0, kse);
    }

    // Update or create the visual KeySig element at tick 0
    Measure* firstMeasure = score->firstMeasure();
    if (firstMeasure) {
        Segment* keySeg = firstMeasure->getSegmentR(SegmentType::KeySig, Fraction(0, 1));
        if (keySeg) {
            EngravingItem* el = keySeg->element(startTrack);
            KeySig* ks = nullptr;
            if (el && el->isKeySig()) {
                ks = static_cast<KeySig*>(el);
            } else {
                ks = Factory::createKeySig(keySeg);
                ks->setTrack(startTrack);
                keySeg->add(ks);
            }
            KeySigEvent targetKse = st->keySigEvent(Fraction(0, 1));
            ks->setKeySigEvent(targetKse);
        }
    }

    // Update radio UI without re-triggering
    if (m_concertRadio) {
        m_concertRadio->blockSignals(true);
        m_writtenRadio->blockSignals(true);
        m_concertRadio->setChecked(concert);
        m_writtenRadio->setChecked(!concert);
        m_concertRadio->blockSignals(false);
        m_writtenRadio->blockSignals(false);
    }
}

void PartRow::setPitchControlEnabled(bool enabled)
{
    if (!m_isTransposing) return;
    if (m_writtenRadio) m_writtenRadio->setEnabled(enabled);
    if (m_concertRadio) m_concertRadio->setEnabled(enabled);
}

void PartRow::setClefControlEnabled(bool enabled)
{
    if (m_clefCombo) m_clefCombo->setEnabled(enabled);
}

void PartRow::applyClefInternal(int clefType)
{
    auto newClef = static_cast<ClefType>(clefType);
    Staff* st = m_part->staff(0);
    st->clefList().setClef(0, ClefTypeList(newClef, newClef));

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

    // Update combo without triggering signal
    if (m_clefCombo) {
        m_clefCombo->blockSignals(true);
        for (int i = 0; i < m_clefCombo->count(); ++i) {
            if (m_clefCombo->itemData(i).toInt() == clefType) {
                m_clefCombo->setCurrentIndex(i);
                break;
            }
        }
        m_clefCombo->blockSignals(false);
    }
}

int PartRow::computeBestClef() const
{
    Staff* st = m_part->staff(0);
    Score* score = m_part->score();
    staff_idx_t staffIdx = st->idx();
    track_idx_t startTrack = staffIdx * VOICES;
    track_idx_t endTrack = startTrack + VOICES;

    int highCount = 0, lowCount = 0;
    for (Segment* seg = score->firstSegment(SegmentType::ChordRest);
         seg; seg = seg->next1(SegmentType::ChordRest)) {
        for (track_idx_t track = startTrack; track < endTrack; ++track) {
            EngravingItem* e = seg->element(track);
            if (!e || !e->isChord()) continue;
            Chord* chord = static_cast<Chord*>(e);
            for (Note* n : chord->notes()) {
                if (n->pitch() >= 60)
                    ++highCount;
                else
                    ++lowCount;
            }
        }
    }

    return (highCount >= lowCount)
        ? static_cast<int>(ClefType::G)
        : static_cast<int>(ClefType::F);
}

void PartRow::applySimplifiedClef()
{
    // Store original clef if not already stored
    if (m_origClefInt < 0) {
        Staff* st = m_part->staff(0);
        m_origClefInt = static_cast<int>(st->clef(Fraction(0, 1)));
    }

    int best = computeBestClef();
    applyClefInternal(best);
}

void PartRow::restoreOriginalClef()
{
    if (m_origClefInt < 0) return;
    applyClefInternal(m_origClefInt);
    m_origClefInt = -1;
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
    if (auto* scrollArea = qobject_cast<QScrollArea*>(parentWidget()->parentWidget())) {
        scrollArea->setUpdatesEnabled(false);
        m_contentArea->setVisible(m_expanded);
        m_arrowButton->setArrowType(m_expanded ? Qt::DownArrow : Qt::RightArrow);
        scrollArea->setUpdatesEnabled(true);
    } else {
        m_contentArea->setVisible(m_expanded);
        m_arrowButton->setArrowType(m_expanded ? Qt::DownArrow : Qt::RightArrow);
    }
}

bool PartRow::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease) {
        toggleExpand();
        return true;
    }
    return QWidget::eventFilter(obj, event);
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

    // --- Global settings ---

    // Transposing instruments
    auto* pitchSection = new QWidget(this);
    auto* pitchLayout = new QVBoxLayout(pitchSection);
    pitchLayout->setContentsMargins(0, 0, 0, 0);
    pitchLayout->setSpacing(2);

    auto* pitchLabel = new QLabel("Transposing Instruments", pitchSection);
    pitchLabel->setStyleSheet("color: #ccc; font-size: 12px;");
    pitchLayout->addWidget(pitchLabel);

    m_transposingListLabel = new QLabel(pitchSection);
    m_transposingListLabel->setStyleSheet("color: #888; font-size: 9px; padding-left: 1px;");
    m_transposingListLabel->setTextFormat(Qt::PlainText);
    m_transposingListLabel->hide();
    pitchLayout->addWidget(m_transposingListLabel);

    auto* pitchRadioRow = new QHBoxLayout();
    pitchRadioRow->setContentsMargins(12, 0, 0, 0);
    pitchRadioRow->setSpacing(10);

    m_globalPitchWritten = new QRadioButton("Written", pitchSection);
    m_globalPitchWritten->setStyleSheet(radioStyleStr);
    m_globalPitchConcert = new QRadioButton("Concert", pitchSection);
    m_globalPitchConcert->setStyleSheet(radioStyleStr);
    m_globalPitchPerPart = new QRadioButton("Per Part", pitchSection);
    m_globalPitchPerPart->setStyleSheet(radioStyleStr);
    m_globalPitchWritten->setChecked(true);

    pitchRadioRow->addWidget(m_globalPitchWritten);
    pitchRadioRow->addWidget(m_globalPitchConcert);
    pitchRadioRow->addWidget(m_globalPitchPerPart);
    pitchRadioRow->addStretch();
    pitchLayout->addLayout(pitchRadioRow);

    layout->addWidget(pitchSection);

    // Clefs
    auto* clefSection = new QWidget(this);
    auto* clefLayout = new QVBoxLayout(clefSection);
    clefLayout->setContentsMargins(0, 4, 0, 0);
    clefLayout->setSpacing(2);

    auto* clefLabel = new QLabel("Clefs", clefSection);
    clefLabel->setStyleSheet("color: #ccc; font-size: 12px;");
    clefLayout->addWidget(clefLabel);

    auto* clefRadioRow = new QHBoxLayout();
    clefRadioRow->setContentsMargins(12, 0, 0, 0);
    clefRadioRow->setSpacing(10);

    m_globalClefWritten = new QRadioButton("Written", clefSection);
    m_globalClefWritten->setStyleSheet(radioStyleStr);
    m_globalClefSimplified = new QRadioButton("Treble && Bass", clefSection);
    m_globalClefSimplified->setStyleSheet(radioStyleStr);
    m_globalClefPerPart = new QRadioButton("Per Part", clefSection);
    m_globalClefPerPart->setStyleSheet(radioStyleStr);
    m_globalClefWritten->setChecked(true);

    clefRadioRow->addWidget(m_globalClefWritten);
    clefRadioRow->addWidget(m_globalClefSimplified);
    clefRadioRow->addWidget(m_globalClefPerPart);
    clefRadioRow->addStretch();
    clefLayout->addLayout(clefRadioRow);

    layout->addWidget(clefSection);

    // Connect global settings
    auto pitchChanged = [this](bool checked) {
        if (!checked) return;
        applyGlobalSettings();
        relayout();
    };
    connect(m_globalPitchWritten, &QRadioButton::toggled, pitchChanged);
    connect(m_globalPitchConcert, &QRadioButton::toggled, pitchChanged);
    connect(m_globalPitchPerPart, &QRadioButton::toggled, pitchChanged);

    auto clefChanged = [this](bool checked) {
        if (!checked) return;
        applyGlobalSettings();
        relayout();
    };
    connect(m_globalClefWritten, &QRadioButton::toggled, clefChanged);
    connect(m_globalClefSimplified, &QRadioButton::toggled, clefChanged);
    connect(m_globalClefPerPart, &QRadioButton::toggled, clefChanged);

    // --- Buttons ---
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
        connect(row, &PartRow::pitchModeChanged, this, &PartPanel::relayout);
        m_rowsLayout->addWidget(row);
        m_rows.push_back(row);
    }

    m_rowsLayout->addStretch();

    // Update transposing instruments list label
    QStringList transposingNames;
    for (auto* row : m_rows) {
        if (row->isTransposing()) {
            transposingNames << row->part()->partName().toQString();
        }
    }
    m_transposingFullText = transposingNames.join(", ");
    updateTransposingLabel();

    // Apply current global settings to new rows
    applyGlobalSettings();
}

void PartPanel::applyGlobalSettings()
{
    bool perPart = m_globalPitchPerPart->isChecked();
    bool concert = m_globalPitchConcert->isChecked();
    bool simplified = m_globalClefSimplified->isChecked();

    for (auto* row : m_rows) {
        // Pitch mode
        if (perPart) {
            row->setPitchControlEnabled(true);
        } else {
            row->applyPitchMode(concert);
            row->setPitchControlEnabled(false);
        }

        // Clef mode
        bool clefPerPart = m_globalClefPerPart->isChecked();
        if (clefPerPart) {
            row->setClefControlEnabled(true);
        } else if (simplified) {
            row->applySimplifiedClef();
            row->setClefControlEnabled(false);
        } else {
            // Written
            row->restoreOriginalClef();
            row->setClefControlEnabled(false);
        }
    }
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

void PartPanel::updateTransposingLabel()
{
    bool hasTransposing = !m_transposingFullText.isEmpty();
    m_globalPitchWritten->setEnabled(hasTransposing);
    m_globalPitchConcert->setEnabled(hasTransposing);
    m_globalPitchPerPart->setEnabled(hasTransposing);

    if (!hasTransposing) {
        m_transposingListLabel->setText("No transposing instruments.");
        m_transposingListLabel->setToolTip(QString());
        m_transposingListLabel->show();
        return;
    }
    m_transposingListLabel->setToolTip(m_transposingFullText);
    QFontMetrics fm(m_transposingListLabel->font());
    int availWidth = width() - 36; // layout margins + label padding
    if (availWidth < 60) availWidth = 160;
    QString elided = fm.elidedText(m_transposingFullText, Qt::ElideRight, availWidth);
    m_transposingListLabel->setText(elided);
    m_transposingListLabel->show();
}

void PartPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateTransposingLabel();
}

} // namespace scoretracker
