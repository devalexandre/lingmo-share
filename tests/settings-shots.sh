#!/bin/bash
# The "Phone and devices" page of Lingmo Settings, under Xvfb.
#   unshare -rmn xvfb-run -a -s "-screen 0 1440x900x24" dbus-run-session -- \
#       bash tests/settings-shots.sh <share-build> <settings-build> <settings-qm> <scratch> <out> light|dark
set -u
# Everything started here goes when the script ends (the fakes outlive their bus otherwise)
trap 'kill $(jobs -p) 2>/dev/null' EXIT
B=$(realpath "$1"); SET=$(realpath "$2"); QM=$(realpath "$3"); S=$(realpath -m "$4"); OUT=$(realpath -m "$5"); THEME=$6; P=$THEME
ip link set lo up
# The settings app reads its translations from /usr only: show the new strings (private mount)
mount --bind "$QM" /usr/share/lingmo-settings/translations/pt_BR.qm 2>/dev/null || echo "(no bind mount: English strings)"
rm -rf "$S"; mkdir -p "$S/home/.config/lingmoos" "$OUT"
export HOME=$S/home XDG_CONFIG_HOME=$S/home/.config XDG_DATA_HOME=$S/home/.local/share
export LANG=pt_BR.UTF-8 LC_ALL=pt_BR.UTF-8 QT_FORCE_STDERR_LOGGING=1 QT_QPA_PLATFORMTHEME=lingmo
export QML_IMPORT_PATH=$B/qml LINGMO_SHARE_TRANSLATIONS=$B LINGMO_SHARE_FAKE_KDECONNECT=1 PATH=$B:$PATH
unset QT_STYLE_OVERRIDE
printf '[Device]\nname=Lingmo de Ana\nreceiveFolder=%s/Downloads\n[LocalSend]\npinEnabled=true\npin=482913\n[Edge]\nside=left\npeerName=Lingmo do Escritório\n[Trusted]\nA1B2C3D4E5F60718293A4B5C6D7E8F90A1B2C3D4E5F60718293A4B5C6D7E8F90=iPhone de Ana\n' "$HOME" > "$HOME/.config/lingmoos/share.conf"
magick "/usr/share/backgrounds/lingmoos/archlingmo-$THEME.jpg" -resize 1440x900^ -gravity center -extent 1440x900 "$S/wall.png"
feh -x -g 1440x900+0+0 --title wallpaper "$S/wall.png" >/dev/null 2>&1 &
"$B/fake-theme" "$THEME" &
sleep 1
"$SET/lingmo-settings" -m phone > "$S/settings.log" 2>&1 &
sleep 5
id=$(xdotool search --onlyvisible --class lingmo-settings | tail -1)
eval "$(xdotool getwindowgeometry --shell "$id")"
import -display "$DISPLAY" -window root -crop "${WIDTH}x${HEIGHT}+${X}+${Y}" +repage "$OUT/17-settings-page-$P.png"; echo "shot 17-settings-page-$P"
# Scroll down to the Android and "next to this one" sections
xdotool mousemove $((X + WIDTH * 2 / 3)) $((Y + HEIGHT / 2))
for i in $(seq 12); do xdotool click 5; sleep 0.05; done
sleep 0.8
import -display "$DISPLAY" -window root -crop "${WIDTH}x${HEIGHT}+${X}+${Y}" +repage "$OUT/18-settings-page-more-$P.png"; echo "shot 18-settings-page-more-$P"
grep -i "qml\|error" "$S/settings.log" | grep -v "Gtk\|portal" | head -10
