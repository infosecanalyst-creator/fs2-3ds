/*
 * A small OpenAL 1.1 implementation on top of the 3DS DSP (ndsp).
 *
 * Covers what fs2_open uses: static and streamed (queued) buffers, gain, pitch, looping,
 * 3D positioning (inverse-distance attenuation + stereo panning from the listener
 * orientation), source-relative sounds. Everything is mixed in software into one
 * stereo 16-bit ndsp channel by a mixer thread on the New 3DS extra CPU core.
 */
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <AL/al.h>
#include <AL/alc.h>

#define MIX_RATE        32728       /* native DSP rate, no resampling in the DSP */
#define MIX_SAMPLES     768         /* per wave buffer (~23 ms) */
#define MIX_BUFFERS     4
#define MAX_SOURCES     64
#define MAX_BUFFERS     1024
#define MAX_QUEUE       16

struct al_buffer {
	int used;
	short *data;          /* always converted to signed 16-bit */
	int channels;
	int freq;
	int frames;           /* samples per channel */
	int bytes_orig;
	int bits_orig;
};

struct al_source {
	int used;
	int state;
	int looping;
	int relative;
	float gain, min_gain, max_gain, pitch;
	float ref_dist, max_dist, rolloff;
	float pos[3], vel[3];
	ALuint queue[MAX_QUEUE];
	int nqueue;
	int qcur;             /* index of buffer being played */
	int processed;        /* buffers finished (static or queued) */
	u32 pos_frac;         /* 16.16 fixed position in current buffer */
	u32 pos_int;
	int static_buffer;    /* AL_BUFFER set directly */
	/* computed per mix */
	int vol_l, vol_r;     /* 0..65536 */
};

struct ALCdevice_struct { int dummy; };
struct ALCcontext_struct { int dummy; };

static al_buffer Buffers[MAX_BUFFERS];
static al_source Sources[MAX_SOURCES];
static float Listener_pos[3], Listener_vel[3];
static float Listener_at[3] = { 0, 0, -1 }, Listener_up[3] = { 0, 1, 0 };
static float Listener_gain = 1.0f;
static ALenum Al_error = AL_NO_ERROR;
static ALenum Distance_model = AL_INVERSE_DISTANCE_CLAMPED;

static LightLock Al_lock;
static Thread Mix_thread;
static volatile bool Mix_run = false;
static ndspWaveBuf Wave[MIX_BUFFERS];
static s16 *Wave_data = NULL;
static int Mix_accum[MIX_SAMPLES * 2];
static LightEvent Mix_event;

static ALCdevice_struct The_device;
static ALCcontext_struct The_context;
static int Ndsp_ok = 0;

static void set_error(ALenum e) { if (Al_error == AL_NO_ERROR) Al_error = e; }

static al_source *get_source(ALuint id)
{
	if (id == 0 || id > MAX_SOURCES || !Sources[id - 1].used) { set_error(AL_INVALID_NAME); return NULL; }
	return &Sources[id - 1];
}

static al_buffer *get_buffer(ALuint id)
{
	if (id == 0 || id > MAX_BUFFERS || !Buffers[id - 1].used) return NULL;
	return &Buffers[id - 1];
}

/* ------------------------------------------------------------------ spatialisation */

static void compute_volume(al_source *s)
{
	float gain = s->gain * Listener_gain;
	float pan = 0.0f;

	float d[3];
	if (s->relative) {
		d[0] = s->pos[0]; d[1] = s->pos[1]; d[2] = s->pos[2];
	} else {
		d[0] = s->pos[0] - Listener_pos[0];
		d[1] = s->pos[1] - Listener_pos[1];
		d[2] = s->pos[2] - Listener_pos[2];
	}
	float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);

	if (Distance_model != AL_NONE && dist > 0.0001f) {
		float dd = dist;
		if (Distance_model == AL_INVERSE_DISTANCE_CLAMPED) {
			if (dd < s->ref_dist) dd = s->ref_dist;
			if (dd > s->max_dist) dd = s->max_dist;
		}
		float denom = s->ref_dist + s->rolloff * (dd - s->ref_dist);
		if (denom > 0.0001f) gain *= s->ref_dist / denom;

		/* right = at x up */
		float r[3] = {
			Listener_at[1] * Listener_up[2] - Listener_at[2] * Listener_up[1],
			Listener_at[2] * Listener_up[0] - Listener_at[0] * Listener_up[2],
			Listener_at[0] * Listener_up[1] - Listener_at[1] * Listener_up[0]
		};
		float rl = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
		if (rl > 0.0001f) pan = (d[0] * r[0] + d[1] * r[1] + d[2] * r[2]) / (rl * dist);
	}

	if (gain < s->min_gain) gain = s->min_gain;
	if (gain > s->max_gain) gain = s->max_gain;
	if (pan < -1.0f) pan = -1.0f;
	if (pan > 1.0f) pan = 1.0f;

	/* constant-power-ish pan, keeps centre at full volume */
	float l = gain * (pan > 0.0f ? 1.0f - pan * 0.7f : 1.0f);
	float rr = gain * (pan < 0.0f ? 1.0f + pan * 0.7f : 1.0f);
	s->vol_l = (int)(l * 65536.0f);
	s->vol_r = (int)(rr * 65536.0f);
}

/* ------------------------------------------------------------------ mixer */

static void mix_source(al_source *s, int *out, int frames)
{
	int done = 0;
	while (done < frames && s->state == AL_PLAYING) {
		if (s->qcur >= s->nqueue) { s->state = AL_STOPPED; break; }
		al_buffer *b = get_buffer(s->queue[s->qcur]);
		if (!b || !b->data || b->frames <= 0) {
			s->qcur++;
			s->processed++;
			s->pos_int = 0; s->pos_frac = 0;
			continue;
		}

		u32 step = (u32)(((float)b->freq * s->pitch / (float)MIX_RATE) * 65536.0f);
		if (step == 0) step = 1;

		const short *src = b->data;
		int vl = s->vol_l, vr = s->vol_r;
		u32 pi = s->pos_int, pf = s->pos_frac;

		if (b->channels == 1) {
			while (done < frames && pi < (u32)b->frames) {
				int v = src[pi];
				out[done * 2]     += (v * vl) >> 16;
				out[done * 2 + 1] += (v * vr) >> 16;
				done++;
				pf += step;
				pi += pf >> 16;
				pf &= 0xffff;
			}
		} else {
			while (done < frames && pi < (u32)b->frames) {
				out[done * 2]     += (src[pi * 2] * vl) >> 16;
				out[done * 2 + 1] += (src[pi * 2 + 1] * vr) >> 16;
				done++;
				pf += step;
				pi += pf >> 16;
				pf &= 0xffff;
			}
		}
		s->pos_int = pi;
		s->pos_frac = pf;

		if (pi >= (u32)b->frames) {
			s->pos_int = 0; s->pos_frac = 0;
			if (s->looping && s->nqueue == 1) continue;
			s->qcur++;
			s->processed++;
			if (s->qcur >= s->nqueue) {
				if (s->looping && s->nqueue > 0) {
					s->qcur = 0;
					s->processed = 0;
				} else {
					s->state = AL_STOPPED;
				}
			}
		}
	}
}

static void ndsp_callback(void *data)
{
	(void)data;
	LightEvent_Signal(&Mix_event);
}

static void mix_thread_func(void *arg)
{
	(void)arg;
	while (Mix_run) {
		for (int w = 0; w < MIX_BUFFERS; w++) {
			if (Wave[w].status != NDSP_WBUF_DONE && Wave[w].status != NDSP_WBUF_FREE) continue;

			memset(Mix_accum, 0, sizeof(Mix_accum));
			LightLock_Lock(&Al_lock);
			for (int i = 0; i < MAX_SOURCES; i++) {
				al_source *s = &Sources[i];
				if (s->used && s->state == AL_PLAYING) {
					compute_volume(s);
					mix_source(s, Mix_accum, MIX_SAMPLES);
				}
			}
			LightLock_Unlock(&Al_lock);

			s16 *dst = (s16 *)Wave[w].data_pcm16;
			for (int i = 0; i < MIX_SAMPLES * 2; i++) {
				int v = Mix_accum[i];
				if (v > 32767) v = 32767;
				if (v < -32768) v = -32768;
				dst[i] = (s16)v;
			}
			DSP_FlushDataCache(dst, MIX_SAMPLES * 4);
			ndspChnWaveBufAdd(0, &Wave[w]);
		}
		LightEvent_Wait(&Mix_event);
	}
}

static void start_mixer(void)
{
	if (Ndsp_ok) return;
	if (R_FAILED(ndspInit())) {
		/* no dspfirm.cdc dump: run silently */
		return;
	}
	Ndsp_ok = 1;
	LightLock_Init(&Al_lock);
	LightEvent_Init(&Mix_event, RESET_ONESHOT);

	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	ndspChnReset(0);
	ndspChnSetInterp(0, NDSP_INTERP_NONE);
	ndspChnSetRate(0, (float)MIX_RATE);
	ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
	float mix[12] = { 1.0f, 1.0f };
	ndspChnSetMix(0, mix);

	Wave_data = (s16 *)linearAlloc(MIX_SAMPLES * 4 * MIX_BUFFERS);
	memset(Wave_data, 0, MIX_SAMPLES * 4 * MIX_BUFFERS);
	memset(Wave, 0, sizeof(Wave));
	for (int w = 0; w < MIX_BUFFERS; w++) {
		Wave[w].data_pcm16 = Wave_data + w * MIX_SAMPLES * 2;
		Wave[w].nsamples = MIX_SAMPLES;
		Wave[w].status = NDSP_WBUF_FREE;
	}
	ndspSetCallback(ndsp_callback, NULL);

	Mix_run = true;
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	Mix_thread = threadCreate(mix_thread_func, NULL, 32 * 1024, prio - 3, 2, false);
	if (!Mix_thread) Mix_thread = threadCreate(mix_thread_func, NULL, 32 * 1024, prio - 3, -2, false);
}

static void stop_mixer(void);
extern "C" void n3ds_audio_shutdown(void) { stop_mixer(); }

static void stop_mixer(void)
{
	if (!Ndsp_ok) return;
	Mix_run = false;
	LightEvent_Signal(&Mix_event);
	if (Mix_thread) {
		threadJoin(Mix_thread, U64_MAX);
		threadFree(Mix_thread);
		Mix_thread = NULL;
	}
	ndspChnReset(0);
	ndspExit();
	linearFree(Wave_data);
	Wave_data = NULL;
	Ndsp_ok = 0;
}

#define LOCK()   do { if (Ndsp_ok) LightLock_Lock(&Al_lock); } while (0)
#define UNLOCK() do { if (Ndsp_ok) LightLock_Unlock(&Al_lock); } while (0)

extern "C" {

/* ------------------------------------------------------------------ ALC */

ALCdevice *alcOpenDevice(const ALCchar *name) { (void)name; start_mixer(); return &The_device; }
ALCboolean alcCloseDevice(ALCdevice *d) { (void)d; stop_mixer(); return ALC_TRUE; }
ALCcontext *alcCreateContext(ALCdevice *d, const ALCint *attr) { (void)d; (void)attr; return &The_context; }
ALCboolean alcMakeContextCurrent(ALCcontext *c) { (void)c; return ALC_TRUE; }
void alcProcessContext(ALCcontext *c) { (void)c; }
void alcSuspendContext(ALCcontext *c) { (void)c; }
void alcDestroyContext(ALCcontext *c) { (void)c; }
ALCcontext *alcGetCurrentContext(void) { return &The_context; }
ALCdevice *alcGetContextsDevice(ALCcontext *c) { (void)c; return &The_device; }
ALCenum alcGetError(ALCdevice *d) { (void)d; return ALC_NO_ERROR; }
ALCboolean alcIsExtensionPresent(ALCdevice *d, const ALCchar *e) { (void)d; (void)e; return ALC_FALSE; }
void *alcGetProcAddress(ALCdevice *d, const ALCchar *f) { (void)d; (void)f; return NULL; }
ALCenum alcGetEnumValue(ALCdevice *d, const ALCchar *e) { (void)d; (void)e; return 0; }

const ALCchar *alcGetString(ALCdevice *d, ALCenum p)
{
	(void)d;
	switch (p) {
		case ALC_DEFAULT_DEVICE_SPECIFIER:
		case ALC_DEVICE_SPECIFIER: return "3DS DSP\0";
		case ALC_EXTENSIONS: return "";
		default: return "";
	}
}

void alcGetIntegerv(ALCdevice *d, ALCenum p, ALCsizei size, ALCint *data)
{
	(void)d;
	if (!data || size <= 0) return;
	switch (p) {
		case ALC_MAJOR_VERSION: *data = 1; break;
		case ALC_MINOR_VERSION: *data = 1; break;
		case ALC_FREQUENCY: *data = MIX_RATE; break;
		case ALC_MONO_SOURCES: *data = MAX_SOURCES; break;
		case ALC_STEREO_SOURCES: *data = MAX_SOURCES; break;
		default: *data = 0; break;
	}
}

/* ------------------------------------------------------------------ AL state */

void alEnable(ALenum c) { (void)c; }
void alDisable(ALenum c) { (void)c; }
ALboolean alIsEnabled(ALenum c) { (void)c; return AL_FALSE; }

const ALchar *alGetString(ALenum p)
{
	switch (p) {
		case AL_VENDOR: return "fs2-3ds";
		case AL_VERSION: return "1.1";
		case AL_RENDERER: return "3DS ndsp software mixer";
		case AL_EXTENSIONS: return "";
		default: return "";
	}
}

ALenum alGetError(void) { ALenum e = Al_error; Al_error = AL_NO_ERROR; return e; }
ALboolean alIsExtensionPresent(const ALchar *e) { (void)e; return AL_FALSE; }
void *alGetProcAddress(const ALchar *f) { (void)f; return NULL; }
ALenum alGetEnumValue(const ALchar *e) { (void)e; return 0; }
void alGetIntegerv(ALenum p, ALint *d) { if (d) *d = (p == AL_DISTANCE_MODEL) ? Distance_model : 0; }
void alGetFloatv(ALenum p, ALfloat *d) { (void)p; if (d) *d = 0.0f; }
void alGetBooleanv(ALenum p, ALboolean *d) { (void)p; if (d) *d = AL_FALSE; }
void alGetDoublev(ALenum p, ALdouble *d) { (void)p; if (d) *d = 0.0; }
ALint alGetInteger(ALenum p) { ALint v; alGetIntegerv(p, &v); return v; }
ALfloat alGetFloat(ALenum p) { (void)p; return 0.0f; }
ALboolean alGetBoolean(ALenum p) { (void)p; return AL_FALSE; }
ALdouble alGetDouble(ALenum p) { (void)p; return 0.0; }

void alDopplerFactor(ALfloat v) { (void)v; }
void alDopplerVelocity(ALfloat v) { (void)v; }
void alSpeedOfSound(ALfloat v) { (void)v; }
void alDistanceModel(ALenum m) { Distance_model = m; }

/* ------------------------------------------------------------------ listener */

void alListenerf(ALenum p, ALfloat v) { if (p == AL_GAIN) Listener_gain = v; }
void alListener3f(ALenum p, ALfloat a, ALfloat b, ALfloat c)
{
	LOCK();
	if (p == AL_POSITION) { Listener_pos[0] = a; Listener_pos[1] = b; Listener_pos[2] = c; }
	else if (p == AL_VELOCITY) { Listener_vel[0] = a; Listener_vel[1] = b; Listener_vel[2] = c; }
	UNLOCK();
}
void alListenerfv(ALenum p, const ALfloat *v)
{
	if (!v) return;
	LOCK();
	if (p == AL_ORIENTATION) {
		memcpy(Listener_at, v, sizeof(float) * 3);
		memcpy(Listener_up, v + 3, sizeof(float) * 3);
	} else if (p == AL_POSITION) memcpy(Listener_pos, v, sizeof(float) * 3);
	else if (p == AL_VELOCITY) memcpy(Listener_vel, v, sizeof(float) * 3);
	else if (p == AL_GAIN) Listener_gain = v[0];
	UNLOCK();
}
void alListeneri(ALenum p, ALint v) { (void)p; (void)v; }
void alListener3i(ALenum p, ALint a, ALint b, ALint c) { alListener3f(p, (float)a, (float)b, (float)c); }
void alListeneriv(ALenum p, const ALint *v) { (void)p; (void)v; }
void alGetListenerf(ALenum p, ALfloat *v) { if (v) *v = (p == AL_GAIN) ? Listener_gain : 0.0f; }
void alGetListener3f(ALenum p, ALfloat *a, ALfloat *b, ALfloat *c)
{
	float *src = (p == AL_VELOCITY) ? Listener_vel : Listener_pos;
	if (a) *a = src[0]; if (b) *b = src[1]; if (c) *c = src[2];
}
void alGetListenerfv(ALenum p, ALfloat *v)
{
	if (!v) return;
	if (p == AL_ORIENTATION) { memcpy(v, Listener_at, 12); memcpy(v + 3, Listener_up, 12); }
	else if (p == AL_GAIN) v[0] = Listener_gain;
	else alGetListener3f(p, &v[0], &v[1], &v[2]);
}
void alGetListeneri(ALenum p, ALint *v) { (void)p; if (v) *v = 0; }
void alGetListener3i(ALenum p, ALint *a, ALint *b, ALint *c) { (void)p; if (a) *a = 0; if (b) *b = 0; if (c) *c = 0; }
void alGetListeneriv(ALenum p, ALint *v) { (void)p; if (v) *v = 0; }

/* ------------------------------------------------------------------ sources */

static void source_defaults(al_source *s)
{
	memset(s, 0, sizeof(*s));
	s->used = 1;
	s->state = AL_INITIAL;
	s->gain = 1.0f; s->min_gain = 0.0f; s->max_gain = 1.0f; s->pitch = 1.0f;
	s->ref_dist = 1.0f; s->max_dist = 1e30f; s->rolloff = 1.0f;
}

void alGenSources(ALsizei n, ALuint *ids)
{
	LOCK();
	for (int k = 0; k < n; k++) {
		int found = 0;
		for (int i = 0; i < MAX_SOURCES; i++) {
			if (!Sources[i].used) {
				source_defaults(&Sources[i]);
				ids[k] = i + 1;
				found = 1;
				break;
			}
		}
		if (!found) { ids[k] = 0; set_error(AL_OUT_OF_MEMORY); }
	}
	UNLOCK();
}

void alDeleteSources(ALsizei n, const ALuint *ids)
{
	LOCK();
	for (int k = 0; k < n; k++) {
		if (ids[k] && ids[k] <= MAX_SOURCES) Sources[ids[k] - 1].used = 0;
	}
	UNLOCK();
}

ALboolean alIsSource(ALuint id) { return (id && id <= MAX_SOURCES && Sources[id - 1].used) ? AL_TRUE : AL_FALSE; }

void alSourcef(ALuint id, ALenum p, ALfloat v)
{
	LOCK();
	al_source *s = get_source(id);
	if (s) {
		switch (p) {
			case AL_GAIN: s->gain = v; break;
			case AL_MIN_GAIN: s->min_gain = v; break;
			case AL_MAX_GAIN: s->max_gain = v; break;
			case AL_PITCH: s->pitch = (v > 0.01f) ? v : 0.01f; break;
			case AL_REFERENCE_DISTANCE: s->ref_dist = v; break;
			case AL_MAX_DISTANCE: s->max_dist = v; break;
			case AL_ROLLOFF_FACTOR: s->rolloff = v; break;
			default: break;
		}
	}
	UNLOCK();
}

void alSource3f(ALuint id, ALenum p, ALfloat a, ALfloat b, ALfloat c)
{
	LOCK();
	al_source *s = get_source(id);
	if (s) {
		if (p == AL_POSITION) { s->pos[0] = a; s->pos[1] = b; s->pos[2] = c; }
		else if (p == AL_VELOCITY) { s->vel[0] = a; s->vel[1] = b; s->vel[2] = c; }
	}
	UNLOCK();
}

void alSourcefv(ALuint id, ALenum p, const ALfloat *v)
{
	if (!v) return;
	if (p == AL_POSITION || p == AL_VELOCITY || p == AL_DIRECTION) alSource3f(id, p, v[0], v[1], v[2]);
	else alSourcef(id, p, v[0]);
}

static void source_set_buffer(al_source *s, ALuint bid)
{
	s->nqueue = 0;
	s->qcur = 0;
	s->processed = 0;
	s->pos_int = s->pos_frac = 0;
	if (bid) {
		s->queue[0] = bid;
		s->nqueue = 1;
		s->static_buffer = 1;
	} else {
		s->static_buffer = 0;
	}
}

void alSourcei(ALuint id, ALenum p, ALint v)
{
	LOCK();
	al_source *s = get_source(id);
	if (s) {
		switch (p) {
			case AL_BUFFER: source_set_buffer(s, (ALuint)v); break;
			case AL_LOOPING: s->looping = v ? 1 : 0; break;
			case AL_SOURCE_RELATIVE: s->relative = v ? 1 : 0; break;
			case AL_REFERENCE_DISTANCE: s->ref_dist = (float)v; break;
			case AL_MAX_DISTANCE: s->max_dist = (float)v; break;
			case AL_ROLLOFF_FACTOR: s->rolloff = (float)v; break;
			default: break;
		}
	}
	UNLOCK();
}

void alSource3i(ALuint id, ALenum p, ALint a, ALint b, ALint c) { alSource3f(id, p, (float)a, (float)b, (float)c); }
void alSourceiv(ALuint id, ALenum p, const ALint *v) { if (v) alSourcei(id, p, v[0]); }

void alGetSourcef(ALuint id, ALenum p, ALfloat *v)
{
	if (!v) return;
	LOCK();
	al_source *s = get_source(id);
	*v = 0.0f;
	if (s) {
		switch (p) {
			case AL_GAIN: *v = s->gain; break;
			case AL_PITCH: *v = s->pitch; break;
			case AL_MIN_GAIN: *v = s->min_gain; break;
			case AL_MAX_GAIN: *v = s->max_gain; break;
			case AL_REFERENCE_DISTANCE: *v = s->ref_dist; break;
			case AL_MAX_DISTANCE: *v = s->max_dist; break;
			case AL_ROLLOFF_FACTOR: *v = s->rolloff; break;
			case AL_SEC_OFFSET: {
				al_buffer *b = (s->qcur < s->nqueue) ? get_buffer(s->queue[s->qcur]) : NULL;
				if (b && b->freq) *v = (float)s->pos_int / (float)b->freq;
				break;
			}
			default: break;
		}
	}
	UNLOCK();
}

void alGetSource3f(ALuint id, ALenum p, ALfloat *a, ALfloat *b, ALfloat *c)
{
	LOCK();
	al_source *s = get_source(id);
	if (s) {
		float *src = (p == AL_VELOCITY) ? s->vel : s->pos;
		if (a) *a = src[0]; if (b) *b = src[1]; if (c) *c = src[2];
	}
	UNLOCK();
}

void alGetSourcefv(ALuint id, ALenum p, ALfloat *v)
{
	if (!v) return;
	if (p == AL_POSITION || p == AL_VELOCITY) alGetSource3f(id, p, &v[0], &v[1], &v[2]);
	else alGetSourcef(id, p, v);
}

void alGetSourcei(ALuint id, ALenum p, ALint *v)
{
	if (!v) return;
	LOCK();
	al_source *s = get_source(id);
	*v = 0;
	if (s) {
		switch (p) {
			case AL_SOURCE_STATE: *v = s->state; break;
			case AL_LOOPING: *v = s->looping; break;
			case AL_SOURCE_RELATIVE: *v = s->relative; break;
			case AL_BUFFER: *v = (s->qcur < s->nqueue) ? (ALint)s->queue[s->qcur] : (s->nqueue ? (ALint)s->queue[0] : 0); break;
			case AL_BUFFERS_QUEUED: *v = s->nqueue; break;
			case AL_BUFFERS_PROCESSED:
				*v = s->static_buffer ? 0 : (s->processed < s->nqueue ? s->processed : s->nqueue);
				break;
			case AL_SAMPLE_OFFSET: *v = (ALint)s->pos_int; break;
			case AL_BYTE_OFFSET: {
				al_buffer *b = (s->qcur < s->nqueue) ? get_buffer(s->queue[s->qcur]) : NULL;
				*v = b ? (ALint)(s->pos_int * b->channels * (b->bits_orig / 8)) : 0;
				break;
			}
			case AL_SOURCE_TYPE: *v = s->static_buffer ? AL_STATIC : (s->nqueue ? AL_STREAMING : AL_UNDETERMINED); break;
			default: break;
		}
	}
	UNLOCK();
}

void alGetSource3i(ALuint id, ALenum p, ALint *a, ALint *b, ALint *c)
{
	float fa = 0, fb = 0, fc = 0;
	alGetSource3f(id, p, &fa, &fb, &fc);
	if (a) *a = (ALint)fa; if (b) *b = (ALint)fb; if (c) *c = (ALint)fc;
}

void alGetSourceiv(ALuint id, ALenum p, ALint *v) { alGetSourcei(id, p, v); }

void alSourcePlay(ALuint id)
{
	LOCK();
	al_source *s = get_source(id);
	if (s) {
		if (s->state != AL_PAUSED) {
			s->qcur = s->static_buffer ? 0 : s->processed;
			if (s->static_buffer) s->processed = 0;
			s->pos_int = s->pos_frac = 0;
		}
		s->state = (s->nqueue > s->qcur) ? AL_PLAYING : AL_STOPPED;
	}
	UNLOCK();
}

void alSourceStop(ALuint id)
{
	LOCK();
	al_source *s = get_source(id);
	if (s) {
		s->state = AL_STOPPED;
		s->processed = s->nqueue;
		s->qcur = s->nqueue;
		s->pos_int = s->pos_frac = 0;
	}
	UNLOCK();
}

void alSourceRewind(ALuint id)
{
	LOCK();
	al_source *s = get_source(id);
	if (s) {
		s->state = AL_INITIAL;
		s->qcur = 0;
		s->processed = 0;
		s->pos_int = s->pos_frac = 0;
	}
	UNLOCK();
}

void alSourcePause(ALuint id)
{
	LOCK();
	al_source *s = get_source(id);
	if (s && s->state == AL_PLAYING) s->state = AL_PAUSED;
	UNLOCK();
}

void alSourcePlayv(ALsizei n, const ALuint *ids) { for (int i = 0; i < n; i++) alSourcePlay(ids[i]); }
void alSourceStopv(ALsizei n, const ALuint *ids) { for (int i = 0; i < n; i++) alSourceStop(ids[i]); }
void alSourceRewindv(ALsizei n, const ALuint *ids) { for (int i = 0; i < n; i++) alSourceRewind(ids[i]); }
void alSourcePausev(ALsizei n, const ALuint *ids) { for (int i = 0; i < n; i++) alSourcePause(ids[i]); }

void alSourceQueueBuffers(ALuint id, ALsizei n, const ALuint *bids)
{
	LOCK();
	al_source *s = get_source(id);
	if (s) {
		if (s->static_buffer) { s->static_buffer = 0; s->nqueue = 0; s->qcur = 0; s->processed = 0; }
		for (int i = 0; i < n; i++) {
			if (s->nqueue >= MAX_QUEUE) { set_error(AL_INVALID_OPERATION); break; }
			s->queue[s->nqueue++] = bids[i];
		}
		/* a streaming source that ran dry keeps playing when fed again */
		if (s->state == AL_STOPPED && s->qcur < s->nqueue && s->processed == s->qcur) {
			/* stays stopped until alSourcePlay, as in OpenAL */
		}
	}
	UNLOCK();
}

void alSourceUnqueueBuffers(ALuint id, ALsizei n, ALuint *bids)
{
	LOCK();
	al_source *s = get_source(id);
	if (s) {
		int avail = (s->processed < s->nqueue) ? s->processed : s->nqueue;
		if (n > avail) { set_error(AL_INVALID_VALUE); n = avail; }
		for (int i = 0; i < n; i++) bids[i] = s->queue[i];
		memmove(s->queue, s->queue + n, (s->nqueue - n) * sizeof(ALuint));
		s->nqueue -= n;
		s->qcur -= n;
		s->processed -= n;
		if (s->qcur < 0) s->qcur = 0;
		if (s->processed < 0) s->processed = 0;
	}
	UNLOCK();
}

/* ------------------------------------------------------------------ buffers */

void alGenBuffers(ALsizei n, ALuint *ids)
{
	LOCK();
	for (int k = 0; k < n; k++) {
		int found = 0;
		for (int i = 0; i < MAX_BUFFERS; i++) {
			if (!Buffers[i].used) {
				memset(&Buffers[i], 0, sizeof(al_buffer));
				Buffers[i].used = 1;
				ids[k] = i + 1;
				found = 1;
				break;
			}
		}
		if (!found) { ids[k] = 0; set_error(AL_OUT_OF_MEMORY); }
	}
	UNLOCK();
}

void alDeleteBuffers(ALsizei n, const ALuint *ids)
{
	LOCK();
	for (int k = 0; k < n; k++) {
		al_buffer *b = get_buffer(ids[k]);
		if (b) {
			free(b->data);
			memset(b, 0, sizeof(*b));
		}
	}
	UNLOCK();
}

ALboolean alIsBuffer(ALuint id) { return get_buffer(id) ? AL_TRUE : AL_FALSE; }

void alBufferData(ALuint id, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq)
{
	int channels, bits;
	switch (format) {
		case AL_FORMAT_MONO8: channels = 1; bits = 8; break;
		case AL_FORMAT_MONO16: channels = 1; bits = 16; break;
		case AL_FORMAT_STEREO8: channels = 2; bits = 8; break;
		case AL_FORMAT_STEREO16: channels = 2; bits = 16; break;
		default: set_error(AL_INVALID_ENUM); return;
	}
	int samples = size / (bits / 8);
	short *conv = (short *)malloc(samples * sizeof(short) + 4);
	if (!conv) { set_error(AL_OUT_OF_MEMORY); return; }
	if (bits == 16) {
		memcpy(conv, data, samples * sizeof(short));
	} else {
		const unsigned char *src = (const unsigned char *)data;
		for (int i = 0; i < samples; i++) conv[i] = (short)(((int)src[i] - 128) << 8);
	}

	LOCK();
	al_buffer *b = get_buffer(id);
	if (!b) { UNLOCK(); free(conv); set_error(AL_INVALID_NAME); return; }
	free(b->data);
	b->data = conv;
	b->channels = channels;
	b->freq = freq > 0 ? freq : 22050;
	b->frames = samples / channels;
	b->bytes_orig = size;
	b->bits_orig = bits;
	UNLOCK();
}

void alBufferf(ALuint id, ALenum p, ALfloat v) { (void)id; (void)p; (void)v; }
void alBuffer3f(ALuint id, ALenum p, ALfloat a, ALfloat b, ALfloat c) { (void)id; (void)p; (void)a; (void)b; (void)c; }
void alBufferfv(ALuint id, ALenum p, const ALfloat *v) { (void)id; (void)p; (void)v; }
void alBufferi(ALuint id, ALenum p, ALint v) { (void)id; (void)p; (void)v; }
void alBuffer3i(ALuint id, ALenum p, ALint a, ALint b, ALint c) { (void)id; (void)p; (void)a; (void)b; (void)c; }
void alBufferiv(ALuint id, ALenum p, const ALint *v) { (void)id; (void)p; (void)v; }

void alGetBufferi(ALuint id, ALenum p, ALint *v)
{
	if (!v) return;
	al_buffer *b = get_buffer(id);
	*v = 0;
	if (!b) { set_error(AL_INVALID_NAME); return; }
	switch (p) {
		case AL_FREQUENCY: *v = b->freq; break;
		case AL_BITS: *v = b->bits_orig; break;
		case AL_CHANNELS: *v = b->channels; break;
		case AL_SIZE: *v = b->bytes_orig; break;
		default: break;
	}
}
void alGetBufferf(ALuint id, ALenum p, ALfloat *v) { (void)id; (void)p; if (v) *v = 0.0f; }
void alGetBuffer3f(ALuint id, ALenum p, ALfloat *a, ALfloat *b, ALfloat *c) { (void)id; (void)p; if (a) *a = 0; if (b) *b = 0; if (c) *c = 0; }
void alGetBufferfv(ALuint id, ALenum p, ALfloat *v) { (void)id; (void)p; if (v) *v = 0.0f; }
void alGetBuffer3i(ALuint id, ALenum p, ALint *a, ALint *b, ALint *c) { (void)id; (void)p; if (a) *a = 0; if (b) *b = 0; if (c) *c = 0; }
void alGetBufferiv(ALuint id, ALenum p, ALint *v) { alGetBufferi(id, p, v); }

} /* extern "C" */
