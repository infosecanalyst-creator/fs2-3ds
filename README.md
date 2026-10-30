# FreeSpace 2 for the New Nintendo 3DS

A port of fs2_open 3.6.10 to the New 3DS / New 3DS XL.

**Bring your own game files.** This repository contains no FreeSpace 2 game data. You need your own copy of FreeSpace 2 (GOG or Steam).

## Features

- **Flight:** the cockpit view and HUD are on the top screen, rendered at the LCD's native 400×240. The touchscreen holds a cockpit panel: target info, ship status, energy (ETS) and quick-order buttons.
- **Menus and briefings:** shown on the top screen at a readable zoom, with a view that follows the pointer. The Circle Pad or C-Stick moves the pointer, A clicks, and L/R zoom out and in. The touchscreen shows a dashboard with pilot and campaign info, the name of whatever the pointer is over, help for the current screen, and BACK / ENTER / ZOOM / MANUAL buttons.
- **Built-in manual:** open it with SELECT + START. The text is on the top screen and the sections are on the touchscreen. Circle Pad or C-Stick scrolls, the D-Pad picks a section, L/R moves to the previous or next section, and B or START closes it.
- **Renderer:** picaGL, a GL 1.x layer on the PICA200, heavily extended. It batches immediate-mode draws, presents asynchronously, and keeps separate overlay and snapshot targets.
- **Sound:** OpenAL on ndsp (software mixer on core 2). Software keyboard for pilot names.

## Game files

Copy these from your FreeSpace 2 install into `sdmc:/3ds/freespace2/`, with the file names in lower case:

```
root_fs2.vp    smarty_fs2.vp   sparky_fs2.vp   stu_fs2.vp
tango1_fs2.vp  tango2_fs2.vp   tango3_fs2.vp   warble_fs2.vp
tangoa_fs2.vp  tangob_fs2.vp
```

In the GOG version, `tangoA_fs2.vp` and `tangoB_fs2.vp` are in the `data2` and `data3` subfolders. `sparky_hi_fs2.vp` is not needed.

Put `fs2-3ds.3dsx` in the same folder and start it from the Homebrew Launcher. Pilot files are saved in `sdmc:/3ds/freespace2/.fs2_open`. A log, including per-stage frame timings, is written to `fs2_3ds.log` in that folder.

## Building

You need devkitARM, libctru, and the 3DS portlibs (libogg, libpng, zlib), plus `picasso` and `3dsxtool`.

```
source env.sh      # sets DEVKITPRO / DEVKITARM / PATH (edit for your install)
make -j4           # -> fs2-3ds.3dsx
```

The 3DS-specific code is in `code/n3ds/` and the renderer in `lib/picaGL/`. Engine changes are guarded by `SCP_3DS`.

## License

The fs2_open code is under the original Volition FreeSpace 2 source code license, which allows non-commercial use only (see `COPYING`). The libraries keep their own licenses.
