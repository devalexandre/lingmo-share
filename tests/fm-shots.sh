#!/bin/bash
# The file manager's "Send to nearby device" menu, under Xvfb in a private
# network namespace (see ui-shots.sh). FILE_X/FILE_Y: where the first file is.
#   unshare -rn xvfb-run -a -s "-screen 0 1440x900x24" dbus-run-session -- \
#       bash tests/fm-shots.sh <share-build> <filemanager-build> <scratch> <out> light|dark
set -u
# Everything started here goes when the script ends (the fakes outlive their bus otherwise)
trap 'kill $(jobs -p) 2>/dev/null' EXIT
B=$(realpath "$1"); FM=$(realpath "$2"); S=$(realpath -m "$3"); OUT=$(realpath -m "$4"); THEME=$5; P=$THEME
ip link set lo up; ip link set lo multicast on; ip route add 224.0.0.0/4 dev lo 2>/dev/null
rm -rf "$S"; mkdir -p "$S/home/.config/lingmoos" "$S/home/Imagens/Viagem" "$S/home/Downloads" "$OUT"
export HOME=$S/home XDG_CONFIG_HOME=$S/home/.config XDG_DATA_HOME=$S/home/.local/share
export LANG=pt_BR.UTF-8 LC_ALL=pt_BR.UTF-8 QT_FORCE_STDERR_LOGGING=1 QT_QPA_PLATFORMTHEME=lingmo
export QML_IMPORT_PATH=$B/qml LINGMO_SHARE_TRANSLATIONS=$B LINGMO_FM_TRANSLATIONS=$FM
export LINGMO_SHARE_FAKE_KDECONNECT=1 LINGMO_SHARE_LOOPBACK=1 LINGMO_SHARE_PORT=53541 PATH=$B:$PATH
unset QT_STYLE_OVERRIDE
for n in Praia Pôr-do-sol Trilha; do
    magick "/usr/share/backgrounds/lingmoos/archlingmo-$THEME.jpg" -resize 800x500 "$HOME/Imagens/Viagem/$n.jpg"
done
magick "/usr/share/backgrounds/lingmoos/archlingmo-$THEME.jpg" -resize 1440x900^ -gravity center -extent 1440x900 "$S/wall.png"
feh -x -g 1440x900+0+0 --title wallpaper "$S/wall.png" >/dev/null 2>&1 &
picom --backend xrender --no-fading-openclose >/dev/null 2>&1 &
"$B/fake-theme" "$THEME" &
"$B/fake-kdeconnectd" > "$S/fake.log" 2>&1 &
printf '[Device]\nname=Lingmo de Ana\nreceiveFolder=%s\n' "$HOME/Downloads" > "$HOME/.config/lingmoos/share.conf"
"$B/lingmo-share" --daemon > "$S/daemon.log" 2>&1 &
sleep 2
"$FM/lingmo-filemanager" "$HOME/Imagens/Viagem" > "$S/fm.log" 2>&1 &
sleep 4
import -display "$DISPLAY" -window root "$S/fm-open.png"
X=${FILE_X:-0}; Y=${FILE_Y:-0}
if [ "$X" -gt 0 ]; then
    xdotool mousemove "$X" "$Y" click 1; sleep 0.5
    xdotool click 3; sleep 1.2
    import -display "$DISPLAY" -window root "$OUT/15-filemanager-menu-$P.png"; echo "shot 15-filemanager-menu-$P"
    if [ -n "${ITEM_Y:-}" ]; then
        xdotool mousemove "$((X + 60))" "$ITEM_Y" click 1; sleep 2.5
        import -display "$DISPLAY" -window root "$OUT/16-filemanager-picker-$P.png"; echo "shot 16-filemanager-picker-$P"
    fi
fi
grep -v "^$\|Gtk-\|portal\|fuse\|MESA" "$S/fm.log" | head -10
