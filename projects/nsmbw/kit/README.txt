New Super Mario Bros. Wii - build kit
=====================================

This kit builds the game for your PC from YOUR OWN copy of the game. It holds the port's source code
and a script that does every step for you; none of the game is in it or downloaded by it.

You need
--------
- New Super Mario Bros. Wii for the USA (game ID SMNE01), REVISION 1, extracted with Dolphin:
  right-click the game in Dolphin's list > Properties > Info shows the ID and revision; then
  right-click > Extract Entire Disc into an empty folder. Other regions and revisions are refused.
- A 64-bit x86 PC (Windows 10/11, or Linux), 8 GB of RAM or more, about 20 GB of free disk space,
  and an internet connection the first time (about 2 GB of compiler tools and libraries).
- Time: the first build translates and compiles the whole game, which can take an hour or more
  depending on the PC. Later builds only redo what changed.

Windows
-------
1. Extract this zip to a short path, for example C:\NSMBW-kit (long paths can break the build).
2. Double-click Build-NSMBW.cmd. If Windows asks whether to run it, choose Run (or More info >
   Run anyway).
3. Pick your extracted game folder (the one with "files" and "sys") when asked.
The game ends up in the NSMBW folder next to this file: run NSMBW.exe.
Python and the .NET 8 SDK are fetched just for the build when the PC doesn't have them.

Linux
-----
1. Extract: tar -xzf NSMBW-*-linux-x86_64-builder.tar.gz
2. Run: ./build-nsmbw.sh --game /path/to/your/extracted/game
   It tells you which packages to install if the PC lacks any (python3, curl, xz, unzip,
   pkg-config, and the X11, ALSA and Vulkan development files).
The game ends up in the NSMBW folder next to this file: run ./NSMBW.

Notes
-----
- Keep your extracted game folder: the game reads its levels, music and graphics from it. The
  build links it as the "game" folder beside the game.
- Older processors without AVX2 get the compatible version automatically (or pass -Compat on
  Windows, --compat on Linux).
- Build logs are in source/native-build/logs if something goes wrong.
- More detail on every step: source/projects/nsmbw/BUILDING.md.
