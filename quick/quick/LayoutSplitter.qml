import QtQuick 2.0
import QtQuick.Layouts 1.1

Rectangle {
    id: divider
    Layout.fillWidth: true
    height: 0
    opacity: 0.0
    color: "black"
    z: 1

    onYChanged: {
        heightmap1.height = y
        heightmap2.y = y
        heightmap2.height = parent.height-y
    }

    Rectangle {
        // this
        height: 2
        anchors.fill: parent
        anchors.topMargin: -height/2
        anchors.bottomMargin: anchors.topMargin
        color: "black"
    }

    Rectangle {
        anchors.fill: parent
        anchors.topMargin: heightmap1.isIOS ? -50 : -5
        anchors.bottomMargin: anchors.topMargin
        opacity: 0.0

        MouseArea {
            id: dragArea
            anchors.fill: parent

            drag.target: divider
            drag.axis: Drag.YAxis
            drag.minimumY: -divider.height/2
            drag.maximumY: divider.parent.height - drag.minimumY - divider.height
            drag.threshold: 0

            hoverEnabled: true
            cursorShape: Qt.SizeVerCursor
            onEntered: divider.opacity=0.5
            onExited: divider.opacity=0.0
        }
    }
}
