// New 3DS: no microphone capture (3.7.2's multiplayer voice uses OpenAL capture).
// The port's OpenAL layer only plays sound, so capture reports no device.
#include "AL/al.h"
#include "AL/alc.h"

extern "C" {
ALCdevice *alcCaptureOpenDevice(const ALCchar *, ALCuint, ALCenum, ALCsizei) { return 0; }
ALCboolean alcCaptureCloseDevice(ALCdevice *) { return ALC_FALSE; }
void alcCaptureStart(ALCdevice *) {}
void alcCaptureStop(ALCdevice *) {}
void alcCaptureSamples(ALCdevice *, ALCvoid *, ALCsizei) {}
}
