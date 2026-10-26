smacx - Sid Meier's Alpha Centauri: Alien Crossfire, native
===========================================================

smacx is a native port of Sid Meier's Alpha Centauri with the Alien Crossfire expansion
(v2.0) for Windows, macOS and Linux, from a reconstruction of the game's source code.
It plays the single-player game with your own copy of the game's files: this package
contains no game data.


1. Get the game
---------------

You need an installed copy of Sid Meier's Alpha Centauri Planetary Pack (Alpha Centauri
with Alien Crossfire, v2.0), e.g. from GOG.com:

  - Windows: install it with GOG's installer (or GOG Galaxy).
  - macOS: install GOG's Mac version (an app with the Windows game inside), or copy the
    game folder of a Windows install to your Mac.
  - Linux: install GOG's Windows installer with Wine, Lutris, Heroic or Bottles, or copy the
    game folder of a Windows install.

The game folder is the one that contains terranx.exe. smacx needs terranx.exe v2.0
(md5 cc659f53cb6fa0eaaddccd0a54176647), the version GOG sells.


2. Run smacx
------------

  - Windows: unzip, then run smacx.exe.
  - macOS: unzip, then open smacx.app. The app is not notarized: the first time, right-click
    it and choose Open (or run: xattr -dr com.apple.quarantine smacx.app).
  - Linux: unpack, then run ./smacx. It needs an X11 or Wayland desktop; the SDL2 libraries
    are included in lib/.


3. The first run
----------------

smacx looks for the game in the usual install locations:

  - Windows: GOG's installations (the registry), C:\GOG Games, Program Files, Steam.
  - macOS: GOG's Mac app in /Applications or ~/Applications (the game inside the app),
    ~/GOG Games, CrossOver bottles.
  - Linux: ~/GOG Games, ~/Games (Lutris, Heroic), Wine prefixes (~/.wine), Bottles,
    Steam/Proton.

If it does not find the game, it asks you for the game folder. It remembers the folder,
then reads the game's initial data from terranx.exe once and keeps it for later runs.
To use another folder later, start smacx with --gamedir FOLDER (or delete smacx.cfg, see
below).

Your settings (Alpha Centauri.Ini), saved games, the Hall of Fame and screenshots are kept
in your own folder:

  - Windows: %APPDATA%\smacx
  - macOS:   ~/Library/Application Support/smacx
  - Linux:   ~/.local/share/smacx

The game folder itself is only read.


4. Notes
--------

  - Single player only; multiplayer is not supported.
  - Alpha Centauri.Ini in this package is the default settings file; your own copy is made
    from it on the first run.
  - The original game, its data and its trademarks belong to their owners. smacx is not
    affiliated with Firaxis, 2K or GOG.

Licence: see LICENSE.txt (smacx) and licenses/ (SDL2 and SDL2_ttf, zlib licence). SDL2_ttf
includes FreeType, (c) The FreeType Project, www.freetype.org (FreeType License); the
Windows SDL2_ttf.dll also includes HarfBuzz (MIT licence).
