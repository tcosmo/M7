#include "partpanel.h"
#include "theme.h"

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
#include <QGuiApplication>
#include <QCoreApplication>
#include <QSet>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

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

    QColor col = visible ? Theme::iconVisible() : Theme::iconHidden();

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
// Octave-shifted clef helpers
// ---------------------------------------------------------------------------

// Returns the base clef (strips octave shift) for G and F clef families.
static ClefType getBaseClef(ClefType clef)
{
    switch (clef) {
    case ClefType::G:
    case ClefType::G8_VB:
    case ClefType::G8_VA:
    case ClefType::G15_MB:
    case ClefType::G15_MA:
    case ClefType::G8_VB_O:
    case ClefType::G8_VB_P:
    case ClefType::G8_VB_C:
        return ClefType::G;
    case ClefType::F:
    case ClefType::F8_VB:
    case ClefType::F_8VA:
    case ClefType::F15_MB:
    case ClefType::F_15MA:
        return ClefType::F;
    default:
        return clef;
    }
}

// Returns the octave offset encoded in an octave-shifted clef variant.
static int getOctaveFromClef(ClefType clef)
{
    switch (clef) {
    case ClefType::G8_VB:
    case ClefType::G8_VB_O:
    case ClefType::G8_VB_P:
    case ClefType::G8_VB_C:
    case ClefType::F8_VB:
        return -1;
    case ClefType::G8_VA:
    case ClefType::F_8VA:
        return 1;
    case ClefType::G15_MB:
    case ClefType::F15_MB:
        return -2;
    case ClefType::G15_MA:
    case ClefType::F_15MA:
        return 2;
    default:
        return 0;
    }
}

// Returns the ClefType for a given base clef and octave offset.
// Only supports G and F families with offsets -1, 0, +1.
static ClefType getOctaveClef(ClefType base, int octave)
{
    if (base == ClefType::G) {
        if (octave == -1) return ClefType::G8_VB;
        if (octave ==  1) return ClefType::G8_VA;
        return ClefType::G;
    }
    if (base == ClefType::F) {
        if (octave == -1) return ClefType::F8_VB;
        if (octave ==  1) return ClefType::F_8VA;
        return ClefType::F;
    }
    return base; // C clefs: no octave variants
}

// Returns true if the base clef supports octave shifting (G or F family).
static bool clefSupportsOctave(ClefType base)
{
    return base == ClefType::G || base == ClefType::F;
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
    m_header = new QWidget(this);
    auto* header = m_header;
    header->setFixedHeight(28);
    header->setAutoFillBackground(true);
    QPalette hpal = header->palette();
    hpal.setColor(QPalette::Window, Theme::panelBg());
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
        if (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) {
            emit soloRequested();
            return;
        }
        bool newState = !m_part->show();
        m_part->setShow(newState);
        updateEyeIcon();
        emit visibilityToggled();
    });

    // Name label
    m_nameLabel = new QLabel(name, header);
    m_nameLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(Theme::textPrimary().name()));
    m_nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    // Arrow button
    m_arrowButton = new QToolButton(header);
    m_arrowButton->setFixedSize(16, 16);
    m_arrowButton->setAutoRaise(true);
    m_arrowButton->setArrowType(Qt::RightArrow);
    m_arrowButton->setStyleSheet(
        QString("QToolButton { color: %1; border: none; background: transparent; }"
                "QToolButton:pressed { color: %1; }"
                "QToolButton:focus { outline: none; }").arg(Theme::arrowColor().name())
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
    cpal.setColor(QPalette::Window, Theme::contentBg());
    m_contentArea->setPalette(cpal);

    auto* contentLayout = new QVBoxLayout(m_contentArea);
    contentLayout->setContentsMargins(32, 6, 12, 6);
    contentLayout->setSpacing(2);

    // Clef selector row
    auto* clefRow = new QHBoxLayout();
    clefRow->setSpacing(6);
    m_clefLabel = new QLabel("Clef", m_contentArea);
    auto* clefLabel = m_clefLabel;
    clefLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(Theme::textSecondary().name()));
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
    m_xmlClefInt = static_cast<int>(currentClef);
    ClefType baseClef = getBaseClef(currentClef);
    for (int i = 0; i < m_clefCombo->count(); ++i) {
        if (m_clefCombo->itemData(i).toInt() == static_cast<int>(baseClef)) {
            m_clefCombo->setCurrentIndex(i);
            break;
        }
    }

    connect(m_clefCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int index) {
        auto selectedClef = static_cast<ClefType>(m_clefCombo->itemData(index).toInt());
        int octave = m_octaveCombo ? m_octaveCombo->currentData().toInt() : 0;
        ClefType finalClef = getOctaveClef(selectedClef, octave);
        applyClefInternal(static_cast<int>(finalClef));
        updateOctaveCombo();
        emit clefChanged();
    });

    clefRow->addWidget(clefLabel);
    clefRow->addWidget(m_clefCombo);
    clefRow->addStretch();
    contentLayout->addLayout(clefRow);

    // Octave selector row
    auto* octaveRow = new QHBoxLayout();
    octaveRow->setSpacing(6);
    m_octaveLabel = new QLabel("Octave", m_contentArea);
    auto* octaveLabel = m_octaveLabel;
    octaveLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(Theme::textSecondary().name()));
    m_octaveCombo = new QComboBox(m_contentArea);
    m_octaveCombo->setFixedWidth(160);
    m_octaveCombo->addItem("Written",           0);
    m_octaveCombo->addItem("8va (octave up)",   1);
    m_octaveCombo->addItem("8vb (octave down)", -1);

    // Set initial octave from current clef
    int initOctave = getOctaveFromClef(currentClef);
    for (int i = 0; i < m_octaveCombo->count(); ++i) {
        if (m_octaveCombo->itemData(i).toInt() == initOctave) {
            m_octaveCombo->setCurrentIndex(i);
            break;
        }
    }

    connect(m_octaveCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int index) {
        int octave = m_octaveCombo->itemData(index).toInt();
        auto baseClef = static_cast<ClefType>(m_clefCombo->currentData().toInt());
        ClefType finalClef = getOctaveClef(baseClef, octave);
        applyClefInternal(static_cast<int>(finalClef));
        emit clefChanged();
    });

    octaveRow->addWidget(octaveLabel);
    octaveRow->addWidget(m_octaveCombo);
    octaveRow->addStretch();
    contentLayout->addLayout(octaveRow);

    // Enable/disable octave combo based on initial clef
    updateOctaveCombo();

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

    m_sectionLabel = new QLabel("Transposing Instrument", transposeSection);
    auto* sectionLabel = m_sectionLabel;
    sectionLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(Theme::textSecondary().name()));
    transposeSectionLayout->addWidget(sectionLabel);

    auto* radioRow = new QHBoxLayout();
    radioRow->setContentsMargins(12, 0, 0, 0);
    radioRow->setSpacing(12);

    m_writtenRadio = new QRadioButton("Written", transposeSection);
    m_writtenRadio->setStyleSheet(Theme::radioStyleStr());
    m_writtenRadio->setChecked(true);
    m_concertRadio = new QRadioButton("Concert", transposeSection);
    m_concertRadio->setStyleSheet(Theme::radioStyleStr());

    radioRow->addWidget(m_writtenRadio);
    radioRow->addWidget(m_concertRadio);
    radioRow->addStretch();
    transposeSectionLayout->addLayout(radioRow);

    if (!m_isTransposing) {
        sectionLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(Theme::textDisabled().name()));
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
        m_nameLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(Theme::textHidden().name()));
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
    if (m_octaveCombo) {
        if (!enabled)
            m_octaveCombo->setEnabled(false);
        else
            updateOctaveCombo();
    }
}

void PartRow::setOctaveControlEnabled(bool enabled)
{
    if (m_octaveCombo) {
        if (!enabled)
            m_octaveCombo->setEnabled(false);
        else
            updateOctaveCombo();
    }
}

void PartRow::updateOctaveCombo()
{
    if (!m_octaveCombo || !m_clefCombo) return;
    auto baseClef = static_cast<ClefType>(m_clefCombo->currentData().toInt());
    bool supported = clefSupportsOctave(baseClef);
    m_octaveCombo->setEnabled(supported);
    if (!supported) {
        // Reset to "Written" (0) when switching to a clef without octave support
        m_octaveCombo->blockSignals(true);
        for (int i = 0; i < m_octaveCombo->count(); ++i) {
            if (m_octaveCombo->itemData(i).toInt() == 0) {
                m_octaveCombo->setCurrentIndex(i);
                break;
            }
        }
        m_octaveCombo->blockSignals(false);
    }
}

void PartRow::applyOriginalClef()
{
    if (m_xmlClefInt < 0) return;
    // Apply the MusicXML clef to the score without touching combos
    auto clef = static_cast<ClefType>(m_xmlClefInt);
    Staff* st = m_part->staff(0);
    st->clefList().setClef(0, ClefTypeList(clef, clef));

    Score* score = m_part->score();
    Measure* m = score->firstMeasure();
    if (m) {
        Segment* seg = m->first(SegmentType::HeaderClef);
        if (seg) {
            track_idx_t track = st->idx() * VOICES;
            EngravingItem* el = seg->element(track);
            if (el && el->isClef())
                static_cast<Clef*>(el)->setClefType(ClefTypeList(clef, clef));
        }
    }
}

void PartRow::reapplyPerPartClef()
{
    if (!m_clefCombo) return;
    auto base = static_cast<ClefType>(m_clefCombo->currentData().toInt());
    int octave = m_octaveCombo ? m_octaveCombo->currentData().toInt() : 0;
    ClefType clef = getOctaveClef(base, octave);
    applyClefInternal(static_cast<int>(clef));
}

QString PartRow::partName() const
{
    return m_part->partName().toQString();
}

int PartRow::clefComboValue() const
{
    return m_clefCombo ? m_clefCombo->currentData().toInt() : -1;
}

int PartRow::octaveComboValue() const
{
    return m_octaveCombo ? m_octaveCombo->currentData().toInt() : 0;
}

void PartRow::setClefFromSettings(int clef, int octave)
{
    if (!m_clefCombo || !m_octaveCombo) return;

    m_clefCombo->blockSignals(true);
    for (int i = 0; i < m_clefCombo->count(); ++i) {
        if (m_clefCombo->itemData(i).toInt() == clef) {
            m_clefCombo->setCurrentIndex(i);
            break;
        }
    }
    m_clefCombo->blockSignals(false);

    m_octaveCombo->blockSignals(true);
    for (int i = 0; i < m_octaveCombo->count(); ++i) {
        if (m_octaveCombo->itemData(i).toInt() == octave) {
            m_octaveCombo->setCurrentIndex(i);
            break;
        }
    }
    m_octaveCombo->blockSignals(false);

    updateOctaveCombo();

    // Apply the clef to the score
    auto baseClef = static_cast<ClefType>(clef);
    ClefType finalClef = getOctaveClef(baseClef, octave);
    applyClefInternal(static_cast<int>(finalClef));
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

    // Update clef combo to show the base clef without triggering signal
    ClefType base = getBaseClef(newClef);
    if (m_clefCombo) {
        m_clefCombo->blockSignals(true);
        for (int i = 0; i < m_clefCombo->count(); ++i) {
            if (m_clefCombo->itemData(i).toInt() == static_cast<int>(base)) {
                m_clefCombo->setCurrentIndex(i);
                break;
            }
        }
        m_clefCombo->blockSignals(false);
    }

    // Update octave combo to reflect the octave offset
    if (m_octaveCombo) {
        int oct = getOctaveFromClef(newClef);
        m_octaveCombo->blockSignals(true);
        for (int i = 0; i < m_octaveCombo->count(); ++i) {
            if (m_octaveCombo->itemData(i).toInt() == oct) {
                m_octaveCombo->setCurrentIndex(i);
                break;
            }
        }
        m_octaveCombo->blockSignals(false);
        updateOctaveCombo();
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
            QString("color: %1; font-size: 12px;")
                .arg(vis ? Theme::textPrimary().name()
                         : Theme::textHidden().name()));
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
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->modifiers() & Qt::ShiftModifier) {
            emit soloRequested();
        } else {
            toggleExpand();
        }
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void PartRow::applyTheme()
{
    // Header background
    if (m_header) {
        QPalette hpal = m_header->palette();
        hpal.setColor(QPalette::Window, Theme::panelBg());
        m_header->setPalette(hpal);
    }

    // Content area background
    if (m_contentArea) {
        QPalette cpal = m_contentArea->palette();
        cpal.setColor(QPalette::Window, Theme::contentBg());
        m_contentArea->setPalette(cpal);
    }

    // Eye icon + name label color
    updateEyeIcon();

    // Arrow button
    if (m_arrowButton) {
        m_arrowButton->setStyleSheet(
            QString("QToolButton { color: %1; border: none; background: transparent; }"
                    "QToolButton:pressed { color: %1; }"
                    "QToolButton:focus { outline: none; }").arg(Theme::arrowColor().name()));
    }

    // Labels
    QString secStyle = QString("color: %1; font-size: 11px;").arg(Theme::textSecondary().name());
    if (m_clefLabel) m_clefLabel->setStyleSheet(secStyle);
    if (m_octaveLabel) m_octaveLabel->setStyleSheet(secStyle);

    // Transpose section label
    if (m_sectionLabel) {
        m_sectionLabel->setStyleSheet(
            QString("color: %1; font-size: 12px;")
                .arg(m_isTransposing ? Theme::textSecondary().name()
                                     : Theme::textDisabled().name()));
    }

    // Radio buttons
    QString radioStyle = Theme::radioStyleStr();
    if (m_writtenRadio) m_writtenRadio->setStyleSheet(radioStyle);
    if (m_concertRadio) m_concertRadio->setStyleSheet(radioStyle);
}

// ---------------------------------------------------------------------------
// PartPanel
// ---------------------------------------------------------------------------

PartPanel::PartPanel(QWidget* parent)
    : QWidget(parent)
{
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Theme::panelBg());
    pal.setColor(QPalette::Base, Theme::panelBg());
    setPalette(pal);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 4, 12, 10);

    // --- Global settings ---

    // Transposing instruments
    auto* pitchSection = new QWidget(this);
    auto* pitchLayout = new QVBoxLayout(pitchSection);
    pitchLayout->setContentsMargins(0, 0, 0, 0);
    pitchLayout->setSpacing(2);

    m_pitchLabel = new QLabel("Transposing Instruments", pitchSection);
    m_pitchLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(Theme::textSecondary().name()));
    pitchLayout->addWidget(m_pitchLabel);

    m_transposingListLabel = new QLabel(pitchSection);
    m_transposingListLabel->setStyleSheet(QString("color: %1; font-size: 9px; padding-left: 1px;").arg(Theme::textHint().name()));
    m_transposingListLabel->setTextFormat(Qt::PlainText);
    m_transposingListLabel->hide();
    pitchLayout->addWidget(m_transposingListLabel);

    auto* pitchRadioRow = new QHBoxLayout();
    pitchRadioRow->setContentsMargins(12, 0, 0, 0);
    pitchRadioRow->setSpacing(10);

    m_globalPitchWritten = new QRadioButton("Written", pitchSection);
    m_globalPitchWritten->setStyleSheet(Theme::radioStyleStr());
    m_globalPitchConcert = new QRadioButton("Concert", pitchSection);
    m_globalPitchConcert->setStyleSheet(Theme::radioStyleStr());
    m_globalPitchPerPart = new QRadioButton("Per Part", pitchSection);
    m_globalPitchPerPart->setStyleSheet(Theme::radioStyleStr());
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

    m_clefSectionLabel = new QLabel("Clefs", clefSection);
    m_clefSectionLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(Theme::textSecondary().name()));
    clefLayout->addWidget(m_clefSectionLabel);

    auto* clefRadioRow = new QHBoxLayout();
    clefRadioRow->setContentsMargins(12, 0, 0, 0);
    clefRadioRow->setSpacing(10);

    m_globalClefWritten = new QRadioButton("Written", clefSection);
    m_globalClefWritten->setStyleSheet(Theme::radioStyleStr());
    m_globalClefPerPart = new QRadioButton("Per Part", clefSection);
    m_globalClefPerPart->setStyleSheet(Theme::radioStyleStr());
    m_globalClefWritten->setChecked(true);

    clefRadioRow->addWidget(m_globalClefWritten);
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
    m_scrollArea->setStyleSheet(Theme::scrollBarStyleStr());

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
        connect(row, &PartRow::soloRequested, [this, row]() {
            for (auto* r : m_rows)
                r->setPartVisible(r == row);
            relayout();
        });
        connect(row, &PartRow::clefChanged, this, &PartPanel::relayout);
        connect(row, &PartRow::clefChanged, this, &PartPanel::saveSettings);
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
    bool clefPerPart = m_globalClefPerPart->isChecked();

    for (auto* row : m_rows) {
        // Pitch mode
        if (perPart) {
            row->setPitchControlEnabled(true);
        } else {
            row->applyPitchMode(concert);
            row->setPitchControlEnabled(false);
        }

        // Clef mode
        if (clefPerPart) {
            row->reapplyPerPartClef();
            row->setClefControlEnabled(true);
        } else {
            row->applyOriginalClef();
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

void PartPanel::showOnlyParts(const QList<int>& partNumbers)
{
    if (!m_score || m_rows.empty()) return;

    QSet<int> visible(partNumbers.begin(), partNumbers.end());
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        updateRow(i, visible.contains(i + 1)); // 1-based
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

void PartPanel::setScoreFileName(const QString& fileName)
{
    m_scoreFileName = fileName;
    loadSettings();
}

static QString settingsPath()
{
    return QCoreApplication::applicationDirPath() + "/settings.json";
}

void PartPanel::loadSettings()
{
    if (m_scoreFileName.isEmpty() || m_rows.empty()) return;

    QFile file(settingsPath());
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();

    // Load default transposing instruments setting
    QJsonObject defaults = root["defaults"].toObject();
    QString transDefault = defaults.value("transposingInstruments").toString("concert");
    if (transDefault == "concert" && m_globalPitchConcert) {
        m_globalPitchConcert->setChecked(true);
    } else if (transDefault == "written" && m_globalPitchWritten) {
        m_globalPitchWritten->setChecked(true);
    }

    // Load per-score part settings
    QJsonObject scores = root["scores"].toObject();
    QJsonObject scoreObj = scores[m_scoreFileName].toObject();
    QJsonObject partsObj = scoreObj["parts"].toObject();

    if (partsObj.isEmpty()) return;

    bool clefPerPart = m_globalClefPerPart->isChecked();

    for (auto* row : m_rows) {
        QString name = row->partName();
        if (!partsObj.contains(name)) continue;

        QJsonObject partSettings = partsObj[name].toObject();
        int clef = partSettings.value("clef").toInt(-1);
        int octave = partSettings.value("octave").toInt(0);
        if (clef < 0) continue;

        row->setClefFromSettings(clef, octave);
    }

    // If we loaded per-part clef settings, switch to Per Part mode
    if (!clefPerPart) {
        m_globalClefPerPart->setChecked(true);
    }
}

void PartPanel::saveSettings()
{
    if (m_scoreFileName.isEmpty()) return;

    QString path = settingsPath();

    // Read existing file to preserve other sections
    QJsonObject root;
    QFile readFile(path);
    if (readFile.open(QIODevice::ReadOnly)) {
        root = QJsonDocument::fromJson(readFile.readAll()).object();
        readFile.close();
    }

    // Ensure defaults section exists with transposingInstruments
    if (!root.contains("defaults")) {
        QJsonObject defaults;
        defaults["transposingInstruments"] = "concert";
        root["defaults"] = defaults;
    }

    // Build per-score parts object (only parts that differ from XML original)
    QJsonObject partsObj;
    for (auto* row : m_rows) {
        int currentClef = row->clefComboValue();
        int currentOctave = row->octaveComboValue();
        int xmlClef = row->xmlClefInt();

        // Determine what the XML original base clef and octave were
        auto xmlClefType = static_cast<ClefType>(xmlClef);
        int xmlBaseClef = static_cast<int>(getBaseClef(xmlClefType));
        int xmlOctave = getOctaveFromClef(xmlClefType);

        if (currentClef != xmlBaseClef || currentOctave != xmlOctave) {
            QJsonObject partObj;
            partObj["clef"] = currentClef;
            partObj["octave"] = currentOctave;
            partsObj[row->partName()] = partObj;
        }
    }

    QJsonObject scores = root["scores"].toObject();
    QJsonObject scoreObj = scores[m_scoreFileName].toObject();

    if (partsObj.isEmpty()) {
        scoreObj.remove("parts");
    } else {
        scoreObj["parts"] = partsObj;
    }

    if (scoreObj.isEmpty()) {
        scores.remove(m_scoreFileName);
    } else {
        scores[m_scoreFileName] = scoreObj;
    }

    if (scores.isEmpty()) {
        root.remove("scores");
    } else {
        root["scores"] = scores;
    }

    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
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

void PartPanel::applyTheme()
{
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Theme::panelBg());
    pal.setColor(QPalette::Base, Theme::panelBg());
    setPalette(pal);

    if (m_pitchLabel)
        m_pitchLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(Theme::textSecondary().name()));
    if (m_transposingListLabel)
        m_transposingListLabel->setStyleSheet(QString("color: %1; font-size: 9px; padding-left: 1px;").arg(Theme::textHint().name()));
    if (m_clefSectionLabel)
        m_clefSectionLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(Theme::textSecondary().name()));

    QString radioStyle = Theme::radioStyleStr();
    if (m_globalPitchWritten) m_globalPitchWritten->setStyleSheet(radioStyle);
    if (m_globalPitchConcert) m_globalPitchConcert->setStyleSheet(radioStyle);
    if (m_globalPitchPerPart) m_globalPitchPerPart->setStyleSheet(radioStyle);
    if (m_globalClefWritten) m_globalClefWritten->setStyleSheet(radioStyle);
    if (m_globalClefPerPart) m_globalClefPerPart->setStyleSheet(radioStyle);

    if (m_scrollArea)
        m_scrollArea->setStyleSheet(Theme::scrollBarStyleStr());

    // Propagate to all part rows
    for (auto* row : m_rows)
        row->applyTheme();
}

} // namespace scoretracker
