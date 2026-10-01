# fujinet-palm

Palm OS apps that talk to a [FujiNet](https://fujinet.online), and the tools
to build them.

A Palm sits in its serial HotSync cradle, wired through a null modem to a
FujiNet running the HotSync service from the `palm-pilot` branch of
[fujinet-firmware](https://github.com/dillera/fujinet-firmware/tree/palm-pilot)
(for example the `fujinet-rs232-s3-palm` build for the FN-RS232). FujiNet then
serves the Palm two ways on the same port:

- **HotSync**: pressing the cradle button syncs with FujiNet, which installs
  the files queued in `/palm/install` on its SD card and backs the Palm up.
- **FujiBus**: apps on the Palm send FujiNet commands at 115200 baud, as the
  CONFIG app does on RS232 machines.

## Apps

| App             | Creator | What it does                                                  |
|-----------------|---------|---------------------------------------------------------------|
| FN HotSync      | `ADHS`  | edit and browse host slots, queue apps for HotSync, HotSync now |
| FN Test         | `ADTS`  | reads FujiNet's network settings to check the link            |
| FN Bounce World | `ADBW`  | client for a [bounce-world](https://github.com/markjfisher/bounce-world) server, through FujiNet's N1: |
| FN Lobby        | `ADLB`  | lists the game servers in the [FujiNet Game Lobby](https://github.com/FujiNetWIFI/fujinet-lobby) (`/viewFull`), with players, platforms and client URLs |
| FN Texas Hold'em | `ADTH` | multiplayer Texas Hold'em against the [FujiNet Hold'em server](https://github.com/dillera/servers/tree/main/fujinet-game-system/texasholdem), with the other platforms' clients, through FujiNet's N1: |

All need Palm OS 3.3 or later (the Serial Manager with `SrmOpen`).

Queuing copies the chosen file into `/palm/install` on the host slot named
`SD`; the next HotSync installs it.

## Building

The toolchain is [prc-tools-remix](https://github.com/jichu4n/prc-tools-remix),
the Palm OS SDKs and PilRC, in a Docker image (x86-64; Apple Silicon runs it
under emulation):

```sh
make image    # once: builds the palmdev image from docker/Dockerfile
make          # builds apps/*/*.prc
make test     # desktop tests: the FujiBus codec against the firmware's own,
              # the lobby JSON reader against a saved /viewFull reply, and
              # the Hold'em state reader against replies saved from its server
```

`make test` and the harness below expect a `fujinet-firmware` checkout next to
this repository.

To install an app, copy its `.prc` to `/palm/install/` on FujiNet's SD card
(the web UI's WebDAV share works: `curl -T app.prc http://<fujinet>/dav/palm/install/`)
and HotSync.

## Layout

| Path                     | Contents                                               |
|--------------------------|--------------------------------------------------------|
| `common/fujibus.*`       | FujiBus (SLIP) request/reply framing, portable C89     |
| `common/fnlink.*`        | FujiNet commands over the cradle port, with retries   |
| `common/fnnet.*`         | N: network devices (open, status, read, write, close) |
| `common/lobbyjson.*`     | streaming reader for the lobby's JSON server list     |
| `common/holdem.*`        | the Hold'em server's binary replies, and its URLs      |
| `apps/*/`                | one app per folder; `apps/app.mk` has the build rules  |
| `tests/`                 | FujiBus interop test against `FujiBusPacket.cpp`; lobby JSON and Hold'em reader tests |
| `tools/make_icon.py`     | draws the launcher icon, the FujiNet logo              |
| `tools/make_cards.py`    | converts the Hold'em card art into `apps/Holdem/cardart.h` |
| `tools/hotsync-harness/` | runs the firmware's HotSync code on a Mac, against a real cradle or an emulator |
| `docker/`                | the palmdev toolchain image                            |

## FN Texas Hold'em

A Palm client for the Texas Hold'em game of
[fujinet-texasHoldEm](https://github.com/dillera/fujinet-texasHoldEm), at the
same tables as its Apple II, Atari, CoCo, MS-DOS and Intellivision clients.
The server, `https://th.carr-designs.com/` by default, is listed in the FujiNet
Game Lobby as "Texas Hold'em" (app key 8); a lobby address such as
`https://th.carr-designs.com/?table=den` can be typed as the server, and its
table is taken from it. The server runs every rule: as the Apple II client
does, the app polls `/state?table=..&player=..&bin=1` (with `be=1` for
big-endian numbers) about once a second through N1:, and plays the move codes
the server offers with `/move/XX`. Leaving the table, or the app, sends
`/leave`.

Pick a name (up to 8 letters and digits) and a table on the first screen, then
Sit Down. Your hole cards are beside the board on the felt; the others sit
above, with their cards as backs until the showdown. On your turn the moves
appear at the bottom, with the server's clock: tap one, or pick one with the
scroll keys and press any app button (the app buttons do not launch apps
while you are seated). Graffiti `f`, `c`, `b`, `r` and `a` play fold,
check or call, bet, raise and all-in. Tap the status line for the last hand's
result.

The cards are the Apple II client's hi-res art: it is monochrome, so it needs
no colour to survive 1-bit, and at 14 x 26 a card puts five on the board and
two in hand across 160 pixels with the room to spare for the pot. The small
cards for the other players are the Atari Lynx client's sprites, with Apple II
glyphs for their ranks. `tools/make_cards.py` converts both from a checkout of
that repository:

```sh
python3 tools/make_cards.py ../fujinet-texasHoldEm   # writes apps/Holdem/cardart.h
```

## Writing an app

Name the app `FN <name>` and give it its own creator code, `AD` plus two
capitals, set once as `CREATOR` in its Makefile; C code reads it as
`APP_CREATOR` (from `fn_types.h`).

Link `common/fujibus.c` and `common/fnlink.c`, and call the `Fn*` functions in
`fnlink.h`. FujiNet listens for a HotSync on the same port whenever no FujiBus
packet has arrived for 10 seconds, taking it for 1.5 s at a time, so a request
can go unanswered; `fnlink.c` resends until FujiNet replies. Close the port
(`FnClose`) before starting a HotSync.

## License

GPL-3.0, as fujinet-firmware, from which the FujiBus framing is ported.

FN Texas Hold'em's card art (`apps/Holdem/cardart.h`) is converted from
[fujinet-texasHoldEm](https://github.com/dillera/fujinet-texasHoldEm), also
GPL-3.0: the Apple II client's hi-res character set and the Atari Lynx
client's card sprites, by the authors of that repository and of Eric Carr's
[fujinet-5cardstud](https://github.com/dillera/fujinet-5cardstud), from which
it grew. The app follows those clients' protocol.
