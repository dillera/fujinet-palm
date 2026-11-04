# Test assets

What the apps are tested with in
[CloudpilotEmu](https://github.com/cloudpilot-emu/cloudpilot-emu), with the
`--serial` bridge that joins the emulated cradle to a FujiNet on the same Mac
(`tools/rig.sh`): Palm OS ROMs in `roms/`, emulator images made from them in
`images/`, and the CloudpilotEmu patch in `cloudpilot/`.

The ROMs are Palm's, so neither they nor the images are in this repository:
git ignores `assets/roms/` and `assets/images/`. Download the ROMs from
PalmDB's [Palm OS Device ROMs](https://palmdb.net/app/palm-roms-complete)
collection into `assets/roms/`, under the names in [ROMs](#roms); the hashes
there are those of the files the apps were tested with. PalmDB has no Palm VII
ROM, so there is no Palm OS 3.2 image: the Palm VIIx (3.5) is the nearest.

Two of PalmDB's ROMs are not quite what their names say. The one PalmDB calls
`Palm-III-3.5-en.bin` is Palm OS 3.0 (its version string is "v. 3.0"), so it
is kept here as `Palm-III-3.0-en.bin`; it shows the apps' "needs Palm OS 3.1"
alert. `Palm-IIIc-3.50-en-beta.rom` is a pre-release 3.5: FN and FI FujiPet
turn on colour only for a released 3.5 or later, so on it they draw in black
and white. Use `palm-iiic-os4.0.img` for colour.

## Images

Each image has finished Setup (pen calibrated, country and time set) and sits
at the launcher. Copy one into a run directory and boot the copy, since
CloudpilotEmu writes to the image it runs.

To make one, boot its ROM, go through Setup, and save the image:

```sh
tools/rig.sh start runs/mk assets/roms/Palm-V-3.3-en.rom none
# calibrate the pen, set the country and time, return to the launcher
tools/rig.sh cmd runs/mk save-image $PWD/assets/images/palm-v-os3.3.img
tools/rig.sh stop runs/mk
```

| Image                     | Palm OS | Device as CloudpilotEmu runs it | Screen       |
|---------------------------|---------|---------------------------------|--------------|
| `palm-iii-os3.0.img`      | 3.0     | Palm III                        | 2-bit grey   |
| `palm-iiix-os3.1.img`     | 3.1     | Palm V (IIIx ROM)               | 2-bit grey   |
| `palm-v-os3.3.img`        | 3.3     | Palm V                          | 2-bit grey   |
| `palm-v-os3.5.img`        | 3.5     | Palm V (generic EZ ROM)         | 2-bit grey   |
| `palm-viix-os3.5.img`     | 3.5     | Palm VIIx                       | 2-bit grey   |
| `palm-m100-os3.5.1.img`   | 3.5.1   | m105 (m100 ROM)                 | 2-bit grey   |
| `palm-iiic-os3.5.img`     | 3.5 beta | Palm IIIc (beta ROM)           | 256 colours  |
| `palm-iiic-os4.0.img`     | 4.0     | Palm IIIc                       | 256 colours  |

```sh
mkdir -p runs/t35 && cp assets/images/palm-m100-os3.5.1.img runs/t35/palm.img
tools/rig.sh start runs/t35 runs/t35/palm.img nio      # or classic, or none
tools/rig.sh cmd runs/t35 install $PWD/apps/FujiNetTest/FujiNetTest.prc
tools/rig.sh cmd runs/t35 launch 'FI\ Test'
tools/rig.sh cmd runs/t35 screenshot a.png
tools/rig.sh stop runs/t35
```

Install from inside another app (Memo Pad, say), not while the launcher shows
the app's icon: on Palm OS 3.1 that ends in a "chunk under-locked" fatal error.
The CLI's `key` command drops characters beyond about ten at a time, so type
long text in short bursts.

## ROMs

| ROM                          | Palm OS | Source  | SHA-256 (first 16)  |
|------------------------------|---------|---------|---------------------|
| `Palm-IIIx-3.1.rom`          | 3.1     | earlier | `6c49768abc2aeeb1`  |
| `Palm-V-3.1-en.rom`          | 3.1     | PalmDB  | `3cba86ab018478e0`  |
| `Palm-III-3.3-en.rom`        | 3.3     | PalmDB  | `eaaf33e624c42b52`  |
| `Palm-V-3.3-en.rom`          | 3.3     | earlier | `d398aee04a707bd2`  |
| `Palm-III-3.0-en.bin`        | 3.0     | PalmDB  | `5e134bdd52e78921`  |
| `Palm-OS-3.5-en-ez.rom`      | 3.5     | PalmDB  | `6f5ea152a22e43e4`  |
| `Palm-VIIx-3.5.bin`          | 3.5     | PalmDB  | `711e8d3e93e80207`  |
| `Palm-IIIc-3.50-en-beta.rom` | 3.5     | PalmDB  | `a7bf15e8facc5432`  |
| `Palm-m100-3.51-en.rom`      | 3.5.1   | PalmDB  | `b70c4e8a082b4cce`  |
| `Palm-IIIc-4.0-en.rom`       | 4.0     | earlier | `f87ffeef2be87fe6`  |
| `Palm-m505-4.0-en.rom`       | 4.0     | earlier | `3c52a3ba757f685e`  |

## CloudpilotEmu

`cloudpilot/cloudpilot-serial-cli.patch` applies to cloudpilot-emu at
`064d5d5` ("Bump version."). It adds the native build's `--serial <pty>`
option, which bridges the cradle port to a host tty, and CLI commands for
driving it from a script: `screenshot`, `tap`, `pen`, `button`, `key`,
`install`, `launch`, `save-image` and the resets.

```sh
git clone https://github.com/cloudpilot-emu/cloudpilot-emu.git
cd cloudpilot-emu && git checkout 064d5d5
git apply /path/to/assets/cloudpilot/cloudpilot-serial-cli.patch
make -C src/cloudpilot -j8 bin    # builds src/cloudpilot/cloudpilot-emu
```
