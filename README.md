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
| FujiNet Config  | `FjCf`  | edit and browse host slots, queue apps for HotSync, HotSync now |
| FujiNet Test    | `FjTs`  | reads FujiNet's network settings to check the link            |

Both need Palm OS 3.3 or later (the Serial Manager with `SrmOpen`).

Queuing copies the chosen file into `/palm/install` on the host slot named
`SD`; the next HotSync installs it.

## Building

The toolchain is [prc-tools-remix](https://github.com/jichu4n/prc-tools-remix),
the Palm OS SDKs and PilRC, in a Docker image (x86-64; Apple Silicon runs it
under emulation):

```sh
make image    # once: builds the palmdev image from docker/Dockerfile
make          # builds apps/*/*.prc
make test     # desktop test of the FujiBus codec against the firmware's own
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
| `apps/*/`                | one app per folder; `apps/app.mk` has the build rules  |
| `tests/`                 | FujiBus interop test against `FujiBusPacket.cpp`       |
| `tools/hotsync-harness/` | runs the firmware's HotSync code on a Mac, against a real cradle or an emulator |
| `docker/`                | the palmdev toolchain image                            |

## Writing an app

Link `common/fujibus.c` and `common/fnlink.c`, and call the `Fn*` functions in
`fnlink.h`. FujiNet listens for a HotSync on the same port whenever no FujiBus
packet has arrived for 10 seconds, taking it for 1.5 s at a time, so a request
can go unanswered; `fnlink.c` resends until FujiNet replies. Close the port
(`FnClose`) before starting a HotSync.

## License

GPL-3.0, as fujinet-firmware, from which the FujiBus framing is ported.
