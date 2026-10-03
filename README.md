<p align="center">
  <img src="docs/banner.svg" alt="PS5CEMU-HAR: Cemu and Azahar, the Wii U and Nintendo 3DS emulators, on PlayStation 5 homebrew" width="100%">
</p>

<p align="center">
  <strong>Wii U and Nintendo 3DS emulation in one PlayStation 5 homebrew app</strong><br>
  <a href="#install">Install</a> · <a href="#ps5-cemu-wii-u">Wii U</a> · <a href="#ps5-azahar-nintendo-3ds">3DS</a> ·
  <a href="#controls">Controls</a> · <a href="docs/BUILDING.md">Building</a> · <a href="#credits">Credits</a>
</p>

**PS5CEMU-HAR** puts two emulators in one app for jailbroken PS5 consoles running etaHEN:

- **PS5 CEMU**, a port of [Cemu](https://github.com/cemu-project/Cemu), the Wii U emulator;
- **PS5 AZAHAR**, a port of [Azahar](https://github.com/azahar-emu/azahar), the Nintendo 3DS emulator.

Both render with Vulkan on Mihawk's RADV driver for the PS5, and both are played with the DualSense.
When the app starts, its screen is split down the middle: Cemu on the left, Azahar on the right. Pick
one and you get that emulator's launcher, in its own colours: Cemu's in dark blue over the Wii U
Homebrew Launcher's rising bubbles, Azahar's in gold over the 3DS Homebrew Launcher's waves.

This is an unofficial project. All credit for the emulators belongs to the Cemu and Azahar teams and
their contributors; it is not affiliated with or endorsed by them, Nintendo or Sony.

> [!NOTE]
> **Status.** The Wii U side has been played on a console (The Wind Waker HD runs, with video,
> controls, sound and saves). The 3DS side is new in 2.0.0 and has not been tested widely on real
> hardware yet: expect rough edges, and please report what you find with the logs (see
> [Problems](#problems)).

## Install

1. Download the release ZIP and extract it, or [build it yourself](docs/BUILDING.md).
2. Copy the `PPSA99360` folder to `/data/homebrew/PPSA99360` on the PS5.
3. Load **etaHEN** and add `PPSA99360` to its app jailbreak list. This gives the app `/data` and the
   JIT memory both emulators' recompilers need. Without it, the app asks elfldr to run its bundled
   `sandbox-elevator.elf`, which grants `/data` only, and Wii U games run on Cemu's much slower
   interpreter.
4. Put your own game dumps in place (see [Game files](#game-files)).
5. Start **PS5CEMU-HAR** from the home screen, choose a side, and open its **Library**.

**Updating.** Copy a new release's `PPSA99360` folder over the old one; everything you have in
`/data/ps5cemu` stays. The PS5 keeps the app's name, tile and background picture from when the app
was registered, so to see new ones, register the app again in the loader you installed it with.

## The start screen and the launchers

| | |
|---|---|
| Left / Right, Cross | Choose PS5 CEMU or PS5 AZAHAR |
| Circle (on a launcher's home) | Back to the start screen |

The app opens on the side you played last. Each launcher has the same parts:

- **Continue playing** and **Recently played**, from the home screen.
- **Library**: every game with its icon beside its name, and on the right its box art, downloaded
  from [GameTDB](https://www.gametdb.com/) the first time the library lists the game (see
  [Box art](#box-art)), or its icon when GameTDB has none.
- **Settings**: video, audio, controls (every button can be set by pressing it), the game files
  folder, installing, and diagnostics.
- **About**: credits, and where the app keeps its files.

## PS5 CEMU (Wii U)

- **Native Cemu.** Its x64 recompiler runs in the JIT memory etaHEN grants.
- **Vulkan at 4K.** Cemu's Vulkan renderer on RADV at the console's 3840x2160 output, with the
  upscaling filter you choose (Bicubic by default), and 120 Hz on displays that take it.
- **Game files anywhere.** WUA, WUD/WUX and unpacked games, in any folder the PS5 can read.
- **Graphic packs per game.** The community graphic packs are included and listed in their folders,
  as in Cemu's Graphic Packs window, with a dropdown for each kind of preset: Breath of the Wild at
  16:9 and 2560x1440 is two choices.
- **Controllers.** Player 1's DualSense is the Wii U GamePad and other signed-in players get Pro
  Controllers, up to four, until you choose otherwise: GamePad, Pro Controller, Classic Controller,
  or a Wii Remote with or without a Nunchuk, with motion controls, vibration, stick deadzones and
  buttons for each.
- **Both screens.** The TV's picture or the GamePad's as the main one, and the other one in a corner
  when you want it.
- **The touchpad is the GamePad's touch screen**, with a cursor where your finger is.
- **Updates and DLC** install into the Wii U's storage from **Settings > Install updates and DLC**;
  those in the game files folder or in a WUA are found as they are.
- **Text entry** with Cemu's keyboard, typed with the D-pad or the touchpad.

## PS5 AZAHAR (Nintendo 3DS)

- **Azahar's core**, from Mihawk's PS5 build of it, with dynarmic's ARM recompiler running in the
  console's executable memory.
- **Vulkan at 4K.** Azahar's Vulkan renderer on RADV, an internal resolution from 1x to 10x
  (2400x1440 at 6x, the default), texture filters (Anime4K, Bicubic, ScaleForce, xBRZ, MMPX), and
  shaders compiled in the background so games keep their speed.
- **The two screens on one TV**: the top one large with the bottom one beside it, the top one alone,
  the two side by side, or one above the other, and either screen in the top one's place.
- **The DualSense as the 3DS.** A on Circle and B on Cross as on the 3DS (or the other way round),
  the circle pad on the left stick, the C-stick on the right stick, ZL and ZR on L2 and R2, and the
  gyroscope and accelerometer as the 3DS's motion sensors. Every button can be set in **Settings >
  Controls**. A New 3DS is emulated.
- **The touchpad is the bottom screen.** Your finger moves a cursor over it; click to touch, keep it
  clicked to drag.
- **Install CIA files** (games, updates and DLC) into the emulated SD card from **Settings > Install
  CIA files**. Installed games appear in the library.
- **Sound** through the PS5's audio output.

## Game files

**Wii U**, in `/data/ps5cemu/games`, or any folder chosen in **Settings > Game files**:

```text
<game files folder>/
├── Game.wua                        # a Wii U archive: the game, its update and DLC in one file
├── Game.wux                        # or Game.wud
└── Game/                           # an unpacked game
    ├── code/
    ├── content/
    └── meta/
```

Encrypted WUD and WUX dumps also need their disc keys in `/data/ps5cemu/keys.txt`.

**Nintendo 3DS**, in `/data/ps5cemu/azahar/games`, or any folder chosen on the 3DS side:

- `.3ds`, `.cci`, `.cxi` and `.app` dumps, `.3dsx` and `.elf` homebrew, and Azahar's compressed
  `.z3ds`, `.zcci`, `.zcxi` and `.z3dsx`, are played as they are.
- `.cia` files are **installed**, not played: install them from **Settings > Install CIA files**,
  and the installed game appears in the library. Update and DLC CIAs are installed the same way and
  apply to their game.
- Encrypted dumps need your own console's `aes_keys.txt` in `/data/ps5cemu/azahar/sysdata`; the app
  includes no keys. Decrypted dumps need none.

The app includes no games, keys, firmware or other copyrighted console data. Dump them from hardware
and software you own. Do not download or redistribute them.

## Box art

The first time a library lists a game, the app asks GameTDB (`art.gametdb.com`) for its cover, by the
ID printed on the game's box (`ALZE01` for Breath of the Wild, `AREE` for Super Mario 3D Land),
over plain HTTP, one game at a time in the background. Covers are kept in
`/data/ps5cemu/covers/boxart`. A game GameTDB has no cover for is remembered and not asked for
again; delete its `.none` file there to ask again. Without a network connection, nothing is
downloaded and the libraries show the games' icons.

## Controls

| In a Wii U game | |
|---|---|
| Touchpad | A cursor on the GamePad's screen: click to touch, keep it clicked to drag |
| Touchpad click + Options | The PS5 CEMU menu |
| Touchpad click + L1 | The TV or the GamePad as the main screen |
| Touchpad click + R1 | The other screen in a corner, or not |

| In a 3DS game | |
|---|---|
| Touchpad | A cursor on the bottom screen: click to touch, keep it clicked to drag |
| Touchpad click + Options | The PS5 AZAHAR menu |
| Touchpad click + L1 | Swap the screens |
| Touchpad click + R1 | The next screen layout |

The in-game menus look like the launchers. The D-pad moves, Cross chooses, Left and Right change a
setting, and Circle goes back to the game. They change the screens, the picture, the volume and the
controls while you play, show a performance overlay, and take you back to the library (press Cross
twice; unsaved progress is lost). Changes are kept for the next games.

## Where things are kept

Everything the app writes is in `/data/ps5cemu`, apart from your game files:

```text
/data/ps5cemu/
├── ps5cemu.json                    the launchers' settings
├── settings.xml                    Cemu's settings
├── controllerProfiles/             Cemu's controller profiles
├── mlc01/                          the Wii U's storage: installed updates and DLC, saves
├── games/                          the default Wii U game files folder
├── keys.txt                        disc keys for encrypted Wii U dumps
├── graphicPacks/                   community graphic packs and your own
├── cache/                          Cemu's shader and pipeline caches
├── azahar/
│   ├── games/                      the default 3DS game files folder
│   ├── sdmc/                       the 3DS's SD card: installed CIAs, saves, extra data
│   ├── nand/                       the 3DS's system storage
│   ├── sysdata/                    aes_keys.txt, if you add it
│   ├── shaders/                    Azahar's shader cache
│   └── log/azahar_log.txt          Azahar's log
├── covers/                         game icons for the launchers, and box art (boxart/)
├── log.txt                         Cemu's log
└── logs/boot.log                   the app's log (boot.prev.log: the session before)
```

**Wii U saves from Cemu on a PC.** Copy a game's save folder from your PC's
`mlc01/usr/save/00050000/<title ID>/user/<account>` to
`/data/ps5cemu/mlc01/usr/save/00050000/<title ID>/user/80000001` while that game is not running.

**3DS saves from Azahar or Citra on a PC.** Copy the game's
`sdmc/Nintendo 3DS/<ID0>/<ID1>/title/00040000/<title ID>/data` folder into the same place under
`/data/ps5cemu/azahar/sdmc/Nintendo 3DS/` (Azahar's IDs are all zeros on both).

## Problems

**Settings > Diagnostics** shows the app's version and where its logs are. When you report a
problem, attach `/data/ps5cemu/logs/boot.log`, and `log.txt` (Wii U) or
`azahar/log/azahar_log.txt` (3DS). The boot log also has a `[memory]` line once a minute during a
game, which shows whether memory keeps growing.

## What's new in 2.0.0

- **Azahar joins Cemu.** The app is now PS5CEMU-HAR: a start screen splits Cemu and Azahar, and the
  3DS side has its own gold launcher, library, settings, controls, CIA installs and in-game menu.
- **Game icons and box art** in both libraries: each game's icon beside its name, its box art from
  GameTDB on the right.
- **Wii U performance as in 1.0.** Two experiments that made Breath of the Wild slower on a console
  were taken back: Cemu's timers on the CPU's time-stamp counter, and pinning the emulated CPU cores
  to console cores. Pinning can still be tried with `"pinCpuThreads": true` in
  `/data/ps5cemu/ps5cemu.json`.
- **Leaks fixed.** Recompiled code's memory is given back with its handle, a start-up probe gives its
  memory back, and a signed-in user without a controller no longer adds a log line every two
  seconds.
- **Controls in the in-game menu**, for each player.
- **One version everywhere.** The launcher, Diagnostics and the PS5's own information all say 2.0.0;
  Diagnostics names the Cemu and Azahar revisions the app is built from.

**Earlier releases.** 1.0.0 made the launcher dark blue with rising bubbles, added graphic pack
dropdowns, controller settings and update and DLC installs. 0.2.0 made games run at their own speed
and added the GamePad screen options, the touch cursor, the in-game menu and the keyboard. 0.1.0 was
the first version that started on a console.

## Known limitations

- Going back to the library restarts the app, and the game keeps running behind the in-game menus.
- The 3DS camera and microphone, local wireless and online play are not emulated on the PS5.
- A CIA install cannot be cancelled once it has started.
- There is no setting yet to turn box art downloads off.
- The DualSense's motion axes have not been checked against every game.

## Building

Run `make release` on Linux (Ubuntu 24.04; WSL works). It fetches every dependency at its pinned
revision, builds Azahar's core, Cemu and the launcher, and writes the app to `build/app/PPSA99360`
and a ZIP to `dist/`. See [docs/BUILDING.md](docs/BUILDING.md) for the details, and `make help` for
the other targets.

The source is all here: the console layer, the emulators' PS5 frontends, the launcher and the in-game
menus in `port/`; the port's changes to Cemu and to Azahar in `patches/`; and the build, packaging
and artwork tools in `tools/`. `extras/ps5mfr` is Alex Free's
[PS5 Make FSELF Recursive](https://github.com/alex-free/ps5-make-fself-recursive), kept with its own
readme and licence.

## Credits

- **[Cemu](https://github.com/cemu-project/Cemu)**, by the Cemu team and contributors (MPL-2.0).
- **[Azahar](https://github.com/azahar-emu/azahar)**, by the Azahar team and the Citra contributors
  before them (GPL-2.0-or-later), with **dynarmic** by MerryMage and contributors.
- **Mihawk**: RADV on the PS5 ([PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa),
  [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan), the payload SDK fork and its platform layer),
  and the PS5 builds of Azahar and dynarmic ([PS5_Azahar](https://github.com/mihawk-99/PS5_Azahar)),
  with **mpereiraesaa**'s contributions.
- **BlackBearReloaded**: [ProsperoEden](https://github.com/blackbearreloaded/ProsperoEden), whose
  design, artwork, fonts and software drawing the launchers are built on, and the
  [PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate): the
  app's runtime, packaging and sandbox elevation.
- **Dimok**: the Wii U Homebrew Launcher, whose background the Cemu side uses.
- **The 3DS Homebrew Launcher**'s authors, whose waves the Azahar side's background recalls.
- **John Törnblom**: the [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk), and **pacbrew**'s
  PS5 libraries.
- **Swordpdf**: the etaHEN jailbreak request from PS5SX2, which this app uses too.
- **[GameTDB](https://www.gametdb.com/)** and its contributors, for the box art.
- The authors of the **[Cemu community graphic packs](https://github.com/cemu-project/cemu_graphic_packs)**.

## License

The app's own code is licensed under GPL-3.0-or-later; see [LICENSE](LICENSE). Files derived from
Cemu keep Cemu's MPL-2.0, as their headers say; Azahar is GPL-2.0-or-later, and the app that
includes it is distributed under GPL-3.0-or-later. Other third-party components keep their own
licences.

## Disclaimer

- **No affiliation.** This is an independent homebrew project. It is not affiliated with, endorsed
  by, or sponsored by Sony Interactive Entertainment, Nintendo, the Cemu project or the Azahar
  project. "PlayStation" and "PS5" are trademarks of Sony Interactive Entertainment Inc.; "Wii U" and
  "Nintendo 3DS" are trademarks of Nintendo.
- **No proprietary material.** No Sony or Nintendo SDK, firmware, encryption keys, games or
  decrypted system modules are included.
- **No warranty.** This project is provided "as is", without warranty of any kind, to the extent
  permitted by law. See sections 15 and 16 of the GPL.
- **Use at your own risk.** Running homebrew requires a modified console, which may void its
  warranty, breach the platform's terms of service, or cause data loss.
- **Legal use only.** Use it only with hardware, accounts and content you own. This project does
  not support or enable piracy.

## AI assistance

This project was developed with AI assistance from Anthropic's Claude.
