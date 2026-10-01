# PS5Cemu

**An unofficial port of [Cemu](https://github.com/cemu-project/Cemu), the Wii U emulator, to PlayStation 5 homebrew.**

PS5Cemu runs Cemu natively on a jailbroken PS5. It renders through Vulkan on the console's GPU with
RADV, Mesa's AMD Vulkan driver, running on a PS5 winsys. It starts into a launcher built from
[ProsperoEden](https://github.com/blackbearreloaded/ProsperoEden)'s design. All credit for the
emulator belongs to the Cemu team and its contributors. PS5Cemu is not affiliated with or endorsed
by the Cemu project, Nintendo or Sony.

> [!WARNING]
> **Status: builds into a complete app, not yet run on a console.** `make release` builds RADV,
> Cemu, the port and the launcher for the PS5 and packages a signed `eboot.bin` with everything
> the app needs. Nothing here has been tested on hardware yet. Expect the first console runs to
> need fixes.

## Features

- **Launcher.** ProsperoEden's layout, artwork and fonts, adapted for Wii U games and driven by the
  DualSense:
  - **Continue Playing** and **Recently Played**, with each game's own icon.
  - **Library:** title ID, version, update and DLC for each game.
  - **Settings:** video, audio, controls, diagnostics, and a folder browser for the game files.
- **Graphic packs per game.** The community graphic packs are bundled, and you turn them on or off
  and pick presets per game, as in Cemu's Graphic Packs window.
- **Vulkan on RADV.**
  - Cemu's Vulkan renderer runs on a `VK_KHR_display` surface on VideoOut.
  - Output is VideoOut's 3840x2160. Cemu scales the game's picture to it with the upscaling filter
    you choose (bicubic by default).
  - 120 Hz output on displays that take it.
- **The recompiler.** Cemu's x64 recompiler runs when etaHEN jailbreaks the process, which grants
  JIT memory. Without that, Cemu falls back to its interpreter, which is much slower.
- **DualSense.**
  - Player 1's controller is the Wii U GamePad. Other signed-in users get Pro Controllers, up to
    four players.
  - The touchpad is the GamePad's touch screen.
  - Motion sensors and vibration work.
- **Audio** through AudioOut. Surround is mixed down to stereo.

### In-game shortcuts

| Shortcut | Action |
|---|---|
| Touchpad click | Swap the TV and GamePad pictures |
| Touchpad click + R1 | Performance overlay |
| Touchpad click + L1, twice | Back to the library (PS5Cemu restarts) |

## Install

1. Build the app (below), or take a release ZIP. Copy its `PPSA99360` folder to
   `/data/homebrew/PPSA99360` on the console.
2. Load **etaHEN** and add `PPSA99360` to its app jailbreak list. This gives PS5Cemu `/data` and
   JIT memory. If the HEN doesn't jailbreak it, PS5Cemu asks elfldr to run its bundled
   `sandbox-elevator.elf`, which grants `/data` only, so games run on the interpreter.
3. Put your own Wii U game dumps in `/data/ps5cemu/games`, or choose another folder in
   **Settings > Game files**.
   - Accepted: `.wua`, `.wud`/`.wux`, and folders with `code`, `content` and `meta`.
   - Encrypted `.wud`/`.wux` dumps also need their disc keys in `/data/ps5cemu/keys.txt`.

PS5Cemu keeps everything it writes in `/data/ps5cemu`:

```text
/data/ps5cemu/
├── settings.xml, ps5cemu.json   Cemu's settings and the launcher's
├── controllerProfiles/          controller profiles
├── mlc01/                       the Wii U's storage: installed games, updates, DLC, saves
├── games/                       the default game files folder
├── keys.txt                     disc keys for encrypted dumps
├── graphicPacks/                community packs (downloadedGraphicPacks/) and your own
├── cache/                       shader and pipeline caches
├── covers/                      game icons for the launcher
├── log.txt                      Cemu's log
└── logs/boot.log                PS5Cemu's log (boot.prev.log: the session before)
```

PS5Cemu includes no games, keys, firmware or other copyrighted console data. Dump them from
hardware and software you own.

## Building

The build runs on Linux and needs:
- `clang-18`, `lld-18` and the LLVM 18 tools;
- `cmake`, `ninja`, `git`, `make` and `python3`;
- for RADV: `meson`, Python's `mako`, `rsync`, and LLVM, Clang, libclc, SPIRV-Tools and the SPIR-V
  translator for Mesa's OpenCL kernels. On Ubuntu 24.04: `pip install meson mako`, then
  `apt install rsync flex llvm-18-dev libclang-18-dev libclc-18-dev libllvmspirvlib-18-dev
  llvm-spirv-18 spirv-tools`.

```bash
make radv      # builds RADV, the Vulkan driver
make release   # build/app/PPSA99360 and dist/PS5Cemu-v0.1.0.zip
make check     # the same build with a stand-in for RADV: checks everything else (not an app)
```

`make help` lists every target. `make deps` fetches every input at the revision pinned in
`tools/deps.json`, then builds the libraries Cemu needs for the PS5 into `build/sysroot`. The
pinned inputs:
- Cemu
- the PS5 Native App Boilerplate (payload SDK, runtime, packaging tool)
- pacbrew's prebuilt PS5 libraries
- Boost, pugixml, libzip, glslang, RapidJSON, RmlUi
- Mihawk-99's PS5_Mesa, PS5_Vulkan and payload SDK fork
- compiler-rt's emulated TLS and CPU-model builtins
- the community graphic packs
- ProsperoEden

**RADV.** `make radv` builds the driver with PS5_Vulkan's own recipe, from the pinned Mesa fork
and the payload SDK fork, whose platform layer the PS5 winsys is built on. A RADV build made
elsewhere can be used instead: point `RADV_ARCHIVE` and `RADV_SDK` at it and the SDK fork it was
built with. The link follows PS5_Vulkan's recipe for titles (`tools/link.sh`).

`make check` links with a driver stand-in instead of RADV. That proves everything else compiles,
links and packages, but its output (`build/app-check`) is not an app.

### How it fits together

| Path | What it is |
|---|---|
| `port/ps5/` | The console layer: the DualSense, VideoOut through Vulkan, logging, notifications, sandbox escape and JIT |
| `port/cemu/` | Cemu's platform classes for the PS5: memory mapper, fibers, AudioOut, the DualSense controller, and a Microsoft-ABI bridge for the recompiler, since the PS5 target has no `ms_abi` |
| `port/app/` | Cemu's start-up without wxWidgets, the game list, game icons, graphic packs |
| `port/frontend/` | The launcher, on RmlUi's Vulkan renderer through RADV |
| `port/main_ps5.cpp` | The entry point: sandbox escape, logs, Cemu's core, the launcher, the game |
| `patches/cemu/` | The port's changes to Cemu's own files (`tools/cemu-patches.sh apply` or `export`) |
| `patches/rmlui/` | RmlUi's Vulkan renderer: its functions come from the driver instead of a loader, and it stops cleanly without a display surface |
| `tools/` | Dependencies, the builds, the PS5 link (`link.sh`) and the packaging (`package.sh`) |
| `sce_sys/` | The title's `param.json`. The icons are drawn by `tools/render-icons.py` |

To change Cemu, edit `.deps/Cemu` on its `ps5` branch, commit there, then run
`tools/cemu-patches.sh export`.

## Extras

`extras/ps5mfr` is [PS5 Make FSELF Recursive](https://github.com/alex-free/ps5-make-fself-recursive)
by Alex Free. It is kept in this repository with its own readme and licence.

## Credits

- **Cemu**, by the Cemu team and contributors (MPL-2.0).
- **ProsperoEden** by BlackBearReloaded: the launcher's design, artwork, fonts, bitmap font engine
  and folder browser, and the **PS5 Native App Boilerplate**: runtime, packaging tool, sandbox
  elevation.
- **Mihawk-99**: RADV on the PS5 (PS5_Mesa, PS5_Vulkan, the payload SDK fork), with
  **mpereiraesaa**'s contributions.
- **John Törnblom** (ps5-payload-dev): the PS5 payload SDK, and **pacbrew**'s PS5 libraries.
- **Swordpdf**: PS5SX2's etaHEN jailbreak request, which PS5Cemu follows.
- The authors of the **Cemu community graphic packs**.

## License

PS5Cemu's own code is GPL-3.0-or-later ([LICENSE](LICENSE)). Files derived from Cemu keep Cemu's
MPL-2.0, as their headers say, and third-party components keep their own licences.

**Disclaimer.** This is an independent homebrew project, provided as is, without warranty.
"PlayStation" and "PS5" are trademarks of Sony Interactive Entertainment, and "Wii U" is a
trademark of Nintendo. Running homebrew requires a modified console, which may void its warranty
or breach the platform's terms of service. Use it only with hardware and software you own.
