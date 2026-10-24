#!/bin/sh
# Runs lingmo-share's daemon against tests/interop_peer.py (a LocalSend v2
# peer written from the official Rust core) inside a private network
# namespace: nothing reaches the real network.
#   unshare -rn sh tests/interop.sh <build-dir> <scratch-dir>
set -e
BUILD=$(realpath "$1"); T=$(realpath -m "$2"); SRC=$(dirname "$(realpath "$0")")
ip link set lo up; ip link set lo multicast on 2>/dev/null || true
rm -rf "$T"; mkdir -p "$T/home/.config/lingmoos" "$T/work/Downloads"
export HOME="$T/home" XDG_CONFIG_HOME="$T/home/.config" XDG_DATA_HOME="$T/home/.local/share"
export QT_QPA_PLATFORM=offscreen LINGMO_SHARE_LOOPBACK=1 LINGMO_SHARE_PORT=53511
PYFP=$(python3 -I "$SRC/interop_peer.py" fingerprint "$T/work")
cat > "$T/home/.config/lingmoos/share.conf" <<CONF
[Device]
name=Lingmo Test
visibility=0
receiveFolder=$T/work/Downloads
[Android]
enabled=false
[Trusted]
$PYFP=Reference Peer
CONF
dbus-run-session -- sh -c '
  "$0/lingmo-share" --daemon > "$1/daemon.log" 2>&1 &
  for i in $(seq 50); do [ -f "$HOME/.local/share/lingmo-share/localsend-cert.pem" ] && break; sleep 0.1; done
  sleep 0.5
  CERT="$HOME/.local/share/lingmo-share/localsend-cert.pem"
  FP=$(openssl x509 -in "$CERT" -outform DER | sha256sum | cut -d" " -f1 | tr a-f A-F)
  echo "lingmo-share fingerprint $FP"
  python3 -I "$2/interop_peer.py" client 53511 "$FP" "$1/work"
  python3 -I "$2/interop_peer.py" client-cancel 53511 "$FP" "$1/work"
  head -c 1048583 /dev/urandom > "$1/work/outgoing.bin"
  python3 -I "$2/interop_peer.py" server 53600 53511 "$FP" "$CERT" "$1/work" &
  PY=$!
  sleep 1.5
  "$0/lingmo-share" devices
  PEER=$("$0/lingmo-share" devices | grep "Reference Peer" | cut -f1)
  "$0/lingmo-share" send --to "$PEER" "$1/work/outgoing.bin"
  wait $PY
  echo "ALL INTEROP CHECKS PASSED"
' "$BUILD" "$T" "$SRC"
