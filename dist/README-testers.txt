FujiNet on the Palm: head-to-head speed test
=============================================
Build @VERSION@, @DATE@

These apps time the same web downloads through each way a Palm can reach
FujiNet, and upload the numbers to one shared server, so runs from different
Palms and setups can be compared side by side:

    http://fujinet.online:8199/h2h/report


WHICH APP
---------
Install the one(s) that match your setup (HotSync, or however you install
.prc files):

  FN_H2H.prc     Palm in a serial cradle, FujiNet running the classic
                 firmware with Palm support.
  FI_H2H.prc     Palm in a serial cradle, FujiNet running fujinet-nio.
  FI_H2H_SB.prc  Handspring Visor with the Springboard NIO module.
  FI_H2H_NL.prc  The same Visor setup, through the NioLib shared library.
                 Needs NioLib.prc as well (see below).

All four can sit on one Palm side by side.


RUNNING A TEST
--------------
1. Open the app. Target should read http://fujinet.online:8199/h2h. Your
   FujiNet makes the connection, so it needs to reach the internet.
   Name starts as your Palm's HotSync name: change it to the name you want
   your runs under, and use the same one in every app. Send needs a name.
2. Leave Reps at 3, and 64K and Sweep ticked, for a full run. Tap Run.
   A full run takes several minutes; tap the screen to stop it early.
   Ping runs only the link test, with no internet involved.
3. When it finishes, the results appear. Tap Send to upload them to the
   server, and Memo to save a copy in Memo Pad. Done goes back, and Results
   shows them again.
4. Look for your run on the report page above. The server's front page,
   http://fujinet.online:8199/h2h/, lists every run by tester; your name
   there shows your latest run of each app side by side.

The results last only while the app stays open: Send (or Memo) before you
switch apps. If Send fails, tap it again.

Any other URL in Target works too (http://example.com/, say): the app then
times that page, but those results can't be sent.

Please tell us, with your run: which Palm (model and OS version), which
FujiNet (hardware, and firmware or NIO version), and anything odd you saw.
The report shows the FujiNet's firmware or NIO version as the Palm saw it.


SPRINGBOARD NOTES
-----------------
- Run FI H2H SB and FI H2H NL from RAM (installing them puts them there).
  Each run is labelled "app in RAM".
- On the computer that runs NIO for the module, set the packet link to 8192
  bytes in NIO's fujinet.yaml:
      channel:
        packet_link:
          capacity: 8192
  The default, 4096, drops any reply over 4 KB, so the sweep's largest sizes
  would time out.
- FI H2H NL calls NioLib, which must be installed in RAM. NioLib.prc here is
  built from fujinet-nio-lib @NIOLIB@, which includes a fix
  for misread Springboard registers (without it, runs show the odd I/O
  error). It has no USB transport: if you rely on NioLib's USB transport,
  keep your own NioLib and skip this one. NioLib uses its Springboard
  transport unless NIO Config saved another; FI H2H NL's results are
  labelled as Springboard runs either way.


WHAT'S BEEN TESTED
------------------
FI H2H SB and FI H2H NL have run on a Visor Prism with the Springboard NIO
module. FN H2H and FI H2H have so far only run in an emulator (CloudpilotEmu,
Palm IIIc, Palm OS 3.5): yours may be the first real run, so a note of what
happened is especially useful.


SOURCE
------
Built from:
  head-to-head-test  @VERSION@
  fujinet-nio-lib    @NIOLIB@
  fujinet-palm       @CLASSIC@ (FN H2H's FujiBus code)
fujinet-nio-lib and fujinet-palm are GPL-3.0, and so are these builds.
