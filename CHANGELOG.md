# Changelog

## 2026-11-21

- One START menu everywhere: RESUME, MANUAL, EXIT GAME, plus GAME MENU in flight (the game's own pause / options screen). The game is paused while it is open. Letting go of a button outside it cancels the press.
- Long target and weapon names are cut with "..." to fit their space instead of overflowing.
- A crash now shows a crash screen (press START to leave) instead of restarting the console.
- The log records how much memory is left once a mission has loaded, and notes it if memory runs low.
- The manual describes all of this and where the program, saves and logs live.

## 2026-11-20

- Pilot and campaign saves are written to a temporary file first and then renamed (a failed write can no longer damage the old save).
- HOME > Close saves your pilot before quitting; pressing HOME itself only pauses the game's clock.
- The log is small and the game faster: detailed logging is now off unless an empty debug.txt is in /3ds/freespace2/.
- Fixed crashes on exit (the timer and mixer threads are stopped first).
- The START menu shows the build version.
- Install path: the program is /3ds/fs2-3ds.3dsx.

## 2026-11-19

- Manual: drag the stylus on the section screen to scroll the text, with a little momentum.
