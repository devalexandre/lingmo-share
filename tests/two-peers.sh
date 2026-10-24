#!/bin/sh
# Two lingmo-share daemons in a private network namespace: they find each
# other by LocalSend multicast (224.0.0.167:53317 on lo) and send files both
# ways. Nothing reaches the real network.
#   unshare -rn sh tests/two-peers.sh <build-dir> <scratch-dir>
set -e
BUILD=$(realpath "$1"); T=$(realpath -m "$2")
ip link set lo up; ip link set lo multicast on
ip route add 224.0.0.0/4 dev lo 2>/dev/null || true
rm -rf "$T"; mkdir -p "$T"
# D-Bus socket paths must be short
B=$(mktemp -d /tmp/claude-1000/lsb.XXXX 2>/dev/null || mktemp -d)
export QT_QPA_PLATFORM=offscreen LINGMO_SHARE_LOOPBACK=1

peer() { # name port
    H="$T/$1"; mkdir -p "$H/.config/lingmoos" "$H/Recebidos"
    cat > "$H/.config/lingmoos/share.conf" <<CONF
[Device]
name=$1
visibility=0
receiveFolder=$H/Recebidos
[Android]
enabled=false
CONF
}
peer Lingmo-Sala 53521
peer Lingmo-Quarto 53522
run() { # name port cmd...
    N=$1; P=$2; shift 2
    env HOME="$T/$N" XDG_CONFIG_HOME="$T/$N/.config" XDG_DATA_HOME="$T/$N/.local/share" LINGMO_SHARE_PORT=$P \
        DBUS_SESSION_BUS_ADDRESS="unix:path=$B/$N" "$@"
}
for N in Lingmo-Sala Lingmo-Quarto; do
    dbus-daemon --session --address="unix:path=$B/$N" --fork --print-pid > "$T/$N.dbuspid"
done
# Each needs the other's certificate fingerprint to trust it: create the identities first
run Lingmo-Sala 53521 "$BUILD/lingmo-share" --daemon > "$T/sala.log" 2>&1 &
run Lingmo-Quarto 53522 "$BUILD/lingmo-share" --daemon > "$T/quarto.log" 2>&1 &
sleep 2
fp() { openssl x509 -in "$T/$1/.local/share/lingmo-share/localsend-cert.pem" -outform DER | sha256sum | cut -d" " -f1 | tr a-f A-F; }
FP_SALA=$(fp Lingmo-Sala); FP_QUARTO=$(fp Lingmo-Quarto)
echo "Sala   $FP_SALA"; echo "Quarto $FP_QUARTO"

echo "== discovery (multicast)"
run Lingmo-Quarto 53522 "$BUILD/lingmo-share" devices
run Lingmo-Sala 53521 "$BUILD/lingmo-share" devices
run Lingmo-Quarto 53522 "$BUILD/lingmo-share" devices | grep -q "ls:$FP_SALA" && echo "ok   Quarto sees Sala"
run Lingmo-Sala 53521 "$BUILD/lingmo-share" devices | grep -q "ls:$FP_QUARTO" && echo "ok   Sala sees Quarto"

echo "== untrusted: Sala asks the user; nobody answers, the sender gives up"
head -c 200000 /dev/urandom > "$T/foto.jpg"
( run Lingmo-Quarto 53522 timeout 6 "$BUILD/lingmo-share" send --to "ls:$FP_SALA" "$T/foto.jpg" || true ) 2>&1 | tail -1
[ ! -e "$T/Lingmo-Sala/Recebidos/foto.jpg" ] && echo "ok   nothing saved without an answer"

echo "== trusted: Sala trusts Quarto (as 'Always accept' does)"
printf '[Trusted]\n%s=Lingmo-Quarto\n' "$FP_QUARTO" >> "$T/Lingmo-Sala/.config/lingmoos/share.conf"
sleep 1
run Lingmo-Quarto 53522 "$BUILD/lingmo-share" send --to "ls:$FP_SALA" "$T/foto.jpg" | tail -1
cmp "$T/foto.jpg" "$T/Lingmo-Sala/Recebidos/foto.jpg" && echo "ok   foto.jpg arrived at Sala intact"

echo "== a folder, the other way"
mkdir -p "$T/Album/Praia"; head -c 50000 /dev/urandom > "$T/Album/Praia/1.png"; echo oi > "$T/Album/nota.txt"
printf '[Trusted]\n%s=Lingmo-Sala\n' "$FP_SALA" >> "$T/Lingmo-Quarto/.config/lingmoos/share.conf"
sleep 1
run Lingmo-Sala 53521 "$BUILD/lingmo-share" send --to "ls:$FP_QUARTO" "$T/Album" | tail -1
cmp "$T/Album/Praia/1.png" "$T/Lingmo-Quarto/Recebidos/Album/Praia/1.png" && cmp "$T/Album/nota.txt" "$T/Lingmo-Quarto/Recebidos/Album/nota.txt" \
    && echo "ok   folder arrived at Quarto with its structure"

echo "== visibility off: Sala disappears"
sed -i 's/^visibility=0/visibility=2/' "$T/Lingmo-Sala/.config/lingmoos/share.conf"
sleep 1
if run Lingmo-Quarto 53522 timeout 10 "$BUILD/lingmo-share" send --no-wait --to "ls:$FP_SALA" "$T/foto.jpg" >/dev/null 2>&1; then :; fi
sleep 2
ls "$T/Lingmo-Sala/Recebidos" | grep -c foto | xargs echo "files at Sala (still 1):"
(exec 3<>/dev/tcp/127.0.0.1/53521) 2>/dev/null && echo "FAIL port open" || echo "ok   port 53521 closed while off"

for N in Lingmo-Sala Lingmo-Quarto; do kill "$(cat "$T/$N.dbuspid")" 2>/dev/null || true; done
kill %1 %2 2>/dev/null || true
echo "TWO PEERS DONE"
