#!/bin/bash
# Screenshots of Lingmo Share under Xvfb, in a private network namespace,
# with a scratch HOME, a fake kdeconnectd and a fake theme service:
#   unshare -rn xvfb-run -a -s "-screen 0 1440x900x24" dbus-run-session -- \
#       bash tests/ui-shots.sh <build-dir> <scratch-dir> <out-dir> light|dark
set -u
# Everything started here goes when the script ends (the fakes outlive their bus otherwise)
trap 'kill $(jobs -p) 2>/dev/null' EXIT
B=$(realpath "$1"); S=$(realpath -m "$2"); OUT=$(realpath -m "$3"); THEME=$4
SRC=$(dirname "$(realpath "$0")")
P=$THEME
ip link set lo up; ip link set lo multicast on; ip route add 224.0.0.0/4 dev lo 2>/dev/null
rm -rf "$S"; mkdir -p "$S/home/.config/lingmoos" "$S/home/Downloads" "$OUT" "$S/files"
export HOME=$S/home XDG_CONFIG_HOME=$S/home/.config XDG_DATA_HOME=$S/home/.local/share
export LANG=pt_BR.UTF-8 LC_ALL=pt_BR.UTF-8 QT_FORCE_STDERR_LOGGING=1
export QT_QPA_PLATFORMTHEME=lingmo QML_IMPORT_PATH=$B/qml LINGMO_SHARE_TRANSLATIONS=$B
export LINGMO_SHARE_FAKE_KDECONNECT=1 LINGMO_SHARE_LOOPBACK=1 LINGMO_SHARE_PORT=53531
unset QT_STYLE_OVERRIDE
shot() { sleep "${2:-1}"; import -display "$DISPLAY" -window root "$OUT/$1-$P.png"; echo "shot $1-$P"; }
appshot() { # crop to the app window
    sleep "${2:-1}"
    local id; id=$(xdotool search --name "^Celular e dispositivos$" | head -1)
    eval "$(xdotool getwindowgeometry --shell "$id")"
    import -display "$DISPLAY" -window root -crop "${WIDTH}x${HEIGHT}+${X}+${Y}" +repage "$OUT/$1-$P.png"; echo "shot $1-$P"
}

convert "/usr/share/backgrounds/lingmoos/archlingmo-$THEME.jpg" -resize 1440x900^ -gravity center -extent 1440x900 "$S/wall.png"
feh -x -g 1440x900+0+0 --title wallpaper "$S/wall.png" >/dev/null 2>&1 &
sleep 1
picom --backend xrender --no-fading-openclose >/dev/null 2>&1 &
"$B/fake-theme" "$THEME" &
"$B/fake-kdeconnectd" > "$S/fake.log" 2>&1 &
sleep 1

# The Lingmo PC on the other desk: another lingmo-share, on its own bus, without a screen
OTHER=$S/other; mkdir -p "$OTHER/.config/lingmoos" "$OTHER/Downloads"
printf '[Device]\nname=Lingmo do Escritório\nreceiveFolder=%s\n[Android]\nenabled=false\n' "$OTHER/Downloads" > "$OTHER/.config/lingmoos/share.conf"
OBUS=$(mktemp -d /tmp/claude-1000/lsb.XXXX)
dbus-daemon --session --address="unix:path=$OBUS/b" --fork --print-pid > "$S/obus.pid"
other() { env HOME="$OTHER" XDG_CONFIG_HOME="$OTHER/.config" XDG_DATA_HOME="$OTHER/.local/share" LINGMO_SHARE_PORT=53532 \
              QT_QPA_PLATFORM=offscreen DBUS_SESSION_BUS_ADDRESS="unix:path=$OBUS/b" "$@"; }
other "$B/lingmo-share" --daemon > "$S/other.log" 2>&1 &
sleep 1.5
fp() { openssl x509 -in "$1/.local/share/lingmo-share/localsend-cert.pem" -outform DER | sha256sum | cut -d" " -f1 | tr a-f A-F; }
FP_OTHER=$(fp "$OTHER")

# This PC: the other one sits on the left and is trusted
cat > "$HOME/.config/lingmoos/share.conf" <<CONF
[Device]
name=Lingmo de Ana
receiveFolder=$HOME/Downloads
[Edge]
side=left
peer=$FP_OTHER
peerName=Lingmo do Escritório
[Trusted]
$FP_OTHER=Lingmo do Escritório
CONF
"$B/lingmo-share" --daemon > "$S/daemon.log" 2>&1 &
sleep 2
FP_ME=$(fp "$HOME")
printf '[Trusted]\n%s=Lingmo de Ana\n' "$FP_ME" >> "$OTHER/.config/lingmoos/share.conf"

# Phones and computers with LocalSend
python3 -I "$SRC/interop_peer.py" present 53531 "$FP_ME" "$S/ipad" "iPad de Ana" iPad mobile >/dev/null
python3 -I "$SRC/interop_peer.py" present 53531 "$FP_ME" "$S/mac" "MacBook Air" Mac desktop >/dev/null
sleep 1

"$B/lingmo-share" --page devices
appshot 01-app-devices 3
"$B/lingmo-share" --page phone
appshot 02-phone-overview 2
"$B/lingmo-share" --page phone:notifications
appshot 03-phone-notifications 2
"$B/lingmo-share" --page phone:messages
appshot 04-phone-messages 3
"$B/lingmo-share" --page guide
appshot 05-guide-qr-codes 2
"$B/lingmo-share" --page settings
appshot 06-app-settings 2

# Receiving: an iPhone (LocalSend) offers three photos -> confirmation
python3 -I "$SRC/interop_peer.py" offer 53531 "$FP_ME" "$S/iphone" "iPhone de Ana" iPhone mobile > "$S/offer.log" 2>&1 &
OFFER=$!
shot 07-receive-request 5
kill $OFFER 2>/dev/null

# The phone sends a photo over KDE Connect -> progress in Transfers
dbus-send --session --dest=org.kde.kdeconnect --type=method_call /test org.lingmo.FakeKdeConnect.incomingFile
head -c 3000000 /dev/urandom > "$S/files/Relatório 2026.pdf"
"$B/lingmo-share" send --no-wait --to kdc:a1b2c3d4_galaxy "$S/files/Relatório 2026.pdf"
"$B/lingmo-share" --page transfers
appshot 08-transfers 1.8

# Pairing: the Pixel asks to pair
dbus-send --session --dest=org.kde.kdeconnect --type=method_call /test org.lingmo.FakeKdeConnect.requestPair
shot 09-pairing-request 2
for w in $(xdotool search --name "^Parear celular$"); do xdotool windowunmap "$w"; done
for w in $(xdotool search --name "^Celular e dispositivos$"); do xdotool windowunmap "$w"; done

# Drag files from a window to the right edge -> drop bubbles
convert "/usr/share/backgrounds/lingmoos/archlingmo-light.jpg" -resize 1200x800 "$S/files/Férias.jpg"
"$B/drag-source" "$S/files/Férias.jpg" > "$S/drag.log" 2>&1 &
DRAG=$!
sleep 1.5
xdotool mousemove 170 130 mousedown 1
for x in 190 260 400 700 1000 1300 1400 1430; do xdotool mousemove $x 450; sleep 0.15; done
xdotool mousemove 1437 450; sleep 0.4; xdotool mousemove 1438 452
shot 10-drop-bubbles 1.2
# Onto the first bubble (column centred on the right panel)
BUBBLE_Y=${BUBBLE_Y:-550}
if [ "$BUBBLE_Y" -gt 0 ]; then
    for y in 440 $BUBBLE_Y; do xdotool mousemove 1365 $y; sleep 0.3; done
    shot 11-drop-hover 0.6
    xdotool mouseup 1
    shot 12-drop-sending 1.2
else
    xdotool mouseup 1
fi
sleep 3
kill $DRAG 2>/dev/null

# "Send to nearby device" picker (file manager / Screenshots / CLI)
"$B/lingmo-share" send --pick "$S/files/Férias.jpg"
shot 13-picker 2.5
xdotool key Escape

# The Lingmo PC on the left sends a photo -> it flies in from the left
other "$B/lingmo-share" send --to "ls:$FP_ME" "$S/files/Férias.jpg" > "$S/other-send.log" 2>&1 &
shot 14-fly-in 1.6

sleep 1
kill "$(cat "$S/obus.pid")" 2>/dev/null
rm -rf "$OBUS"
echo "--- daemon log"; grep -v "^$\|Gtk-\|portal\|fusermount\|fuse init\|MESA" "$S/daemon.log" | head -40
echo "--- fake log"; tail -15 "$S/fake.log"
echo "--- received"; ls -la "$HOME/Downloads" "$OTHER/Downloads"
