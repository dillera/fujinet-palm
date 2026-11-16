# head-to-head-test

How fast does a Palm reach the web through FujiNet, and where does the time go?
One test app, built three ways, runs the same tests against the same target
server and reports the same numbers, so the stacks can be compared line by line:

| App | Creator | Palm | Link | FujiNet |
|---|---|---|---|---|
| **FN H2H** | `ADHH` | Palm IIIc | serial cradle, 115200, classic FujiBus | FN-RS232 running classic **fujinet-firmware** |
| **H2H** | `ANHH` | Palm IIIc | serial cradle, 115200, NIO FujiBus | **fujinet-nio** (on the FN-RS232, or on this Mac) |
| **H2H SB** | `ANHB` | Visor (Prism) | Springboard module, CS1 bus | RP2350 + **fujinet-nio** on this Mac |
| **H2H NL** | `ANHN` | Visor (Prism) | the same, through the installed **NioLib** | RP2350 + **fujinet-nio** on this Mac |

NIO Config's H2H page (nio-config, built with `H2H_DIR=` this directory's `palm/`) runs the same suite through
NioLib's saved transport on a colour Palm OS 5 screen: `niolib-network` runs come from NioLib's Network transport,
the NIO on the Palm itself.

This directory is part of fujinet-palm's `nio` branch. The Makefile finds its neighbours from `FUJIPALM` (the folder
holding the checkouts, three levels up): the fujinet-nio workspace, fujinet-palm's `classic` branch checkout (FN H2H's
FujiBus code) and palm-sync.

The two serial apps separate *firmware* (same Palm, same cable, classic or NIO).
The two NIO apps separate *link* (same NIO, serial or Springboard).
H2H SB and H2H NL separate *library*: SB links fujinet-nio-lib's static
palmos-springboard archive, NL only `libniolib-client.a` and calls mozzwald's
installed NioLib (fujinet-nio-lib's palmos-shared target, in RAM).

```
palm/       the app: suite.c (tests, report, upload), clock.c (us timer), h2h_main.c (UI),
            link_classic.c, link_nio.c, probe_serial.c, probe_sb.c; Makefile (runs in Docker)
server/     h2h_server.py: the known target, and where the results meet (Python 3, no deps)
tests/      host tests: the server, and palm/suite.c built for the Mac (tests/host) run against it
tools/      make_icon.py
```

## Running it

```sh
make                 # palm/build/FN_H2H.prc, H2H.prc, H2H_SB.prc (palmdev Docker image)
make server          # the target on this Mac, port 8080; results go to results/
make install-iiic    # queue FN H2H + H2H on the classic FujiNet, then press HotSync on the cradle
make install-visor   # palm-sync over USB, then press HotSync on the Visor's cradle
make flash-visor     # or: H2H SB in the Springboard module's ROM, flashed, then reset the Visor
make install-visor   # H2H RAM over a USB cradle; or on the Visor: H2H SB, Options > Copy to RAM
make report          # every stack's latest run side by side, as text
open http://localhost:8080/h2h/report    # the same, with charts
make test            # host tests
make dist            # the testers' zip: build/h2h-testers-<commit>.zip
make server-release  # the server, version fixed: build/h2h-server-<commit>.zip
```

`make dist` builds everything, then zips FN H2H, H2H, H2H SB, H2H NL
and NioLib with a README.txt for testers (`dist/README-testers.txt`, with
the commits it was built from filled in). The zip is named by this
checkout's commit; commit first, or it is `-dirty`.

The apps start with the target `http://fujinet.online:8199/h2h`, the shared
server for every tester (`H2H_TARGET` overrides it at build time:
`make H2H_TARGET=http://<this Mac>:8080/h2h` for `make server` here; it can
also be edited on the Palm). The FujiNet, not the Palm, connects to it. The
apps' settings are version 2, so builds from before the move forget a saved
LAN target and start with this one.

On each Palm: **Run** (or **Ping** for the link test alone), wait, read the
report, then **Send** to put the run on the server (and **Memo** for a copy in
Memo Pad). **Name** is the tester's name (it starts as the Palm's HotSync
user name); Send needs one. Every upload carries it (`w=`), and the server
keeps each tester's runs apart: a run's key is `name/stack:id`. The report
shows each tester's latest run of each stack (`?who=a,b` keeps those
testers), and the server's front page lists every run by tester. A tap during a run stops it. **Reps** (default 3) repeats each
download and upload; **64K** and **Sweep** add the long tests (64K both ways).

### The three rigs

- **Visor + Springboard**: as in `~/code/springboard-nio`: module flashed,
  `host/nio.sh start` (NIO on this Mac on the module's USB port).
  `make flash-visor` puts H2H SB and H2H NL in the module ROM ("SBNIO
  0.4 H2H": SB NIO and the two apps, 57 of its 64 KB; NIO Net, FI Config,
  ISS Tracker and Weather make way), builds the module firmware with it,
  flashes the module and restarts NIO; then reset the Visor. Set NIO's packet link to 8192 bytes,
  as the module allows (`channel: packet_link: capacity: 8192` in
  `host/nio-run/fujinet-data/fujinet.yaml`; the default, 4096, drops any
  reply over 4 KB, so the sweep's top sizes would time out).
- **Palm IIIc + classic firmware**: the IIIc in its cradle, null modem to the
  FN-RS232 running `palm-demo-fozz` (192.168.1.252). Run **FN H2H**.
- **Palm IIIc + NIO**: either flash the FN-RS232 with NIO
  (`fujibus-gpio-rs232-cdc-log-s3-wroom-1-n16r8`, see `fujinet-nio-palm-install.pdf` in the FujiPalm folder),
  or move the cradle cable to this Mac's USB serial adapter and run NIO's
  `fujibus-rs232` POSIX build on it. Run **H2H**. Note which in your notes:
  the report shows NIO's version and build profile, which tells them apart.

### The same target on the web

`server/h2h_server.py` is a single file with no dependencies, so it runs
unchanged on a web host (`python3 h2h_server.py --port 8080 --data results`);
the shared one is `http://fujinet.online:8199/h2h`. Its version is the git
commit it runs from (`-dirty` if the file has changed since), or, for a copy
outside a checkout, the one `make server-release` stamped into it;
`--server-version` or `H2H_VERSION` overrides both. It is in every reply's
`Server: h2h/<version>` header, at `/h2h/version`, on the index page, in
both reports, and `python3 h2h_server.py --version` prints it.
Point every Palm's target at it (`http://host:8080/h2h`) and all three stacks
cross the same internet path to the same bytes. Running it on this Mac
instead takes the internet out, which shows what the Palm, the link and the
FujiNet themselves cost. Both are worth a run.

Any other URL works as a target too (`http://example.com/`): the app then
runs ping plus a repeated GET of that page, with no size or content check, so
a real public site can be timed. Its results stay on the Palm (Send needs an
h2h server).

### H2H NL and NioLib

`make` also builds NioLib for Palm OS 3.5 (`make niolib`: the palmdev-py image,
without the USB transport, which needs the Handspring SDK). H2H NL needs
NioLib in the Visor's RAM. For a Visor without it, flash a ROM with NioLib in
place of H2H SB (`SB_ROM_APPS`, see the Makefile; 60 of 64 KB). NioLib
travels there as `NioLib (ROM copy)`, type `NLrm`, so Palm OS never loads it
from the module; on the Visor, H2H NL's **Options > Install NioLib** copies
it into RAM as the real `NioLib` (`libr`/`NiL1`), which is where NioLib's guide
wants it (it refuses while NioLib is loaded). NioLib uses its Springboard
transport unless NIO Config saved another. **Options > Copy to RAM** makes
H2H NL RAM, as for H2H SB.

NioLib makes its exchanges inside its own code, where the app's probe cannot
reach: its runs report each call's total time, and the link split shows as
"not split". Its packets are 1024 bytes on every transport, so reads stop at
1002. NioLib checks the Springboard mapping (three Handspring calls) on every
bus access and saves a preference at every doorbell; these runs show what that
costs.

## The tests

| Test | What it is |
|---|---|
| `ping1` | the first link call, which pays for any wake-up: NIO's GetInfo after 30 s idle, or classic's HotSync listener holding the port |
| `ping` | 32 of the stack's smallest round trips with no network: classic `N1: STATUS` (nothing open), NIO `Clock.GetTime` |
| `get16` | a GET of 16 bytes: the cost of one request (open, DNS/connect, headers, close) |
| `get1k` … `get64k` | GETs of 1, 4, 16 and 64 KB, read with the size the shipped apps use (classic 256, NIO serial 490, Springboard 1002) |
| `redir1` | `get1k`'s body behind one 302 with no body (`/h2h/redirect/302e/bytes/1024`), asked to follow (`FN_OPEN_FOLLOW_REDIR`) |
| `redir3` | the same behind a 301, a 307 and a 308 (`301a-307-308a`), each with a short HTML page as most servers send, Locations absolute, path-only, absolute |
| `keep302` | a 302 not asked to follow: it must come back as a 302 |
| `post1k`, `put1k`, `post16k`, `post64k` | POST or PUT of that many pattern bytes to `/h2h/sink/N`, which checks every byte; opened with `FN_OPEN_BODY_UNKNOWN`, written with the read size, then committed with a write of nothing. `post64k` only with **64K** |
| `echo4k` | 4 KB sent to `/h2h/echo` and read back, checked on both sides |
| `post307` | `post1k` behind a 307, which has the body sent again |
| `swNNN` | 16 KB read NNN bytes at a time, from 64 up to the most one exchange can carry |
| `page` | the target itself, when it is not an h2h server |

Bodies come from `/h2h/bytes/N`: byte *i* is `(i ^ (i >> 8)) & 0xFF`, so the
Palm checks every byte as it arrives (a dropped or repeated piece fails the
test). The check is timed and reported apart. Uploads are the same pattern,
and the server checks them: `ok N`, or a 422 with the first wrong byte (or
too few, too many), which the Palm reports as *server: bad upload*.

The redirect and upload tests name what went wrong: *not followed* (a
redirect came back although following was asked), *followed unasked*, *not
on server* (a 404: an h2h server from before these tests) and *server: bad
upload*. A redirect's cost is the time over `get1k` (`post1k` for
`post307`) per hop; an upload's rate is its body over the writes plus the
wait for the reply, because a NIO that holds the body until the commit
(curl) sends it during that wait. What to expect, from each NIO's code:

| NIO | Redirects | Uploads (`FN_OPEN_BODY_UNKNOWN`) |
|---|---|---|
| POSIX (curl): the Visor's, or the emulator's | followed; a POST to a 301/302/303 becomes a GET, to a 307/308 is sent again | held until the commit, then sent with a Content-Length |
| ESP32 (FN-RS232) | followed for GETs, but ESP-IDF hands NIO each redirect's own body, which it passes on (and since d2d22b2 the redirect's status sticks): `redir3` should fail, `redir1` (no bodies) pass; an upload's redirect is not followed (`post307`) | sent chunked as it is written |
| NioLib Network (NIO on the Palm) | not followed: `redir1`, `redir3` and `post307` say so | held in a 1 KB buffer until the commit: over 1 KB fails (`post16k`, `post64k`, `echo4k`) |

The server's log (`/h2h/log.csv`) has, for every request with a body, its
size, how it came (`length` or `chunked`), how long it took to arrive and
what the check found; for every redirect, where it pointed. The reports show
it under each test as *server saw*: the hops a run followed, the encoding
and the check.

## Reading the numbers

Times are the Palm's own, in microseconds. On a 68k Palm `clock.c` reads the
Dragonball timer that makes the system tick (it finds which one by watching it
wrap with the tick count), so resolution is about 0.1–30 µs instead of 10 ms;
the report says which clock it got (`TMR1 0.1us`, or `ticks 10ms` as a fallback).

Every download is cut into:

- **open**: the open call; **ready**: from then until the reply has started
  (classic: the first STATUS, which is when FujiNet makes the request; NIO:
  `fn_info` until the status line is in); **body**: from then to the last byte;
  **close**. **ttfb** is from the start to the first body byte.
- **rate**: body bytes over the body time.

and every request/reply exchange on the link into:

- **send**: the request leaving the Palm. Classic waits for the bytes to drain
  (`SrmSendWait`), so this is their wire time; NIO's serial transport hands
  them to the Serial Manager and moves on; on the Springboard it is the CS1 writes.
- **wait**: from then until the reply starts: the FujiNet (or the RP2350, USB and
  NIO on the Mac), and the network when the call waits on it.
- **recv**: the rest of the reply coming in and being checked.
- **palm**: everything between exchanges (the app, the library, the body check).

plus **exchanges** (round trips), **empty** (reads that found nothing yet),
**bytes per read**, **wire bytes** both ways (SLIP framing and escapes
included; on the Springboard, CS1 words × 2), and **line time**, what those
bytes need at 115200 baud, against the whole.

**Link notes** close the report: one line per library error (which call,
NIO's code, and the last exchange: `#seq r<result> n<reply length> t<sends>`
and the reply's first bytes), and on the Springboard anything odd the probe
saw: a result other than OK, a resend, a failed check word, or a reply that
passed its check word but is not a FujiBus packet answering the request.
That last one is read a second time, and the note says whether the second
read matched. Notes also go to the firmware console (`palm: ...`, next to
its own `#seq request` and `#seq reply` lines) and to the server with Send,
which tries each request up to three times.

What to look for:

- **send + wait + recv per exchange** in `ping` is the link's floor. Every
  read pays it, so the number of exchanges matters as much as the bytes.
  Classic needs two per piece (STATUS then READ); NIO needs one.
- **recv close to line time** means the serial line is the limit; **recv well
  above it** means the Palm can't keep up with the bytes.
- **wait large with little data** means the FujiNet side: device turnaround,
  its own polling, or the network. The server's *send* time (its log) shows the
  far end is not the cause.
- **The sweep** finds the read size where the rate stops rising: the most a
  stack can do. `*` marks the size the shipped apps use, so the gap between
  `*` and the best is speed left on the table.

## What these builds change, and why

So the sweep can try reads past the shipped size, the NIO library is built
here (from `$(REPOS)/fujinet-nio-lib`, into
`palm/build/`) with bigger packet buffers: 4096 bytes on serial (shipped 512)
and 8192 on the Springboard (shipped 1024, the module's limit). The main
download tests still read the shipped size, so they match the FI apps; only
the sweep goes further. The serial receive buffers are bigger too (classic
8 KB, NIO 6 KB, shipped 1 KB), so a whole large reply fits while the Palm
reads it a byte at a time. The apps get an 8 KB stack.

The timing hooks sit outside the library: the linker wraps
`fn_transport_exchange*` and `fn_channel_*_byte` (serial) and
`fn_palmos_exchange` (Springboard). The Springboard one is a copy of the
library's exchange with clock reads between its steps (`probe_sb.c`, from
fujinet-nio-lib 2733ba1); keep it in step if that file changes. The classic link is
fujinet-palm's FN client logic (same codec, retries and byte-at-a-time
receive) with the same clock reads.

## Checked so far

- `make test`: the server, and the suite on the Mac against it (all sizes,
  sweep, upload, both reports, a corrupted body caught).
- CloudpilotEmu, Palm IIIc OS 3.5 image: **FN H2H** against FujiNet-PC
  (classic) and **H2H** against NIO's POSIX `fujibus-rs232` build, full
  suite, Send and Memo. The emulator's serial timing is not a real Palm's, so
  its numbers mean nothing; it proved the apps, the timer, the uploads and
  the report.
- **H2H SB** builds and links, but no emulator has a Springboard: its first
  run is on the Visor.
- The redirect and upload tests (2026-11-15): `make test` runs them through
  the suite on the Mac, its link sending bodies chunked (as the ESP32 NIO
  does) and, with `H2H_HOST_BODY=length`, with a length (as curl does), and
  following redirects as curl does; `H2H_HOST_NOFOLLOW=1` and a server
  without the endpoints check *not followed* and *not on server*. In
  CloudpilotEmu (Palm IIIc, OS 3.5), **H2H NL** through NioLib 11's serial
  transport and NIO's POSIX build passed all nine, Send included: curl
  followed the 302, the 301-307-308 chain and the POST's 307, kept the 302
  it was not asked to follow, and sent every body with a length.

On the Visor (2026-11-14):

- **Misread words.** The module answers each Palm read from SRAM within the
  bus cycle; now and then (most often just as it publishes a reply, while its
  Core0 was formatting console lines) a read returned another word: RESULT
  read 17666 then 0. One such read failed the exchange (`FN_ERR_IO`), and a
  misread length or check word cost a pass over the reply. fujinet-nio-lib's
  Springboard exchange now reads RESP_SEQ, RESULT, RESP_LEN and RESP_CHECK
  until two reads agree (reply words were already covered by the check word),
  and the firmware gives Core1 SRAM priority and logs each request and reply
  only after `log on`. The notes show each misread the probe catches.
- **Code in the module ROM is slow.** On the 64K download 74% of the time
  was the Palm between exchanges: the library, the app and the body check,
  all fetched over the Springboard bus with no cache. H2H RAM is the same
  app in RAM (H2H SB's Options > Copy to RAM writes it, resource by
  resource, with no cradle); its results are labelled `nio-springboard-ram`,
  so the two runs show what running from the module costs.

Things the emulator runs turned up that the real runs may show too:

- NIO's POSIX builds log `POSIX idle delay: 50 ms (no transport wait)`:
  every request can wait up to 50 ms for NIO to look at it (`ping` ≈ 59 ms
  against classic FujiNet-PC's ≈ 24 ms). It is the serial POSIX build's: the
  Springboard's NIO (`fujibus-link-serial-debug`) logs 1 ms, so it does not
  slow the Springboard. NIO for the IIIc on this Mac would have it.
- Palm OS 3.5's `StrPrintF` has no `%c`: using it misaligns the arguments and
  crashes the Palm. Nothing here uses it.
