# fujinet-palm

Palm OS apps that talk to a [FujiNet](https://fujinet.online), and the tools
to build them.

This branch holds the **FI** apps, rebuilt for
[FujiNet NIO](https://github.com/markjfisher/fujinet-nio) on `fujinet-nio-lib`
(its `palmos-serial` target). The classic **FN** apps, for fujinet-firmware, are on the
`classic` branch.

A Palm sits in its serial HotSync cradle, wired through a null modem to a
FujiNet NIO with its HotSync service on (`hotsync.enabled` and `hotsync.cradle`
in `fujinet.yaml`; see NIO's `docs/hotsync.md`). NIO then serves the Palm two
ways on the same port:

- **HotSync**: pressing the cradle button syncs with NIO, which installs the
  files queued in `hotsync/install/` on its persistent filesystem and backs the
  Palm up.
- **FujiBus**: apps on the Palm talk to NIO's devices at 115200 baud through
  `fujinet-nio-lib`.

## Apps

| App              | Creator | What it does                                                  |
|------------------|---------|---------------------------------------------------------------|
| FI Config        | `ANHS`  | keeps a list of TNFS servers, browses them, and installs `.prc`/`.pdb`/`.pqa` files from them straight into the Palm, or queues them in NIO's HotSync install folder and starts a HotSync |
| FI Test          | `ANTS`  | checks the link to NIO: firmware, board and network, and fetches a web page |
| FI Bounce        | `ANBW`  | client for a [bounce-world](https://github.com/markjfisher/bounce-world) server (default `bouncy.diller.org:9003`); its Info box counts cradle exchanges, so it doubles as a link stress test |
| FI Lobby         | `ANLB`  | lists the game servers in the [FujiNet Game Lobby](https://github.com/FujiNetWIFI/fujinet-lobby) (`/viewFull`), with players, platforms and client URLs, and a QA lobby switch in the menu |
| FI Texas Hold'em | `ANTH`  | multiplayer Texas Hold'em against the [FujiNet Hold'em server](https://github.com/dillera/servers/tree/main/fujinet-game-system/texasholdem), with the other platforms' clients |
| FI FujiPet       | `ANFP`  | a virtual pet that grows up like the 1996 original, and meets other pets on a FujiPet friend server |

All run on Palm OS 3.1 or later. They open the cradle port with the Serial
Manager's `SrmOpen` on Palm OS 3.3 and later, and with the older serial
library's `SerOpen` before that. `make traps` lists any Palm OS call an app
makes that is newer than 3.1, so a new one is noticed before it crashes an
older Palm.

Each FI app has its own creator code (`AN..` where its FN twin has `AD..`), so
both families can sit on the same Palm. Every request goes through
`fujinet-nio-lib`, which reclaims the line from NIO's HotSync listener after an
idle spell. NIO's NetworkDevice replays a READ or WRITE asked for again at the
same offset, so an app retries a reply lost on the cradle without losing or
repeating stream bytes.

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

`make` also builds [fujinet-nio-lib](https://github.com/markjfisher/fujinet-nio-lib)
for the apps, with its `palmos-serial` target (in its `master` since
2026-11-04):

```sh
git clone https://github.com/markjfisher/fujinet-nio-lib.git
make NIO_LIB=$PWD/fujinet-nio-lib
```

Without `NIO_LIB`, `make` looks for the library in
`../fujinet-nio-workspace/repos/fujinet-nio-lib`. `make test` and the harness below expect a `fujinet-firmware`
checkout next to this repository.

To install an app, install it from a TNFS server with FI Config, or put its
`.prc` in NIO's `hotsync/install/` and HotSync: queue it from a TNFS server
with FI Config, or from NIO's console with
`hotsync.fetch http://<host>/<app>.prc` (see NIO's `docs/hotsync.md`).

## Layout

| Path                     | Contents                                               |
|--------------------------|--------------------------------------------------------|
| `common/fujibus.*`       | FujiBus (SLIP) request/reply framing, portable C89     |
| `common/fnlink.*`, `common/fnnet.*` | the classic firmware's FujiBus commands and N: devices (FN apps; unused here) |
| `common/lobbyjson.*`     | streaming reader for the lobby's JSON server list     |
| `common/holdem.*`        | the Hold'em server's binary replies, and its URLs      |
| `apps/*/`                | one app per folder; `apps/app.mk` has the build rules  |
| `apps/FujiConfig/filedev.*` | NIO's FileDevice (list, read, write, make directory) over `fn_raw_call` |
| `tests/`                 | FujiBus interop test against `FujiBusPacket.cpp`; lobby JSON and Hold'em reader tests; FujiPet's pet, protocol and server tests |
| `tools/make_icon.py`     | draws the launcher icon, the FujiNet logo              |
| `tools/palm_traps.py`    | lists the Palm OS calls an app makes that are newer than 3.1 (`make traps`) |
| `tools/make_cards.py`    | converts the Hold'em card art into `apps/Holdem/cardart.h` |
| `tools/make_pet_art.py`  | FI FujiPet's sprites and icon: writes `apps/FujiPet/petart.h` |
| `tools/fujipet-server/`  | the FujiPet friend server (Python 3, no dependencies)  |
| `docker/`                | the palmdev toolchain image                            |
| `docs/guide/`            | the FujiNet Palm Guide (XeLaTeX, `make` there): every FN and FI app, with screenshots; `fujinet-palm-guide.pdf` |
| `assets/`                | Palm OS ROMs, emulator images with Setup done (OS 3.1 to 4.0), and the CloudpilotEmu `--serial` patch; see `assets/README.md` |
| `tools/rig.sh`           | runs an emulated Palm joined to FujiNet-PC or a POSIX FujiNet NIO, for testing |

## FI Config

FN HotSync rebuilt for NIO. NIO has no host slots, so the app keeps its own
list of TNFS servers (fujinet.diller.org and apps.irata.online to start).
Browse a server, pick a Palm app or database, and choose:

- **Install** streams the file from the server into the Palm's memory with
  Palm OS's `ExgDBRead`, a chunk at a time through NIO's FileDevice, with no
  HotSync. A database of the same name already on the Palm is replaced once
  you agree; one in ROM is left there and the new copy takes its place. If the
  stream stops short, `ExgDBRead` throws away what it had built. An installed
  app gets `sysAppLaunchCmdSyncNotify`, as after a HotSync.
- **Queue** copies it through NIO's FileDevice into `hotsync/install/` on
  NIO's persistent filesystem; the next HotSync installs it, from the app's
  HotSync button or the cradle's. Queue lists what is waiting.

Info shows NIO's firmware and network.

Each FileDevice read costs NIO a TNFS open, read and close, about a third of a
second to fujinet.diller.org, so an install runs at about 600 bytes a second
on the cradle (256-byte reads) whatever the line speed: a 31 KB app takes
under a minute.

The same source builds for a Handspring Visor with a
[Springboard NIO](https://github.com/dillera/springboard-nio) module, on
`fujinet-nio-lib`'s `palmos-springboard` target, with `FI_NO_HOTSYNC`
defined: there is no HotSync behind the module, so it only installs, with
512-byte reads: on a Visor Prism the 31 KB app installs in about 23 seconds.
springboard-nio's `palm/Makefile` builds it into the module's ROM.

FileDevice has no rename or delete, so a copy that fails part way stays in the
queue. FI Config writes the file's first entry offset last, so HotSync refuses
a cut-short file rather than installing a truncated app; queue the file again
to replace it. The app assumes NIO's `hotsync.root` is `/hotsync`, the default.

## FI Texas Hold'em

A Palm client for the Texas Hold'em game of
[fujinet-texasHoldEm](https://github.com/dillera/fujinet-texasHoldEm), at the
same tables as its Apple II, Atari, CoCo, MS-DOS and Intellivision clients.
The server, `https://th.carr-designs.com/` by default, is listed in the FujiNet
Game Lobby as "Texas Hold'em" (app key 8); a lobby address such as
`https://th.carr-designs.com/?table=den` can be typed as the server, and its
table is taken from it. The server runs every rule: as the Apple II client
does, the app polls `/state?table=..&player=..&bin=1` (with `be=1` for
big-endian numbers) about once a second, each request an HTTP GET opened with
`fujinet-nio-lib` and read while the event loop keeps running, and plays the move codes
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

## FI FujiPet

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
server (`tools/fujipet-server/`, set as `host:port` in Settings) through NIO;
if the server goes away, the pet reconnects by itself. With the POSIX NIO on a
Mac, start the server with `--host ""` so it also listens on IPv6 (NIO tries
only `::1` for `localhost`), or use `127.0.0.1:7878` in Settings.
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

Name the app `FI <name>` and give it its own creator code, `AN` plus two
capitals, set once as `CREATOR` in its Makefile; C code reads it as
`APP_CREATOR` (from `fn_types.h`). Set `NIO = 1` in the Makefile to link
`fujinet-nio-lib` (see `apps/app.mk`), call `fn_init()` at start and
`fn_shutdown()` at the end, and before starting a HotSync.

`fn_read` does not wait: it answers `FN_ERR_NOT_READY` until data arrives, so
poll it from the event loop. When a reply is lost (`FN_ERR_TIMEOUT`,
`FN_ERR_TRANSPORT` or `FN_ERR_IO`), ask again at the same offset; FI Bounce
and FI Lobby show the pattern.

## License

GPL-3.0, as fujinet-firmware, from which the FujiBus framing is ported.

FI Texas Hold'em's card art (`apps/Holdem/cardart.h`) is converted from
[fujinet-texasHoldEm](https://github.com/dillera/fujinet-texasHoldEm), also
GPL-3.0: the Apple II client's hi-res character set and the Atari Lynx
client's card sprites, by the authors of that repository and of Eric Carr's
[fujinet-5cardstud](https://github.com/dillera/fujinet-5cardstud), from which
it grew. The app follows those clients' protocol.
