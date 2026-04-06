/*!
    \file        FontSystem.qml
    \brief       Provides the FontSystem core QML definition for RAAD.
    \details     This file contains shared FontSystem values and behavior used across the RAAD QML user interface.

    \author      Kambiz Asadzadeh <https://github.com/thecompez>
    \copyright   Copyright (c) 2026 Genyleap. All rights reserved.
    \license     https://github.com/genyleap/raad/blob/main/LICENSE.md
*/

pragma Singleton
import QtQuick

Item {

    property alias getAwesomeBrand: fontAwesomeBrand
    property alias getAwesomeRegular: fontAwesomeRegular
    property alias getAwesomeLight: fontAwesomeLight
    property alias getAwesomeThin: fontAwesomeThin
    property alias getAwesomeSolid: fontAwesomeSolid
    property alias getContentFont: contentFontRegular
    property alias getTitleBoldFont: contentFontRegular
    property alias getContentFontRegular: contentFontRegular
    property alias getContentFontThin: fontAwesomeThin
    property alias getFontSize: fontSize

    QtObject {
        id: fontSize
        readonly property int       h1 : 32
        readonly property int       h2 : 24
        readonly property double    h3 : 18.72
        readonly property int       h4 : 16
        readonly property double    h5 : 13.28
        readonly property double    h6 : 10.72

        readonly property int content : 14
    }

    FontLoader {
        id: fontAwesomeBrand
        source: "qrc:/resources/fonts/Font Awesome 6 Brands-Regular-400.otf"
    }

    FontLoader {
        id: fontAwesomeRegular
        source: "qrc:/resources/fonts/Font Awesome 6 Pro-Regular-400.otf"
    }

    FontLoader {
        id: fontAwesomeLight
        source: "qrc:/resources/fonts/Font Awesome 6 Pro-Light-300.otf"
    }

    FontLoader {
        id: fontAwesomeThin
        source: "qrc:/resources/fonts/Font Awesome 6 Pro-Thin-100.otf"
    }

    FontLoader {
        id: fontAwesomeSolid
        source: "qrc:/resources/fonts/Font Awesome 6 Pro-Solid-900.otf"
    }

    FontLoader {
        id: contentFontRegular
        // source: Main.appRoot.isLeftToRight ? "resources/fonts/Inter-Medium.ttf" : "resources/fonts/IRANSansX-Regular.ttf"
        source: "qrc:/resources/fonts/Inter-Medium.ttf"
    }
}
