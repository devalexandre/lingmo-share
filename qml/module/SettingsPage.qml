import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI
import Lingmo.Share

// "Phone and devices" settings: used by Lingmo Settings and by the app
ColumnLayout {
    id: page
    spacing: LingmoUI.Units.largeSpacing

    ShareSettings {
        id: settings
    }

    component SectionTitle: Label {
        Layout.fillWidth: true
        Layout.topMargin: LingmoUI.Units.smallSpacing
        font.pointSize: LingmoUI.Units.fontCaption
        font.weight: Font.DemiBold
        color: LingmoUI.Theme.secondaryTextColor
    }

    component SettingRow: RowLayout {
        id: row
        property string title
        property string subtitle
        default property alias trailing: trailingBox.data
        Layout.fillWidth: true
        Layout.minimumHeight: LingmoUI.Units.controlHeight
        spacing: LingmoUI.Units.largeSpacing

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Label {
                color: LingmoUI.Theme.textColor
                Layout.fillWidth: true
                text: row.title
                font.pointSize: LingmoUI.Units.fontBody
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                visible: text.length > 0
                text: row.subtitle
                font.pointSize: LingmoUI.Units.fontCaption
                color: LingmoUI.Theme.secondaryTextColor
                wrapMode: Text.WordWrap
            }
        }
        RowLayout {
            id: trailingBox
            spacing: LingmoUI.Units.smallSpacing
        }
    }

    component Divider: Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: LingmoUI.Theme.dividerColor
    }

    component Section: LingmoUI.Card {
        id: section
        default property alias content: sectionBody.data
        Layout.fillWidth: true
        ColumnLayout {
            id: sectionBody
            anchors.fill: parent
            anchors.leftMargin: section.padding
            anchors.rightMargin: section.padding
            anchors.topMargin: LingmoUI.Units.largeSpacing
            anchors.bottomMargin: LingmoUI.Units.largeSpacing
            spacing: LingmoUI.Units.largeSpacing
        }
    }

    // ------------------------------------------------ this computer
    SectionTitle { text: qsTr("This computer") }
    Section {
        SettingRow {
            title: qsTr("Device name")
            subtitle: qsTr("How phones and other computers see this computer.")
            TextField {
                Layout.preferredWidth: 240
                text: settings.deviceName
                selectByMouse: true
                maximumLength: 48
                onEditingFinished: settings.deviceName = text
            }
        }
        Divider {}
        SettingRow {
            title: qsTr("Visible to")
            subtitle: settings.visibility === 0 ? qsTr("Anyone nearby can find this computer. You are asked before anything is received.")
                    : settings.visibility === 1 ? qsTr("Only trusted devices can find this computer and send to it.")
                    : qsTr("Nothing on the network can see this computer. You can still send.")
            LingmoUI.SegmentedControl {
                model: [qsTr("Everyone"), qsTr("Trusted only"), qsTr("Off")]
                currentIndex: settings.visibility
                onActivated: (index) => settings.visibility = index
            }
        }
        Divider {}
        SettingRow {
            title: qsTr("Receive folder")
            subtitle: settings.receiveFolder
            LingmoUI.ActionButton {
                text: qsTr("Choose…")
                onClicked: folderDialog.open()
            }
        }
        Divider {}
        SettingRow {
            title: qsTr("Drop bubbles while dragging")
            subtitle: qsTr("Drag files to the right edge of the screen to send them to a nearby device.")
            Switch {
                checked: settings.dropEdgeEnabled
                onToggled: settings.dropEdgeEnabled = checked
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Receive folder")
        currentFolder: "file://" + settings.receiveFolder
        onAccepted: settings.receiveFolder = selectedFolder.toString()
    }

    // ------------------------------------------------ LocalSend
    SectionTitle { text: qsTr("iPhone, iPad, Mac and Windows (LocalSend)") }
    Section {
        SettingRow {
            title: qsTr("LocalSend")
            subtitle: qsTr("Send and receive files with the LocalSend app, encrypted on your network.")
            Switch {
                checked: settings.localSendEnabled
                onToggled: settings.localSendEnabled = checked
            }
        }
        Divider { visible: settings.localSendEnabled }
        SettingRow {
            visible: settings.localSendEnabled
            title: qsTr("Ask for a PIN")
            subtitle: qsTr("Senders must type this PIN before they can ask to send.")
            TextField {
                visible: settings.pinEnabled
                Layout.preferredWidth: 110
                text: settings.pin
                inputMethodHints: Qt.ImhDigitsOnly
                maximumLength: 12
                onEditingFinished: settings.pin = text
            }
            Switch {
                checked: settings.pinEnabled
                onToggled: {
                    if (checked && settings.pin.length === 0)
                        settings.pin = settings.randomPin()
                    settings.pinEnabled = checked
                }
            }
        }
        Divider { visible: settings.localSendEnabled && trustedRepeater.count > 0 }
        Label {
            visible: settings.localSendEnabled && trustedRepeater.count > 0
            text: qsTr("Trusted devices: they send without asking")
            font.pointSize: LingmoUI.Units.fontCaption
            color: LingmoUI.Theme.secondaryTextColor
        }
        Repeater {
            id: trustedRepeater
            model: settings.localSendEnabled ? settings.trustedDevices : []
            delegate: SettingRow {
                required property var modelData
                title: modelData.alias
                subtitle: qsTr("Key %1").arg(modelData.fingerprint.substring(0, 8))
                LingmoUI.ActionButton {
                    text: qsTr("Remove")
                    destructive: true
                    subtle: true
                    onClicked: settings.untrust(modelData.fingerprint)
                }
            }
        }
    }

    // ------------------------------------------------ Android
    SectionTitle { text: qsTr("Android (KDE Connect)") }
    Section {
        LingmoUI.Banner {
            Layout.fillWidth: true
            visible: !settings.androidInstalled
            type: LingmoUI.Banner.Info
            title: qsTr("Android support is not installed")
            text: qsTr("It adds notifications, messages, battery and more from Android phones (about 27 MB).")
            closable: false
            LingmoUI.ActionButton {
                text: qsTr("Install Android support")
                primary: true
                onClicked: settings.installAndroidSupport()
            }
        }
        SettingRow {
            visible: settings.androidInstalled
            title: qsTr("Connect to Android phones")
            subtitle: qsTr("Through KDE Connect, encrypted. Pairing is needed once per phone.")
            Switch {
                checked: settings.androidEnabled
                onToggled: settings.androidEnabled = checked
            }
        }
        Repeater {
            model: settings.androidInstalled && settings.androidEnabled ? settings.plugins() : []
            delegate: ColumnLayout {
                required property string modelData
                Layout.fillWidth: true
                spacing: LingmoUI.Units.largeSpacing
                Divider {}
                SettingRow {
                    title: settings.pluginTitle(modelData)
                    subtitle: settings.pluginDescription(modelData)
                    Switch {
                        checked: settings.pluginEnabled(modelData)
                        onToggled: settings.setPluginEnabled(modelData, checked)
                    }
                }
            }
        }
    }

    // ------------------------------------------------ Lingmo next to Lingmo
    SectionTitle { text: qsTr("Another Lingmo computer next to this one") }
    Section {
        SettingRow {
            title: qsTr("Where it sits")
            subtitle: qsTr("Drag files across that edge of the screen to send them to it. Only files: mouse and keyboard stay on each computer.")
            LingmoUI.SegmentedControl {
                readonly property var sides: ["none", "left", "right", "top"]
                model: [qsTr("None"), qsTr("Left"), qsTr("Right"), qsTr("Above")]
                currentIndex: Math.max(0, sides.indexOf(settings.edgeSide))
                onActivated: (index) => settings.edgeSide = sides[index]
            }
        }
        Divider { visible: settings.edgeSide !== "none" }
        SettingRow {
            visible: settings.edgeSide !== "none"
            title: qsTr("Computer")
            subtitle: settings.edgePeerName.length > 0 ? qsTr("It must have Lingmo Share on, and should trust this computer.")
                                                       : qsTr("Turn the other computer on and open Phone and devices there.")
            ComboBox {
                id: peerCombo
                Layout.preferredWidth: 220
                readonly property var peers: settings.devices.filter(d => d.lingmo)
                model: peers.length > 0 ? peers.map(d => d.name) : [settings.edgePeerName.length > 0 ? settings.edgePeerName : qsTr("None found")]
                enabled: peers.length > 0
                currentIndex: {
                    for (let i = 0; i < peers.length; ++i)
                        if (peers[i].id === "ls:" + settings.edgePeer) return i
                    return -1
                }
                onActivated: (index) => {
                    settings.edgePeer = peers[index].id.substring(3)
                    settings.edgePeerName = peers[index].name
                }
            }
            LingmoUI.IconButton {
                iconName: "view-refresh"
                tipText: qsTr("Look again")
                onClicked: settings.refreshDevices()
            }
        }
    }

    // ------------------------------------------------ guide
    SectionTitle { text: qsTr("On your phone") }
    PhoneGuide {
        Layout.fillWidth: true
        deviceName: settings.deviceName
    }
}
