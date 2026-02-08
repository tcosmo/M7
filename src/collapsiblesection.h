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

        // Header — transparent so it inherits sidebar background
        auto* header = new QWidget(this);
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(4, 4, 4, 8);

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

        // Content — wrapped with horizontal margin so dark bg is inset from sidebar edges
        auto* contentWrapper = new QWidget(this);
        auto* wrapperLayout = new QHBoxLayout(contentWrapper);
        wrapperLayout->setContentsMargins(8, 0, 8, 0);
        wrapperLayout->setSpacing(0);
        m_content->setParent(contentWrapper);
        wrapperLayout->addWidget(m_content);
        layout->addWidget(contentWrapper);

        connect(m_arrow, &QToolButton::clicked, this, &CollapsibleSection::toggle);
    }

    void toggle()
    {
        m_collapsed = !m_collapsed;
        m_arrow->setArrowType(m_collapsed ? Qt::RightArrow : Qt::DownArrow);
        m_content->setVisible(!m_collapsed);

        int headerH = m_header->sizeHint().height();

        auto* splitter = qobject_cast<QSplitter*>(parentWidget());
        if (splitter) {
            QList<int> sizes = splitter->sizes();
            int idx = splitter->indexOf(this);
            if (idx >= 0) {
                if (m_collapsed) {
                    m_expandedSize = sizes[idx];
                    int diff = m_expandedSize - headerH;
                    sizes[idx] = headerH;
                    sizes[sizes.size() - 1] += diff;
                } else {
                    int diff = m_expandedSize - headerH;
                    sizes[idx] = m_expandedSize;
                    sizes[sizes.size() - 1] -= diff;
                }

                if (m_collapsed) {
                    setFixedHeight(headerH);
                } else {
                    setMinimumHeight(0);
                    setMaximumHeight(QWIDGETSIZE_MAX);
                }

                splitter->setSizes(sizes);
            }
        } else {
            if (m_collapsed) {
                setFixedHeight(headerH);
            } else {
                setMinimumHeight(0);
                setMaximumHeight(QWIDGETSIZE_MAX);
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
    int m_expandedSize = 0;
};

} // namespace scoretracker
