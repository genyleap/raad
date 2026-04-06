/*!
    \file        Colors.qml
    \brief       Provides the Colors core QML definition for RAAD.
    \details     This file contains shared Colors values and behavior used across the RAAD QML user interface.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

pragma Singleton

import QtQuick
import QtQuick.Controls

QtObject {
    id: styleObject

    readonly property int radius : 15
    readonly property int modeSystem: 0
    readonly property int modeDark: 1
    readonly property int modeLight: 2

    readonly property bool systemLightMode: {
        if (!AppGlobals.appWindow)
            return true
        return Qt.darker(AppGlobals.appWindow.palette.window, 1.2) !== AppGlobals.appWindow.palette.window
    }

    property int mode: modeSystem

    property bool lightMode: mode === modeLight ? true
                                             : (mode === modeDark ? false : systemLightMode)

    // Accent and page colors
    readonly property color accent: lightMode ? "#ffffff" : "#1e1e1e"
    readonly property color pageground: lightMode ? "#ebecf1" : "#232323"

    readonly property color accentPrimary: lightMode ? "#1b1b1b" : "#ffffff"
    readonly property color accentSecondry: lightMode ? "#ffffff" : "#1b1b1b"

    // ----------------------------------------------------------------------------
    // Surfaces / panels (rows, toolbars, cards)
    // ----------------------------------------------------------------------------
    readonly property color pagespaceActivated: lightMode ? "#f7f7f7" : "#1a1a1a"
    readonly property color pagespacePressed:   lightMode ? "#E9EEF6" : "#17171e"
    readonly property color pagespaceHovered:   lightMode ? "#F1F4FA" : "#1a1a20"

    readonly property color logoStyle: lightMode ? "#000000" : "#FFFFFF"
    readonly property color logoSideStyle: lightMode ? "#FFFFFF" : "#000000"

    // Text colors
    readonly property color textPrimary: lightMode ? "#272727" : "#ffffff"
    readonly property color textSecondary: lightMode ? "#555555" : "#cccccc"
    readonly property color textMuted: lightMode ? "#888888" : "#888888"
    readonly property color textAccent: lightMode ? "#3a86ff" : "#2a5dcc"
    readonly property color textSuccess: lightMode ? "#0cce6b" : "#0c9944"
    readonly property color textWarning: lightMode ? "#ee7e0a" : "#cc6600"
    readonly property color textError: lightMode ? "#cc3333" : "#ff6666"

    // Static colors
    readonly property color staticPrimary: "#ffffff"
    readonly property color staticSecondry: "#000000"

    readonly property color sideBar: lightMode ? "#1b1b1b" : "#ffffff"
    readonly property color sideBarContainer: lightMode ? "#f7f8f8" : "#161616"
    readonly property color sideBarItem: lightMode ? "#6f6f6f" : "#e4e4e4"

    readonly property color gradientPrimary: lightMode ? "#ffffff" : "#1b1b1b"
    readonly property color gradientSecondry: lightMode ? "#e4e4e4" : "#555555"

    // Backgrounds
    readonly property color background: lightMode ? "#ffffff" : "#1b1b1b"
    readonly property color backgroundActivated: lightMode ? "#ffffff" : "#2b2b2b"
    readonly property color backgroundDeactivated: lightMode ? "#E5E5E5" : "#3a3a3a"
    readonly property color backgroundHovered: lightMode ? "#dcdcdc" : "#444444"
    readonly property color backgroundFocused: lightMode ? "#fafafa" : "#333333"

    // Background items
    readonly property color backgroundItemActivated: lightMode ? "#F1F1F1" : "#212121"
    readonly property color backgroundItemDeactivated: lightMode ? "#F1F1F1" : "#3b3b3b"
    readonly property color backgroundItemHovered: lightMode ? "#f7f7f7" : "#2c2c2c"
    readonly property color backgroundItemFocused: lightMode ? "#f2f2f2" : "#3e3e3e"

    // Foregrounds
    readonly property color foregroundActivated: lightMode ? "#ffffff" : "#eeeeee"
    readonly property color foregroundDeactivated: lightMode ? "#9097a6" : "#888888"
    readonly property color foregroundHovered: lightMode ? "#767676" : "#bbbbbb"
    readonly property color foregroundFocused: lightMode ? "#ffffff" : "#ffffff"

    // Borders
    readonly property color borderActivated: lightMode ? "#d6d6d6" : "#414141"
    readonly property color borderDeactivated: lightMode ? "#D9D9DB" : "#555555"
    readonly property color borderHovered: lightMode ? "#F1F1F1" : "#3b3b3b"
    readonly property color borderFocused: lightMode ? "#50535a" : "#ffffff"

    // Lines
    readonly property color lineBorderActivated: lightMode ? "#d6d6d6" : "#414141"
    readonly property color lineBorderDeactivated: lightMode ? "#f1f1f1" : "#444444"
    readonly property color lineBorderHovered: lightMode ? "#f1f1f1" : "#555555"
    readonly property color lineBorderFocused: lightMode ? "#f1f1f1" : "#777777"

    readonly property color liquidGlassActivated: lightMode ? "#99ffffff" : "#991d1d1d"

    // Header and footer
    readonly property color header: lightMode ? "#ddd9f1" : "#222222"
    readonly property color footer: lightMode ? "#0e121b" : "#111111"

    // Status colors
    readonly property color primary: lightMode ? "#6e707b" : "#a0a0a0"
    readonly property color primaryBack: lightMode ? "#306e707b" : "#30a0a0a0"
    readonly property color secondry: lightMode ? "#4572e8" : "#4572e8"
    readonly property color secondryBack: lightMode ? "#304572e8" : "#304572e8"
    readonly property color success: lightMode ? "#50b761" : "#50b761"
    readonly property color successBack: lightMode ? "#3050b761" : "#3050b761"
    readonly property color warning: lightMode ? "#b89250" : "#b89250"
    readonly property color warningBack: lightMode ? "#30b89250" : "#30b89250"
    readonly property color error: lightMode ? "#b85050" : "#b85050"
    readonly property color errorBack: lightMode ? "#30b85050" : "#30b85050"

    // Shadows
    readonly property color lightShadow: lightMode ? "#28555555" : "#66000000"
    readonly property color darkShadow: lightMode ? "#66000000" : "#28555555"

    //55ffffff

    // Misc
    readonly property bool shadow: true
}
