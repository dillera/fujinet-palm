# FI apps: screenshots and usage notes

All shots are raw 160x160 PNGs from CloudpilotEmu (`tools/rig.sh ... nio`),
with the emulated Palm's cradle joined to a POSIX FujiNet NIO
(firmware 0.1.1, board "POSIX + FujiBus over RS-232"), taken 25 Oct 2026.

Devices:

- **m100**: `palm-m100-os3.5.1.img`, Palm m100 ROM, Palm OS 3.5.1, 2-bit grey.
- **IIIc 4.0**: `palm-iiic-os4.0.img`, Palm IIIc, Palm OS 4.0, 256 colours.
  The colour FujiPet shots were taken on this image, not on
  `palm-iiic-os3.5.img`. That image's ROM is the 3.50 *beta*, and
  FujiPet's colour test (`OpenColour`: ROM version >= 3.5.0 *release*) turns
  it down, so on the IIIc 3.5 image FujiPet draws in black, white and grey
  dither on a colour screen (the system's title bars are still purple).

HotSync user name: "Andrew" (NIO's `hotsync.user_name`). Pets: "Fuji" the
Cat (colour) and "Mochi" the Dog (m100, the friend).

---

## Launcher

| File | Device | Caption |
|---|---|---|
| fi-launcher.png | m100 | The launcher's Unfiled category with the six FI apps: FI Bounce, FI Config, FI FujiPet, FI Lobby, FI Test, FI Texas Hold'em. |

Notes: new apps land in **Unfiled**, so the launcher's default **Main** page
does not show them. Tap the category name at the top right and pick Unfiled
(or All). Every app except FI FujiPet has the same FujiNet-logo icon; FujiPet
has its own cat icon. The Hold'em label is cut short to "FI Texas H...".

---

## FI Test

| File | Device | Caption |
|---|---|---|
| fi-test-start.png | m100 | FI Test on launch: tap Ping or Fetch. |
| fi-test-ping.png | m100 | After Ping: FujiNet found, firmware, board, IP and Wi-Fi. |
| fi-test-fetch.png | m100 | After Fetch: HTTP 200, 577 bytes, page title "Example Domain". |

How to use it:

- **Ping**: asks the FujiNet on the cradle for its firmware and board, then
  its network status and Wi-Fi config. It shows "Contacting FujiNet...",
  then "FujiNet found!", "Firmware ...", "Board ...", "IP ..." and "WiFi ...".
  With no FujiNet it shows "No answer from FujiNet. / Is the cradle cabled
  to it?".
- **Fetch**: does an HTTP GET of `http://example.com/` through the FujiNet
  (20 s limit) and shows "HTTP <status>, <n> bytes" and the page's
  `<title>`. Errors show as "Open: ..." or "Fetch: ...", or "Fetch timed out."
- No menu and no other keys.

Surprising:

- The Board line is wider than the screen and is cut off at the right
  ("Board POSIX + FujiBus over RS-232 (Am...").
- On the POSIX NIO, Ping shows "IP none" and an empty "WiFi" line. That is
  correct (the desktop build has no Wi-Fi), but it looks like a failure.

---

## FI Config

| File | Device | Caption |
|---|---|---|
| fi-config-main.png | m100 | The TNFS servers screen: fujinet.diller.org, apps.irata.online and six empty slots. |
| fi-config-info.png | m100 | The Info alert: Wi-Fi, IP, firmware, board and "Replies asked again". |
| fi-config-edit.png | m100 | Edit Server, editing the apps.irata.online slot. |
| fi-config-browse.png | m100 | Browsing fujinet.diller.org:/NIO/PALM/: the six FI .prc files. |
| fi-config-confirm.png | m100 | "Install FI-FujiPet.prc on the next HotSync?" confirmation. |
| fi-config-progress.png | m100 | The copy in progress: "Queuing: 5632 of 35029 bytes". |
| fi-config-queued-alert.png | m100 | "FI-FujiPet.prc is queued. Tap HotSync to install it." |
| fi-config-queued.png | m100 | Queued for HotSync: FI-FujiPet.prc waiting. |
| fi-config-queue-empty.png | m100 | The queue after the HotSync: "Nothing queued". |
| fi-hotsync.png | m100 | HotSync started from FI Config's queue: "Synchronizing FI FujiPet", the queued app being installed. |
| fi-hotsync-done.png | m100 | "HotSync operation complete.", user Andrew. |
| fi-hotsync-log.png | m100 | HotSync Log: "Installed FI-FujiPet.prc / Backed up 10 databases / FujiNet HotSync complete". |

How to use it:

- **Main screen ("TNFS servers")**: a list of 8 server slots, kept in the
  app's preferences. It starts with fujinet.diller.org and apps.irata.online;
  unused slots read "(empty)". Tap a server to select it, then:
  - **Browse**: opens the selected server at "/". With nothing selected:
    "Pick a server to browse."
  - **Edit**: edits the selected slot, which can be "(empty)". With nothing
    selected: "Pick a server to edit."
  - **Queue**: shows what is queued for the next HotSync.
  - **Info**: an alert with Wi-Fi SSID ("unknown" if none), IP ("none"),
    Firmware, Board and "Replies asked again: N" (how many FileDevice
    replies were lost on the cradle and asked for again).
  - **HotSync**: closes the cradle port and starts a local HotSync (the
    HotSync app's cradle sync), which installs everything queued.
- **Edit Server**: one field, "TNFS server (blank to clear):", up to 31
  characters, as a host name or host:port, for example apps.irata.online.
  **Save** stores it (a blank entry clears the slot back to "(empty)");
  **Cancel** goes back unchanged.
- **Browse**: the title shows `server:/path/` (it reads "Reading..." while
  the folder loads). Folders end in "/". Select a line, then:
  - **Open**: goes into a folder; on a .prc, .pdb or .pqa it asks "Install
    <name> on the next HotSync?" (**Queue** / **Cancel**). Any other file:
    "Only .prc, .pdb and .pqa files install on a Palm." A file under 78
    bytes: "<name> is too short to be a Palm app or database."
  - **Up**: goes to the parent folder.
  - **Done**: back to the servers screen.
  - While a file is copied, the line above the list shows "Queuing: N of M
    bytes". It ends with "<name> is queued. Tap HotSync to install it." A
    failed copy says "Could not queue that file. HotSync will skip what was
    copied."
- **Queued for HotSync**: lists the files in NIO's `hotsync/install/`
  (title "Nothing queued" when it is empty). **HotSync Now** starts the
  HotSync; **Done** goes back.
- **HotSync itself**: the standard Palm HotSync screen. NIO installs the
  queued files, backs the Palm up, and empties the queue. The cradle's
  HotSync button does the same from anywhere.
- No menus, no Graffiti shortcuts.

Setup needed on NIO: `hotsync: enabled: true, cradle: true` in
`fujinet.yaml` (I also set `port: 0` and `user_name: Andrew`). FI Config
assumes `hotsync.root` is `/hotsync`.

Surprising:

- Copying is slow over the 115200-baud cradle: about 1 KB a second, so
  FI-FujiPet.prc (35 KB) took about 35 s. The screen does not respond
  during the copy.
- The fujinet.diller.org root lists two odd entries, "~" and "== Welcome to
  Diller.org FujiNet ==/", which show up as folders.
- After a HotSync, the first Queue or Browse sits on "Reading..." for about
  3 s while the app takes the line back from NIO's HotSync listener.
- The HotSync replaced FI FujiPet even though it was already installed;
  nothing on screen warns that it is a reinstall.

---

## FI Bounce

| File | Device | Caption |
|---|---|---|
| fi-bounce-connect.png | m100 | Connect screen: server bouncy.diller.org, port 9003, name Andrew, and the key help. |
| fi-bounce-world.png | m100 | The world running with six bodies bouncing. |
| fi-bounce-info.png | m100 | Info box after about a minute: "Req 509 Xch 1558 Rty 0 Lost 0". |
| fi-bounce-who.png | m100 | Info and Who boxes together; Who lists "Andrew". |
| fi-bounce-add.png | m100 | The Add popup: body sizes 1 to 5. |

How to use it:

- **Connect screen**: fields **Server** (up to 47 characters, default
  bouncy.diller.org), **Port** (numeric, default 9003, the server's framed
  TCP port on bounce-world 2.3 and later) and **Name** (up to 8, default
  "palm"). The fields are saved. **Connect** shows "Connecting..." in the
  title, then opens the world, or shows "Could not join host:port."
- **World screen**: your 40x36-character part of the shared world, drawn
  as 4x4-pixel cells. The buttons along the bottom:
  - **Frz**: freeze/unfreeze the world (x-freeze).
  - **Rst**: reset the world.
  - **+** / **-**: faster / slower.
  - **Add**: a popup list 1 to 5; adds a body of that size.
  - **Who**: toggles a box listing the clients connected.
  - **Info**: toggles a box with your name, client count C, the world size,
    "frozen" if frozen, the body counts by size, and the link counters:
    **Req** (requests), **Xch** (FujiBus exchanges), **Rty** (replies asked
    again) and **Lost**.
- **Graffiti keys**: F freeze, R reset, + (or =) faster, - slower, 1 to 5
  add a body of that size, W who, I info, Q disconnect.
- **Hardware buttons while connected** (they do not launch apps): scroll up
  faster, scroll down slower, app button 1 freeze, 2 add a size-3 body,
  3 who, 4 info.
- **Menu "World"**: Freeze (/F), Reset (/R), Who (/W), Info (/I),
  Disconnect (/Q). Disconnect, or Q, closes the session and returns to the
  connect screen.
- The server's operator can send commands that invert the screen, show or
  hide Who or Info, or show a message box.

Surprising:

- **The World menu cannot be read**: the world keeps redrawing over the
  open menu, so the menu items get covered within a fraction of a second
  (even when frozen). Use the Graffiti command strokes or the buttons
  instead. I did not capture a menu shot for this reason.
- The "Add" popup trigger is drawn under the Who button and shows as
  "Ad(".
- The Bodies line counts the whole world (80x24 here), not only your part.
- Once, adding a size-5 body from the popup and then pressing Q gave "Lost
  the connection to the server. The server closed the connection." On
  reconnect the world was empty. A second Q later disconnected cleanly.

---

## FI Lobby

| File | Device | Caption |
|---|---|---|
| fi-lobby-loading.png | m100 | Loading the lobby: "Reading... 17 KB, 17 servers". |
| fi-lobby-list.png | m100 | The full list: "36 servers, 11 games", each with game, server name and players. |
| fi-lobby-detail.png | m100 | Game Server detail: Texas Hold'em, AI Room - 2 bots, online, 1 of 6 players, app key 8, server URL. |
| fi-lobby-detail-clients.png | m100 | The detail scrolled down: platforms and each platform's client URL. |
| fi-lobby-menu.png | m100 | The Lobby menu: Refresh, Use QA Lobby, About. |
| fi-lobby-about.png | m100 | The About box, naming the lobby URL in use. |

How to use it:

- On launch it fetches `http://lobby.fujinet.online/viewFull` and fills the
  list as the reply streams in. The status line reads "Connecting...",
  "Asking the lobby...", then "Reading... N KB, M servers", and at the end
  "N servers, M games" (with "(QA)" for the QA lobby). It loads in about
  15 s.
- Each row shows the game, the server name (shortened with "...") and the
  players "cur/max", or "off" for an offline server. The list scrolls with
  its arrows or the scroll buttons.
- **Tap a row**, or select one and tap **Details**, to open **Game Server**:
  game, server, "Status: online, region us", "Players: N of M", "App key",
  "Server URL", then "Platforms (n)" and each client's URL. Scroll with the
  scroll bar or the hardware scroll buttons (a page at a time). **Done**
  goes back.
- **Refresh** (button, or menu Refresh /R) reloads the list. A failure
  leaves "Partial list. Tap Refresh." or "Failed. Tap Refresh to retry."
- **Menu "Lobby"**: Refresh (/R); **Use QA Lobby** (/Q) switches between
  the live lobby and `qalobby.fujinet.online` (the title becomes "FI QA
  Lobby", and the choice is saved); **About** names the lobby URL.

Surprising: after you close a detail, the row you opened stays highlighted.

---

## FI Texas Hold'em

| File | Device | Caption |
|---|---|---|
| fi-holdem-setup.png | m100 | Setup: name Andrew, server https://th.carr-designs.com/, five tables listed, table ai2. |
| fi-holdem-table.png | m100 | Seated at ai2 on your turn: "Your move! 32 s", with Fold, Call 5, Raise 15, Raise 25 and All-in. |
| fi-holdem-showdown.png | m100 | Showdown: the board, a 45 pot, "Jim bot won with pair, aces, kickers k...". |
| fi-holdem-showdown2.png | m100 | Another showdown, with the Leave button showing (not your turn). |
| fi-holdem-result.png | m100 | The Last Hand alert: "Clyd bot won with flush, ace-high, kickers queen, seven, three, two". |
| fi-holdem-menu.png | m100 | The Table menu: Leave Table, Last Result, About. |
| fi-holdem-menu-setup.png | m100 | The Game menu on the setup screen: Refresh Tables, Default Server, About. |
| fi-holdem-about.png | m100 | The About box. |

How to use it:

- **Setup screen**:
  - **Name**: up to 8 letters and digits. Sit Down without one: "Enter a
    name to play under: letters and digits, up to 8."
  - **Server**: default `https://th.carr-designs.com/`. A lobby address
    such as `https://th.carr-designs.com/?table=den` also works, and its
    table is used.
  - **Tables** list, filled on launch: name and seated/max. **Refresh**
    reloads it. Tapping a table copies its id into the **Table** field
    (for example ai2); you can also type the table name.
  - **Sit Down**: opens the table ("Taking a seat...").
  - Status line: "Asking the server...", then "N tables. Pick one, Sit Down."
  - **Menu "Game"**: Refresh Tables (/R), Default Server (puts the default
    server back into the field), About.
- **Table screen**: the other players across the top (name, chips, last
  action; their cards are backs until the showdown, and the player to act
  is highlighted). The felt shows the five board cards, your two hole cards
  and the stage (Pre-flop, Flop, Turn, River, Showdown) with the pot. Below
  that is your line (name, chips, your bet, your last action), then the
  status line ("Your move! N s" with the server's clock, "Waiting on X...",
  or the last hand's result).
  - **On your turn** the moves appear as buttons along the bottom (for
    example Fold / Check or Call n / Bet / Raise n / All-in). Tap one; or
    pick one with the hardware **scroll up/down** buttons and press **any
    app button**; or write a Graffiti letter: **f** fold, **c** check or
    call, **b** bet, **r** raise, **a** all-in (each plays the first move
    that starts with that letter).
  - **Not your turn**: a **Leave** button sits at the bottom right.
  - **Tap the status line** to see the last hand's result ("Last Hand"
    alert, or "No hand has finished yet.").
  - **Menu "Table"**: Leave Table (/L), Last Result (/S), About.
  - While you are seated the app buttons play moves; they do not launch
    apps. Leaving the table, or the app, tells the server you left.

Surprising:

- The setup status line runs into the Graffiti indicator and is cut off:
  "5 tables. Pick one, Sit Dc".
- The About box is too long: its last paragraph (the card-art credit) is
  cut off below the visible text.
- The "AI Room - 2 Bots" count still read 1/6 after I left the table (the
  list is not refreshed when you come back).
- Once, opening the Game menu right after launch, while the tables were
  still loading, left the setup screen stuck on "Asking the server..." with
  an empty list (the NIO log shows the Palm stopped asking). Tapping
  **Refresh** fixed it.
- Bots play quickly, so hands often end in a showdown within a few
  seconds; the server gives you about 35 s to move.

---

## FI FujiPet

| File | Device | Caption |
|---|---|---|
| fi-fujipet-new.png | IIIc 4.0 | New FujiPet: name "Fuji", type Cat, with the adult and egg preview. |
| fi-fujipet-main.png | IIIc 4.0 | Fuji, an adult Cat (Grumpuss form), on the main screen; "1 friend online" on the ground line. |
| fi-fujipet-feed.png | IIIc 4.0 | The Feed popup: Meal or Snack. |
| fi-fujipet-play.png | IIIc 4.0 | The left-or-right game: "Round 1 of 5: wins 0. Which way will it look?" |
| fi-fujipet-status.png | IIIc 4.0 | Status: Fuji the Cat, Adult: Grumpuss, age 4, weight 53g, Hungry and Happy hearts, Discipline, care mistakes 11. |
| fi-fujipet-menu-pet.png | IIIc 4.0 | The Pet menu: Status, Friends, New Egg... |
| fi-fujipet-menu-options.png | IIIc 4.0 | The Options menu: Settings..., Reconnect, About. |
| fi-fujipet-settings.png | IIIc 4.0 | Settings: friend server localhost:7878, Time Normal/Fast/Turbo, Meet friends online, Sounds. |
| fi-fujipet-friends.png | IIIc 4.0 | FujiPet Friends: "Fuji is #1. 1 of 1 online.", Mochi (Dog Baby) online, and "Mochi: You're the best!" received. |
| fi-fujipet-message.png | IIIc 4.0 | A message popping up on the main screen: "Mochi: Let's be pals!" |
| fi-fujipet-about.png | IIIc 4.0 | The About box. |
| fi-fujipet-say.png | m100 | The friend's Palm: the Say list of eight canned messages. |
| fi-fujipet-friends-sent.png | m100 | The friend's Friends screen after sending: "You to Fuji: You're the best!" |
| fi-fujipet-main-mono.png | m100 | Mochi the Dog as a baby, on a grey Palm: outlines and grey dither. |

How to use it:

- **New FujiPet** (first launch, or Pet > New Egg..., or tapping a dead
  pet): **Name your pet** (up to 10 letters and digits), **Pick a type**:
  Cat, Dog, Bunny or Dino (the preview shows that type as an adult and its
  egg), then **Hatch!** (or **Cancel**). "Hatches in 5 pet minutes; grows
  up over days."
- **Main screen**: four icons along the top (**Feed**, **Light**, **Play**,
  **Medicine**) and four along the bottom (**Clean**, **Status**,
  **Discipline**, **Friends**). Between them: the pet's name and the pet's
  clock, a red "!" when it calls for care, a skull when it is sick, its
  poops, and the ground line, which shows notices ("Fuji hatched! Feed
  it!", "Lights off.", "Nothing to clean.") or the friend status ("0 pets
  on the server", "1 friend online", "Connecting...", "Offline", "No friend
  server", "No FujiNet in the cradle").
  - **Feed**: popup **Meal** (fills a hunger heart; "is full!" when full)
    or **Snack** (fills a happy heart; too many sweets make it sick).
  - **Light**: lights on/off. Turn it off when it falls asleep, or a care
    mistake follows. The light comes back on when it wakes.
  - **Play**: five rounds of "Which way will it look?". Tap the left or
    right half of the screen (or press scroll up for left, scroll down for
    right). Three right guesses win and make it happy. Not while it is sick.
  - **Medicine**: cures sickness ("Fuji is all better!", or "isn't sick").
  - **Clean**: sweeps the mess away.
  - **Status**: hearts for Hungry and Happy, Discipline, stage and form,
    age (days) and weight, and care mistakes (or "Sick! It needs
    medicine.").
  - **Discipline**: scold it when it calls for no reason (children and
    teens do now and then): "learned a lesson". Otherwise "did nothing
    wrong!".
  - **Friends**: the Friends screen.
  - Tapping the pet makes it happy (an animation only). While it sleeps
    the care icons answer "Shh! Fuji is asleep."; an egg answers "Wait for
    the egg to hatch."
- **Menus**: Pet: Status (/S), Friends (/F), New Egg... (asks "Start over
  with a new egg? Fuji will be gone for good."). Options: Settings...,
  Reconnect (/R; tries the friend server again), About.
- **Settings**: **Friend server (host:port)**, default `localhost:7878`
  (blank means the default); **Time**: **Normal** (real time), **Fast** (a
  pet minute a second), **Turbo** (a pet day a minute); **Meet friends
  online** (keep a connection to the friend server); **Sounds**. **OK**
  saves and reconnects if the server or the online setting changed;
  **Cancel** leaves things as they were. Going back to Normal sets the
  pet's clock to the Palm's again.
- **FujiPet Friends**: the info line ("N pets here. Tap Register!", or once
  registered "Fuji is #1. N of M online."), the list of registered pets
  (a dot for online, name, type and stage, age in days), **Say:** popup
  with eight messages (Hi, friend! / Want to play? / I'm hungry! / You're
  the best! / Let's be pals! / Yay! Hooray! / Good night! / Bye bye!) and
  **Send** (to the friend selected in the list; it must be online), then the
  message log (the last three messages sent and received). Buttons: **Done**,
  **Refresh** (asks for the list again, or reconnects), and **Register**
  (adds your pet to the server; the button disappears once you are
  registered). A message received while you are on the main screen pops up
  in a box at the top for a few seconds; a kind one cheers the pet up.
- The pet lives on while the app is closed: the next launch catches up.

Setup needed: a FujiPet friend server (`tools/fujipet-server/fujipet_server.py
--host "" --port 7878`; `--host ""` so it also listens on IPv6, since POSIX
NIO connects to `::1` for localhost).

Surprising:

- **Colour on the IIIc 3.5 image**: see the device note at the top. On
  `palm-iiic-os3.5.img` (the 3.50 beta ROM) FujiPet is in black and white.
  The colour shots are from the IIIc OS 4.0 image.
- **Turbo makes good care almost impossible**: a pet day passes every real
  minute, so a 15-pet-minute call becomes a care mistake in under a second.
  In about 3 minutes of Turbo, with meals and cleaning sent every couple of
  seconds, Fuji went egg, baby, child, teen and adult, and ended up a
  neglected adult (Grumpuss, 11 care mistakes) and sick. To get a
  well-raised form, use Fast or Normal.
- After switching from Turbo back to Normal, Fuji stayed asleep at 6:17 pm
  until the next minute ticked over, then woke up.
- A snack right after curing made it sick again (sweets).
- Losing the game, even twice, did not change the Happy hearts.
- On the Friends screen, the first friend in the list is shown selected.

---

## Shots not taken

- **FI Bounce World menu**: the world draws over the open menu, so the
  shot cannot be taken (see the FI Bounce notes).
- **FI FujiPet colour shots on the IIIc OS 3.5 image**: the app draws in
  black and white there; the colour shots are from the IIIc OS 4.0 image.
