# Changelog

## 2026-11-21 (night: audit fixes)

- Training messages on the cockpit panel no longer flip pages by themselves every 3 seconds. A long message shows a page mark (PAGE 1/2) and stays until you tap the message box for the next page. The box holds seven lines instead of six.
- When the game asks a question in a pop-up ("Do you really want to end the mission?"), the touchscreen shows the text and one big button per answer. The focus starts on No. B cancels.
- START menu: rows are full width. New SETTINGS page with LEFT HAND and DEBUG LOG. In flight, END MISSION replaces GAME MENU (it asks No / Yes, Quit / Yes, Restart).
- EXIT GAME now says what really happens: "Your pilot is saved first." and, in flight, "The mission you are flying is not saved."
- The full debug log is a setting (START > SETTINGS > DEBUG LOG, off by default). A debug.txt file left on the card no longer switches it on.
- Dashboard: the current screen's main actions are buttons (READY ROOM, TECH ROOM, OPTIONS; BRIEFING, SHIPS, WEAPONS, COMMIT; CONTINUE; NEXT TAB, ACCEPT).
- Screens swapped in menus: the dashboard fills the top screen (no black bars), and L / R / ZOOM zoom the menu on the touchscreen so small text can be read.
- Cockpit panel: threat warnings in words on the first line of the message box ("! MISSILE LOCKED ON YOU", "! ENEMY IS LOCKING ON", "! ENEMY GUNS ON YOU").
- Cockpit panel: shorter button labels that fit with a margin (ENEMY, SUB, ATTKR, COMMS, EQ SH); BURN and SPEED instead of AFTB and SPD; "none" instead of "-" for an empty weapon bank.
- Manual: CLOSE button; the control hints are a strip under the text; scrolling never shows a line cut in half.
- Settings that cannot be written to the card are reported (log line, and a line on the SETTINGS page).
- The game starts without sound instead of crashing if there is no memory for the sound buffers at launch.
- The "new user tips" pop-up is off: its tips name PC keys ("Press [Alt-M]").

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
