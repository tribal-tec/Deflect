import QtQuick 2.4
import QtQuick.Controls 1.1
import QtQuick.Controls.Styles 1.1

Item {
    id: item
    width: 200
    height: 300


    signal pressed(int buttonid)

    Column
    {
        anchors.fill: parent
        anchors.margins: 30
        spacing: 30

        Row {
            spacing: 30

            Button {
                id: tf1
                width: 0.4 * item.width
                height: 0.4 * item.height
                text: qsTr("Blue")
                onClicked: item.pressed(0)
                style: ButtonStyle {
                  label: Text {
                    renderType: Text.NativeRendering
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                    color: "blue"
                    text: control.text
                  }
                }
            }

            Button {
                id: tf2
                width: 0.4 * item.width
                height: 0.4 * item.height
                text: qsTr("Yellow")
                onClicked: item.pressed(1)
                style: ButtonStyle {
                  label: Text {
                    color: "yellow"
                    text: control.text
                  }
                }
            }
        }

        Row {
            spacing: 30

            Button {
                id: tf3
                width: 0.4 * item.width
                height: 0.4 * item.height
                text: qsTr("Gray")
                onClicked: item.pressed(2)
                style: ButtonStyle {
                  label: Text {
                    color: "gray"
                    text: control.text
                  }
                }
            }

            Button {
                id: tf4
                width: 0.4 * item.width
                height: 0.4 * item.height
                text: qsTr("Red")
                onClicked: item.pressed(3)
                style: ButtonStyle {
                  label: Text {
                    color: "red"
                    text: control.text
                  }
                }
            }
        }
    }
}
