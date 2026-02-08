#include "partpanel.h"

#include "engraving/dom/score.h"
#include "engraving/dom/part.h"
#include "engraving/rendering/iscorerenderer.h"
#include "engraving/types/fraction.h"
#include "engraving/types/types.h"
#include "engraving/style/styledef.h"

using namespace mu::engraving;
using namespace mu::engraving::rendering;

namespace scoretracker {

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

    // Part list
    m_listWidget = new QListWidget(this);
    layout->addWidget(m_listWidget);

    connect(m_listWidget, &QListWidget::itemChanged, this, &PartPanel::onItemChanged);

    // Allow clicking empty areas to steal focus from the list
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
    int totalRowH = 0;
    for (int i = 0; i < m_listWidget->count(); ++i) {
        totalRowH += m_listWidget->sizeHintForRow(i);
    }
    // rows + list frame + buttons + layout margins/padding
    return totalRowH + 4 + 40 + 30;
}

void PartPanel::populateList()
{
    m_listWidget->blockSignals(true);
    m_listWidget->clear();
    m_parts.clear();

    if (!m_score) {
        m_listWidget->blockSignals(false);
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

        auto* item = new QListWidgetItem(name, m_listWidget);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(part->show() ? Qt::Checked : Qt::Unchecked);
    }

    m_listWidget->blockSignals(false);
}

void PartPanel::onItemChanged(QListWidgetItem* item)
{
    int index = m_listWidget->row(item);
    if (index < 0 || index >= static_cast<int>(m_parts.size())) return;

    m_parts[index]->setShow(item->checkState() == Qt::Checked);
    relayout();
}

void PartPanel::showSoloPart()
{
    if (!m_score || m_parts.empty()) return;

    // Solo the currently selected part, or first part
    int selectedRow = m_listWidget->currentRow();
    if (selectedRow < 0) selectedRow = 0;

    m_listWidget->blockSignals(true);
    for (int i = 0; i < static_cast<int>(m_parts.size()); ++i) {
        bool show = (i == selectedRow);
        m_parts[i]->setShow(show);
        m_listWidget->item(i)->setCheckState(show ? Qt::Checked : Qt::Unchecked);
    }
    m_listWidget->blockSignals(false);
    relayout();
}

void PartPanel::showAllParts()
{
    if (!m_score) return;

    m_listWidget->blockSignals(true);
    for (int i = 0; i < static_cast<int>(m_parts.size()); ++i) {
        m_parts[i]->setShow(true);
        m_listWidget->item(i)->setCheckState(Qt::Checked);
    }
    m_listWidget->blockSignals(false);
    relayout();
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
