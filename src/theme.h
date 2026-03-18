#pragma once

#include <QColor>
#include <QString>

namespace scoretracker {

// Discord-inspired dark theme — no light mode, always dark.
struct Theme {
    // Always dark
    static bool isDark() { return true; }

    // Background layers (dark → darker)
    static QColor panelBg()   { return QColor(0x2f, 0x31, 0x36); } // sidebar panels
    static QColor contentBg() { return QColor(0x29, 0x2b, 0x2f); } // expandable content areas
    static QColor scoreBg()   { return QColor(0x36, 0x39, 0x3f); } // score canvas area
    static QColor inputBg()   { return QColor(0x40, 0x44, 0x4b); } // combo boxes / inputs
    static QColor surfaceBg() { return QColor(0x20, 0x22, 0x25); } // deepest layer (behind everything)

    // Text hierarchy
    static QColor textPrimary()   { return QColor(0xdc, 0xdd, 0xde); } // main text — off-white
    static QColor textSecondary() { return QColor(0xb9, 0xbb, 0xbe); } // labels
    static QColor textDisabled()  { return QColor(0x72, 0x76, 0x7d); } // disabled controls
    static QColor textHidden()    { return QColor(0x4f, 0x54, 0x5c); } // hidden parts
    static QColor textHint()      { return QColor(0x96, 0x98, 0x9d); } // subtle hints

    // Icons
    static QColor iconVisible() { return QColor(0xdc, 0xdd, 0xde); }
    static QColor iconHidden()  { return QColor(0x4f, 0x54, 0x5c); }
    static QColor arrowColor()  { return QColor(0xb9, 0xbb, 0xbe); }

    // Accent colors — refined slate blue
    static QColor accent()      { return QColor(0x5B, 0x8D, 0xBF); } // slate blue — buttons, active
    static QColor accentHover() { return QColor(0x4A, 0x7A, 0xA8); } // darker slate pressed
    static QColor green()       { return QColor(0x57, 0xF2, 0x87); } // online / tracking on
    static QColor red()         { return QColor(0xED, 0x42, 0x45); } // danger / stop
    static QColor yellow()      { return QColor(0xFE, 0xE7, 0x5C); } // idle / warning

    static QString radioStyleStr() {
        return QString(
            "QRadioButton { color: %1; font-size: 11px; spacing: 4px; }"
            "QRadioButton::indicator { width: 9px; height: 9px; }"
            "QRadioButton::indicator::unchecked { border: 1px solid %2; border-radius: 5px; background: transparent; }"
            "QRadioButton::indicator::checked { border: 1px solid %3; border-radius: 5px; background: %3; }"
            "QRadioButton:disabled { color: %4; }"
            "QRadioButton::indicator:disabled { border-color: %5; background: transparent; }"
            "QRadioButton::indicator::checked:disabled { border-color: %4; background: %4; }"
        ).arg(textSecondary().name(),
              textHint().name(),
              accent().name(),
              textDisabled().name(),
              QColor(0x40, 0x44, 0x4b).name());
    }

    static QString scrollBarStyleStr() {
        return "QScrollBar:vertical { background: transparent; border: none; width: 14px; }"
               "QScrollBar::handle:vertical { background: rgba(255,255,255,0.10); border-radius: 4px; min-height: 30px; margin: 2px 3px; }"
               "QScrollBar::handle:vertical:hover { background: rgba(255,255,255,0.18); }"
               "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
               "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }";
    }

    // Global stylesheet for all widgets — Discord feel
    static QString globalStyleSheet() {
        return QString(
            // Base
            "* { font-family: 'Helvetica Neue', 'Segoe UI', sans-serif; }"

            // Splitter handles
            "QSplitter::handle { background: %1; }"
            "QSplitter::handle:vertical { height: 4px; }"
            "QSplitter::handle:horizontal { width: 4px; }"

            // Toolbar
            "QToolBar { background: %2; border: none; spacing: 4px; padding: 2px 4px; }"
            "QToolBar QToolButton { color: %3; border: none; padding: 4px 8px; border-radius: 4px; font-size: 12px; }"
            "QToolBar QToolButton:hover { background: rgba(255,255,255,0.06); }"
            "QToolBar QToolButton:pressed { background: rgba(255,255,255,0.03); }"
            "QToolBar QToolButton:checked { color: white; }"

            // Buttons
            "QPushButton { color: %3; background: transparent; border: none; padding: 4px 10px; border-radius: 4px; font-size: 12px; }"
            "QPushButton:hover { background: rgba(255,255,255,0.06); }"
            "QPushButton:pressed { background: rgba(255,255,255,0.03); }"
            "QPushButton:checked { color: %4; font-weight: bold; }"

            // Combo boxes
            "QComboBox { background: %5; color: %3; border: none; border-radius: 4px; padding: 4px 8px; font-size: 11px; }"
            "QComboBox::drop-down { border: none; }"
            "QComboBox QAbstractItemView { background: %7; color: %3; border: 1px solid %1; selection-background-color: %4; border-radius: 4px; }"

            // Checkboxes
            "QCheckBox { color: %3; font-size: 11px; spacing: 6px; }"
            "QCheckBox::indicator { width: 14px; height: 14px; border-radius: 3px; }"
            "QCheckBox::indicator:unchecked { background: %5; border: 1px solid %6; }"
            "QCheckBox::indicator:checked { background: %4; border: 1px solid %4; }"

            // Spin boxes
            "QSpinBox { background: %5; color: %3; border: none; border-radius: 4px; padding: 3px 6px; font-size: 11px; }"

            // Labels
            "QLabel { color: %3; }"

            // Slider — blurple track
            "QSlider::groove:horizontal { background: %5; height: 6px; border-radius: 3px; }"
            "QSlider::handle:horizontal { background: %4; width: 14px; height: 14px; margin: -4px 0; border-radius: 7px; }"
            "QSlider::sub-page:horizontal { background: %4; border-radius: 3px; }"

            // Scroll bars
            "%8"

            // Tooltips
            "QToolTip { background: %7; color: %3; border: 1px solid %1; padding: 6px 8px; border-radius: 6px; font-size: 11px; }"

            // Menu (dropdowns)
            "QMenu { background: %7; color: %3; border: 1px solid %1; border-radius: 6px; padding: 4px; }"
            "QMenu::item { padding: 6px 20px; border-radius: 3px; }"
            "QMenu::item:selected { background: %4; }"
            "QMenu::separator { height: 1px; background: %1; margin: 4px 8px; }"
        ).arg(
            QColor(0x20, 0x22, 0x25).name(),   // %1 — borders / deepest
            QColor(0x2f, 0x31, 0x36).name(),   // %2 — toolbar bg
            textPrimary().name(),               // %3 — text
            accent().name(),                    // %4 — accent (blurple)
            inputBg().name(),                   // %5 — input bg
            textHint().name(),                  // %6 — subtle arrows/borders
            QColor(0x18, 0x19, 0x1c).name(),   // %7 — popup/menu bg (darkest)
            scrollBarStyleStr()                 // %8 — scrollbar styles
        );
    }
};

} // namespace scoretracker
