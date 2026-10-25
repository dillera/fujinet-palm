# FN apps: screenshot notes

These are the classic FujiNet Palm apps. Every shot is a raw 160x160 PNG of the
emulated LCD, taken in CloudpilotEmu with its cradle bridged to FujiNet-PC
(`tools/rig.sh ... classic`).

## Devices used, and why they differ from the plan

- **Black-and-white shots: Palm V, Palm OS 3.5** (`assets/images/palm-v-os3.5.img`).
  The planned `palm-iii-os3.5.img` cannot run any FN app. Every app stops at
  "This app needs Palm OS 3.1 or later." On that image, Prefs lists Buttons,
  Digitizer, Formats, General, Modem, Network, Owner and ShortCuts, with no
  Connection panel and no "Stay on in Cradle". That matches OS 3.0/3.1, not
  3.5. So `Palm-III-3.5-en.bin` (and the image made from it) looks like a
  Palm III ROM older than 3.1 under the wrong name. The Palm V OS 3.5 image does
  show the Connection panel and runs every app. The Palm V has the same
  160x160 screen as the Palm III, so the shots look the same. Most apps draw
  in 1 bit; FN FujiPet switches the screen to 4-bit greys.
- **Colour shots: Palm IIIc, Palm OS 4.0** (`assets/images/palm-iiic-os4.0.img`).
  The planned `palm-iiic-os3.5.img` (the IIIc 3.5 *beta* ROM) shows FN FujiPet
  in black and white only. FujiPet turns colour on only when the ROM version is
  at least 3.5.0 *release*, and a beta stage of 3.5 counts as lower than that.
  The OS 4.0 IIIc image draws in colour.

## Launcher

| File | Device / OS | Caption |
|---|---|---|
| fn-launcher.png | Palm V, OS 3.5 | The six FN apps in the launcher's Unfiled category. |

The apps install into **Unfiled**. Pick Unfiled (or All) from the category menu
at the top right of the launcher to see them. The icon names are FN Bounce
(the database is "FN Bounce World"), FN FujiPet, FN HotSync, FN Lobby, FN Test
and FN Texas Hold'em. The Palm V launcher cuts long names short ("FN Boun...",
"FN Texas...").

## FN Test

| File | Device / OS | Caption |
|---|---|---|
| fn-test-start.png | Palm V, OS 3.5 | FN Test at launch, before the ping. |
| fn-test.png | Palm V, OS 3.5 | After Ping FujiNet: FujiNet found, with its firmware, IP and Wi-Fi. |

**Use.** There is one screen with one button.
- **Ping FujiNet** asks the FujiNet on the cradle port for its adapter
  settings. The screen first shows "Contacting FujiNet...", then:
  - **FujiNet found!** followed by `Firmware <version>`, `IP a.b.c.d` and
    `WiFi <SSID>`, or
  - **No answer from FujiNet. / Is the cradle cabled to it?**, or
    `Error <hex>`.
- There are no menus, settings or shortcuts.

**Notes.** On FujiNet-PC the IP is shown as 127.0.0.1 and the Wi-Fi as "Dummy
Cafe", the FujiNet-PC placeholder. A real FujiNet shows its real values.

## FN HotSync (FujiConfig.prc)

| File | Device / OS | Caption |
|---|---|---|
| fn-hotsync-main.png | Palm V, OS 3.5 | Main screen: FujiNet's eight host slots. |
| fn-hotsync-edit.png | Palm V, OS 3.5 | Edit Host Slot, here for apps.irata.online. |
| fn-hotsync-browse.png | Palm V, OS 3.5 | Browsing fujinet.diller.org:/PALM/. |
| fn-hotsync-queue.png | Palm V, OS 3.5 | "Queue for HotSync" confirmation for FN-Test.prc. |
| fn-hotsync-queued-alert.png | Palm V, OS 3.5 | The alert after queuing: "FN-Test.prc is queued. Tap HotSync to install it." |
| fn-hotsync-queued.png | Palm V, OS 3.5 | Queued for HotSync: three .prc files waiting in /palm/install. |
| fn-hotsync-info.png | Palm V, OS 3.5 | Info alert: FujiNet's Wi-Fi, IP and firmware. |

**Use.**
- **Main screen ("FN HotSync").** The title reads "Asking FujiNet..." while it
  loads. It then lists FujiNet's 8 host slots, with "(empty)" for unused ones.
  Tap a slot to select it. Buttons:
  - **Browse** opens the selected host at "/". With no slot selected, or an
    empty one, it says "Pick a host slot to browse."
  - **Edit** opens Edit Host Slot for the selected slot (it needs a selection).
  - **Queue** shows the files waiting in `/palm/install` on the slot named
    **SD**. With no slot named SD it says "No host slot is named SD, where
    queued files go."
  - **Info** opens the FujiNet alert: `Wi-Fi: <SSID>`, `IP: a.b.c.d`,
    `Firmware: <version>`.
  - **HotSync** closes the FujiNet link and starts the Palm's HotSync app as a
    local (cradle) sync. If the HotSync app is missing it says "The HotSync app
    is missing."
- **Edit Host Slot.** One field, "Host name (blank to clear):", up to 31
  characters. A hint below says "SD is FujiNet's card; others are TNFS
  servers, e.g. apps.irata.online". **Save** writes all 8 slots back to FujiNet
  and returns. **Cancel** returns without saving.
- **Browse.** The title shows `<host>:<path>` and reads "Reading..." while it
  loads. The list holds up to 100 entries, and folders end in "/". Use the
  list's scroll arrows to move through it. Buttons:
  - **Open** on a folder goes into it. On a `.prc`, `.pdb` or `.pqa` it asks
    "Install <name> on the next HotSync?" with **Queue** or **Cancel**. Queue
    copies the file from the TNFS host to SD:`/palm/install/` and confirms
    "<name> is queued. Tap HotSync to install it." Any other file type gives
    "Only .prc, .pdb and .pqa files install on a Palm."
  - **Up** goes to the parent folder.
  - **Done** returns to the main screen.
- **Queued for HotSync.** This lists the files (not folders) in
  SD:`/palm/install/`. The title is "Nothing queued" when that folder is
  empty. **HotSync Now** starts the HotSync. **Done** returns.
- There are no menus or Graffiti shortcuts.

**Notes.**
- Selecting a list row takes two steps: tap the row, then tap Open. One tap
  does not open it.
- Edit's Save rewrites all 8 host slots at once.
- There is no way to remove a queued file from the Palm side.
- The first tap on the list's down arrow scrolled the list and also seemed to
  select the last visible row (favicon.ico). Check the selection before
  tapping Open.

## FN Bounce World

| File | Device / OS | Caption |
|---|---|---|
| fn-bounce-connect.png | Palm V, OS 3.5 | Connect screen with bouncy.diller.org, port 9003, name Andrew. |
| fn-bounce-world.png | Palm V, OS 3.5 | The world running with several bodies. |
| fn-bounce-info.png | Palm V, OS 3.5 | Info lines on: name, clients, world size, bodies by size. |
| fn-bounce-who.png | Palm V, OS 3.5 | Info plus the Who box listing the clients. |
| fn-bounce-add.png | Palm V, OS 3.5 | The Add popup: pick a body size from 1 to 5. |

**Use.**
- **Connect screen ("FN Bounce World").**
  - **Server:** up to 47 characters, default `localhost`. Change it, for
    example to `bouncy.diller.org`.
  - **Port:** numeric, default `9003`, which is the framed TCP port of
    bounce-world 2.3 and later.
  - **Name:** up to 8 characters, default `palm`.
  - All three are saved. **Connect** opens `N1:TCP://server:port/`, loads the
    shapes, registers as a 40x36-character client and enters the world. The
    title reads "Connecting..." meanwhile. On failure an alert says "Could not
    join host:port." with the reason (no answer from FujiNet, refused, server
    closed, no answer, unexpected reply).
  - The screen also lists the keys.
- **World screen.** The world fills the screen above a button bar, with each
  character cell drawn as 4x4 pixels. Buttons:
  - **Frz** freezes or unfreezes the world. This is server-wide.
  - **Rst** resets the world.
  - **+** / **-** speed it up or slow it down.
  - **Add** is a popup of 1 to 5 that adds a body of that size.
  - **Who** toggles a box with the clients' names.
  - **Info** toggles two lines: `<name>  C:<clients>  <W>x<H> [frozen]` and
    `Bodies 1:n 2:n 3:n 4:n 5:n`.
- **Graffiti keys:** **F** freeze, **R** reset, **+** (or =) faster,
  **-** slower, **1** to **5** add a body of that size, **W** who, **I** info,
  **Q** disconnect.
- **Hardware buttons while connected:** **Up** faster, **Down** slower,
  **app 1** (Date Book) freeze, **app 2** add a size-3 body, **app 3** who,
  **app 4** info. They do not launch their apps while you are in the world.
- **Menu "World":** Freeze /F, Reset /R, Who /W, Info /I, Disconnect /Q.
  Disconnect sends `close` and returns to the connect screen.
- The server's operator can send commands to the client: dark (inverted)
  mode, show or hide Who, show a message box, show or hide Info.
- After 5 failed polls in a row, or if the server closes or forgets the
  client, it shows "Lost the connection to the server." and goes back to the
  connect screen.

**Notes.**
- **The World menu cannot be read.** The app redraws the world every step,
  over the open menu, even while the world is frozen. You see a blank box under
  the "World" title. The menu items still work if you tap where they are, and
  the Graffiti shortcuts work. Use the buttons or keys instead. For this
  reason there is no menu screenshot.
- The "Add" trigger label is cut to "Adc" by the button next to it.
- bouncy.diller.org reported an 80x24 world, and another client was already
  there under the name "Andrew" (C:2).

## FN Lobby

| File | Device / OS | Caption |
|---|---|---|
| fn-lobby-list.png | Palm V, OS 3.5 | The full lobby list: 36 servers, 11 games. |
| fn-lobby-detail.png | Palm V, OS 3.5 | Game Server detail for 5 Card Stud, "AI Room - 2 bots". |
| fn-lobby-detail-more.png | Palm V, OS 3.5 | The same detail paged down: platforms and client URLs. |
| fn-lobby-menu.png | Palm V, OS 3.5 | The Lobby menu. |
| fn-lobby-about.png | Palm V, OS 3.5 | The About alert, showing which lobby URL is in use. |

**Use.**
- **Main screen ("FN Lobby").** The list loads by itself at launch from
  `http://lobby.fujinet.online/viewFull`. While it loads, the status line shows
  "Connecting...", "Asking the lobby..." and then "Reading... n KB, n
  servers". When it is done the status reads "<n> servers, <n> games".
  - Each row shows the game, the server name and players `cur/max`, or "off"
    for an offline server.
  - Tapping a row opens its detail at once.
  - **Refresh** fetches the list again.
  - **Details** opens the detail for the selected row.
- **Game Server detail** (a modal form):
  - game and server name
  - `Status: online, region us`
  - `Players: n of m`
  - `App key`
  - Server URL
  - `Platforms (n):` followed by the list
  - `Clients:` followed by `platform: url` lines
  The hardware **Up/Down** buttons page through it, and so does the scroll
  bar. **Done** closes it.
- **Menu "Lobby":**
  - **Refresh /R** fetches the list again.
  - **Use QA Lobby /Q** switches between the production and QA lobby
    (`qalobby.fujinet.online`). The choice is saved, the title becomes "FN QA
    Lobby" and the list reloads.
  - **About** shows the lobby URL in use.
- The list holds at most 48 servers. When the lobby has more, the status reads
  "n of m servers".

**Notes.**
- The app fetches over plain HTTP, not HTTPS.
- After you close a detail, the row you opened stays highlighted.

## FN Texas Hold'em

| File | Device / OS | Caption |
|---|---|---|
| fn-holdem-setup.png | Palm V, OS 3.5 | Setup: name Andrew, server th.carr-designs.com, five tables listed, ai2 picked. |
| fn-holdem-table.png | Palm V, OS 3.5 | Seated at ai2 on your turn: Fold, Call 10, Raise 20, Raise 30, All-in. |
| fn-holdem-showdown.png | Palm V, OS 3.5 | Showdown: the board, your hole cards and Jim Bot's shown cards, with the result line. |
| fn-holdem-result.png | Palm V, OS 3.5 | The Last Hand alert (tap the status line). |
| fn-holdem-menu.png | Palm V, OS 3.5 | The Table menu at the table. |

**Use.**
- **Setup screen ("FN Texas Hold'em").**
  - **Name:** up to 8 letters and digits. It must be filled in.
  - **Server:** up to 63 characters, default `https://th.carr-designs.com/`.
    You can paste a lobby address that includes `?table=...`; the app splits
    it into the server and the table.
  - **Tables** list: fetched by itself when the screen opens. Each row shows
    the table name and seats taken out of the total ("0 / 2"). Tapping a row
    copies its id into the **Table:** field (up to 8 characters, default
    `ai2`).
  - **Refresh** fetches the table list again.
  - **Sit Down** joins the table. Without a name it asks "Enter a name to play
    under: letters and digits, up to 8." Without a table it asks "Pick a table
    from the list, or type its name."
  - The status line at the bottom shows progress, for example "5 tables. Pick
    one, Sit Down." (cut short on screen).
  - **Menu "Game":** Refresh Tables /R, Default Server (puts the default URL
    back), About.
- **Table screen.**
  - **Top:** the opponents in two columns. Each shows the name, chips and last
    move, with mini cards: backs, or faces at showdown. The player whose turn
    it is has a black bar behind their name.
  - **Middle (the felt):** five board slots (empty ones are dotted), your two
    hole cards, the street (Pre-flop, Flop, Turn, River, Showdown) and the pot.
  - **Below the felt:** your name, chips, current bet and last move (or
    "Folded" or "Next hand"). Then the status line: "Your move!  <n> s" with a
    countdown, "Waiting on <name>...", or the last result.
  - **Bottom bar:** on your turn, one button for each move the server offers,
    such as Fold, Check, Call n, Bet n, Raise n or All-in. The highlighted one
    is the move the hardware buttons will play. Between turns the bar shows
    "Table: <id>" and a **Leave** button.
  - **Tapping the status line** opens **Last Hand**, the full text of the last
    result.
- **Keys at the table:**
  - **Up/Down** hardware buttons move the highlight between the moves.
  - **Any app button** (1 to 4) plays the highlighted move. While you are
    seated these buttons do not launch apps.
  - **Graffiti letter:** the first move whose name starts with that letter:
    **f** fold, **c** check or call, **b** bet, **r** raise, **a** all-in.
- **Menu "Table":** Leave Table /L, Last Result /S, About.
- When your turn starts the app beeps and highlights Check (or Call). If the
  move clock runs out, the server moves for you.
- After 4 failed polls in a row it shows "Lost the table." and returns to
  Setup. Before that the status line shows "Retrying (n)...".

**Notes.**
- On ai2 the bots fold often, so many hands end "Andrew won by default" before
  the flop.
- A previous hand's result stays on the status line while the next hand is
  still being dealt (for example "Clyd bot won by default").
- Leaving the app, or picking Leave, gives the seat back to the server.

## FN FujiPet

| File | Device / OS | Caption |
|---|---|---|
| fn-fujipet-new.png | Palm IIIc, OS 4.0 (colour) | New FujiPet: name Fuji, type Cat, with a colour preview. |
| fn-fujipet-new-mono.png | Palm V, OS 3.5 (greys) | New FujiPet on the grey Palm: name Palm, type Dog. |
| fn-fujipet-egg.png | Palm IIIc, OS 4.0 | Just hatched: Fuji's egg ("Keep Fuji's egg warm..."). |
| fn-fujipet-settings.png | Palm IIIc, OS 4.0 | Settings: friend server 127.0.0.1:7888, Turbo time, online and sounds on. |
| fn-fujipet-main.png | Palm IIIc, OS 4.0 | Fuji the child cat (Kitling) on the main screen, "1 friend online". |
| fn-fujipet-mono.png | Palm V, OS 3.5 (greys) | Palm the child dog (Pupling) on the grey Palm's main screen. |
| fn-fujipet-feed.png | Palm IIIc, OS 4.0 | The Feed popup: Meal or Snack. |
| fn-fujipet-game.png | Palm IIIc, OS 4.0 | The left-or-right game: "Round 1 of 5... Which way will it look?" |
| fn-fujipet-game-guess.png | Palm IIIc, OS 4.0 | The game after a guess: "Yes! Right guess!", with the pet looking right. |
| fn-fujipet-lights-off.png | Palm IIIc, OS 4.0 | Lights off: the night scene. |
| fn-fujipet-status.png | Palm IIIc, OS 4.0 | Status: Fuji the Cat, Child: Kitling, age, weight, Hungry/Happy hearts, Discipline, care mistakes. |
| fn-fujipet-menu.png | Palm IIIc, OS 4.0 | The Pet menu open over the main screen. |
| fn-fujipet-menu-options.png | Palm IIIc, OS 4.0 | The Options menu (over the egg). |
| fn-fujipet-about.png | Palm IIIc, OS 4.0 | The About alert. |
| fn-fujipet-friends-register.png | Palm IIIc, OS 4.0 | Friends before registering: "0 pets here. Tap Register!" |
| fn-fujipet-friends.png | Palm IIIc, OS 4.0 | Friends: Palm (Dog Child) online, with received and sent messages in the log. |
| fn-fujipet-friends-mono.png | Palm V, OS 3.5 (greys) | Friends on the second Palm: Fuji online, and the messages sent to it. |
| fn-fujipet-say.png | Palm V, OS 3.5 (greys) | The Say popup with its eight canned messages. |
| fn-fujipet-bubble.png | Palm V, OS 3.5 (greys) | A message arriving on the main screen: the "Fuji: Want to play?" bubble. |

**Use.**
- **New FujiPet.**
  - **Name your pet:** up to 10 letters and digits. A blank name gives "Give
    your pet a name: letters and digits, up to 10."
  - **Pick a type:** Cat, Dog, Bunny or Dino. The preview on the right shows
    the adult and the egg in that type's colours.
  - **Hatch!** starts the egg.
  - **Cancel** returns to the existing pet. It only appears when you already
    have one.
  - The screen notes "Hatches in 5 pet minutes; grows up over days."
- **Main screen.**
  - **Top icon bar:** **Feed** (popup: Meal or Snack), **Light** (on or off),
    **Play** (the left-or-right game), **Medicine**.
  - **Bottom icon bar:** **Clean** (removes droppings), **Status**, **Scold**,
    **Friends**.
  - **Pet area:** the name at top left and the pet's clock at top right. The
    pet walks and bobs. Droppings appear on the right. A red "!" at the top
    right means the pet is calling for something (hungry, unhappy, lights, or
    being naughty). A skull means it is sick. "Z z Z" means it is asleep.
  - **Status line:** short notices such as "Fuji is full!", "Fuji is sleepy.
    Lights off!", "Shh! Fuji is asleep." or "Wait for the egg to hatch."
    Otherwise it shows the network state: "n friends online", "n pets on the
    server", "Connecting...", "No friend server", "No FujiNet in the cradle"
    or "Offline".
  - **Taps:** tapping the pet makes it happy (a heart). Tapping a message
    bubble dismisses it. When the pet has died, tapping the screen starts a
    new egg.
- **Left-or-right game** (Play). It has five rounds and you need 3 to win.
  - Tap the left or right half of the screen, or press the hardware **Up**
    (left) or **Down** (right) button, to guess which way the pet will look.
  - Winning makes the pet happy: "Fuji won n of 5! Happy!". Losing gives
    "Only n of 5. Try again!".
  - Status and Friends still work during the game. Any other icon ends it.
- **Care rules** (pet.c):
  - An egg hatches into a baby after 5 pet minutes. The baby becomes a child
    after about an hour, a teen a day later and an adult two days after that.
    With good care it reaches senior after more than 10 days.
  - Hunger and happiness each drain a heart at a time.
  - A call left unanswered for 15 minutes counts as a care mistake. So does
    leaving the light on once the pet is asleep.
  - Mess and too many sweets make it sick, and medicine cures it.
  - Children and teens sometimes call for no reason. Scolding then teaches
    discipline; scolding at any other time upsets them.
  - Twelve hours starving or sick kills it.
  - Sleep hours: child 20:00 to 09:00, teen 21:00 to 09:00, adult 22:00 to
    09:00, senior 21:00 to 08:00.
  - The pet keeps living while the app is closed and catches up at the next
    launch (at most 30 days).
- **Status** (a modal form):
  - `<name> the <type>`
  - `<Stage>: <form name>`, for example Child: Kitling
  - `Age <days>   Weight <n>g`
  - **Hungry** and **Happy**, as 4 hearts each
  - **Discipline**, as 4 boxes
  - `Care mistakes: n`, or "Sick! It needs medicine."
  **Done** closes it.
- **Menu "Pet":**
  - **Status /S**
  - **Friends /F**
  - **New Egg...** asks "Start over with a new egg? <name> will be gone for
    good." with **New Egg** or **Cancel**.
- **Menu "Options":**
  - **Settings...**
  - **Reconnect /R** reconnects to the friend server.
  - **About**
- **Settings** (a modal form):
  - **Friend server (host:port):** default `localhost:7878`. A blank field
    falls back to that default.
  - **Time:** **Normal** (real time), **Fast** (60 pet minutes per real
    minute) or **Turbo** (1440, a pet day per real minute). Going back to
    Normal resets the pet's clock to the Palm's clock.
  - **Meet friends online:** connect to the friend server.
  - **Sounds:** beeps.
  - **OK** saves and reconnects if the server or the online setting changed.
    **Cancel** closes without saving.
- **Friends ("FujiPet Friends").**
  - **Top line:** "<name> is #<id>. n of m online.", or "n pets here. Tap
    Register!" before you register.
  - **List:** the registered pets, with the online ones first, in bold, with a
    filled dot. Each row shows type and stage, and age in days.
  - **Say:** a popup of eight canned messages: Hi, friend! / Want to play? /
    I'm hungry! / You're the best! / Let's be pals! / Yay! Hooray! / Good
    night! / Bye bye!
  - **Send** sends the chosen message to the selected pet. That pet must be
    online.
  - **Log:** the last 3 messages, as `<from>: <text>` or `You to <name>:
    <text>`.
  - **Done** returns to the main screen.
  - **Refresh** reloads the list, or reconnects if offline.
  - **Register** puts this pet on the server. It is hidden once the pet is
    registered.
  - A friendly message that arrives cheers the pet up (at most once a minute).
    It also pops up as a bubble on the main screen, and the status line shows
    when a friend comes online.
- **Friend server.** `tools/fujipet-server/fujipet_server.py`, a TCP line
  protocol, default port 7878, with a status page on 7879. The address is as
  FujiNet sees it, so use `localhost`/`127.0.0.1` with FujiNet-PC on the same
  Mac.

**Notes.**
- Colour needs Palm OS 3.5 *release* or later. On the IIIc 3.5 beta ROM the pet
  shows in black and white. On a Palm V with OS 3.5 the app picks 4-bit greys,
  in which the grass strip and the sky are nearly the same grey (see
  fn-fujipet-mono.png). On OS 3.1 to 3.3 it uses the 1-bit dither drawing.
- Turbo is fast: about 8 real seconds took the egg to a child (Kitling or
  Pupling). It was then asleep at 8:35 pm pet time, with droppings, and had
  made 2 care mistakes. Switching back to Normal moved the pet's clock to the
  Palm's 6 pm, and it woke at the next minute. Do not leave a pet on Turbo
  unattended: a pet day passes each minute, and twelve pet hours of starving
  kills it.
- On OS 4.0 the Menu silkscreen button sometimes opens only the menu bar,
  with no pulldown. Tap a menu title to drop it down.
- Status's Done button sits at y≈147, low on the screen.
- At launch with the default server (localhost:7878) the app reported "0 pets
  on the server". Something on this Mac was already listening on 7878, so the
  default can reach a server other than your own. Set the server in Settings.

## Shots not taken

- **FN Bounce World's World menu.** The world redraws over it (see above).
- **The HotSync itself** (HotSync / HotSync Now). The rig's FujiNet-PC has its
  HotSync server turned off, so the result would only be a failed sync.
