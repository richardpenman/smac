OpenSMAC - Sid Meier's Alpha Centauri: Alien Crossfire, native
===========================================================

OpenSMAC is a native port of Sid Meier's Alpha Centauri with the Alien Crossfire expansion
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

The game folder is the one that contains terranx.exe. OpenSMAC needs terranx.exe v2.0
(md5 cc659f53cb6fa0eaaddccd0a54176647), the version GOG sells.


2. Run OpenSMAC
------------

  - Windows: unzip, then run opensmac.exe.
  - macOS: unzip, then open OpenSMAC.app. The app is not notarized: the first time, right-click
    it and choose Open (or run: xattr -dr com.apple.quarantine OpenSMAC.app).
  - Linux: unpack, then run ./opensmac. It needs an X11 or Wayland desktop; the SDL2 libraries
    are included in lib/.


3. The first run
----------------

OpenSMAC looks for the game in the usual install locations:

  - Windows: GOG's installations (the registry), C:\GOG Games, Program Files, Steam.
  - macOS: GOG's Mac app in /Applications or ~/Applications (the game inside the app),
    ~/GOG Games, CrossOver bottles.
  - Linux: ~/GOG Games, ~/Games (Lutris, Heroic), Wine prefixes (~/.wine), Bottles,
    Steam/Proton.

If it does not find the game, it asks you for the game folder. It remembers the folder,
then reads the game's initial data from terranx.exe once and keeps it for later runs.
To use another folder later, start OpenSMAC with --gamedir FOLDER (or delete opensmac.cfg, see
below).

Your settings (Alpha Centauri.Ini), saved games, the Hall of Fame and screenshots are kept
in your own folder:

  - Windows: %APPDATA%\OpenSMAC
  - macOS:   ~/Library/Application Support/OpenSMAC
  - Linux:   ~/.local/share/opensmac

OpenSMAC was called smacx up to version 0.2: on its first start it moves smacx's folder
(%APPDATA%\smacx, ~/Library/Application Support/smacx, ~/.local/share/smacx) to the new
one, so your settings and saved games carry over. The game folder itself is only read.


4. Screen
---------

OpenSMAC starts fullscreen at your display's size: the map fills the screen, the game's
windows keep their size and the interface sits at the bottom centre. On large displays every
pixel of the game is shown as 2 x 2 (or 3 x 3) pixels, so it stays sharp and readable (a 4K
display shows a 1920 x 1080 screen at twice the size). Alt+Enter switches between fullscreen
and a window, and is remembered. A window can be resized: the picture is scaled to fit.

Settings in Alpha Centauri.Ini, section [Alpha Centauri] (read at start):

  Screen Size=auto     the game's screen in pixels, e.g. 1920x1080 (auto: the display's
                       size divided by the UI Scale; at least 1024x768)
  UI Scale=auto        1, 2 or 3: the size of each game pixel on the display (auto: the
                       largest that leaves the game at least 768 pixels high)
  Fullscreen=1         0: in a window

The start option --size WxH (e.g. --size 1280x1024) overrides them for one run, in a window.


5. Notes
--------

  - Single player only; multiplayer is not supported.
  - Alpha Centauri.Ini in this package is the default settings file; your own copy is made
    from it on the first run.
  - The original game, its data and its trademarks belong to their owners. OpenSMAC is not
    affiliated with Firaxis, 2K or GOG.

Licence: see LICENSE.txt (OpenSMAC) and licenses/ (SDL2 and SDL2_ttf, zlib licence). SDL2_ttf
includes FreeType, (c) The FreeType Project, www.freetype.org (FreeType License); the
Windows SDL2_ttf.dll also includes HarfBuzz (MIT licence).
