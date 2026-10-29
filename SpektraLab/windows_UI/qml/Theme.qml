pragma Singleton
import QtQuick
QtObject {
    // Ported from upstream Theme.swift at 61bfc49, not operating-system defaults.
    readonly property color ground: "#5f5f5f"
    readonly property color card: "#2d2d2c"
    readonly property color text: "#faf8f4"
    readonly property color muted: "#898989"
    readonly property color accent: "#e1a95f"
    readonly property color line: "#3a3b39"
    readonly property color regionLine: "#1a1a19"
    readonly property color well: "#5f5f5f"
    readonly property color wellDark: "#1e1f1e"
    readonly property color selection: "#ffffff"
    readonly property color selectionText: "#0f0e0e"
    readonly property int leftWidth: 254
    readonly property int rightWidth: 288
    readonly property int topHeight: 38
    readonly property int stripHeight: 132
}
