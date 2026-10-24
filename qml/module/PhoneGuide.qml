import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// "On your phone": which app to install (KDE Connect for Android, LocalSend for
// iPhone, iPad, Mac, Windows and Android too), QR codes to their official
// store pages and the pairing steps. Lingmo has no phone app of its own.
ColumnLayout {
    id: guide
    // The name this computer shows on the phone
    property string deviceName
    // Show only one of the two apps ("android", "localsend") or both ("")
    property string only: ""
    property bool compact: false

    spacing: LingmoUI.Units.largeSpacing

    component Step: RowLayout {
        property int number
        property string text
        Layout.fillWidth: true
        spacing: LingmoUI.Units.smallSpacing * 1.5

        Rectangle {
            Layout.alignment: Qt.AlignTop
            implicitWidth: 22
            implicitHeight: 22
            radius: 11
            color: LingmoUI.Theme.highlightColor
            Label {
                anchors.centerIn: parent
                text: parent.parent.number
                color: LingmoUI.Theme.highlightedTextColor
                font.pointSize: LingmoUI.Units.fontCaption
                font.weight: Font.DemiBold
            }
        }
        Label {
            Layout.fillWidth: true
            text: parent.text
            wrapMode: Text.WordWrap
            textFormat: Text.StyledText
            font.pointSize: LingmoUI.Units.fontBody
            color: LingmoUI.Theme.textColor
        }
    }

    component AppCard: LingmoUI.Card {
        id: appCard
        property string appName
        property string platforms
        property string iconName
        property color tint
        property var codes: []
        property var steps: []
        Layout.fillWidth: true

        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: appCard.padding
            anchors.rightMargin: appCard.padding
            anchors.topMargin: LingmoUI.Units.largeSpacing
            anchors.bottomMargin: LingmoUI.Units.largeSpacing
            spacing: LingmoUI.Units.largeSpacing

            RowLayout {
                spacing: LingmoUI.Units.largeSpacing
                Rectangle {
                    implicitWidth: LingmoUI.Units.iconTileSize
                    implicitHeight: LingmoUI.Units.iconTileSize
                    radius: LingmoUI.Units.iconTileRadius
                    color: appCard.tint
                    Image {
                        anchors.centerIn: parent
                        width: 16; height: 16
                        sourceSize: Qt.size(32, 32)
                        source: appCard.iconName
                    }
                }
                ColumnLayout {
                    spacing: 0
                    Label {
                        color: LingmoUI.Theme.textColor
                        text: appCard.appName
                        font.pointSize: LingmoUI.Units.fontSubheading
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: appCard.platforms
                        font.pointSize: LingmoUI.Units.fontCaption
                        color: LingmoUI.Theme.secondaryTextColor
                    }
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: LingmoUI.Units.largeSpacing * 2
                visible: !guide.compact
                Repeater {
                    model: appCard.codes
                    delegate: QrCard {
                        required property var modelData
                        label: modelData.label
                        url: modelData.url
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: LingmoUI.Units.smallSpacing * 1.5
                Repeater {
                    model: appCard.steps
                    delegate: Step {
                        required property var modelData
                        required property int index
                        number: index + 1
                        text: modelData
                    }
                }
            }
        }
    }

    AppCard {
        visible: guide.only === "" || guide.only === "android"
        appName: "KDE Connect"
        platforms: qsTr("Android phones and tablets: notifications, messages, files, clipboard and more")
        iconName: Qt.resolvedUrl("images/phone.svg")
        tint: LingmoUI.Theme.greenColor
        codes: [
            { label: qsTr("Google Play"), url: "https://play.google.com/store/apps/details?id=org.kde.kdeconnect_tp" },
            { label: qsTr("F-Droid"), url: "https://f-droid.org/packages/org.kde.kdeconnect_tp/" }
        ]
        steps: [
            qsTr("On the phone, install <b>KDE Connect</b>: point the camera at a code above."),
            qsTr("Connect the phone and this computer to the same Wi-Fi network."),
            qsTr("Open KDE Connect on the phone. <b>%1</b> shows up under <i>Available devices</i>: tap it, then tap <i>Request pairing</i>.").arg(guide.deviceName),
            qsTr("Here, accept the request and check that the verification key is the same on both screens."),
            qsTr("On the phone, allow what KDE Connect asks for (notifications, SMS, files) to use each feature.")
        ]
    }

    AppCard {
        visible: guide.only === "" || guide.only === "localsend"
        appName: "LocalSend"
        platforms: qsTr("iPhone, iPad, Mac, Windows and Android: send files both ways")
        iconName: Qt.resolvedUrl("images/send.svg")
        tint: LingmoUI.Theme.blueColor
        codes: [
            { label: qsTr("App Store"), url: "https://apps.apple.com/app/localsend/id1661733229" },
            { label: qsTr("Google Play"), url: "https://play.google.com/store/apps/details?id=org.localsend.localsend_app" },
            { label: qsTr("localsend.org"), url: "https://localsend.org/download" }
        ]
        steps: [
            qsTr("Install <b>LocalSend</b> on the phone, tablet or computer: point the camera at a code above."),
            qsTr("Connect it and this computer to the same Wi-Fi network."),
            qsTr("To send here: open LocalSend, tap <i>Send</i>, pick the files and tap <b>%1</b> under <i>Nearby devices</i>. Accept the request on this computer (tick <i>Always accept from this device</i> to stop being asked).").arg(guide.deviceName),
            qsTr("To send to the phone: keep LocalSend open on <i>Receive</i>, then drag files to the edge of this screen or use <i>Send to nearby device</i>."),
            qsTr("If you set a PIN in the settings, LocalSend asks for it when sending.")
        ]
    }
}
