#include "partpanel.h"

#include "engraving/dom/score.h"
#include "engraving/dom/part.h"
#include "engraving/rendering/iscorerenderer.h"
#include "engraving/types/fraction.h"

using namespace mu::engraving;
using namespace mu::engraving::rendering;

namespace scoretracker {

PartPanel::PartPanel(QWidget* parent)
    : QDockWidget("Parts", parent)
{
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);

    // Buttons
    auto* btnLayout = new QHBoxLayout();
    auto* btnFull = new QPushButton("Full Score", container);
    auto* btnSolo = new QPushButton("Solo", container);
    auto* btnAll = new QPushButton("Show All", container);

    btnLayout->addWidget(btnFull);
    btnLayout->addWidget(btnSolo);
    btnLayout->addWidget(btnAll);
    layout->addLayout(btnLayout);

    connect(btnFull, &QPushButton::clicked, this, &PartPanel::showFullScore);
    connect(btnSolo, &QPushButton::clicked, this, &PartPanel::showSoloPart);
    connect(btnAll, &QPushButton::clicked, this, &PartPanel::showAllParts);

    // Part list
    m_listWidget = new QListWidget(container);
    layout->addWidget(m_listWidget);

    connect(m_listWidget, &QListWidget::itemChanged, this, &PartPanel::onItemChanged);

    setWidget(container);
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

void PartPanel::showFullScore()
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
    showFullScore(); // Same behavior
}

void PartPanel::relayout()
{
    if (!m_score || !m_renderer) return;

    m_renderer->layoutScore(m_score, Fraction(0, 1), Fraction(-1, 1));
    emit partsChanged();
}

} // namespace scoretracker
