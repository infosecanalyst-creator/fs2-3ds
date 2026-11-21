// selftest_port.h - FreeSpace 2's additions to the shared selftest.c (3ds-test-suite hwtest/selftest).
// selftest.c includes this file twice: ST_PORT_CONFIG (settings) and ST_PORT_CODE (extra code).
#ifdef ST_PORT_CONFIG
// One selftest thread in all (writer + watchdog): FreeSpace 2 runs close to the 3DS thread limit,
// and two extra threads made its own thread creation fail and the game hang. It stays on the
// game's core above the game (FS2 runs its own sound mixer and SDL threads on core 2), so a hung
// game cannot starve it.
#define ST_ONE_THREAD 1
#define ST_WRITER_CORE (-2)
#define ST_WRITER_PRIO_OFFSET (-1)
// Trace lines in FS2's own log (sdmc:/3ds/freespace2/debug.txt switches it on).
#ifdef __cplusplus
extern "C"
#endif
void n3ds_log(const char *fmt, ...);
#define ST_TRACE(...) n3ds_log(__VA_ARGS__)
// Lets the emulator suite run the hooks too (no --selftest argument there).
#define ST_EMU_SCRIPT "sdmc:/3ds/testagent/emu_selftest.txt"
#endif
