#!/bin/sh
# Copy the FujiPet friend server to a host and (re)start it there.
#   deploy.sh [host]          default host: actual
# It lands in ~/_services/fujipet on the host and runs as the fujipet systemd
# unit (fujipetctl install). The pets in ~/_services/fujipet/data are kept.
# FRP and DNS are one-off steps: see frpc-fujipet.toml.
set -e
HOST=${1:-actual}
HERE=$(cd "$(dirname "$0")" && pwd)

ssh "$HOST" 'mkdir -p _services/fujipet/data'
scp -q "$HERE/../fujipet_server.py" "$HERE/fujipet.service" "$HERE/fujipetctl" \
    "$HERE/frpc-fujipet.toml" "$HOST:_services/fujipet/"
ssh "$HOST" 'chmod +x _services/fujipet/fujipetctl && _services/fujipet/fujipetctl install'
