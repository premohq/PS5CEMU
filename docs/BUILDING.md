# Building PS5Cemu

`make release` builds everything from source on Linux and writes the app to `build/app/PPSA99360`
and `dist/PS5Cemu-v0.2.0.zip`. Ubuntu 24.04 is what it is built on; WSL works.

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
make release   # the app (build/app/PPSA99360) and dist/PS5Cemu-v0.2.0.zip with its SHA256SUMS
make check     # the same build with a stand-in for RADV: checks everything else, not an app
```

`make help` lists every target. `make deps` fetches every input at the revision pinned in
`tools/deps.json`, then builds the libraries Cemu needs for the PS5 into `build/sysroot`:

- Cemu
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

**Link check.** `make check` links with a driver stand-in instead of RADV. That proves everything
else compiles, links and packages, but its output (`build/app-check`) is not an app.

## How it fits together

| Path | What it is |
|---|---|
| `port/ps5/` | The console layer: the DualSense, VideoOut through Vulkan, logging, notifications, the sandbox escape and JIT memory |
| `port/cemu/` | Cemu's platform classes for the PS5: the memory mapper, fibers, AudioOut, the DualSense controller, and a Microsoft-ABI bridge for the recompiler, since the PS5 target has no `ms_abi` |
| `port/app/` | Cemu's start-up without wxWidgets, the game list, game icons, graphic packs, and what the port shows over a game: its menu, the GamePad's screen and the touchpad's cursor |
| `port/frontend/` | The launcher: ProsperoEden's RmlUi layout, drawn in software and shown on VideoOut through SDL, as ProsperoEden does |
| `port/main_ps5.cpp` | The entry point: the sandbox escape, logs, Cemu's core, the launcher, the game |
| `patches/cemu/` | The port's changes to Cemu's own files |
| `tools/` | The dependencies, the builds, the PS5 link (`link.sh`), the packaging (`package.sh`) and the artwork |
| `sce_sys/` | The title's `param.json` and its home screen background, `pic0.dds` |

### Changing Cemu

The port's changes to Cemu are the patches in `patches/cemu/`, which the build puts on a branch
named `ps5` in `.deps/Cemu` (`tools/cemu-patches.sh apply`). To change Cemu, edit `.deps/Cemu` on
that branch, commit there, then run `tools/cemu-patches.sh export` to write the patches again.

## Artwork

Everything the app and this repository show is drawn by a script, so it can be changed in one place
and drawn again:

| Script | What it draws |
|---|---|
| `tools/render-background.py` | The launcher's backgrounds: the Wii U Homebrew Launcher's blue gradient and discs, under a dark overlay. Run by `package.sh` |
| `tools/render-icons.py` | The home screen tile (`icon0.png`) and the launcher's icons: the GamePad on that background. Run by `package.sh` |
| `tools/render-presentation.py` | The home screen background (`sce_sys/pic0.dds`, installed as `pic0.dds` and `pic1.dds`): a 3840x2160 BC7 DDS it encodes itself. Needs Pillow, numpy and a bold sans-serif font; its output is committed, so the build does not |
| `tools/render-banner.py` | This repository's banner, `docs/banner.svg` |

The first two need only Python's standard library.
