# Building PS5CEMU-HAR

`make release` builds everything from source on Linux and writes the app to `build/app/PPSA99360`
and `dist/PS5CEMU-HAR-v2.0.0.zip`. Ubuntu 24.04 is what it is built on; WSL works.

## Requirements

- `clang-18`, `lld-18` and the LLVM 18 tools
- `cmake`, `ninja`, `git`, `make` and `python3`
- For RADV, the Vulkan driver: `meson`, Python's `mako` and `packaging`, `rsync`,
  `glslangValidator`, and LLVM, Clang, libclc, SPIRV-Tools and the SPIR-V translator for Mesa's
  OpenCL kernels

On Ubuntu 24.04:

```bash
pip install meson mako packaging
sudo apt install rsync flex glslang-tools llvm-18-dev libclang-18-dev libclc-18-dev libllvmspirvlib-18-dev llvm-spirv-18 spirv-tools
```

## Targets

```bash
make radv      # RADV, the Vulkan driver
make azahar    # Azahar's core and its PS5 frontend (build/azahar)
make release   # the app (build/app/PPSA99360) and dist/PS5CEMU-HAR-v2.0.0.zip with its SHA256SUMS
make check     # the same build with a stand-in for RADV: checks everything else, not an app
```

`make help` lists every target. `make deps` fetches every input at the revision pinned in
`tools/deps.json`, then builds the libraries the emulators need for the PS5 into `build/sysroot`:

- Cemu, and Mihawk's PS5 build of Azahar (with its dynarmic and the submodules it builds with)
- the PS5 Native App Boilerplate (payload SDK, runtime, packaging tool)
- pacbrew's prebuilt PS5 libraries
- Boost, pugixml, libzip, glslang and RapidJSON
- Mihawk's PS5_Mesa, PS5_Vulkan and payload SDK fork
- compiler-rt's emulated TLS and CPU-model builtins
- the Cemu community graphic packs
- ProsperoEden

**RADV.** `make radv` builds the driver with PS5_Vulkan's own recipe, from the pinned Mesa fork and
the payload SDK fork, whose platform layer the PS5 winsys is built on. A RADV build made elsewhere
can be used instead: point `RADV_ARCHIVE` and `RADV_SDK` at it and at the SDK fork it was built
with. The link follows PS5_Vulkan's recipe for titles (`tools/link.sh`).

**Azahar.** `make azahar` (`tools/build-azahar.sh`) builds Azahar's core libraries with the same
PS5 toolchain as Cemu (`tools/ps5.cmake`), without its own frontends, and builds the app's PS5
frontend for it (`port/azahar`'s window, input, sound and in-game menu hook) in Azahar's tree. It
shares Cemu's copies of fmt, glslang, zstd and OpenSSL, so the app has one of each, and writes the
list of archives the app links to `build/azahar/azahar_ps5_libraries.txt`. Without that build, the
app still builds, and 3DS games say why they cannot start.

**Link check.** `make check` links with a driver stand-in instead of RADV. That proves everything
else compiles, links and packages, but its output (`build/app-check`) is not an app.

## How it fits together

| Path | What it is |
|---|---|
| `port/ps5/` | The console layer: the DualSense, VideoOut through Vulkan, logging, notifications, the sandbox escape, JIT memory and thread placement |
| `port/cemu/` | Cemu's platform classes for the PS5: the memory mapper, fibers, AudioOut, the DualSense controller, and a Microsoft-ABI bridge for the recompiler, since the PS5 target has no `ms_abi` |
| `port/app/` | Cemu's start-up without wxWidgets, the game list, game icons and box art, graphic packs, controller settings, installs, and what the app shows over a game: both in-game menus, the GamePad's screen and the touchpad's cursor |
| `port/azahar/` | Azahar's side: the 3DS library (read by the launcher itself), its controls, and its PS5 frontend (built in Azahar's tree): the window on VideoOut, the DualSense as the 3DS, AudioOut, CIA installs |
| `port/frontend/` | The launcher: the start screen and each side's screens, ProsperoEden's RmlUi layout drawn in software and shown on VideoOut through SDL, over the Homebrew Launcher's bubbles (`bubbles.cpp`) or the 3DS one's waves (`wave.cpp`) |
| `port/main_ps5.cpp` | The entry point: the sandbox escape, logs, Cemu's core, the launcher, the game |
| `patches/cemu/`, `patches/azahar/` | The port's changes to Cemu's and Azahar's own files |
| `tools/` | The dependencies, the builds, the PS5 link (`link.sh`), the packaging (`package.sh`), the artwork and the launcher's preview |
| `sce_sys/` | The title's `param.json` and its home screen background, `pic0.dds` |

### Changing Cemu or Azahar

The port's changes to each emulator are the patches in `patches/cemu/` and `patches/azahar/`, which
the build puts on a branch named `ps5` in `.deps/Cemu` and `.deps/PS5_Azahar`
(`tools/cemu-patches.sh apply`, `tools/azahar-patches.sh apply`). To change one, edit its checkout
on that branch, commit there, then run its script with `export` to write the patches again.

## Artwork

Everything the app and this repository show is drawn by a script, so it can be changed in one place
and drawn again:

| Script | What it draws |
|---|---|
| `tools/recolour-ui.py` | ProsperoEden's launcher artwork and stylesheet in each side's colours, dark blue and gold: its panels and rows drawn again from their SVGs. Run by `package.sh` |
| `tools/render-layout.py` | The launcher's layouts: the start screen, and each side's screens. Run by `package.sh` |
| `tools/render-background.py` | The Wii U Homebrew Launcher's background as a still picture, which the icons, the home screen background and the banner draw on. The launcher draws it moving (`port/frontend/bubbles.cpp`) |
| `tools/render-icons.py` | The home screen tile (`icon0.png`) and the launcher's icons: the GamePad on the bubbles, the 3DS on the waves, and the two side by side. Run by `package.sh` |
| `tools/render-presentation.py` | The home screen background (`sce_sys/pic0.dds`, installed as `pic0.dds` and `pic1.dds`): a 3840x2160 BC7 DDS it encodes itself. Needs Pillow, numpy and a bold sans-serif font; its output is committed, so the build does not |
| `tools/render-banner.py` | This repository's banner, `docs/banner.svg` |

All but `render-presentation.py` need only Python's standard library.

## The launcher on a PC

`tools/preview-launcher.sh` builds the launcher's own code for the PC, on RmlUi built for the PC,
with sample games, graphic packs and controllers in place of the emulators, and saves its screens as
PNGs in `build/preview`. A script (`tools/launcher-preview/screens.txt` by default) presses the
DualSense's buttons and says when to save a screen, so a change to the layout can be seen without a
console. It needs `make deps` first, and zlib's headers (`zlib1g-dev`). Box art can be tried by
putting TGAs in `build/preview/boxart/<wiiu|3ds>/<ID>.tga`.
