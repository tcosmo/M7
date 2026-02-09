#pragma once

#include <QGuiApplication>
#include <QPalette>
#include <QColor>
#include <QString>

namespace scoretracker {

struct Theme {
    static bool isDark() {
        return QGuiApplication::palette().color(QPalette::Window).value() < 128;
    }

    static QColor panelBg() {
        return isDark() ? QColor(37, 37, 37) : QColor(232, 232, 232);
    }

    static QColor contentBg() {
        return isDark() ? QColor(42, 42, 42) : QColor(222, 222, 222);
    }

    static QColor textPrimary() {
        return isDark() ? Qt::white : Qt::black;
    }

    static QColor textSecondary() {
        return isDark() ? QColor("#ccc") : QColor("#444");
    }

    static QColor textDisabled() {
        return isDark() ? QColor("#555") : QColor("#aaa");
    }

    static QColor textHidden() {
        return isDark() ? QColor("#666") : QColor("#999");
    }

    static QColor textHint() {
        return isDark() ? QColor("#888") : QColor("#777");
    }

    static QColor iconVisible() {
        return isDark() ? Qt::white : Qt::black;
    }

    static QColor iconHidden() {
        return isDark() ? QColor("#666") : QColor("#999");
    }

    static QColor arrowColor() {
        return isDark() ? QColor("#aaa") : QColor("#666");
    }

    static QString radioStyleStr() {
        QString textCol = textSecondary().name();
        QString hintCol = textHint().name();
        QString disabledCol = textDisabled().name();
        QString disabledBorder = isDark() ? "#444" : "#bbb";
        return QString(
            "QRadioButton { color: %1; font-size: 11px; spacing: 4px; }"
            "QRadioButton::indicator { width: 9px; height: 9px; }"
            "QRadioButton::indicator::unchecked { border: 1px solid %2; border-radius: 5px; background: transparent; }"
            "QRadioButton::indicator::checked { border: 1px solid #4a9eff; border-radius: 5px; background: #4a9eff; }"
            "QRadioButton:disabled { color: %3; }"
            "QRadioButton::indicator:disabled { border-color: %4; background: transparent; }"
            "QRadioButton::indicator::checked:disabled { border-color: %3; background: %3; }"
        ).arg(textCol, hintCol, disabledCol, disabledBorder);
    }

    static QString scrollBarStyleStr() {
        if (isDark()) {
            return "QScrollBar:vertical { background: palette(window); border: none; width: 14px; }"
                   "QScrollBar::handle:vertical { background: rgba(255,255,255,0.15); border-radius: 4px; min-height: 30px; margin: 2px 3px; }"
                   "QScrollBar::handle:vertical:hover { background: rgba(255,255,255,0.25); }"
                   "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
                   "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }";
        }
        return "QScrollBar:vertical { background: palette(window); border: none; width: 14px; }"
               "QScrollBar::handle:vertical { background: rgba(0,0,0,0.14); border-radius: 4px; min-height: 30px; margin: 2px 3px; }"
               "QScrollBar::handle:vertical:hover { background: rgba(0,0,0,0.24); }"
               "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
               "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }";
    }
};

} // namespace scoretracker
