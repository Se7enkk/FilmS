import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.impl
import QtQuick.Dialogs
import SpektraLab.Native 1.0

ApplicationWindow {
    id: window
    objectName: "editorWindow"
    width: 1540; height: 980
    minimumWidth: 1100; minimumHeight: 740
    visible: true
    color: "transparent"
    flags: Qt.Window | Qt.FramelessWindowHint
    title: (preview.hasFrame ? preview.fileName + " — " : "") + "FilmS"
    font.family: "Segoe UI"
    font.pixelSize: 12
    palette.window: Theme.card
    palette.windowText: Theme.text
    palette.text: Theme.text
    palette.buttonText: Theme.text
    palette.base: Theme.well
    palette.button: Theme.well
    palette.highlight: "#e8e8e6"
    palette.highlightedText: "#1a1a1a"
    property bool closePending: false
    property bool cropMode: false
    property rect draftCrop: Qt.rect(0, 0, 1, 1)
    property int exportFormat: 0
    property int exportQuality: 95
    property int exportLongEdge: 0
    readonly property bool canEditCrop: preview.hasFrame && !preview.busy && !preview.batchExporting
    readonly property bool canBrowse: preview.ready && !preview.busy && !preview.batchExporting && !cropMode
    // Text controls keep their own undo stack while they own keyboard focus.
    readonly property bool editingText: activeFocusItem !== null && typeof activeFocusItem.text === "string" && typeof activeFocusItem.undo === "function"
    onCropModeChanged: updateDialogState()

    function updateDialogState() {
        preview.setDialogOpen(cropMode || openDialog.visible || saveDialog.visible || batchDialog.visible || exportPopup.visible)
    }
    function startCrop() {
        if (!canEditCrop) return
        draftCrop = preview.cropRect
        cropMode = true
        Qt.callLater(function() { canvas.resetView(true) })
    }
    function finishCrop() {
        var next = draftCrop
        var commit = cropMode && canEditCrop
        cropMode = false
        if (commit) preview.setCropRect(next)
        Qt.callLater(function() { canvas.resetView(true) })
    }
    function cancelCrop() { cropMode = false; canvas.resetView(true) }
    function filmGroup(positive) {
        var rows = []
        for (var i = 0; i < preview.films.length; ++i)
            if (preview.films[i].positive === positive)
                rows.push({ "name": preview.films[i].name, "catalogIndex": i, "id": preview.films[i].id })
        return rows
    }
    function openExport() { if (preview.hasFrame && !preview.busy && !cropMode) exportPopup.open() }
    function showExportOptions() { openExport() }
    function formatFilter() {
        if (exportFormat === 3) return ["JPEG 图像 (*.jpg *.jpeg)"]
        if (exportFormat === 1 || exportFormat === 2) return ["PNG 图像 (*.png)"]
        return ["16 位 TIFF (*.tif *.tiff)"]
    }
    onClosing: function(event) {
        if (preview.batchExporting) preview.cancelExport()
        if (preview.busy) { closePending = true; event.accepted = false }
    }
    Connections {
        target: preview
        function onStateChanged() { if (window.closePending && !preview.busy) window.close() }
        function onSourceOpened() { window.cropMode = false; canvas.resetView(true) }
    }
    FileDialog {
        id: openDialog
        title: "导入照片"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["照片 (*.jpg *.jpeg *.png *.bmp *.tif *.tiff *.webp *.arw *.nef *.nrw *.dng *.cr2 *.cr3 *.raf *.rw2 *.orf *.pef *.srw)", "相机 RAW (*.arw *.nef *.nrw *.dng *.cr2 *.cr3 *.raf *.rw2 *.orf *.pef *.srw)", "所有文件 (*)"]
        onVisibleChanged: window.updateDialogState()
        onAccepted: preview.openFiles(selectedFiles)
    }
    FileDialog {
        id: saveDialog
        title: "导出当前画面 · 不覆盖现有文件"
        fileMode: FileDialog.SaveFile
        nameFilters: window.formatFilter()
        defaultSuffix: window.exportFormat === 3 ? "jpg" : window.exportFormat === 0 ? "tif" : "png"
        currentFolder: preview.sourceFolder
        onVisibleChanged: window.updateDialogState()
        onAccepted: preview.exportImage(selectedFile, window.exportFormat, window.exportQuality, window.exportLongEdge)
    }
    FolderDialog {
        id: batchDialog
        title: "选择批量导出文件夹 · 跳过已有文件"
        currentFolder: preview.sourceFolder
        onVisibleChanged: window.updateDialogState()
        onAccepted: preview.exportBatch(selectedFolder, window.exportFormat, window.exportQuality, window.exportLongEdge)
    }
    Shortcut { sequences: [StandardKey.Open]; enabled: window.canBrowse; onActivated: openDialog.open() }
    Shortcut { sequence: "Ctrl+E"; enabled: preview.hasFrame && !preview.busy && !window.cropMode; onActivated: window.openExport() }
    Shortcut { sequence: "Ctrl+0"; enabled: preview.hasFrame; onActivated: canvas.resetView(true) }
    Shortcut { sequence: "Ctrl+1"; enabled: preview.hasFrame && !window.cropMode; onActivated: canvas.resetView(false) }
    Shortcut { sequence: "Ctrl+Z"; enabled: preview.settingsEditable && preview.canUndo && !window.cropMode && !window.editingText; onActivated: preview.undo() }
    Shortcut { sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]; enabled: preview.settingsEditable && preview.canRedo && !window.cropMode && !window.editingText; onActivated: preview.redo() }
    Shortcut { sequence: "Escape"; enabled: window.cropMode; onActivated: window.cancelCrop() }
    Shortcut { sequence: "Return"; enabled: window.cropMode; onActivated: window.finishCrop() }

    // Before/after compare: press Space to toggle the un-graded original.
    property bool showOriginal: false
    Shortcut {
        sequence: "Space"
        enabled: preview.hasFrame && !window.cropMode && !preview.busy
        onActivated: window.showOriginal = !window.showOriginal
    }
    property int leftPanelWidth: 254
    property int rightPanelWidth: 288
    property bool draggingLeft: false
    property bool draggingRight: false
    property real dragStartX: 0
    property real dragStartWidth: 0
    Shortcut {
        sequence: "Space"
        enabled: preview.hasFrame && !window.cropMode && !preview.busy
        onActivated: window.showOriginal = true
    }

    component Note: Text {
        Layout.fillWidth: true
        color: Theme.muted
        font.pixelSize: 10
        wrapMode: Text.WordWrap
        lineHeight: 1.35
    }
    component Adjustment: ColumnLayout {
        id: adjustment
        property string label
        property string setting
        property real from: 0
        property real to: 2
        property real step: 0.05
        property int decimals: 2
        property string suffix: ""
        property real fallback: 0
        property bool available: true
        readonly property real current: preview.adjustments[setting] === undefined ? fallback : Number(preview.adjustments[setting])
        Layout.fillWidth: true; spacing: 2
        RowLayout {
            Layout.fillWidth: true
            Text { text: adjustment.label; color: adjustment.available ? Theme.text : Theme.muted; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            Text { text: adjustment.current.toFixed(adjustment.decimals) + adjustment.suffix; color: Theme.muted; font.pixelSize: 10 }
        }
        Slider {
            id: adjSlider
            Layout.fillWidth: true; implicitHeight: 24
            from: adjustment.from; to: adjustment.to; stepSize: adjustment.step
            value: adjustment.current
            enabled: preview.settingsEditable && adjustment.available
            onPressedChanged: {
                if (pressed) preview.beginEditGesture(adjustment.setting)
                else preview.endEditGesture()
            }
            onMoved: preview.setAdjustment(adjustment.setting, value)
        }
    }
    component Effect: ColumnLayout {
        id: effect
        property string label
        property string activeKey
        property string amountKey
        property real maximum: 2
        Layout.fillWidth: true; spacing: 1
        CheckBox {
            text: effect.label; font.pixelSize: 11
            palette.windowText: Theme.text
            checked: preview.adjustments[effect.activeKey] === undefined ? true : preview.adjustments[effect.activeKey]
            enabled: preview.settingsEditable
            onToggled: preview.setAdjustment(effect.activeKey, checked)
        }
        Adjustment { label: "强度"; setting: effect.amountKey; fallback: 1; from: 0; to: effect.maximum; available: preview.adjustments[effect.activeKey] !== false }
    }
    component FilmGroup: ColumnLayout {
        id: group
        property bool positive: false
        Layout.fillWidth: true; spacing: 3
        Text { text: group.positive ? "反转片 · Positive" : "负片 · Negative"; color: Theme.muted; font.pixelSize: 10; font.bold: true }
        ListView {
            id: list
            objectName: group.positive ? "positiveFilmList" : "filmList"
            Layout.fillWidth: true
            implicitHeight: contentHeight
            height: contentHeight
            interactive: false
            model: window.filmGroup(group.positive); clip: false
            currentIndex: {
                for (var i = 0; i < model.length; ++i)
                    if (model[i].catalogIndex === preview.filmIndex) return i
                return -1
            }
            onCurrentIndexChanged: { if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain) }
            onCountChanged: Qt.callLater(function() { if (list.currentIndex >= 0) list.positionViewAtIndex(list.currentIndex, ListView.Contain) })
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                required property var modelData
                width: list.width; height: 30; radius: 4
                color: modelData.catalogIndex === preview.filmIndex ? Theme.selection : filmMouse.containsMouse ? "#41413e" : "transparent"
                Text {
                    anchors.left: parent.left; anchors.leftMargin: 8
                    anchors.right: cover.left; anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.name
                    color: modelData.catalogIndex === preview.filmIndex ? Theme.selectionText : preview.settingsEditable ? Theme.text : Theme.muted
                    font.pixelSize: 10; elide: Text.ElideRight
                }
                Rectangle {
                    id: cover
                    anchors.right: parent.right; anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    width: 28; height: 20; radius: 2; clip: true
                    color: "#1a1a19"
                    visible: coverImage.status === Image.Ready
                    Image {
                        id: coverImage
                        anchors.fill: parent; fillMode: Image.PreserveAspectCrop
                        source: "FilmCovers/" + modelData.id + ".jpg"
                    }
                }
                MouseArea { id: filmMouse; anchors.fill: parent; hoverEnabled: true; enabled: preview.settingsEditable; onClicked: preview.filmIndex = modelData.catalogIndex }
            }
        }
    }

    component TitleButton: Rectangle {
        width: 46; height: 38
        property string __text: ""
        property var __clicked: function() {}
        property color hoverColor: "#3a3a38"
        property real __fontSize: 12
        color: titleMouse.containsMouse ? hoverColor : "transparent"
        radius: 0
        Text { anchors.centerIn: parent; text: parent.__text; color: Theme.text; font.pixelSize: parent.__fontSize }
        MouseArea { id: titleMouse; anchors.fill: parent; hoverEnabled: true; onClicked: parent.__clicked() }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.card
        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // Title bar (flush, 38px, matches macOS TopBar)
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 38
                color: Theme.card
                RowLayout {
                    anchors.fill: parent
                    spacing: 0
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        MouseArea {
                            anchors.fill: parent
                            onPressed: function(mouse) { window.startSystemMove() }
                            onDoubleClicked: function(mouse) {
                                if (window.visibility === Window.Maximized) window.showNormal()
                                else window.showMaximized()
                            }
                        }
                        RowLayout {
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8
                            Text { text: "FilmS"; color: Theme.text; font.pixelSize: 12; font.bold: true }
                            Text {
                                text: preview.hasFrame ? "  ·  " + preview.fileName : ""
                                color: Theme.muted; font.pixelSize: 11
                                elide: Text.ElideRight
                                Layout.maximumWidth: 600
                            }
                        }
                    }
                    TitleButton { __text: "—"; __clicked: function() { window.showMinimized() } }
                    Rectangle {
                        width: 46; height: 38
                        color: maxBtn.containsMouse ? "#3a3a38" : "transparent"
                        MouseArea { id: maxBtn; anchors.fill: parent; hoverEnabled: true; onClicked: {
                            if (window.visibility === Window.Maximized) window.showNormal()
                            else window.showMaximized()
                        }}
                        // Draw maximize/restore icon manually
                        Rectangle {
                            anchors.centerIn: parent
                            width: 10; height: 10
                            color: "transparent"
                            border.width: 1.2; border.color: Theme.text
                            visible: window.visibility !== Window.Maximized
                        }
                        // Restore icon: two overlapping rectangles
                        Item {
                            anchors.centerIn: parent
                            width: 10; height: 10
                            visible: window.visibility === Window.Maximized
                            Rectangle {
                                x: 2; y: 2; width: 8; height: 8
                                color: Theme.card; border.width: 1.2; border.color: Theme.text
                            }
                            Rectangle {
                                x: 0; y: 0; width: 8; height: 8
                                color: "transparent"; border.width: 1.2; border.color: Theme.text
                            }
                        }
                    }
                    TitleButton {
                        __text: "✕"
                        __clicked: function() { window.close() }
                        hoverColor: "#c42b1c"
                    }
                }
            }
            // Region divider
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.regionLine }

    RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
        Rectangle {
            Layout.preferredWidth: window.leftPanelWidth; Layout.fillHeight: true; color: Theme.card
            ColumnLayout {
                anchors.fill: parent; spacing: 0
                Text { text: "胶片与相纸"; color: Theme.text; font.pixelSize: 12; font.bold: true; Layout.leftMargin: 16; Layout.preferredHeight: Theme.topHeight; verticalAlignment: Text.AlignVCenter }
                ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width; spacing: 0
                        Section {
                            title: "导航图"
                            Rectangle {
                                Layout.fillWidth: true; Layout.preferredHeight: 132; color: Theme.wellDark
                                FrameCanvas { anchors.fill: parent; anchors.margins: 6; image: preview.previewImage; enabled: false }
                                Text { anchors.centerIn: parent; visible: !preview.hasFrame; text: "导入照片开始"; color: Theme.muted; font.pixelSize: 11 }
                            }
                            RowLayout {
                                ActionButton { text: "适合"; implicitHeight: 23; enabled: preview.hasFrame; onClicked: canvas.resetView(true) }
                                Item { Layout.fillWidth: true }
                                Text { text: preview.hasFrame ? canvas.scalePercent.toFixed(1) + "%" : ""; color: Theme.muted; font.pixelSize: 10 }
                            }
                        }
                        Section {
                            title: "胶片"
                            FilmGroup { positive: true }
                            FilmGroup { positive: false }
                        }
                        Section {
                            title: "相纸"
                            Note { visible: preview.filmIsPositive; text: "反转片直接扫描，不使用相纸。"; color: Theme.accent }
                            ListView {
                                id: papers; objectName: "paperList"
                                Layout.fillWidth: true
                                implicitHeight: contentHeight
                                height: contentHeight
                                interactive: false
                                clip: false
                                model: preview.papers; currentIndex: preview.paperIndex
                                onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
                                onCountChanged: Qt.callLater(function() { papers.positionViewAtIndex(preview.paperIndex, ListView.Contain) })
                                ScrollBar.vertical: ScrollBar {}
                                delegate: Rectangle {
                                    required property var modelData
                                    required property int index
                                    width: papers.width; height: 27; radius: 4
                                    color: !preview.filmIsPositive && index === preview.paperIndex ? Theme.selection : paperMouse.containsMouse && paperMouse.enabled ? "#41413e" : "transparent"
                                    Text {
                                        anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 6; verticalAlignment: Text.AlignVCenter; text: modelData.name
                                        color: !preview.filmIsPositive && index === preview.paperIndex ? Theme.selectionText : preview.settingsEditable && !preview.filmIsPositive ? Theme.text : Theme.muted
                                        font.pixelSize: 10; elide: Text.ElideRight
                                    }
                                    MouseArea { id: paperMouse; anchors.fill: parent; hoverEnabled: true; enabled: preview.settingsEditable && !preview.filmIsPositive; onClicked: preview.paperIndex = index }
                                }
                            }
                        }
                        Section {
                            title: "片边"
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "启用"; color: Theme.text; font.pixelSize: 11 }
                                Item { Layout.fillWidth: true }
                                Switch {
                                    id: overscanSwitch
                                    Layout.preferredWidth: 36; Layout.preferredHeight: 18
                                    checked: preview.adjustments.overscanActive === true
                                    enabled: preview.settingsEditable
                                    onCheckedChanged: if (preview.settingsEditable) preview.setAdjustment("overscanActive", checked)
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 4
                                visible: overscanSwitch.checked
                                ComboBox {
                                    Layout.fillWidth: true
                                    model: ["135 全幅","135 半幅","120 645","120 方幅","120 6x7","120 6x8","120 6x9","135 宽幅","120 6x12","120 6x17"]
                                    property var eng: ["135","135_half","120_645","120_6x6","120_6x7","120_6x8","120_6x9","135_xpan","120_6x12","120_6x17"]
                                    currentIndex: Math.max(0, eng.indexOf(preview.adjustments.overscanFormat))
                                    enabled: preview.settingsEditable
                                    onActivated: preview.setAdjustment("overscanFormat", eng[currentIndex])
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "片幅"; color: Theme.muted; font.pixelSize: 10; Layout.preferredWidth: 50 }
                                    ComboBox {
                                        Layout.fillWidth: true
                                        model: ["条状","存档"]
                                        currentIndex: 0
                                        enabled: preview.settingsEditable
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "片门"; color: Theme.muted; font.pixelSize: 10; Layout.preferredWidth: 50 }
                                    ComboBox {
                                        Layout.fillWidth: true
                                        model: ["自动","直角","圆角","耳形","肩形","踢角"]
                                        property var eng: ["auto","square","rounded","eared","shouldered","kicked"]
                                        currentIndex: Math.max(0, eng.indexOf(preview.adjustments.overscanGate))
                                        enabled: preview.settingsEditable
                                        onActivated: preview.setAdjustment("overscanGate", eng[currentIndex])
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "孔色"; color: Theme.muted; font.pixelSize: 10; Layout.preferredWidth: 50 }
                                    ComboBox {
                                        Layout.fillWidth: true
                                        model: ["白色","黑色"]
                                        property var eng: ["white","black"]
                                        currentIndex: Math.max(0, eng.indexOf(preview.adjustments.overscanHoles))
                                        enabled: preview.settingsEditable
                                        onActivated: preview.setAdjustment("overscanHoles", eng[currentIndex])
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "背纸"; color: Theme.muted; font.pixelSize: 10; Layout.preferredWidth: 50 }
                                    ComboBox {
                                        Layout.fillWidth: true
                                        model: ["无","黑底","敞开"]
                                        property var eng: ["none","black","open"]
                                        currentIndex: Math.max(0, eng.indexOf(preview.adjustments.overscanCarrier))
                                        enabled: preview.settingsEditable
                                        onActivated: preview.setAdjustment("overscanCarrier", eng[currentIndex])
                                    }
                                }
                                Adjustment { label: "边缘漏光"; setting: "overscanFog"; from: 0; to: 4; step: 0.1; decimals: 1 }
                                Adjustment { label: "片仓漏光"; setting: "overscanLeaks"; from: 0; to: 4; step: 0.1; decimals: 1 }
                            }
                        }
                        Section {
                            title: "日期后背"
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "曝光日期"; color: Theme.text; font.pixelSize: 11 }
                                Item { Layout.fillWidth: true }
                                Switch {
                                    id: dateBackSwitch
                                    Layout.preferredWidth: 36; Layout.preferredHeight: 18
                                    checked: preview.adjustments.dateBack === true
                                    enabled: preview.settingsEditable
                                    onCheckedChanged: {
                                        if (preview.settingsEditable) {
                                            preview.setAdjustment("dateBack", checked)
                                            // Auto-fill from EXIF when turning on with empty field.
                                            if (checked && dateTextField.text.length === 0) {
                                                var dt = preview.metadata.datetime || ""
                                                var m = dt.match(/^(\d{4})[-:](\d{2})[-:](\d{2})/)
                                                if (m) {
                                                    var y2 = m[1].substr(2), mo = m[2], d = m[3]
                                                    dateTextField.text = "'" + y2 + " " + mo + " " + d
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 4
                                visible: dateBackSwitch.checked
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "日期"; color: Theme.muted; font.pixelSize: 10; Layout.preferredWidth: 50 }
                                    TextField {
                                        id: dateTextField
                                        Layout.fillWidth: true
                                        placeholderText: "留空用拍摄日期"
                                        color: Theme.text
                                        font.pixelSize: 11
                                        enabled: preview.settingsEditable
                                        onTextChanged: if(cursorPosition > 0) preview.setAdjustment("dateBackText", text)
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "格式"; color: Theme.muted; font.pixelSize: 10; Layout.preferredWidth: 50 }
                                    ComboBox {
                                        id: dateFormatBox
                                        Layout.fillWidth: true
                                        model: ["'YY MM DD", "YY MM DD", "YYYY MM DD", "MM DD YY", "YY.MM.DD"]
                                        currentIndex: 0
                                        enabled: preview.settingsEditable
                                        onActivated: {
                                            // Re-format the EXIF date with the chosen format.
                                            var dt = preview.metadata.datetime || ""
                                            var m = dt.match(/^(\d{4})[-:](\d{2})[-:](\d{2})/)
                                            if (m) {
                                                var y4 = m[1], y2 = y4.substr(2), mo = m[2], d = m[3]
                                                var fmt = ["'" + y2 + " " + mo + " " + d,
                                                           y2 + " " + mo + " " + d,
                                                           y4 + " " + mo + " " + d,
                                                           mo + " " + d + " " + y2,
                                                           y2 + "." + mo + "." + d][currentIndex]
                                                dateTextField.text = fmt
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        Section {
                            title: "裁切"
                            RowLayout {
                                Layout.fillWidth: true
                                ComboBox {
                                    id: cropAspect; Layout.fillWidth: true
                                    model: ["自由比例", "1:1", "3:2", "4:3", "16:9"]; enabled: window.canEditCrop
                                    currentIndex: {
                                        var c = preview.cropRect
                                        if (!preview.hasFrame || (c.x === 0 && c.y === 0 && c.width === 1 && c.height === 1)) return 0
                                        var ratio = c.width * preview.metadata.sourceWidth / (c.height * preview.metadata.sourceHeight)
                                        if (preview.quarterTurns % 2 !== 0) ratio = 1 / ratio
                                        var values = [0, 1, 1.5, 4 / 3, 16 / 9]
                                        for (var i = 1; i < values.length; ++i)
                                            if (Math.abs(ratio - values[i]) < 0.002) return i
                                        return 0
                                    }
                                    onActivated: { window.cropMode = false; preview.setCropAspect([0, 1, 1.5, 4 / 3, 16 / 9][currentIndex]); canvas.resetView(true) }
                                }
                                ActionButton { text: "重置"; enabled: window.canEditCrop; onClicked: { window.cropMode = false; preview.resetCrop(); canvas.resetView(true) } }
                            }
                            ActionButton { Layout.fillWidth: true; text: window.cropMode ? "完成裁切" : "在画面中拖选裁切"; accented: window.cropMode; enabled: window.canEditCrop; onClicked: window.cropMode ? window.finishCrop() : window.startCrop() }
                            Note { text: window.cropMode ? "在未旋转原图上拖出矩形；完成后恢复方向与拉直。Enter 确认，Esc 取消。" : "预览与导出使用同一裁切与方向；原始 RAW 不变。" }
                            RowLayout {
                                Layout.fillWidth: true
                                ActionButton { objectName: "rotateLeftButton"; Layout.fillWidth: true; text: "左转 90°"; enabled: window.canEditCrop && !window.cropMode; onClicked: { preview.rotateCounterClockwise(); canvas.resetView(true) } }
                                ActionButton { objectName: "rotateRightButton"; Layout.fillWidth: true; text: "右转 90°"; enabled: window.canEditCrop && !window.cropMode; onClicked: { preview.rotateClockwise(); canvas.resetView(true) } }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                ActionButton { objectName: "flipHorizontalButton"; Layout.fillWidth: true; text: "水平翻转"; accented: preview.flipHorizontal; enabled: window.canEditCrop && !window.cropMode; onClicked: preview.setFlipHorizontal(!preview.flipHorizontal) }
                                ActionButton { objectName: "flipVerticalButton"; Layout.fillWidth: true; text: "垂直翻转"; accented: preview.flipVertical; enabled: window.canEditCrop && !window.cropMode; onClicked: preview.setFlipVertical(!preview.flipVertical) }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "拉直"; color: Theme.text; font.pixelSize: 11 }
                                Item { Layout.fillWidth: true }
                                Text { text: (straightenSlider.pressed ? straightenSlider.from + straightenSlider.position * (straightenSlider.to - straightenSlider.from) : preview.straightenDegrees).toFixed(1) + "°"; color: Theme.muted; font.pixelSize: 10 }
                            }
                            Slider {
                                id: straightenSlider; objectName: "straightenSlider"
                                Layout.fillWidth: true; implicitHeight: 24
                                from: -45; to: 45; stepSize: 0.1; live: false
                                value: preview.straightenDegrees
                                enabled: window.canEditCrop && !window.cropMode
                                onPressedChanged: {
                                    if (pressed) preview.beginEditGesture("straightenDegrees")
                                    else {
                                        preview.setStraightenDegrees(Math.round((from + position * (to - from)) * 10) / 10)
                                        preview.endEditGesture()
                                    }
                                }
                                onMoved: { if (!pressed) preview.setStraightenDegrees(value) }
                            }
                            Note { text: "拉直在松开滑块后应用。边缘自动收紧以避免空白。" }
                            ActionButton { objectName: "resetGeometryButton"; Layout.fillWidth: true; text: "重置裁切与方向"; enabled: window.canEditCrop && !window.cropMode; onClicked: { preview.resetGeometry(); canvas.resetView(true) } }
                        }
                    }
                }
            }
        }
        // Draggable divider: left rail | center
        Rectangle {
            id: leftDivider
            Layout.preferredWidth: 6; Layout.fillHeight: true; color: dividerL.containsMouse || dividerL.pressed ? "#555553" : "transparent"
            property real startX: 0
            property real startWidth: 0
            Rectangle { anchors.centerIn: parent; width: 1; height: parent.height; color: Theme.regionLine }
            MouseArea {
                id: dividerL
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.SplitHCursor
                onPressed: (mouse)=> {
                    leftDivider.startX = mouse.x
                    leftDivider.startWidth = window.leftPanelWidth
                }
                onPositionChanged: (mouse)=> {
                    if (pressed)
                        window.leftPanelWidth = Math.max(180, Math.min(480, Math.round(leftDivider.startWidth + (mouse.x - leftDivider.startX))))
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; color: Theme.ground
            ColumnLayout {
                anchors.fill: parent; spacing: 0
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: Theme.topHeight; color: Theme.card
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 6
                        ActionButton { text: "导入照片"; enabled: window.canBrowse; onClicked: openDialog.open(); implicitHeight: 27 }
                        ActionButton { text: "导出"; enabled: preview.hasFrame && !preview.busy && !window.cropMode; onClicked: window.openExport(); implicitHeight: 27 }
                        ActionButton { text: "裁切"; accented: window.cropMode; enabled: window.canEditCrop; onClicked: window.cropMode ? window.finishCrop() : window.startCrop(); implicitHeight: 27 }
                        ActionButton { objectName: "undoButton"; text: "撤销"; enabled: preview.settingsEditable && preview.canUndo && !window.cropMode; onClicked: preview.undo(); implicitHeight: 27; ToolTip.visible: hovered; ToolTip.text: "撤销当前照片的上一步调整 · Ctrl+Z" }
                        ActionButton { objectName: "redoButton"; text: "重做"; enabled: preview.settingsEditable && preview.canRedo && !window.cropMode; onClicked: preview.redo(); implicitHeight: 27; ToolTip.visible: hovered; ToolTip.text: "重做当前照片的调整 · Ctrl+Shift+Z / Ctrl+Y" }
                        Item { Layout.fillWidth: true }
                        Text { text: preview.hasFrame ? canvas.scalePercent.toFixed(1) + "%" : ""; color: Theme.muted; font.pixelSize: 10; horizontalAlignment: Text.AlignRight }
                        ActionButton { text: "适合"; accented: canvas.fit; enabled: preview.hasFrame; onClicked: canvas.resetView(true); implicitHeight: 27 }
                        ActionButton { text: "100%"; accented: !canvas.fit; enabled: preview.hasFrame && !window.cropMode; onClicked: canvas.resetView(false); implicitHeight: 27 }
                    }
                }
                Item {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    FrameCanvas { id: canvas; objectName: "frameCanvas"; anchors.fill: parent; anchors.margins: 18; image: window.showOriginal ? preview.originalImage : (window.cropMode ? preview.uncroppedImage : preview.previewImage); enabled: !window.cropMode }
                    Rectangle { anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 26; width: 48; height: 20; radius: 2; color: "#b32d2d2c"; visible: preview.hasFrame && window.showOriginal
                        Text { anchors.centerIn: parent; text: "原图"; color: Theme.accent; font.pixelSize: 9; font.bold: true }
                    }
                    Rectangle {
                        anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 26
                        width: 72; height: 24; radius: 3
                        color: compareMouse.pressed ? "#3d3d3b" : (window.showOriginal ? "#555553" : "#2d2d2c")
                        visible: preview.hasFrame
                        border.width: 1; border.color: window.showOriginal ? Theme.accent : "#444442"
                        Text { anchors.centerIn: parent; text: "对比原图"; color: window.showOriginal ? Theme.accent : Theme.text; font.pixelSize: 10 }
                        MouseArea { id: compareMouse; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onPressed: window.showOriginal = true; onReleased: window.showOriginal = false; onCanceled: window.showOriginal = false }
                    }
                    Item {
                        id: cropOverlay
                        objectName: "cropOverlay"
                        anchors.fill: canvas
                        visible: window.cropMode
                        readonly property rect imageBox: canvas.imageRect
                        readonly property rect selectionBox: Qt.rect(imageBox.x + window.draftCrop.x * imageBox.width, imageBox.y + window.draftCrop.y * imageBox.height, window.draftCrop.width * imageBox.width, window.draftCrop.height * imageBox.height)
                        Rectangle { x: cropOverlay.imageBox.x; y: cropOverlay.imageBox.y; width: cropOverlay.imageBox.width; height: Math.max(0, cropOverlay.selectionBox.y - y); color: "#99000000" }
                        Rectangle { x: cropOverlay.imageBox.x; y: cropOverlay.selectionBox.y + cropOverlay.selectionBox.height; width: cropOverlay.imageBox.width; height: Math.max(0, cropOverlay.imageBox.y + cropOverlay.imageBox.height - y); color: "#99000000" }
                        Rectangle { x: cropOverlay.imageBox.x; y: cropOverlay.selectionBox.y; width: Math.max(0, cropOverlay.selectionBox.x - x); height: cropOverlay.selectionBox.height; color: "#99000000" }
                        Rectangle { x: cropOverlay.selectionBox.x + cropOverlay.selectionBox.width; y: cropOverlay.selectionBox.y; width: Math.max(0, cropOverlay.imageBox.x + cropOverlay.imageBox.width - x); height: cropOverlay.selectionBox.height; color: "#99000000" }
                        Rectangle {
                            x: cropOverlay.selectionBox.x; y: cropOverlay.selectionBox.y; width: cropOverlay.selectionBox.width; height: cropOverlay.selectionBox.height
                            color: "transparent"; border.color: Theme.accent; border.width: 1
                            Rectangle { x: parent.width / 3; width: 1; height: parent.height; color: "#70ffffff" }
                            Rectangle { x: parent.width * 2 / 3; width: 1; height: parent.height; color: "#70ffffff" }
                            Rectangle { y: parent.height / 3; height: 1; width: parent.width; color: "#70ffffff" }
                            Rectangle { y: parent.height * 2 / 3; height: 1; width: parent.width; color: "#70ffffff" }
                        }
                        MouseArea {
                            anchors.fill: parent; enabled: window.canEditCrop; cursorShape: Qt.CrossCursor
                            property point origin
                            property bool drawing: false
                            function normalized(x, y) {
                                return Qt.point(Math.max(0, Math.min(1, (x - cropOverlay.imageBox.x) / cropOverlay.imageBox.width)), Math.max(0, Math.min(1, (y - cropOverlay.imageBox.y) / cropOverlay.imageBox.height)))
                            }
                            function updateCrop(x, y) {
                                var p = normalized(x, y)
                                window.draftCrop = Qt.rect(Math.min(origin.x, p.x), Math.min(origin.y, p.y), Math.abs(p.x - origin.x), Math.abs(p.y - origin.y))
                            }
                            onPressed: function(mouse) {
                                var b = cropOverlay.imageBox
                                if (mouse.x < b.x || mouse.y < b.y || mouse.x > b.x + b.width || mouse.y > b.y + b.height) { mouse.accepted = false; return }
                                origin = normalized(mouse.x, mouse.y); drawing = true
                            }
                            onPositionChanged: function(mouse) { if (drawing) updateCrop(mouse.x, mouse.y) }
                            onReleased: function(mouse) {
                                if (!drawing) return
                                updateCrop(mouse.x, mouse.y); drawing = false
                                if (window.draftCrop.width > 0.003 && window.draftCrop.height > 0.003) window.finishCrop()
                                else window.draftCrop = preview.cropRect
                            }
                            onCanceled: { drawing = false; window.draftCrop = preview.cropRect }
                        }
                    }
                    Column {
                        anchors.centerIn: parent; spacing: 16; visible: !preview.hasFrame
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "FilmS"; color: "#e5e2dc"; font.pixelSize: 32; font.letterSpacing: 1 }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "导入 RAW，逐张选择胶片与相纸"; color: "#d0cdc6"; font.pixelSize: 13 }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "支持一次拖入多张照片"; color: "#c0bdb7"; font.pixelSize: 11 }
                    }
                    DropArea { anchors.fill: parent; enabled: window.canBrowse; onDropped: function(drop) { if (drop.hasUrls) preview.openFiles(drop.urls) } }
                    Rectangle {
                        anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 26
                        width: window.cropMode ? 78 : 48; height: 20; radius: 2; color: "#b32d2d2c"; visible: preview.hasFrame
                        Text { anchors.centerIn: parent; text: window.cropMode ? "裁切原图" : "FULL"; color: Theme.text; font.pixelSize: 9; font.letterSpacing: 1 }
                    }
                    BusyIndicator { anchors.centerIn: parent; running: preview.busy; visible: running; width: 44; height: 44 }
                }
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: 28; color: Theme.card
                    Text { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; verticalAlignment: Text.AlignVCenter; text: preview.hasFrame ? preview.metadata.width + " × " + preview.metadata.height + "   ·   SDR sRGB / 8 位预览   ·   " + (preview.dirty ? "更新预览中…" : "选择与调整自动应用") : "完整分辨率预览"; color: preview.dirty ? Theme.accent : Theme.muted; font.pixelSize: 10; elide: Text.ElideRight }
                }
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: Theme.stripHeight; color: Theme.card
                    Rectangle { width: parent.width; height: 1; color: Theme.line; opacity: 0.5 }
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 8; spacing: 6
                        RowLayout {
                            Layout.fillWidth: true; spacing: 6
                            Text { text: preview.libraryItems.length + " 张照片 · 已选 " + preview.selectedCount; color: Theme.muted; font.pixelSize: 10 }
                            Item { Layout.fillWidth: true }
                            ActionButton { text: "全选"; implicitHeight: 23; font.pixelSize: 10; enabled: window.canBrowse && preview.libraryItems.length > 0; onClicked: preview.selectAllPhotos(true) }
                            ActionButton { text: "清除选择"; implicitHeight: 23; font.pixelSize: 10; enabled: window.canBrowse && preview.selectedCount > 0; onClicked: preview.selectAllPhotos(false) }
                            ActionButton { text: "同步设置"; implicitHeight: 23; font.pixelSize: 10; enabled: window.canBrowse && preview.hasFrame && preview.selectedCount > 0; onClicked: preview.syncSelectedSettings(false); ToolTip.visible: hovered; ToolTip.text: "将当前照片的胶片、相纸、解码和效果同步到所选照片，不同步裁切。" }
                        }
                        ListView {
                            id: filmstrip; objectName: "photoStrip"
                            Layout.fillWidth: true; Layout.fillHeight: true
                            orientation: ListView.Horizontal; spacing: 8; clip: true
                            model: preview.libraryItems; currentIndex: preview.activeIndex
                            onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
                            ScrollBar.horizontal: ScrollBar {}
                            delegate: Rectangle {
                                required property var modelData
                                required property int index
                                width: 132; height: filmstrip.height - 3
                                color: Theme.wellDark; border.color: modelData.active ? Theme.accent : Theme.line; border.width: modelData.active ? 2 : 0
                                Text { anchors.centerIn: parent; anchors.verticalCenterOffset: -8; text: modelData.error ? "无法读取" : "点击预览"; color: modelData.error ? "#ffb39f" : Theme.muted; font.pixelSize: 10 }
                                FrameCanvas { anchors.fill: parent; anchors.margins: 5; anchors.bottomMargin: 22; image: modelData.thumbnail; enabled: false }
                                Text { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 6; text: modelData.name; color: Theme.text; font.pixelSize: 10; elide: Text.ElideMiddle }
                                MouseArea {
                                    anchors.fill: parent; enabled: window.canBrowse; hoverEnabled: true
                                    onClicked: { window.cropMode = false; preview.selectImage(index) }
                                    ToolTip.visible: containsMouse && modelData.error.length > 0; ToolTip.text: modelData.error
                                }
                                CheckBox { anchors.top: parent.top; anchors.right: parent.right; padding: 0; scale: 0.8; checked: modelData.selected; enabled: window.canBrowse; onToggled: preview.setItemSelected(index, checked) }
                            }
                        }
                    }
                }
            }
        }
        // Draggable divider: center | right rail
        Rectangle {
            id: rightDivider
            Layout.preferredWidth: 6; Layout.fillHeight: true; color: dividerR.containsMouse || dividerR.pressed ? "#555553" : "transparent"
            property real startX: 0
            property real startWidth: 0
            Rectangle { anchors.centerIn: parent; width: 1; height: parent.height; color: Theme.regionLine }
            MouseArea {
                id: dividerR
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.SplitHCursor
                onPressed: (mouse)=> {
                    rightDivider.startX = mouse.x
                    rightDivider.startWidth = window.rightPanelWidth
                }
                onPositionChanged: (mouse)=> {
                    if (pressed)
                        window.rightPanelWidth = Math.max(200, Math.min(520, Math.round(rightDivider.startWidth - (mouse.x - rightDivider.startX))))
                }
            }
        }
        Rectangle {
            Layout.preferredWidth: window.rightPanelWidth; Layout.fillHeight: true; color: Theme.card
            ColumnLayout {
                anchors.fill: parent; spacing: 0
                Rectangle {
                    id: tabBar; Layout.fillWidth: true; Layout.preferredHeight: 36; color: Theme.card
                    RowLayout {
                        anchors.fill: parent; spacing: 0
                        Rectangle {
                            Layout.fillWidth: true; Layout.fillHeight: true; color: "transparent"
                            MouseArea { anchors.fill: parent; onClicked: tabBar.current = 0 }
                            Text { anchors.centerIn: parent; text: "显影前"; color: tabBar.current === 0 ? Theme.text : Theme.muted; font.pixelSize: 12; font.bold: tabBar.current === 0 }
                            Rectangle { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; height: 2; color: Theme.accent; visible: tabBar.current === 0 }
                        }
                        Rectangle {
                            Layout.fillWidth: true; Layout.fillHeight: true; color: "transparent"
                            MouseArea { anchors.fill: parent; onClicked: tabBar.current = 1 }
                            Text { anchors.centerIn: parent; text: "显影后"; color: tabBar.current === 1 ? Theme.text : Theme.muted; font.pixelSize: 12; font.bold: tabBar.current === 1 }
                            Rectangle { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; height: 2; color: Theme.accent; visible: tabBar.current === 1 }
                        }
                    }
                    property int current: 0
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.regionLine }
                ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width; spacing: 0
                        // --- 显影前 tab ---
                        ColumnLayout {
                            visible: tabBar.current === 0; Layout.fillWidth: true; spacing: 0
                            Section {
                                title: "胶片曝光"
                                Adjustment { label: "胶片曝光"; setting: "filmExposureEv"; from: -4; to: 4; step: 0.1; decimals: 1; suffix: " EV" }
                                RowLayout {
                                    Text { text: "画幅长边"; color: Theme.text; font.pixelSize: 11 }
                                    ComboBox {
                                        Layout.fillWidth: true
                                        model: ["18 mm", "24 mm", "35 mm（默认）", "36 mm（135）", "56 mm", "70 mm", "90 mm"]
                                        readonly property var lengths: [18, 24, 35, 36, 56, 70, 90]
                                        readonly property real actualLength: Number(preview.adjustments.filmFormatMm === undefined ? 35 : preview.adjustments.filmFormatMm)
                                        currentIndex: lengths.indexOf(actualLength)
                                        displayText: currentIndex < 0 ? actualLength.toFixed(1) + " mm（自定）" : currentText
                                        enabled: preview.settingsEditable
                                        onActivated: preview.setAdjustment("filmFormatMm", lengths[currentIndex])
                                    }
                                }
                                Effect { label: "颗粒"; activeKey: "grainActive"; amountKey: "grainAmount" }
                                Effect { label: "光晕"; activeKey: "halationActive"; amountKey: "halationAmount"; maximum: 4 }
                                Effect { label: "眩光"; activeKey: "glareActive"; amountKey: "glareAmount"; maximum: 30 }
                            }
                        }
                        // --- 显影后 tab ---
                        ColumnLayout {
                            visible: tabBar.current === 1; Layout.fillWidth: true; spacing: 0
                            Section {
                                title: "放大机与相纸"
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "相纸亮度"; color: Theme.text; font.pixelSize: 11 }
                                    Item { Layout.fillWidth: true }
                                    Text { text: preview.filmIsPositive ? "不适用" : (preview.exposureEv >= 0 ? "+" : "") + preview.exposureEv.toFixed(2) + " EV"; color: Theme.muted; font.pixelSize: 10 }
                                }
                                Slider {
                                    objectName: "exposureSlider"; Layout.fillWidth: true; from: -3; to: 3; stepSize: 0.25; implicitHeight: 24
                                    value: preview.exposureEv; enabled: preview.settingsEditable && !preview.filmIsPositive
                                    onPressedChanged: { if (pressed) preview.beginEditGesture("exposureEv"); else preview.endEditGesture() }
                                    onMoved: preview.exposureEv = value
                                }
                                Adjustment { label: "黄色滤镜"; setting: "yFilterShift"; from: -1; to: 1; step: 0.05; decimals: 2; available: !preview.filmIsPositive }
                                Adjustment { label: "洋红滤镜"; setting: "mFilterShift"; from: -1; to: 1; step: 0.05; decimals: 2; available: !preview.filmIsPositive }
                                Adjustment { label: "预闪"; setting: "preflash"; from: 0; to: 3; step: 0.25; decimals: 2; suffix: " ×100"; available: !preview.filmIsPositive }
                            }
                            Section {
                                title: "基础调整"
                                Adjustment { label: "亮度"; setting: "brightness"; from: -100; to: 100; step: 1; decimals: 0 }
                                Adjustment { label: "对比度"; setting: "contrast"; from: 0.5; to: 2; step: 0.01; decimals: 2; fallback: 1 }
                                Adjustment { label: "饱和度"; setting: "saturation"; from: 0; to: 2; step: 0.01; decimals: 2; fallback: 1 }
                            }
                            Section {
                                title: "RGB 通道"
                                Adjustment { label: "红色"; setting: "rGain"; from: 0.5; to: 1.5; step: 0.01; decimals: 2; fallback: 1 }
                                Adjustment { label: "绿色"; setting: "gGain"; from: 0.5; to: 1.5; step: 0.01; decimals: 2; fallback: 1 }
                                Adjustment { label: "蓝色"; setting: "bGain"; from: 0.5; to: 1.5; step: 0.01; decimals: 2; fallback: 1 }
                            }
                            Section {
                                title: "曲线"
                                Adjustment { label: "黑场"; setting: "blackPoint"; from: 0; to: 30; step: 1; decimals: 0 }
                                Adjustment { label: "白场"; setting: "whitePoint"; from: 225; to: 255; step: 1; decimals: 0; fallback: 255 }
                                Adjustment { label: "曲线强度"; setting: "curveStrength"; from: 0; to: 1; step: 0.01; decimals: 2 }
                            }
                            Section {
                                title: "色彩平衡"
                                Adjustment { label: "色温"; setting: "temperature"; from: -100; to: 100; step: 1; decimals: 0 }
                                Adjustment { label: "色调"; setting: "tint"; from: -100; to: 100; step: 1; decimals: 0 }
                                Adjustment { label: "高光"; setting: "highlights"; from: -100; to: 100; step: 1; decimals: 0 }
                                Adjustment { label: "阴影"; setting: "shadows"; from: -100; to: 100; step: 1; decimals: 0 }
                            }
                            Section {
                                title: "暗角"
                                Adjustment { label: "数量"; setting: "vignetteAmount"; from: -100; to: 100; step: 1; decimals: 0 }
                                Adjustment { label: "中点"; setting: "vignetteMidpoint"; from: 0; to: 100; step: 1; decimals: 0; fallback: 50 }
                            }
                        }
                        Section {
                            title: "RAW 解码"
                            ComboBox { id: decode; Layout.fillWidth: true; model: ["兼容模式（默认）", "高光扩展（实验）"]; currentIndex: preview.decodeMode; enabled: preview.settingsEditable; onActivated: preview.decodeMode = currentIndex }
                            Note { text: preview.decodeMode ? "保留去马赛克后的部分高光及负值；仍使用整数去马赛克。" : "与已验证的原生 RAW 流程保持一致。" }
                        }
                        Section {
                            title: "照片信息"
                            Note { text: preview.hasFrame ? preview.metadata.camera : "尚未打开照片"; color: Theme.text }
                            Note { visible: preview.hasFrame && preview.metadata.exif; text: preview.metadata.exif; color: Theme.text }
                            Note { text: preview.hasFrame ? preview.metadata.width + " × " + preview.metadata.height + " 像素\n" + (preview.metadata.mode === "headroom" ? "高光扩展解码" : "兼容解码") : "" }
                            Note { text: preview.hasFrame ? "本次渲染 " + (preview.metadata.renderMs / 1000).toFixed(2) + " 秒 · " + (preview.metadata.cached ? "复用负片" : "完整渲染") : "" }
                            ActionButton { Layout.fillWidth: true; text: "重置当前照片设置"; enabled: preview.settingsEditable; onClicked: preview.resetSettings() }
                        }
                        Section {
                            title: preview.batchExporting ? "批量导出" : preview.failed ? "无法完成操作" : "状态"
                            Note { text: window.closePending ? "等待当前操作完成后关闭…" : preview.status; color: preview.failed ? "#ffb39f" : Theme.muted }
                            Note { objectName: "savedStatusNote"; text: preview.savedStatus; visible: text.length > 0 }
                            Note { objectName: "persistenceWarningNote"; text: preview.persistenceWarning; visible: text.length > 0; color: "#ffb39f" }
                            ProgressBar { Layout.fillWidth: true; visible: preview.batchExporting; from: 0; to: Math.max(1, preview.batchTotal); value: preview.batchProgress }
                            ActionButton { Layout.fillWidth: true; text: "当前照片完成后停止"; visible: preview.batchExporting; onClicked: preview.cancelExport() }
                            ActionButton { Layout.fillWidth: true; text: "重新尝试预览"; visible: preview.failed && preview.hasFrame && !preview.batchExporting; enabled: !preview.busy; onClicked: preview.apply() }
                        }
                    }
                }
            }
        }
    }
        }
    }
    Popup {
        id: exportPopup
        objectName: "exportPopup"
        anchors.centerIn: parent
        width: 470; padding: 22
        modal: true; focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onVisibleChanged: window.updateDialogState()
        background: Rectangle { color: Theme.card; border.color: Theme.line; radius: 7 }
        contentItem: ColumnLayout {
            spacing: 14
            Text { text: "导出照片"; color: Theme.text; font.pixelSize: 20; font.bold: true }
            Note { text: "当前画面或所选照片 · sRGB\n裁切与效果一起写入文件。" }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line; opacity: 0.5 }
            RowLayout {
                Text { text: "格式"; color: Theme.text; Layout.preferredWidth: 76 }
                ComboBox { Layout.fillWidth: true; model: ["TIFF · 16 位", "PNG · 8 位", "PNG · 16 位", "JPEG · 8 位"]; currentIndex: window.exportFormat; onActivated: window.exportFormat = currentIndex }
            }
            RowLayout {
                visible: window.exportFormat === 3
                Text { text: "JPEG 质量"; color: Theme.text; Layout.preferredWidth: 76 }
                Slider { Layout.fillWidth: true; from: 1; to: 100; stepSize: 1; value: window.exportQuality; onMoved: window.exportQuality = value }
                Text { text: window.exportQuality; color: Theme.muted; Layout.preferredWidth: 28 }
            }
            RowLayout {
                Text { text: "输出尺寸"; color: Theme.text; Layout.preferredWidth: 76 }
                ComboBox {
                    Layout.fillWidth: true; model: ["原始裁切尺寸", "长边 3840 像素", "长边 2560 像素", "长边 1920 像素", "长边 1080 像素"]
                    readonly property var sizes: [0, 3840, 2560, 1920, 1080]
                    currentIndex: Math.max(0, sizes.indexOf(window.exportLongEdge))
                    onActivated: window.exportLongEdge = sizes[currentIndex]
                }
            }
            Note { text: "保持长宽比例，不放大图像。PNG 无损保存，JPEG 为有损压缩。" }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line; opacity: 0.5 }
            ActionButton { Layout.fillWidth: true; accented: true; text: "导出当前已显示画面…"; enabled: preview.hasFrame && !preview.busy; onClicked: { saveDialog.open(); exportPopup.close() } }
            ActionButton { Layout.fillWidth: true; text: "导出所选 " + preview.selectedCount + " 张到文件夹…"; enabled: preview.selectedCount > 0 && !preview.busy; onClicked: { batchDialog.open(); exportPopup.close() } }
            Note { text: "当前画面导出不覆盖已有文件。批量导出逐张处理，自动跳过同名文件；未访问的照片也按各自设置渲染。" }
            ActionButton { Layout.alignment: Qt.AlignRight; text: "取消"; onClicked: exportPopup.close() }
        }
    }
}
