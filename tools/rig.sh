#!/bin/sh
# Palm test rig: CloudpilotEmu <-> socat pty pair <-> a FujiNet on this Mac.
#   rig.sh start <run-dir> <rom-or-image> <classic|nio|none> [cloudpilot options]
#   rig.sh stop <run-dir>
#   rig.sh cmd <run-dir> <cloudpilot command...>     e.g. rig.sh cmd r31 screenshot a.png
# Boot a copy of an image from assets/images (setup done), not the original.
# classic runs FujiNet-PC (fujinet-firmware: ./build.sh -p RS232); nio runs the
# POSIX fujibus-rs232-debug build of fujinet-nio. CloudpilotEmu needs the
# --serial bridge in assets/cloudpilot. Each run dir keeps its own logs and pids,
# so stop only touches what it started. WEB (default 8001) is FujiNet-PC's web
# port; give concurrent classic rigs their own.
# Paths default to the ~/code/FujiPalm layout; EMU, FNPC, FNPC_SEED and NIO
# override them.
set -e
FUJIPALM=${FUJIPALM:-$HOME/code/FujiPalm}
EMU=${EMU:-$FUJIPALM/shared/emulators/cloudpilot-emu/src/cloudpilot/cloudpilot-emu}
FNPC=${FNPC:-$FUJIPALM/classic/fujinet-firmware/build/dist/fujinet}
FNPC_SEED=${FNPC_SEED:-$FUJIPALM/shared/emulators/fnpc}
NIO=${NIO:-$FUJIPALM/nio/fujinet-nio-workspace/repos/fujinet-nio/build/fujibus-rs232-debug/fujinet-nio}

stop() {
    [ -d "$1" ] || return 0
    for p in "$1"/*.pid; do
        [ -f "$p" ] && kill "$(cat "$p")" 2>/dev/null || true
        rm -f "$p"
    done
}

case "$1" in
stop) stop "$2"; exit 0 ;;
cmd)  RUN=$2; shift 2; echo "$*" > "$RUN/cmd"; exit 0 ;;
start) ;;
*) sed -n '2,15p' "$0"; exit 2 ;;
esac

mkdir -p "$2"
RUN=$(cd "$2" && pwd); ROM=$(cd "$(dirname "$3")" && pwd)/$(basename "$3"); KIND=$4
shift 4
stop "$RUN"

SERIAL=
if [ "$KIND" != none ]; then
    socat -d -d -x pty,raw,echo=0,link="$RUN/palm-tty" pty,raw,echo=0,link="$RUN/fn-tty" \
        > "$RUN/socat.log" 2>&1 &
    echo $! > "$RUN/socat.pid"
    sleep 0.5
    SERIAL="--serial $RUN/palm-tty"
fi

case "$KIND" in
classic)
    mkdir -p "$RUN/fnpc"
    [ -d "$RUN/fnpc/SD" ] || cp -R "$FNPC_SEED/SD" "$RUN/fnpc/SD"
    mkdir -p "$RUN/fnpc/SD/palm/install"
    [ -d "$RUN/fnpc/data" ] || cp -R "$(dirname "$FNPC")/data" "$RUN/fnpc/data"
    sed "/^\[Serial\]/,/^\[/s#^port=.*#port=$RUN/fn-tty#" "$FNPC_SEED/fnconfig.ini" > "$RUN/fnpc/fnconfig.ini"
    (cd "$RUN/fnpc" && exec "$FNPC" -c fnconfig.ini -s SD -u "http://0.0.0.0:${WEB:-8001}" \
        > "$RUN/fujinet.log" 2>&1) &
    echo $! > "$RUN/fujinet.pid" ;;
nio)
    mkdir -p "$RUN/nio"
    (cd "$RUN/nio" && FN_SERIAL_PORT="$RUN/fn-tty" FN_SERIAL_BAUD=115200 exec "$NIO" \
        > "$RUN/nio.log" 2>&1) &
    echo $! > "$RUN/nio.pid" ;;
esac
sleep 1

[ -p "$RUN/cmd" ] || mkfifo "$RUN/cmd"
# Hold the FIFO open so each echo does not end the CLI's input.
(exec 3<>"$RUN/cmd"; cd "$RUN" && exec "$EMU" "$ROM" $SERIAL "$@" < "$RUN/cmd" > "$RUN/emu.log" 2>&1) &
echo $! > "$RUN/emu.pid"
echo "rig up in $RUN ($KIND)"
