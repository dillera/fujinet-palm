# fujinet-palm

Palm OS apps that talk to a [FujiNet](https://fujinet.online), and the tools
to build them. This branch holds the **FN** apps, for the classic
fujinet-firmware; the **FI** apps, rebuilt for FujiNet NIO, are on the `nio`
branch.

A Palm sits in its serial HotSync cradle, wired through a null modem to a
FujiNet running the HotSync service from the `palm-demo-fozz` branch of
[fujinet-firmware](https://github.com/dillera/fujinet-firmware/tree/palm-demo-fozz)
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
| FN FujiPet      | `ADFP`  | a virtual pet that grows up like the 1996 original, and meets other pets on a FujiPet friend server through FujiNet's N1: |

All run on Palm OS 3.1 or later. They open the cradle port with the Serial
Manager's `SrmOpen` on Palm OS 3.3 and later, and with the older serial
library's `SerOpen` before that. `make traps` lists any Palm OS call an app
makes that is newer than 3.1, so a new one is noticed before it crashes an
older Palm.

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
              # the Hold'em state reader against replies saved from its server,
              # FujiPet's life cycle and protocol, and its friend server
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
| `tests/`                 | FujiBus interop test against `FujiBusPacket.cpp`; lobby JSON and Hold'em reader tests; FujiPet's pet, protocol and server tests |
| `tools/make_icon.py`     | draws the launcher icon, the FujiNet logo              |
| `tools/palm_traps.py`    | lists the Palm OS calls an app makes that are newer than 3.1 (`make traps`) |
| `tools/make_cards.py`    | converts the Hold'em card art into `apps/Holdem/cardart.h` |
| `tools/make_pet_art.py`  | FN FujiPet's sprites and icon: writes `apps/FujiPet/petart.h` |
| `tools/fujipet-server/`  | the FujiPet friend server (Python 3, no dependencies)  |
| `tools/hotsync-harness/` | runs the firmware's HotSync code on a Mac, against a real cradle or an emulator |
| `docker/`                | the palmdev toolchain image                            |
| `docs/guide/`            | the FujiNet Palm Guide (XeLaTeX, `make` there): every FN and FI app, with screenshots; `fujinet-palm-guide.pdf` |
| `assets/`                | Palm OS ROMs, emulator images with Setup done (OS 3.1 to 4.0), and the CloudpilotEmu `--serial` patch; see `assets/README.md` |
| `tools/rig.sh`           | runs an emulated Palm joined to FujiNet-PC or a POSIX FujiNet NIO, for testing |

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

## FN FujiPet

A virtual pet after the original Tamagotchi. Name it and pick a type (Cat, Dog,
Bunny or Dino) on the first screen, and its egg hatches five minutes later.
The baby becomes a child within the hour, a teen a day later and an adult two
days after that; how well it was looked after decides which of two teens and
three adults it grows into (each type has its own: a well-raised cat becomes a
Starcat, a neglected one a Grumpuss) and how long it lives. The best-kept
live past ten days as seniors. When it dies it becomes an angel, and a tap
starts a new egg.

The eight icons are the original's: Feed (a meal fills a hunger heart, a snack
a happy one), Light, Play (guess which way it will look, three of five to
win), Medicine, Clean, Status (hearts, discipline, age, weight), Discipline
and Friends. A heart running empty makes it call (the "!" in the corner); a
call left for 15 minutes is a care mistake, as is leaving the light on once it
sleeps. Children and teens also call for no reason now and then: scolding
them then teaches discipline. Mess and too many sweets make it sick, and
twelve hours sick or starving kill it.

The pet lives on while the app is closed: the next launch catches up on the
time that passed. **Options > Settings** can run its time Fast (a pet minute a
second) or Turbo (a pet day a minute) to watch it grow up.

With **Meet friends online** on, the app keeps a connection to a FujiPet friend
server (`tools/fujipet-server/`, set as `host:port` in Settings) through N1:.
The Friends screen lists the pets registered there, online ones first; **Register**
adds yours. Pick a friend that is online and a message, and **Send**: it pops up
on their Palm at once, and a kind one cheers their pet up. The ground line on
the main screen says how many friends are online.

One `.prc` serves every Palm from OS 3.1: on a colour Palm (OS 3.5 and later)
it switches to 256 colours and draws each type in its own; on a
black-and-white one the same sprites are outlines with a grey dither. The art
is ASCII in `tools/make_pet_art.py`, which writes `apps/FujiPet/petart.h`:

```sh
python3 tools/make_pet_art.py --preview sheet.png   # also draws every sprite
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
