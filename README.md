# smacx

A native, cross-platform reconstruction of *Sid Meier's Alpha Centauri: Alien Crossfire*,
rebuilt from the original game and verified to play identically to it. It runs on Windows, macOS
and Linux.

You need your own copy of the game to load the original art, music, movies, text, and rules.

## Play

Download the package for your system from this repository's Releases page:

| Package | Run |
| --- | --- |
| `smacx-...-windows-x86_64.zip` | unzip, run `smacx.exe` |
| `smacx-...-macos-arm64.zip` | unzip, open `smacx.app` (the first time: right-click > Open) |
| `smacx-...-linux-x86_64.tar.gz` | unpack, run `./smacx` |

On first start smacx looks for your installed game and asks for its folder (the one containing
`terranx.exe`) if it can't find it. Settings and saves are kept in your user folder.

## Build from source

Install the tools:

| System | Install |
| --- | --- |
| macOS | `xcode-select --install`, then `brew install cmake sdl2 sdl2_ttf python` |
| Debian/Ubuntu | `sudo apt install build-essential cmake python3 libsdl2-dev libsdl2-ttf-dev` |
| Windows | MSYS2 CLANG64: `pacman -S make mingw-w64-clang-x86_64-{clang,cmake,ninja,python,SDL2,SDL2_ttf}` |

Then, in the repository folder:

```sh
make run        # build and start the game
make test       # run the tests
make clean      # start the build again from scratch
```

## Status

The whole game works: game setup, the map, units, bases, research, the design workshop,
diplomacy, the Planetary Council, reports, saving and loading, sound, music and movies.
Multiplayer works between copies of smacx: hotseat, play by email and online (TCP); it can't join
games with the original. Known gaps are listed in the [UI plan](docs/UI_PLAN.md) (section 13) and
[MULTIPLAYER.md](docs/MULTIPLAYER.md).

smacx fixes the original game's bugs by default ([the list](docs/COMPAT.md)); start it with
`--original-bugs` (or set `Original Bugs=1` in its ini) to play with every bug as the original does.

| | |
|---|---|
| ![Developed land, late game](docs/screenshots/map.png) | ![Base window](docs/screenshots/base.png) |
| ![Diplomacy](docs/screenshots/diplo.png) | ![Design workshop](docs/screenshots/design.png) |

Development details (how the reconstruction is verified, releases, layout): [DEVELOPMENT.md](docs/DEVELOPMENT.md).
