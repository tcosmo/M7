#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QSplitter>
#include <QEvent>

namespace scoretracker {

class CollapsibleSection : public QWidget
{
    Q_OBJECT

public:
    explicit CollapsibleSection(const QString& title, QWidget* content, QWidget* parent = nullptr)
        : QWidget(parent), m_content(content)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        // Header
        auto* header = new QWidget(this);
        header->setStyleSheet("background-color: #333333;");
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(4, 4, 4, 4);

        m_arrow = new QToolButton(header);
        m_arrow->setArrowType(Qt::DownArrow);
        m_arrow->setAutoRaise(true);
        m_arrow->setFixedSize(16, 16);
        headerLayout->addWidget(m_arrow);

        auto* label = new QLabel(title, header);
        auto font = label->font();
        font.setBold(true);
        label->setFont(font);
        headerLayout->addWidget(label);
        headerLayout->addStretch();

        header->setCursor(Qt::PointingHandCursor);
        header->installEventFilter(this);
        m_header = header;

        layout->addWidget(header);

        // Content
        m_content->setParent(this);
        layout->addWidget(m_content);

        connect(m_arrow, &QToolButton::clicked, this, &CollapsibleSection::toggle);
    }

    void toggle()
    {
        m_collapsed = !m_collapsed;
        m_arrow->setArrowType(m_collapsed ? Qt::RightArrow : Qt::DownArrow);
        m_content->setVisible(!m_collapsed);

        int headerH = m_header->sizeHint().height();

        if (m_collapsed) {
            m_expandedHeight = height();
            setFixedHeight(headerH);
        } else {
            setMinimumHeight(0);
            setMaximumHeight(QWIDGETSIZE_MAX);
        }

        // Redistribute space in parent splitter
        if (auto* splitter = qobject_cast<QSplitter*>(parentWidget())) {
            QList<int> sizes = splitter->sizes();
            int idx = splitter->indexOf(this);
            if (idx >= 0) {
                int oldSize = sizes[idx];
                int newSize = m_collapsed ? headerH : m_expandedHeight;
                int diff = oldSize - newSize;
                sizes[idx] = newSize;
                sizes[sizes.size() - 1] += diff;
                splitter->setSizes(sizes);
            }
        }
    }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override
    {
        if (obj == m_header && event->type() == QEvent::MouseButtonRelease) {
            toggle();
            return true;
        }
        return QWidget::eventFilter(obj, event);
    }

private:
    QWidget* m_header = nullptr;
    QWidget* m_content = nullptr;
    QToolButton* m_arrow = nullptr;
    bool m_collapsed = false;
    int m_expandedHeight = 0;
};

} // namespace scoretracker
