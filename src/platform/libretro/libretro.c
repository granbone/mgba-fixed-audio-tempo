/* Copyright (c) 2013-2015 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "libretro.h"

#include <mgba-util/common.h>

#include <mgba/core/cheats.h>
#include <mgba/core/audio-clock.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/core/serialize.h>
#include <mgba/core/version.h>
#ifdef M_CORE_GB
#include <mgba/gb/core.h>
#include <mgba/internal/gb/gb.h>
#include <mgba/internal/gb/mbc.h>
#include <mgba/internal/gb/overrides.h>
#endif
#ifdef M_CORE_GBA
#include <mgba/gba/core.h>
#include <mgba/gba/interface.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/audio.h>
#include <mgba/internal/gba/b6jj-audio.h>
#include <mgba/internal/gba/dma.h>
#include <mgba/internal/gba/io.h>
#include <mgba/internal/gba/mp2k-events.h>
#include <mgba/internal/gba/mp2k-profile.h>
#include <mgba/internal/gba/mp2k-semantic.h>
#include <mgba/internal/gba/mp2k-ownership.h>
#include <mgba/internal/arm/arm.h>
#include <mgba/internal/arm/isa-inlines.h>
#include "../../../tools/mp2k-audio-trace/bridge/bridge.h"
#ifdef _WIN32
#include <windows.h>
#endif
#endif
#include <mgba-util/memory.h>
#include <mgba-util/vfs.h>
#include <stdio.h>
#include <time.h>

#include "libretro_core_options.h"
#include "fixed_audio_mode.h"
#include "fixed_audio_rate.h"

#ifdef M_CORE_GBA
mLOG_DECLARE_CATEGORY(GBA_MP2K_EVENTS);

struct _MP2kAudioStats {
	bool initialized;
	retro_perf_get_time_usec_t clock;
	retro_time_t start;
	unsigned runs;
	unsigned audioCalls;
	uint64_t available;
	uint64_t sent;
};
static struct _MP2kAudioStats mp2kAudioStats;

/* Opt-in wall probe. Runs and actual GBA frames are separate counters;
 * neither presentation FPS nor frontend target-rate claims drive synthesis. */
static struct {
	retro_perf_get_time_usec_t clock;
	uint64_t runs, samples, transportUs, gameUs, audioUs;
	uint64_t gbaFrames, gbaStepAnomalies, callbackRequested, callbackAccepted;
} fixedAudioProbe;

struct _MP2kPcmTrace {
	FILE* pcm;
	uint64_t submittedFrames;
	uint64_t inputSequence;
	uint16_t previousKeys;
	bool enabled;
};
static struct _MP2kPcmTrace mp2kPcmTrace;
struct _GBAStemFiles {
	struct GBAAudioStemTrace trace;
	FILE* files[GBA_AUDIO_STEM_COUNT];
	uint64_t frames;
	bool enabled;
};
static struct _GBAStemFiles gbaStemFiles;
static struct GBAMP2kFrontendState mp2kCurrentFrontend;
static struct mAudioClock fixedAudioClock;
enum FixedAudioBackend {
	FIXED_AUDIO_BACKEND_NATIVE,
	FIXED_AUDIO_BACKEND_MP2K,
	FIXED_AUDIO_BACKEND_B6JJ
};
static enum FixedAudioBackend fixedAudioBackend;
static enum FixedAudioMode fixedAudioMode;
static struct B6JJAudio* b6jjAudio;
static int16_t b6jjOutput[2048*2];
static unsigned b6jjLoggedSpeed;
static uint64_t b6jjCallbackFrames, b6jjPartialBatches;
static bool b6jjRecoveryPending, b6jjRewinding;
static uint64_t b6jjGeneration, b6jjLoads, b6jjRebuilds;
static uint64_t b6jjRewindRebuildBase;
static uint64_t fixedAudioClockRuns;
static bool fixedAudioToneEnabled;
static bool fixedAudioRunTraceEnabled;
static struct GBAMP2kProfile runtimeMp2kProfile;
static struct GBAMP2kFunction runtimeMp2kFunctions[9];
static struct GBAMP2kSemanticQueue runtimeMp2kQueue;
static unsigned runtimeMp2kValidatedStarts;
static bool runtimeAudioPending;
static bool runtimeAudioPermissive;
static uint64_t runtimeOwnershipUnverified;
static bool runtimeAudioPartial;
static bool runtimeScannerDetected;
static const char* runtimeProbeReason;
static void _pollRuntimeMP2kPlayers(void);
static void _ownershipFallback(const char* reason);
static bool _runtimeFifoOwned(struct GBA* gba, uint32_t source);
static void _detectRuntimeMP2kProfile(void);
static void _openRuntimeMP2kProbe(void);
struct _MP2kCandidate {
	bool enabled;
	bool stateTrace;
	bool psgOwnerTrace;
	bool timingTrace;
	uint64_t timingTraceUntil;
	uint64_t timingLastCall;
	bool fadeTrace;
	uint16_t priorFade[GBA_MP2K_MAX_PLAYERS][3];
	uint16_t priorCandidateFade[GBA_MP2K_MAX_PLAYERS];
	int activeSong[GBA_MP2K_MAX_PLAYERS];
	bool nativeModeLogged;
	uint64_t nextStateSample;
	FILE* pcm;
	uint64_t frames;
	struct GBAMP2kSemanticQueue queue;
	int16_t* buffer;
	size_t capacity;
#ifdef _WIN32
	HMODULE library;
	void* player;
	void* (*create)(const uint8_t*, size_t, uint32_t);
	void (*destroy)(void*);
	int (*play)(void*, uint16_t, uint8_t);
	int (*play_at_tick)(void*, uint16_t, uint8_t, uint64_t);
	int (*stop)(void*, uint16_t, uint8_t);
	int (*fade_player)(void*, uint16_t, uint8_t, uint16_t);
	int (*render)(void*, int16_t*, size_t);
	int (*get_state)(void*, uint8_t, struct mp2k_bridge_state*);
	int (*get_mode)(void*, struct mp2k_bridge_mode*);
	int (*get_timing)(void*, struct mp2k_bridge_timing*);
	int (*get_microframe)(void*, uint64_t, struct mp2k_bridge_microframe*);
	int (*get_fade_state)(void*, uint8_t, struct mp2k_bridge_fade_state*);
	int (*rebind_begin)(void*, const struct mp2k_bridge_rebind_player*, size_t);
	int (*rebind_step)(void*, uint32_t);
	const char* (*error)(void);
#endif
};
static struct _MP2kCandidate mp2kCandidate;
struct _FixedAudioOutput {
	bool requested;
	bool active;
	bool outputRateRefreshPending;
	bool recovering;
	bool hardFallback;
	bool diagnostics;
	bool clockSupported;
	unsigned healthyRuns;
	unsigned unsupportedRuns;
	double lastSupportedRate;
	const char* fallbackReason;
	int bridgeRenderResult;
	int lastSong;
	int lastPlayer;
	int lastSemantic;
	uint64_t nativeSamplePosition;
	uint64_t recoveryCount;
	bool recoveryRamp;
	int16_t lastNativeSample[2];
	int loggedSpeed;
	bool dmaOwned[4];
	uint32_t dmaSource[4];
	uint64_t fallbackCount;
	uint64_t underrunCount;
	uint64_t overrunCount;
	uint64_t nativeFrames;
	uint64_t candidateFrames;
	uint64_t callbackFrames;
	uint64_t readFrame;
	uint64_t writeFrame;
	uint64_t startupRemaining;
	size_t lookahead;
	size_t capacity;
	int16_t* ring;
	int16_t* output;
	size_t outputCapacity;
	FILE* callbackPcm;
};
static struct _FixedAudioOutput fixedAudioOutput;
enum _StateLoadStage {
	STATE_LOAD_NONE, STATE_LOAD_RATE_SETTLING, STATE_LOAD_REWINDING,
	STATE_LOAD_REWIND_END_PENDING, STATE_LOAD_REARM_PENDING, STATE_LOAD_REBIND, STATE_LOAD_RECOVERING
};
static enum _StateLoadStage stateLoadStage;
static uint64_t stateLoadBeginRun;
static unsigned stateLoadSeekRuns;
static unsigned stateLoadRetryWait;
static uint64_t stateLoadLastRun;
static bool stateLoadSeen;
static bool stateLoadWasRewind;
static bool stateLoadWaitForNativeStart;
static bool ramPlayerContinueRebindPending;
static bool ramPlayerContinueRecovery;
static bool ramPlayerContinueCompletesLoad;
static unsigned stateLoadStableRuns;
static double stateLoadStableRate;
#define STATE_LOAD_SETTLE_RUNS 5
#define STATE_LOAD_BURST_RUNS 3
static void _stateLoadTransportRun(void);
/* Diagnostic streams survive a rebind, never the renderer or its PCM. */
static FILE* stateLoadCandidatePcm;
static void _stateLoadDropCandidate(void);
/* Opt-in load profiling also works with Fixed Audio OFF for a native control. */
static struct {
	bool enabled;
	uint64_t loads, destroys, creates, rebinds, seeks, validations, epochs;
	uint64_t loadUs, seekUs, firstUs, avRefreshes, avUs;
} stateLoadProfile;
static uint64_t _stateLoadTimeUs(void) {
#ifdef _WIN32
	LARGE_INTEGER now, frequency;
	QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
	return (uint64_t) (now.QuadPart / frequency.QuadPart * 1000000 + now.QuadPart % frequency.QuadPart * 1000000 / frequency.QuadPart);
#else
	return (uint64_t) clock() * 1000000 / CLOCKS_PER_SEC;
#endif
}
static void _stateLoadProfileLog(void) {
	if (!stateLoadProfile.enabled || !stateLoadProfile.loads) return;
	uint64_t elapsed = _stateLoadTimeUs() - stateLoadProfile.firstUs;
	mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO LOAD PROFILE] loads=%llu loadUs=%llu elapsedUs=%llu",
		(unsigned long long) stateLoadProfile.loads, (unsigned long long) stateLoadProfile.loadUs, (unsigned long long) elapsed);
	mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO LOAD PROFILE] destroys=%llu creates=%llu rebinds=%llu seeks=%llu seekUs=%llu validations=%llu epochs=%llu",
		(unsigned long long) stateLoadProfile.destroys, (unsigned long long) stateLoadProfile.creates,
		(unsigned long long) stateLoadProfile.rebinds, (unsigned long long) stateLoadProfile.seeks,
		(unsigned long long) stateLoadProfile.seekUs, (unsigned long long) stateLoadProfile.validations,
		(unsigned long long) stateLoadProfile.epochs);
	mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO LOAD PROFILE] avRefreshes=%llu avUs=%llu",
		(unsigned long long) stateLoadProfile.avRefreshes, (unsigned long long) stateLoadProfile.avUs);
}
static void _stateLoadRebindRun(bool validateOnly);
static void _fixedAudioFallback(const char* reason);
static void _fixedAudioTransient(const char* reason);
static void _fixedAudioDiagnostic(const char* transition);
static void _closeFixedAudioOutput(void);
static void _openFixedAudioOutput(void);
static void _submitFixedAudio(size_t nativeFrames);
#endif

#define GB_SAMPLES 512
/* An alpha factor of 1/180 is *somewhat* equivalent
 * to calculating the average for the last 180
 * frames, or 3 seconds of runtime... */
#define SAMPLES_PER_FRAME_MOVING_AVG_ALPHA (1.0f / 180.0f)
#define EVENT_RATE 60

#define VIDEO_WIDTH_MAX  256
#define VIDEO_HEIGHT_MAX 224
#define VIDEO_BUFF_SIZE  (VIDEO_WIDTH_MAX * VIDEO_HEIGHT_MAX * sizeof(mColor))

static retro_environment_t environCallback;
static retro_video_refresh_t videoCallback;
static retro_audio_sample_batch_t audioCallback;
#ifdef M_CORE_GBA
static size_t _probeAudioCallback(const int16_t* data, size_t frames) {
	size_t accepted = audioCallback(data, frames);
	if (fixedAudioProbe.clock) {
		fixedAudioProbe.callbackRequested += frames;
		fixedAudioProbe.callbackAccepted += accepted;
	}
	return accepted;
}
#endif
static retro_input_poll_t inputPollCallback;
static retro_input_state_t inputCallback;
static retro_log_printf_t logCallback;
static retro_set_rumble_state_t rumbleCallback;
static retro_sensor_get_input_t sensorGetCallback;
static retro_set_sensor_state_t sensorStateCallback;

static void GBARetroLog(struct mLogger* logger, int category, enum mLogLevel level, const char* format, va_list args);

static void _postAudioBuffer(struct mAVStream*, struct mAudioBuffer*);
#ifdef M_CORE_GBA
static void _closeMP2kCandidate(void);
static void _openMP2kCandidate(void);
static void _renderMP2kCandidate(void);
static void _traceMP2kState(void);
static void _traceMP2kFadeState(void);
static void _closeGBAStemTrace(void);
static void _openGBAStemTrace(void);
static void _writeGBAStemTrace(size_t frames);
#endif
static void _audioRateChanged(struct mAVStream*, unsigned rate);
static void _setRumble(struct mRumbleIntegrator*, float level);
static uint8_t _readLux(struct GBALuminanceSource* lux);
static void _updateLux(struct GBALuminanceSource* lux);
static void _updateCamera(const uint32_t* buffer, unsigned width, unsigned height, size_t pitch);
static void _startImage(struct mImageSource*, unsigned w, unsigned h, int colorFormats);
static void _stopImage(struct mImageSource*);
static void _requestImage(struct mImageSource*, const void** buffer, size_t* stride, enum mColorFormat* colorFormat);
static void _updateRotation(struct mRotationSource* source);
static int32_t _readTiltX(struct mRotationSource* source);
static int32_t _readTiltY(struct mRotationSource* source);
static int32_t _readGyroZ(struct mRotationSource* source);

static struct mCore* core;
static mColor* outputBuffer = NULL;
static int16_t *audioSampleBuffer = NULL;
static size_t audioSampleBufferSize;
static float audioSamplesPerFrameAvg;
static void* data;
static size_t dataSize;
static void* savedata;
static struct mAVStream stream;
static bool sensorsInitDone;
static bool rumbleInitDone;
static struct mRumbleIntegrator rumble;
static struct GBALuminanceSource lux;
static struct mRotationSource rotation;
static bool tiltEnabled;
static bool gyroEnabled;
static int luxLevelIndex;
static uint8_t luxLevel;
static bool luxSensorEnabled;
static bool luxSensorUsed;
static struct mLogger logger;
static struct retro_camera_callback cam;
static struct mImageSource imageSource;
static uint32_t* camData = NULL;
static unsigned camWidth;
static unsigned camHeight;
static unsigned imcapWidth;
static unsigned imcapHeight;
static size_t camStride;
static bool deferredSetup = false;
#ifdef M_CORE_GBA
static double fixedAudioAdvertisedRate;
#endif
static bool useBitmasks = true;
static bool envVarsUpdated;
static int32_t tiltX = 0;
static int32_t tiltY = 0;
static int32_t gyroZ = 0;
static bool audioLowPassEnabled = false;
static int32_t audioLowPassRange = 0;
static int32_t audioLowPassLeftPrev = 0;
static int32_t audioLowPassRightPrev = 0;

static const int keymap[] = {
	RETRO_DEVICE_ID_JOYPAD_A,
	RETRO_DEVICE_ID_JOYPAD_B,
	RETRO_DEVICE_ID_JOYPAD_SELECT,
	RETRO_DEVICE_ID_JOYPAD_START,
	RETRO_DEVICE_ID_JOYPAD_RIGHT,
	RETRO_DEVICE_ID_JOYPAD_LEFT,
	RETRO_DEVICE_ID_JOYPAD_UP,
	RETRO_DEVICE_ID_JOYPAD_DOWN,
	RETRO_DEVICE_ID_JOYPAD_R,
	RETRO_DEVICE_ID_JOYPAD_L,
};

/* Audio post processing */
static void _audioLowPassFilter(int16_t* buffer, int count) {
	int16_t* out = buffer;

	/* Restore previous samples */
	int32_t audioLowPassLeft = audioLowPassLeftPrev;
	int32_t audioLowPassRight = audioLowPassRightPrev;

	/* Single-pole low-pass filter (6 dB/octave) */
	int32_t factorA = audioLowPassRange;
	int32_t factorB = 0x10000 - factorA;

	int samples;
	for (samples = 0; samples < count; ++samples) {
		/* Apply low-pass filter */
		audioLowPassLeft = (audioLowPassLeft * factorA) + (out[0] * factorB);
		audioLowPassRight = (audioLowPassRight * factorA) + (out[1] * factorB);

		/* 16.16 fixed point */
		audioLowPassLeft  >>= 16;
		audioLowPassRight >>= 16;

		/* Update sound buffer */
		out[0] = (int16_t) audioLowPassLeft;
		out[1] = (int16_t) audioLowPassRight;
		out += 2;
	};

	/* Save last samples for next frame */
	audioLowPassLeftPrev = audioLowPassLeft;
	audioLowPassRightPrev = audioLowPassRight;
}

static void _loadAudioLowPassFilterSettings(void) {
	struct retro_variable var;
	audioLowPassEnabled = false;
	audioLowPassRange = (60 * 0x10000) / 100;

	var.key = "mgba_audio_low_pass_filter";
	var.value = 0;

	if (environCallback(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
		if (strcmp(var.value, "enabled") == 0) {
			audioLowPassEnabled = true;
		}
	}

	var.key = "mgba_audio_low_pass_range";
	var.value = 0;

	if (environCallback(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
		audioLowPassRange = (strtol(var.value, NULL, 10) * 0x10000) / 100;
	}
}

static void _initSensors(void) {
	if (sensorsInitDone) {
		return;
	}

	struct retro_sensor_interface sensorInterface;
	if (environCallback(RETRO_ENVIRONMENT_GET_SENSOR_INTERFACE, &sensorInterface)) {
		sensorGetCallback = sensorInterface.get_sensor_input;
		sensorStateCallback = sensorInterface.set_sensor_state;

		if (sensorStateCallback && sensorGetCallback) {
			if (sensorStateCallback(0, RETRO_SENSOR_ACCELEROMETER_ENABLE, EVENT_RATE)) {
				tiltEnabled = true;
			}

			if (sensorStateCallback(0, RETRO_SENSOR_GYROSCOPE_ENABLE, EVENT_RATE)) {
				gyroEnabled = true;
			}

			if (sensorStateCallback(0, RETRO_SENSOR_ILLUMINANCE_ENABLE, EVENT_RATE)) {
				luxSensorEnabled = true;
			}
		}
	}

	sensorsInitDone = true;
}

static void _initRumble(void) {
	if (rumbleInitDone) {
		return;
	}

	struct retro_rumble_interface rumbleInterface;
	if (environCallback(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &rumbleInterface)) {
		rumbleCallback = rumbleInterface.set_rumble_state;
	}

	rumbleInitDone = true;
}

#ifdef M_CORE_GB
static void _updateGbPal(void) {
	struct retro_variable var;
	var.key = "mgba_gb_colors";
	var.value = 0;
	if (environCallback(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
		const struct GBColorPreset* presets;
		size_t listSize = GBColorPresetList(&presets);
		size_t i;
		for (i = 0; i < listSize; ++i) {
			if (strcmp(presets[i].name, var.value) != 0) {
				continue;
			}
			mCoreConfigSetUIntValue(&core->config, "gb.pal[0]", presets[i].colors[0] & 0xFFFFFF);
			mCoreConfigSetUIntValue(&core->config, "gb.pal[1]", presets[i].colors[1] & 0xFFFFFF);
			mCoreConfigSetUIntValue(&core->config, "gb.pal[2]", presets[i].colors[2] & 0xFFFFFF);
			mCoreConfigSetUIntValue(&core->config, "gb.pal[3]", presets[i].colors[3] & 0xFFFFFF);
			mCoreConfigSetUIntValue(&core->config, "gb.pal[4]", presets[i].colors[4] & 0xFFFFFF);
			mCoreConfigSetUIntValue(&core->config, "gb.pal[5]", presets[i].colors[5] & 0xFFFFFF);
			mCoreConfigSetUIntValue(&core->config, "gb.pal[6]", presets[i].colors[6] & 0xFFFFFF);
			mCoreConfigSetUIntValue(&core->config, "gb.pal[7]", presets[i].colors[7] & 0xFFFFFF);
			mCoreConfigSetUIntValue(&core->config, "gb.pal[8]", presets[i].colors[8] & 0xFFFFFF);
			mCoreConfigSetUIntValue(&core->config, "gb.pal[9]", presets[i].colors[9] & 0xFFFFFF);
			mCoreConfigSetUIntValue(&core->config, "gb.pal[10]", presets[i].colors[10] & 0xFFFFFF);
			mCoreConfigSetUIntValue(&core->config, "gb.pal[11]", presets[i].colors[11] & 0xFFFFFF);
			core->reloadConfigOption(core, "gb.pal", NULL);
			break;
		}
	}
}
#endif

static void _reloadSettings(void) {
	struct mCoreOptions opts = {
		.useBios = true,
		.volume = 0x100,
	};

	struct retro_variable var;
#ifdef M_CORE_GB
	enum GBModel model;
	const char* modelName;

	var.key = "mgba_gb_model";
	var.value = 0;
	if (environCallback(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
		if (strcmp(var.value, "Game Boy") == 0) {
			model = GB_MODEL_DMG;
		} else if (strcmp(var.value, "Super Game Boy") == 0) {
			model = GB_MODEL_SGB;
		} else if (strcmp(var.value, "Game Boy Color") == 0) {
			model = GB_MODEL_CGB;
		} else if (strcmp(var.value, "Super Game Boy Color") == 0) {
			model = GB_MODEL_SCGB;
		} else if (strcmp(var.value, "Game Boy Advance") == 0) {
			model = GB_MODEL_AGB;
		} else {
			model = GB_MODEL_AUTODETECT;
		}

		modelName = GBModelToName(model);
		mCoreConfigSetDefaultValue(&core->config, "gb.model", modelName);
		mCoreConfigSetDefaultValue(&core->config, "sgb.model", modelName);
		mCoreConfigSetDefaultValue(&core->config, "cgb.model", modelName);
		mCoreConfigSetDefaultValue(&core->config, "cgb.hybridModel", modelName);
		mCoreConfigSetDefaultValue(&core->config, "cgb.sgbModel", modelName);
	}

	var.key = "mgba_sgb_borders";
	var.value = 0;
	if (environCallback(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
		mCoreConfigSetDefaultIntValue(&core->config, "sgb.borders", strcmp(var.value, "ON") == 0);
	}

	var.key = "mgba_gb_colors_preset";
	var.value = 0;
	if (environCallback(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
		mCoreConfigSetDefaultIntValue(&core->config, "gb.colors", strtol(var.value, NULL, 10));
	}

	_updateGbPal();
#endif

	var.key = "mgba_use_bios";
	var.value = 0;
	if (environCallback(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
		opts.useBios = strcmp(var.value, "ON") == 0;
	}

	var.key = "mgba_skip_bios";
	var.value = 0;
	if (environCallback(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
		opts.skipBios = strcmp(var.value, "ON") == 0;
	}

	var.key = "mgba_frameskip";
	var.value = 0;
	if (environCallback(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
		opts.frameskip = strtol(var.value, NULL, 10);
	}

	_loadAudioLowPassFilterSettings();

	var.key = "mgba_idle_optimization";
	var.value = 0;
	if (environCallback(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
		if (strcmp(var.value, "Don't Remove") == 0) {
			mCoreConfigSetDefaultValue(&core->config, "idleOptimization", "ignore");
		} else if (strcmp(var.value, "Remove Known") == 0) {
			mCoreConfigSetDefaultValue(&core->config, "idleOptimization", "remove");
		} else if (strcmp(var.value, "Detect and Remove") == 0) {
			mCoreConfigSetDefaultValue(&core->config, "idleOptimization", "detect");
		}
	}

#ifdef M_CORE_GBA
	var.key = "mgba_force_gbp";
	var.value = 0;
	if (environCallback(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
		mCoreConfigSetDefaultIntValue(&core->config, "gba.forceGbp", strcmp(var.value, "ON") == 0);
	}
#endif

	mCoreConfigLoadDefaults(&core->config, &opts);
	mCoreLoadConfig(core);
}

static void _doDeferredSetup(void) {
	// Libretro API doesn't let you know when it's done copying data into the save buffers.
	// On the off-hand chance that a core actually expects its buffers to be populated when
	// you actually first get them, you're out of luck without workarounds. Yup, seriously.
	// Here's that workaround, but really the API needs to be thrown out and rewritten.
	struct VFile* save = VFileFromMemory(savedata, GBA_SIZE_FLASH1M);
	if (!core->loadSave(core, save)) {
		save->close(save);
	}
	deferredSetup = false;
}

unsigned retro_api_version(void) {
	return RETRO_API_VERSION;
}

void retro_set_environment(retro_environment_t env) {
	environCallback = env;

#ifdef M_CORE_GB
	const struct GBColorPreset* presets;
	size_t listSize = GBColorPresetList(&presets);

	size_t colorOpt;
	for (colorOpt = 0; option_defs_us[colorOpt].key; ++colorOpt) {
		if (strcmp(option_defs_us[colorOpt].key, "mgba_gb_colors") == 0) {
			break;
		}
	}
	size_t i;
	for (i = 0; i < listSize && i < RETRO_NUM_CORE_OPTION_VALUES_MAX; ++i) {
		option_defs_us[colorOpt].values[i].value = presets[i].name;
	}
#endif

	bool categoriesSupported;
	libretro_set_core_options(environCallback, &categoriesSupported);
}

void retro_set_video_refresh(retro_video_refresh_t video) {
	videoCallback = video;
}

void retro_set_audio_sample(retro_audio_sample_t audio) {
	UNUSED(audio);
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t audioBatch) {
	audioCallback = audioBatch;
}

void retro_set_input_poll(retro_input_poll_t inputPoll) {
	inputPollCallback = inputPoll;
}

void retro_set_input_state(retro_input_state_t input) {
	inputCallback = input;
}

void retro_get_system_info(struct retro_system_info* info) {
	info->need_fullpath = false;
#ifdef M_CORE_GB
	info->valid_extensions = "gba|gb|gbc|sgb";
#else
	info->valid_extensions = "gba";
#endif
	info->library_version = projectVersion;
	info->library_name = projectName;
	info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info* info) {
	unsigned width, height;
	core->currentVideoSize(core, &width, &height);
	info->geometry.base_width = width;
	info->geometry.base_height = height;
	info->geometry.aspect_ratio = width / (double) height;

	core->baseVideoSize(core, &width, &height);
	info->geometry.max_width = width;
	info->geometry.max_height = height;

	info->timing.fps = core->frequency(core) / (float) core->frameCycles(core);
	info->timing.sample_rate = core->audioSampleRate(core);
#ifdef M_CORE_GBA
	if (fixedAudioToneEnabled || fixedAudioOutput.active || b6jjAudio) {
		info->timing.sample_rate = fixedAudioClock.outputSampleRate;
	}
#endif
}

void retro_init(void) {
	enum retro_pixel_format fmt;
#ifdef COLOR_16_BIT
#ifdef COLOR_5_6_5
	fmt = RETRO_PIXEL_FORMAT_RGB565;
#else
#warning This pixel format is unsupported. Please use -DCOLOR_16-BIT -DCOLOR_5_6_5
	fmt = RETRO_PIXEL_FORMAT_0RGB1555;
#endif
#else
#warning This pixel format is unsupported. Please use -DCOLOR_16-BIT -DCOLOR_5_6_5
	fmt = RETRO_PIXEL_FORMAT_XRGB8888;
#endif
	environCallback(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);

	struct retro_input_descriptor inputDescriptors[] = {
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "R" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "L" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3, "Brighten Solar Sensor" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3, "Darken Solar Sensor" },
		{ 0 }
	};
	environCallback(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, &inputDescriptors);

	useBitmasks = environCallback(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL);

	// TODO: RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME when BIOS booting is supported

	rumbleInitDone = false;
	mRumbleIntegratorInit(&rumble);
	rumble.setRumble = _setRumble;
	rumbleCallback = 0;

	sensorsInitDone = false;
	sensorGetCallback = 0;
	sensorStateCallback = 0;

	tiltEnabled = false;
	gyroEnabled = false;
	rotation.sample = _updateRotation;
	rotation.readTiltX = _readTiltX;
	rotation.readTiltY = _readTiltY;
	rotation.readGyroZ = _readGyroZ;

	envVarsUpdated = true;
	luxSensorUsed = false;
	luxSensorEnabled = false;
	luxLevelIndex = 0;
	luxLevel = 0;
	lux.readLuminance = _readLux;
	lux.sample = _updateLux;
	_updateLux(&lux);
	if (luxSensorUsed && !luxSensorEnabled) {
		// No illuminance sensor was found during startup, but it might finish setup before the first frame
		sensorsInitDone = false;
	}

	struct retro_log_callback log;
	if (environCallback(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log)) {
		logCallback = log.log;
	} else {
		logCallback = 0;
	}
	logger.log = GBARetroLog;
	mLogSetDefaultLogger(&logger);

	stream.videoDimensionsChanged = NULL;
	stream.postAudioFrame = NULL;
	stream.postAudioBuffer = NULL;
	stream.postVideoFrame = NULL;
	stream.audioRateChanged = _audioRateChanged;

	imageSource.startRequestImage = _startImage;
	imageSource.stopRequestImage = _stopImage;
	imageSource.requestImage = _requestImage;
}

void retro_deinit(void) {
#ifdef M_CORE_GBA
	_closeGBAStemTrace();
	_closeMP2kCandidate();
	if (mp2kPcmTrace.pcm) {
		fclose(mp2kPcmTrace.pcm);
		mp2kPcmTrace.pcm = NULL;
	}
	memset(&mp2kPcmTrace, 0, sizeof(mp2kPcmTrace));
	memset(&fixedAudioClock, 0, sizeof(fixedAudioClock));
	fixedAudioToneEnabled = false;
#endif
	free(outputBuffer);

	if (audioSampleBuffer) {
		free(audioSampleBuffer);
		audioSampleBuffer = NULL;
	}
	audioSampleBufferSize = 0;
	audioSamplesPerFrameAvg = 0.0f;

	if (sensorStateCallback) {
		sensorStateCallback(0, RETRO_SENSOR_ACCELEROMETER_DISABLE, EVENT_RATE);
		sensorStateCallback(0, RETRO_SENSOR_GYROSCOPE_DISABLE, EVENT_RATE);
		sensorStateCallback(0, RETRO_SENSOR_ILLUMINANCE_DISABLE, EVENT_RATE);
		sensorGetCallback = NULL;
		sensorStateCallback = NULL;
	}

	tiltEnabled = false;
	gyroEnabled = false;
	luxSensorEnabled = false;
	sensorsInitDone = false;
	useBitmasks = false;

	audioLowPassEnabled = false;
	audioLowPassRange = 0;
	audioLowPassLeftPrev = 0;
	audioLowPassRightPrev = 0;
}

#ifdef M_CORE_GBA
static const char* const _gbaStemNames[GBA_AUDIO_STEM_COUNT] = {
	"fifo-a", "fifo-b", "direct", "psg"
};

static void _closeGBAStemTrace(void) {
	if (gbaStemFiles.enabled && core && core->platform(core) == mPLATFORM_GBA) {
		struct GBA* gba = core->board;
		if (gba->audio.stemTrace == &gbaStemFiles.trace) {
			gba->audio.stemTrace = NULL;
		}
	}
	if (gbaStemFiles.enabled) {
		mLOG(GBA_MP2K_EVENTS, INFO, "[GBA STEM] end frames=%llu",
			(unsigned long long) gbaStemFiles.frames);
		GBAAudioStemTraceDeinit(&gbaStemFiles.trace);
	}
	for (unsigned i = 0; i < GBA_AUDIO_STEM_COUNT; ++i) {
		if (gbaStemFiles.files[i]) {
			fclose(gbaStemFiles.files[i]);
		}
	}
	memset(&gbaStemFiles, 0, sizeof(gbaStemFiles));
}

static void _openGBAStemTrace(void) {
	_closeGBAStemTrace();
	const char* enabled = getenv("MGBA_GBA_AUDIO_STEMS");
	const char* prefix = getenv("MGBA_GBA_AUDIO_STEM_PREFIX");
	if (!enabled || strcmp(enabled, "1") || !prefix || !*prefix ||
	    !core || core->platform(core) != mPLATFORM_GBA || !GBAMP2kEventsEnabled(core)) {
		return;
	}
	char paths[GBA_AUDIO_STEM_COUNT][1024];
	for (unsigned i = 0; i < GBA_AUDIO_STEM_COUNT; ++i) {
		int length = snprintf(paths[i], sizeof(paths[i]), "%s-%s.s16le", prefix, _gbaStemNames[i]);
		if (length < 0 || (size_t) length >= sizeof(paths[i])) {
			mLOG(GBA_MP2K_EVENTS, ERROR, "[GBA STEM] prefix too long");
			return;
		}
		FILE* existing = fopen(paths[i], "rb");
		if (existing) {
			fclose(existing);
			mLOG(GBA_MP2K_EVENTS, ERROR, "[GBA STEM] file exists: %s", paths[i]);
			return;
		}
	}
	for (unsigned i = 0; i < GBA_AUDIO_STEM_COUNT; ++i) {
		gbaStemFiles.files[i] = fopen(paths[i], "wb");
		if (!gbaStemFiles.files[i]) {
			mLOG(GBA_MP2K_EVENTS, ERROR, "[GBA STEM] cannot open: %s", paths[i]);
			_closeGBAStemTrace();
			return;
		}
	}
	GBAAudioStemTraceInit(&gbaStemFiles.trace);
	gbaStemFiles.enabled = true;
	struct GBA* gba = core->board;
	gba->audio.stemTrace = &gbaStemFiles.trace;
	mLOG(GBA_MP2K_EVENTS, INFO, "[GBA STEM] prefix=%s format=s16le channels=2 rate=%u",
		prefix, core->audioSampleRate(core));
}

static void _writeGBAStemTrace(size_t frames) {
	if (!gbaStemFiles.enabled || !frames) {
		return;
	}
	int16_t samples[1024 * 2];
	for (unsigned stem = 0; stem < GBA_AUDIO_STEM_COUNT; ++stem) {
		struct mAudioBuffer* buffer = &gbaStemFiles.trace.buffers[stem];
		if (mAudioBufferAvailable(buffer) < frames) {
			mLOG(GBA_MP2K_EVENTS, ERROR, "[GBA STEM] underflow stem=%s at=%llu",
				_gbaStemNames[stem], (unsigned long long) gbaStemFiles.frames);
			_closeGBAStemTrace();
			return;
		}
	}
	for (size_t offset = 0; offset < frames; offset += 1024) {
		size_t count = frames - offset < 1024 ? frames - offset : 1024;
		for (unsigned stem = 0; stem < GBA_AUDIO_STEM_COUNT; ++stem) {
			struct mAudioBuffer* buffer = &gbaStemFiles.trace.buffers[stem];
			if (mAudioBufferRead(buffer, samples, count) != count ||
			    fwrite(samples, sizeof(int16_t) * 2, count, gbaStemFiles.files[stem]) != count) {
				mLOG(GBA_MP2K_EVENTS, ERROR, "[GBA STEM] write failed stem=%s at=%llu",
					_gbaStemNames[stem], (unsigned long long) (gbaStemFiles.frames + offset));
				_closeGBAStemTrace();
				return;
			}
		}
	}
	gbaStemFiles.frames += frames;
}

static void _closeMP2kPcmTrace(void) {
	if (mp2kPcmTrace.pcm) {
		mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K PCM] end frames=%llu bytes=%llu",
			(unsigned long long) mp2kPcmTrace.submittedFrames,
			(unsigned long long) mp2kPcmTrace.submittedFrames * 4);
		fclose(mp2kPcmTrace.pcm);
	}
	memset(&mp2kPcmTrace, 0, sizeof(mp2kPcmTrace));
}

static void _openMP2kPcmTrace(void) {
	_closeMP2kPcmTrace();
	const char* enabled = getenv("MGBA_MP2K_AUDIO_TRACE");
	if (!enabled || strcmp(enabled, "1") ||
	    (!GBAMP2kEventsEnabled(core) && fixedAudioBackend != FIXED_AUDIO_BACKEND_B6JJ)) {
		return;
	}
	char defaultPath[80];
	const char* path = getenv("MGBA_MP2K_AUDIO_TRACE_PATH");
	if (!path || !*path) {
		snprintf(defaultPath, sizeof(defaultPath), "mp2k-audio-%lld.s16le", (long long) time(NULL));
		path = defaultPath;
	}
	FILE* existing = fopen(path, "rb");
	if (existing) {
		fclose(existing);
		mLOG(GBA_MP2K_EVENTS, ERROR, "[MP2K PCM] file exists; refusing to overwrite: %s", path);
		return;
	}
	mp2kPcmTrace.pcm = fopen(path, "wb");
	if (!mp2kPcmTrace.pcm) {
		mLOG(GBA_MP2K_EVENTS, ERROR, "[MP2K PCM] cannot open: %s", path);
		return;
	}
	mp2kPcmTrace.enabled = true;
	mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K PCM] file=%s format=s16le channels=2 rate=%u start=0",
		path, fixedAudioBackend == FIXED_AUDIO_BACKEND_B6JJ ? 65536 : core->audioSampleRate(core));
}

static void _traceMP2kInput(uint16_t keys) {
	if (!mp2kPcmTrace.enabled || !GBAMP2kEventsEnabled(core)) {
		return;
	}
	static const char* const names[] = {
		"A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L"
	};
	uint16_t changed = keys ^ mp2kPcmTrace.previousKeys;
	mp2kPcmTrace.previousKeys = keys;
	if (!changed) {
		return;
	}
	uint64_t cycle = 0;
	uint64_t pcmSample = 0;
	GBAMP2kEventsTracePosition(core, &cycle, fixedAudioToneEnabled ? NULL : &pcmSample);
	if (fixedAudioToneEnabled) {
		pcmSample = fixedAudioClock.absoluteAudioSample;
	}
	for (unsigned i = 0; i < sizeof(names) / sizeof(*names); ++i) {
		if (changed & (1U << i)) {
			mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K INPUT] #%llu %s %s cy=%llu pcm=%llu ff=%d rate=%.3f",
				(unsigned long long) ++mp2kPcmTrace.inputSequence, names[i],
				keys & (1U << i) ? "pressed" : "released",
				(unsigned long long) cycle, (unsigned long long) pcmSample,
				mp2kCurrentFrontend.fastForwardKnown ? (int) mp2kCurrentFrontend.fastForwardActive : -1,
				mp2kCurrentFrontend.runRate);
		}
	}
}

static enum GBAMP2kFrontendThrottleMode _mp2kThrottleMode(unsigned mode) {
	switch (mode) {
	case RETRO_THROTTLE_NONE: return GBA_MP2K_THROTTLE_NORMAL;
	case RETRO_THROTTLE_FRAME_STEPPING: return GBA_MP2K_THROTTLE_FRAME_STEP;
	case RETRO_THROTTLE_FAST_FORWARD: return GBA_MP2K_THROTTLE_FAST_FORWARD;
	case RETRO_THROTTLE_SLOW_MOTION: return GBA_MP2K_THROTTLE_SLOW_MOTION;
	case RETRO_THROTTLE_REWINDING: return GBA_MP2K_THROTTLE_REWIND;
	case RETRO_THROTTLE_VSYNC: return GBA_MP2K_THROTTLE_VSYNC;
	case RETRO_THROTTLE_UNBLOCKED: return GBA_MP2K_THROTTLE_UNBLOCKED;
	default: return GBA_MP2K_THROTTLE_UNKNOWN;
	}
}

static void _updateMP2kFrontendState(void) {
	if (!GBAMP2kEventsEnabled(core) && !fixedAudioClock.enabled && fixedAudioBackend != FIXED_AUDIO_BACKEND_B6JJ) {
		return;
	}
	if (!mp2kAudioStats.initialized) {
		struct retro_perf_callback perf = { 0 };
		if (environCallback(RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &perf)) {
			mp2kAudioStats.clock = perf.get_time_usec;
		}
		mp2kAudioStats.initialized = true;
	}
	struct GBAMP2kFrontendState state = { 0 };
	state.throttleMode = GBA_MP2K_THROTTLE_UNKNOWN;
	state.runRate = -1.f;
	struct retro_throttle_state throttle = { 0 };
	if (environCallback(RETRO_ENVIRONMENT_GET_THROTTLE_STATE, &throttle)) {
		state.throttleStateKnown = true;
		state.throttleMode = _mp2kThrottleMode(throttle.mode);
		if (throttle.rate >= 0.f && throttle.rate < 1000000.f) {
			state.runRate = throttle.rate;
		}
		if (state.throttleMode != GBA_MP2K_THROTTLE_UNKNOWN) {
			state.fastForwardKnown = true;
			state.fastForwardActive = state.throttleMode == GBA_MP2K_THROTTLE_FAST_FORWARD;
		}
	}
	bool legacyFastForward = false;
	if (environCallback(RETRO_ENVIRONMENT_GET_FASTFORWARDING, &legacyFastForward)) {
		state.fastForwardApiKnown = true;
		state.fastForwardApiActive = legacyFastForward;
		if (!state.fastForwardKnown) {
			state.fastForwardKnown = true;
			state.fastForwardActive = legacyFastForward;
		}
	}
	GBAMP2kEventsSetFrontendState(core, &state);
	mp2kCurrentFrontend = state;
}

static void _loadFixedAudioMode(void) {
	struct retro_variable option = { .key = "mgba_fixed_audio_tempo_mode" };
	struct retro_variable legacy = { .key = "mgba_fixed_audio_tempo" };
	environCallback(RETRO_ENVIRONMENT_GET_VARIABLE, &option);
	environCallback(RETRO_ENVIRONMENT_GET_VARIABLE, &legacy);
	fixedAudioMode = fixedAudioResolveMode(option.value, legacy.value,
		getenv("MGBA_FIXED_AUDIO_TEMPO"), getenv("MGBA_FIXED_AUDIO_EWRAM_EXPERIMENTAL_ALL"));
	mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO MODE] selected=%s reload-required=1",
		fixedAudioMode == FIXED_AUDIO_DISABLED ? "Disabled" :
		fixedAudioMode == FIXED_AUDIO_CONSERVATIVE ? "Conservative" : "Experimental");
}

static const char* _fixedAudioBridgePath(void) {
	const char* explicitPath = getenv("MGBA_MP2K_BRIDGE_PATH");
	if (explicitPath && *explicitPath) return explicitPath;
#ifdef _WIN32
	static char path[MAX_PATH];
	HMODULE module;
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
	    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR) &_fixedAudioBridgePath, &module)) return NULL;
	DWORD length = GetModuleFileNameA(module, path, sizeof(path));
	if (!length || length >= sizeof(path)) return NULL;
	char* slash = strrchr(path, '\\');
	const char* name = "libmgba_mp2k_bridge.dll";
	if (!slash || (size_t) (slash + 1 - path) + strlen(name) >= sizeof(path)) return NULL;
	strcpy(slash + 1, name);
	return path;
#else
	return NULL;
#endif
}

static void _detectRuntimeMP2kProfile(void) {
	const char* enabled = getenv("MGBA_MP2K_RUNTIME_DETECT");
	const char* output = fixedAudioMode != FIXED_AUDIO_DISABLED ? "1" : "0";
	runtimeAudioPending = false;
	runtimeAudioPartial = false;
	runtimeScannerDetected = false;
	runtimeMp2kValidatedStarts = 0;
	runtimeProbeReason = "no MP2K scanner result";
	if (fixedAudioMode == FIXED_AUDIO_DISABLED) return;
	GBAMP2kEventsEnableKnownProfile(core);
	if (core && core->platform(core) == mPLATFORM_GBA &&
	    fixedAudioMode == FIXED_AUDIO_CONSERVATIVE && !GBAMP2kConservativeIdentity(core->board)) {
		runtimeProbeReason = "outside Conservative tested identities";
		return;
	}
	const char* unsafe = getenv("MGBA_FIXED_AUDIO_FORCE_UNSAFE_AUDIO");
	const char* strict = getenv("MGBA_FIXED_AUDIO_STRICT_OWNERSHIP");
	const struct GBAMP2kProfile* priorProfile = GBAMP2kEventsProfile(core);
	/* Known-profile routing stays conservative; detected profiles default to
	 * permissive audio ownership. Neither policy bypasses data validation. */
	runtimeAudioPermissive = output && !strcmp(output, "1") &&
		(!strict || strcmp(strict, "1")) &&
		(!priorProfile || !priorProfile->known || (unsafe && !strcmp(unsafe, "1")));
	runtimeOwnershipUnverified = 0;
	if (((!enabled || strcmp(enabled, "1")) && (!output || strcmp(output, "1"))) ||
	    !core || core->platform(core) != mPLATFORM_GBA || GBAMP2kEventsEnabled(core)) return;
	struct mLogger* logger = mLogGetContext();
	if (logger && logger->filter) mLogFilterSet(logger->filter, "gba.mp2k.events", mLOG_INFO | mLOG_WARN | mLOG_ERROR | mLOG_FATAL);
#ifdef _WIN32
	const char* bridgePath = _fixedAudioBridgePath();
	if (!bridgePath || !*bridgePath) { runtimeProbeReason = "bridge unavailable"; return; }
	HMODULE library = LoadLibraryA(bridgePath);
	if (!library) { runtimeProbeReason = "bridge load failed"; return; }
	uint32_t (*version)(void) = (void*) GetProcAddress(library, "mp2k_bridge_api_version");
	int (*scan)(const uint8_t*, size_t, struct mp2k_bridge_scan_result*, size_t, size_t*) =
		(void*) GetProcAddress(library, "mp2k_bridge_scan");
	struct mp2k_bridge_scan_result results[16] = { 0 };
	size_t count = 0;
	struct GBA* gba = core->board;
	bool scanned = version && version() == 2 && scan &&
		!scan((const uint8_t*) gba->memory.rom, gba->memory.romSize, results, 16, &count);
	runtimeScannerDetected = scanned && count > 0;
	bool built = scanned && count == 1 && results[0].player_table_reference_valid &&
	    GBAMP2kProfileBuildRuntimeWithPolicy(&runtimeMp2kProfile, runtimeMp2kFunctions, gba,
	        results[0].song_table_offset, results[0].song_count,
	        results[0].player_table_offset, results[0].player_count, results[0].sound_mode,
	        fixedAudioMode == FIXED_AUDIO_EXPERIMENTAL);
	bool installed = built && GBAMP2kEventsUseRuntimeProfile(core, &runtimeMp2kProfile);
	runtimeProbeReason = installed ? "awaiting live song/player validation" :
		!version || version() != 2 ? "bridge ABI v2 required" :
		!scanned ? "scanner failed or invalid ROM header" :
		count > 1 ? "ambiguous MP2K tables" :
		!count ? "unknown/non-MP2K driver" : "invalid or unsupported MP2K tables/SoundMode";
	FreeLibrary(library);
#else
	runtimeProbeReason = "bridge requires Windows";
#endif
}

static void _runtimeEventSink(const struct GBAMP2kEvent* raw, void* context) {
	(void) context;
	if (raw->type == GBA_MP2K_MPLAY_START && raw->parentSequence && raw->playerGuardPassed &&
	    raw->songId >= 0 && raw->playerId >= 0) {
		if (!runtimeMp2kValidatedStarts++) {
			mLOG(GBA_MP2K_EVENTS, INFO,
				"[MP2K RUNTIME] profile=runtime-mp2k confidence=runtime-validated song=%d player=%d (table, parent call, player magic)",
				raw->songId, raw->playerId);
		}
	}
	struct mAudioClockEvent semantic;
	if (GBAMP2kSemanticPushRaw(&runtimeMp2kQueue, raw, &semantic)) {
		mLOG(GBA_MP2K_EVENTS, INFO,
			"[MP2K SEMANTIC PROBE] type=%s song=%d player=%d audioSample=%llu",
			semantic.type == M_AUDIO_CLOCK_PLAY_SONG ? "PLAY_SONG" :
			semantic.type == M_AUDIO_CLOCK_STOP_SONG ? "STOP_SONG" : "FADE_PLAYER",
			semantic.songId, semantic.playerId,
			(unsigned long long) semantic.audioSampleTimestamp);
	}
}

static void _openRuntimeMP2kProbe(void) {
	const struct GBAMP2kProfile* profile = GBAMP2kEventsProfile(core);
	if (!profile || profile->known || !fixedAudioClock.enabled || mp2kCandidate.enabled) return;
	GBAMP2kSemanticReset(&runtimeMp2kQueue);
	runtimeMp2kValidatedStarts = 0;
	GBAMP2kEventsSetSink(core, _runtimeEventSink, NULL);
}

static void _openFixedAudioClock(void) {
	memset(&fixedAudioClock, 0, sizeof(fixedAudioClock));
	fixedAudioClockRuns = 0;
	fixedAudioToneEnabled = false;
	if (fixedAudioMode == FIXED_AUDIO_DISABLED) return;
	const char* enabled = getenv("MGBA_FIXED_AUDIO_CLOCK_TRACE");
	const char* runTrace = getenv("MGBA_FIXED_AUDIO_RUN_TRACE");
	fixedAudioRunTraceEnabled = (enabled && !strcmp(enabled, "1")) ||
		(runTrace && !strcmp(runTrace, "1"));
	const char* candidate = getenv("MGBA_FIXED_AUDIO_PROTOTYPE");
	const char* output = "1";
	const char* runtime = getenv("MGBA_MP2K_RUNTIME_DETECT");
	if (((!enabled || strcmp(enabled, "1")) && (!candidate || strcmp(candidate, "1")) &&
	    (!output || strcmp(output, "1")) && (!runtime || strcmp(runtime, "1"))) ||
	    !core || core->platform(core) != mPLATFORM_GBA) {
		return;
	}
	double nominal = (double) core->frequency(core) / core->frameCycles(core);
	/* The validated profile fixes the frontend rate independently of native FF. */
	const struct GBAMP2kProfile* profile = GBAMP2kEventsProfile(core);
	unsigned outputRate = output && !strcmp(output, "1") && profile ?
		profile->outputSampleRate : core->audioSampleRate(core);
	mAudioClockInit(&fixedAudioClock, nominal, outputRate);
	if (fixedAudioClock.enabled) {
		const char* tone = getenv("MGBA_FIXED_AUDIO_CLOCK_TONE");
		fixedAudioToneEnabled = tone && !strcmp(tone, "1") &&
			(!candidate || strcmp(candidate, "1"));
		mLOG(GBA_MP2K_EVENTS, INFO,
			"[AUDIO CLOCK] enabled nominalRate=%.6f outputRate=%.0f mode=%s",
			fixedAudioClock.nominalRunRate, fixedAudioClock.outputSampleRate,
			fixedAudioToneEnabled ? "diagnostic-tone" : "trace-only");
	}
}

/* Recovery preserves the independent player's position. A transport glitch is
 * not evidence that ROM data is invalid; failed validation still latches off.
 * Eight healthy runs prevent rapid native/candidate oscillation. */
#define FIXED_AUDIO_RECOVERY_RUNS 8
#define FIXED_AUDIO_CLOCK_GRACE_RUNS 2

static void _fixedAudioDiagnostic(const char* transition) {
	if (!fixedAudioOutput.diagnostics) return;
	uint64_t cycle = 0, nativeSample = 0;
	GBAMP2kEventsTracePosition(core, &cycle, &nativeSample);
	const struct GBAMP2kProfile* profile = GBAMP2kEventsProfile(core);
	bool loaded = false;
#ifdef _WIN32
	loaded = mp2kCandidate.library && mp2kCandidate.player;
#endif
	static const char* const semanticNames[] = {
		"SONG_START", "SONG_STOP", "MPLAY_START", "MPLAY_STOP", "MPLAY_CONTINUE",
		"FADE_OUT", "NATIVE_FIRST_TICK", "UNKNOWN_CALL"
	};
	const char* semanticName = (unsigned) fixedAudioOutput.lastSemantic < sizeof(semanticNames) / sizeof(*semanticNames) ?
		semanticNames[fixedAudioOutput.lastSemantic] : "UNKNOWN_CALL";
	/* Each line fits the existing 256-byte libretro logger buffer. */
	mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO DIAG] run=%llu transition=%s fixed_audio_enabled=%d candidate_active=%d native_fallback=%d fallback_is_sticky=%d",
		(unsigned long long) fixedAudioClockRuns, transition, (int) fixedAudioOutput.requested,
		(int) fixedAudioOutput.active, (int) !fixedAudioOutput.active, (int) fixedAudioOutput.hardFallback);
	mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO DIAG] run=%llu fallback_reason=%s bridge_loaded=%d bridge_render_result=%d healthy_runs=%u recovery_count=%llu",
		(unsigned long long) fixedAudioClockRuns, fixedAudioOutput.fallbackReason ? fixedAudioOutput.fallbackReason : "NONE",
		(int) loaded, fixedAudioOutput.bridgeRenderResult, fixedAudioOutput.healthyRuns,
		(unsigned long long) fixedAudioOutput.recoveryCount);
	mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO DIAG] run=%llu ownership_state=%s ownership_confidence=%u unverified_ownership=%llu song_id=%d player_id=%d semantic_event=%s(%d)",
		(unsigned long long) fixedAudioClockRuns,
		runtimeOwnershipUnverified ? "unverified-observed" : profile && profile->known ? "known" : "observed",
		runtimeOwnershipUnverified ? 0 : profile ? profile->known ? 3 : 1 : 0, (unsigned long long) runtimeOwnershipUnverified,
		fixedAudioOutput.lastSong, fixedAudioOutput.lastPlayer, semanticName, fixedAudioOutput.lastSemantic);
	mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO DIAG] run=%llu frontend_speed_ratio=%.6f frontend_rate=%.6f throttle_known=%d throttle_mode=%d fixed_clock_state=%s clock_rate=%.6f",
		(unsigned long long) fixedAudioClockRuns, fixedAudioClock.nominalRunRate > 0 ? mp2kCurrentFrontend.runRate / fixedAudioClock.nominalRunRate : 0,
		(double) mp2kCurrentFrontend.runRate, (int) mp2kCurrentFrontend.throttleStateKnown,
		(int) mp2kCurrentFrontend.throttleMode, fixedAudioOutput.clockSupported ? "SUPPORTED" :
		fixedAudioOutput.lastSupportedRate && !fixedAudioOutput.hardFallback ? "BOUNDED_LAST_VALID_RATE" : "UNSUPPORTED",
		fixedAudioClock.frontendRunRate);
	mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO DIAG] run=%llu clock_sample=%llu ring_fill=%llu underrun_count=%llu overrun_count=%llu candidate_sample_position=%llu native_sample_position=%llu",
		(unsigned long long) fixedAudioClockRuns, (unsigned long long) fixedAudioClock.absoluteAudioSample,
		(unsigned long long) (fixedAudioOutput.writeFrame - fixedAudioOutput.readFrame),
		(unsigned long long) fixedAudioOutput.underrunCount, (unsigned long long) fixedAudioOutput.overrunCount,
		(unsigned long long) mp2kCandidate.frames, (unsigned long long) fixedAudioOutput.nativeSamplePosition);
	mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO DIAG] run=%llu native_trace_sample_position=%llu game_cycle=%llu queue_size=%zu queue_dropped=%llu queue_unresolved=%llu",
		(unsigned long long) fixedAudioClockRuns, (unsigned long long) nativeSample, (unsigned long long) cycle,
		mp2kCandidate.queue.size, (unsigned long long) mp2kCandidate.queue.dropped,
		(unsigned long long) mp2kCandidate.queue.unresolvedStarts);
}

static void _setFixedAudioFallback(const char* reason, bool hard) {
	if (fixedAudioOutput.hardFallback || (!fixedAudioOutput.active &&
	    !runtimeAudioPending && !fixedAudioOutput.recovering)) return;
	bool changed = fixedAudioOutput.active || runtimeAudioPending || hard;
	runtimeAudioPending = false;
	fixedAudioOutput.active = false;
	fixedAudioOutput.recovering = !hard;
	fixedAudioOutput.hardFallback = hard;
	fixedAudioOutput.fallbackReason = reason;
	fixedAudioOutput.healthyRuns = 0;
	if (!changed) return;
	fixedAudioOutput.outputRateRefreshPending = true;
	++fixedAudioOutput.fallbackCount;
	mLOG(GBA_MP2K_EVENTS, WARN,
		"[FIXED AUDIO] SAFE FALLBACK reason=%s class=%s sticky=%d count=%llu underrun=%llu overrun=%llu",
		reason, hard ? "HARD" : "TRANSIENT", (int) hard,
		(unsigned long long) fixedAudioOutput.fallbackCount,
		(unsigned long long) fixedAudioOutput.underrunCount, (unsigned long long) fixedAudioOutput.overrunCount);
	mLOG(GBA_MP2K_EVENTS, WARN, "[FIXED AUDIO] status=FALLBACK audio=native class=%s sticky=%d reason=%s",
		hard ? "HARD" : "TRANSIENT", (int) hard, reason);
	_fixedAudioDiagnostic("fallback");
}

static void _fixedAudioFallback(const char* reason) {
	_setFixedAudioFallback(reason, true);
}

static void _fixedAudioTransient(const char* reason) {
	_setFixedAudioFallback(reason, false);
}

static bool _fixedAudioAppend(const int16_t* pcm, size_t frames) {
	if (!fixedAudioOutput.active && !fixedAudioOutput.recovering) return true;
	if (stateLoadStage == STATE_LOAD_RECOVERING && frames > fixedAudioOutput.capacity) {
		/* Silent catch-up may produce more than one ring. Retain only its
		 * current tail; old reconstruction PCM must never reach the frontend. */
		pcm += (frames - fixedAudioOutput.capacity) * 2;
		frames = fixedAudioOutput.capacity;
	}
	if (!fixedAudioOutput.ring || !fixedAudioOutput.capacity ||
	    fixedAudioOutput.writeFrame < fixedAudioOutput.readFrame ||
	    fixedAudioOutput.writeFrame - fixedAudioOutput.readFrame > fixedAudioOutput.capacity) {
		_fixedAudioFallback("INVALID_RING_BOUNDS"); return false;
	}
	if (fixedAudioOutput.recovering && frames <= fixedAudioOutput.capacity &&
	    frames > fixedAudioOutput.capacity - (size_t) (fixedAudioOutput.writeFrame - fixedAudioOutput.readFrame))
		fixedAudioOutput.readFrame = fixedAudioOutput.writeFrame - (fixedAudioOutput.capacity - frames);
	const char* overflow = getenv("MGBA_FIXED_AUDIO_INJECT_RING_OVERRUN_AT");
	bool injected = overflow && fixedAudioClockRuns == strtoull(overflow, NULL, 10);
	if (injected || frames > fixedAudioOutput.capacity - (size_t) (fixedAudioOutput.writeFrame - fixedAudioOutput.readFrame)) {
		++fixedAudioOutput.overrunCount;
		_fixedAudioTransient("RING_OVERRUN");
		fixedAudioOutput.readFrame = fixedAudioOutput.writeFrame;
		return false;
	}
	for (size_t i = 0; i < frames; ++i) {
		size_t slot = (size_t) ((fixedAudioOutput.writeFrame + i) % fixedAudioOutput.capacity);
		fixedAudioOutput.ring[2 * slot] = pcm[2 * i];
		fixedAudioOutput.ring[2 * slot + 1] = pcm[2 * i + 1];
	}
	fixedAudioOutput.writeFrame += frames;
	return true;
}

static void _closeFixedAudioOutput(void) {
	_stateLoadProfileLog();
	memset(&stateLoadProfile, 0, sizeof(stateLoadProfile));
	stateLoadSeen = stateLoadWasRewind = stateLoadWaitForNativeStart = false;
	ramPlayerContinueRebindPending = ramPlayerContinueRecovery = ramPlayerContinueCompletesLoad = false;
	stateLoadStableRuns = 0;
	fixedAudioAdvertisedRate = 0;
	stateLoadStage = STATE_LOAD_NONE;
	if (stateLoadCandidatePcm) { fclose(stateLoadCandidatePcm); stateLoadCandidatePcm = NULL; }
	if (fixedAudioOutput.requested) {
		mLOG(GBA_MP2K_EVENTS, INFO,
			"[FIXED AUDIO] end candidate=%llu native=%llu callback=%llu fill=%llu underrun=%llu overrun=%llu fallback=%llu",
			(unsigned long long) fixedAudioOutput.candidateFrames,
			(unsigned long long) fixedAudioOutput.nativeFrames,
			(unsigned long long) fixedAudioOutput.callbackFrames,
			(unsigned long long) (fixedAudioOutput.writeFrame - fixedAudioOutput.readFrame),
			(unsigned long long) fixedAudioOutput.underrunCount,
			(unsigned long long) fixedAudioOutput.overrunCount,
			(unsigned long long) fixedAudioOutput.fallbackCount);
	}
	if (runtimeOwnershipUnverified) {
		mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO] unverifiedOwnership=%llu permissive=%d",
			(unsigned long long) runtimeOwnershipUnverified, (int) runtimeAudioPermissive);
	}
	if (fixedAudioOutput.callbackPcm) fclose(fixedAudioOutput.callbackPcm);
	free(fixedAudioOutput.ring);
	free(fixedAudioOutput.output);
	memset(&fixedAudioOutput, 0, sizeof(fixedAudioOutput));
}

static void _openFixedAudioOutput(void) {
	_closeFixedAudioOutput();
	if (fixedAudioMode == FIXED_AUDIO_DISABLED) return;
	fixedAudioOutput.requested = true;
	fixedAudioOutput.hardFallback = true;
	fixedAudioOutput.fallbackReason = "INITIALIZATION_FAILED";
	const char* diagnostics = getenv("MGBA_FIXED_AUDIO_DIAGNOSTICS");
	fixedAudioOutput.diagnostics = diagnostics && !strcmp(diagnostics, "1");
	fixedAudioOutput.lastSong = -1;
	fixedAudioOutput.lastPlayer = -1;
	fixedAudioOutput.lastSemantic = GBA_MP2K_UNKNOWN_CALL;
	if (runtimeAudioPermissive) mLOG(GBA_MP2K_EVENTS, WARN, "[FIXED AUDIO] EXPERIMENTAL PERMISSIVE AUDIO MODE memory/pointer/buffer checks remain enabled");
	const struct GBAMP2kProfile* profile = GBAMP2kEventsProfile(core);
	if (!fixedAudioClock.enabled || !GBAMP2kEventsEnabled(core) ||
	    !profile || !mp2kCandidate.enabled ||
	    fixedAudioClock.outputSampleRate != profile->outputSampleRate) {
		fixedAudioOutput.fallbackCount = 1;
		fixedAudioOutput.fallbackReason = !profile ? (runtimeProbeReason ? runtimeProbeReason : "INVALID_PROFILE") :
			!fixedAudioClock.enabled ? "INVALID_AUDIO_CLOCK" :
			!mp2kCandidate.enabled ? "BRIDGE_UNAVAILABLE_OR_ABI_INVALID" : "UNSUPPORTED_OUTPUT_SAMPLE_RATE";
		_fixedAudioDiagnostic("initialization-failed");
		mLOG(GBA_MP2K_EVENTS, WARN, "[FIXED AUDIO] SAFE FALLBACK reason=%s",
			!profile ? (runtimeProbeReason ? runtimeProbeReason : "profile validation") :
			!fixedAudioClock.enabled ? "independent clock unavailable" :
			!mp2kCandidate.enabled ? "bridge unavailable" : "unsupported output sample rate");
		char code[5] = "????";
		if (core && core->platform(core) == mPLATFORM_GBA) {
			struct GBA* gba = core->board;
			if (gba->memory.rom && gba->memory.romSize >= 0xB0) memcpy(code, (const uint8_t*) gba->memory.rom + 0xAC, 4);
			for (unsigned i = 0; i < 4; ++i) if (code[i] < ' ' || code[i] > '~') code[i] = '?';
		}
		mLOG(GBA_MP2K_EVENTS, WARN, "[FIXED AUDIO] game=%s profile=%s driver=%s status=%s audio=native reason=%s",
			code, profile ? profile->name : "none", profile || runtimeScannerDetected ? "MP2K" : "unknown/non-MP2K",
			profile || runtimeScannerDetected ? "FALLBACK" : "UNSUPPORTED", runtimeProbeReason ? runtimeProbeReason : "bridge/clock unavailable");
		return;
	}
	/* Profile lookahead covers the observed native tick and callback boundary. */
	fixedAudioOutput.lookahead = profile->known ? profile->lookaheadSamples :
		(size_t) ceil(fixedAudioClock.outputSampleRate / fixedAudioClock.nominalRunRate) + 32;
	fixedAudioOutput.startupRemaining = fixedAudioOutput.lookahead;
	size_t maxRun = (size_t) (fixedAudioClock.outputSampleRate / fixedAudioClock.nominalRunRate) + 2;
	fixedAudioOutput.capacity = fixedAudioOutput.lookahead + maxRun * 4;
	fixedAudioOutput.ring = calloc(fixedAudioOutput.capacity * 2, sizeof(int16_t));
	fixedAudioOutput.outputCapacity = maxRun + fixedAudioOutput.lookahead;
	fixedAudioOutput.output = calloc(fixedAudioOutput.outputCapacity * 2, sizeof(int16_t));
	if (!fixedAudioOutput.ring || !fixedAudioOutput.output) {
		fixedAudioOutput.fallbackCount = 1;
		fixedAudioOutput.fallbackReason = "RING_ALLOCATION_FAILED";
		_fixedAudioDiagnostic("initialization-failed");
		mLOG(GBA_MP2K_EVENTS, ERROR, "[FIXED AUDIO] SAFE FALLBACK reason=ring allocation failed");
		return;
	}
	const char* outputPath = getenv("MGBA_FIXED_AUDIO_OUTPUT_PATH");
	if (outputPath && *outputPath) {
		FILE* existing = fopen(outputPath, "rb");
		if (existing) { fclose(existing); fixedAudioOutput.fallbackCount = 1;
			fixedAudioOutput.fallbackReason = "OUTPUT_TRACE_EXISTS"; _fixedAudioDiagnostic("initialization-failed");
			mLOG(GBA_MP2K_EVENTS, WARN, "[FIXED AUDIO] SAFE FALLBACK reason=output trace exists"); return; }
		fixedAudioOutput.callbackPcm = fopen(outputPath, "wb");
		if (!fixedAudioOutput.callbackPcm) { fixedAudioOutput.fallbackCount = 1;
			fixedAudioOutput.fallbackReason = "OUTPUT_TRACE_OPEN_FAILED"; _fixedAudioDiagnostic("initialization-failed");
			mLOG(GBA_MP2K_EVENTS, WARN, "[FIXED AUDIO] SAFE FALLBACK reason=output trace cannot open"); return; }
	}
	fixedAudioOutput.hardFallback = false;
	fixedAudioOutput.fallbackReason = NULL;
	fixedAudioOutput.active = profile->known;
	runtimeAudioPending = !profile->known;
	/* Generic fades, SE coverage and timing have not received listening validation. */
	runtimeAudioPartial = !profile->known;
	mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO] game=%s profile=%s driver=MP2K songs=%u players=%u status=%s audio=%s confidence=%u functions=%zu permissive=%d",
		profile->gameCode, profile->name, profile->songCount, profile->playerCount, profile->known ? "VALIDATED" : "EXPERIMENTAL",
		profile->known ? "fixed" : "native-pending-validation", profile->known ? 3 : profile->confidence, profile->functionCount, (int) runtimeAudioPermissive);
	mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO] active lookahead=%zu latencyMs=%.3f capacity=%zu rate=%.0f",
		fixedAudioOutput.lookahead, fixedAudioOutput.lookahead * 1000. / fixedAudioClock.outputSampleRate,
		fixedAudioOutput.capacity, fixedAudioClock.outputSampleRate);
}

static void _candidateEventSink(const struct GBAMP2kEvent* raw, void* context) {
	struct _MP2kCandidate* candidate = context;
	const struct GBAMP2kProfile* currentProfile = GBAMP2kEventsProfile(core);
	if (!currentProfile || !candidate->enabled || fixedAudioOutput.hardFallback) return;
	/* Continue resumes an existing sequence, not a new MPlayStart. A loaded
	 * paused player may have no renderer/observer song mapping at all. Defer
	 * reconstruction until the native call and several native runs complete;
	 * never restart at the song header or mutate watches inside this sink. */
	if (currentProfile->playerBacking == GBA_MP2K_RAM_PLAYER &&
	    raw->type == GBA_MP2K_MPLAY_CONTINUE && raw->playerGuardPassed && raw->playerId >= 0 &&
	    (unsigned) raw->playerId < currentProfile->playerCount &&
	    (stateLoadStage == STATE_LOAD_NONE || stateLoadStage == STATE_LOAD_RECOVERING)) {
		const struct GBAMP2kFunction* fn = GBAMP2kProfileFunctionAt(currentProfile, raw->functionAddress);
		const struct GBAMP2kMusicPlayerInfo* native = GBAMP2kPlayerAt(core->board, currentProfile, raw->playerId);
		const struct GBAMP2kMusicPlayerTrack* tracks = native && native->trackCount ?
			GBAMP2kPlayerRam(core->board, native->tracks, native->trackCount * sizeof(*tracks)) : NULL;
		bool resumable = false;
		for (unsigned i = 0; tracks && i < native->trackCount; ++i)
			resumable |= (tracks[i].flags & 0xC0) == 0x80;
		if (fn && fn->argument == GBA_MP2K_ARG_PLAYER && native && native->songHeader && native->clock &&
		    resumable && (native->status & 0x80000000U)) {
			ramPlayerContinueRebindPending = true;
			mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO] RAM_PLAYER_CONTINUE_REBIND_PENDING player=%d address=%08x header=%08x status=%08x clock=%u reason=NATIVE_CONTINUE",
				raw->playerId, GBAMP2kEventsPlayers(core)->slots[raw->playerId].address, native->songHeader, native->status, native->clock);
			return;
		}
	}
	if (currentProfile->playerBacking == GBA_MP2K_RAM_PLAYER && raw->type == GBA_MP2K_MPLAY_START &&
	    (raw->playerId < 0 || raw->songId < 0 || !raw->playerGuardPassed)) {
		_fixedAudioTransient("RAM_PLAYER_MISMATCH");
		_stateLoadDropCandidate();
		fixedAudioOutput.recovering = false;
		stateLoadStage = STATE_LOAD_REARM_PENDING;
		stateLoadRetryWait = 0;
		stateLoadBeginRun = fixedAudioClockRuns;
		return;
	}
	fixedAudioOutput.lastSong = raw->songId;
	fixedAudioOutput.lastPlayer = raw->playerId;
	fixedAudioOutput.lastSemantic = raw->type;
	if (raw->type != GBA_MP2K_NATIVE_FIRST_TICK) _fixedAudioDiagnostic("semantic");
	if (!currentProfile->known && raw->type != GBA_MP2K_NATIVE_FIRST_TICK) {
		if ((raw->type == GBA_MP2K_MPLAY_STOP || raw->type == GBA_MP2K_FADE_OUT || raw->type == GBA_MP2K_MPLAY_CONTINUE) &&
		    raw->songId < 0 && raw->playerId >= 0 && (unsigned) raw->playerId < currentProfile->playerCount) return;
		if (raw->songId < 0 || (unsigned) raw->songId >= currentProfile->songCount ||
		    raw->playerId < 0 || (unsigned) raw->playerId >= currentProfile->playerCount) {
			mLOG(GBA_MP2K_EVENTS, WARN, "[FIXED AUDIO] INVALID_RUNTIME_EVENT game=%s type=%u song=%d player=%d pc=%08x guard=%d detailed_reason=%s songCount=%u playerCount=%u",
				currentProfile->gameCode, raw->type, raw->songId, raw->playerId, raw->programCounter, raw->playerGuardPassed,
				raw->songId >= 0 && (unsigned) raw->songId >= currentProfile->songCount ? "SONG_INDEX_OOB" : raw->playerId < 0 ? "UNKNOWN_PLAYER_POINTER" :
				(unsigned) raw->playerId >= currentProfile->playerCount ? "PLAYER_INDEX_OOB" : "UNKNOWN_SONG_HEADER",
				currentProfile->songCount, currentProfile->playerCount);
			_fixedAudioFallback("invalid runtime song/player event"); return;
		}
		if ((raw->type == GBA_MP2K_MPLAY_START || raw->type == GBA_MP2K_MPLAY_STOP || raw->type == GBA_MP2K_FADE_OUT) && !raw->playerGuardPassed) {
			_fixedAudioFallback("runtime player magic validation failed"); return;
		}
		if (currentProfile->playerBacking == GBA_MP2K_RAM_PLAYER && raw->type == GBA_MP2K_MPLAY_START)
			GBAMP2kPlayerSetState(GBAMP2kEventsPlayers(core), currentProfile, raw->playerId, GBA_MP2K_PLAYER_ARMED);
		if (raw->type == GBA_MP2K_MPLAY_START && runtimeAudioPending && stateLoadStage == STATE_LOAD_NONE) {
			if (!fixedAudioClock.frontendRateKnown || fixedAudioClock.unlimited || !fixedAudioClock.runAdvance) {
				_fixedAudioFallback("unsupported runtime output clock"); return;
			}
			runtimeAudioPending = false;
			++runtimeMp2kValidatedStarts;
			runtimeMp2kProfile.confidence = 3;
			fixedAudioOutput.active = true;
			fixedAudioOutput.outputRateRefreshPending = true;
			if (stateLoadWaitForNativeStart) {
				mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO] %s runs=%llu state=ACTIVE mode=RAM_PLAYER_NATIVE_START player=%d song=%d",
					ramPlayerContinueRecovery && !ramPlayerContinueCompletesLoad ? "RAM_PLAYER_CONTINUE_RECOVERED" : "STATE_LOAD_RECOVERED",
					(unsigned long long) (fixedAudioClockRuns - stateLoadBeginRun), raw->playerId, raw->songId);
				if (stateLoadWasRewind) mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO] REWIND_RECOVERED runs=%llu state=ACTIVE mode=RAM_PLAYER_NATIVE_START",
					(unsigned long long) (fixedAudioClockRuns - stateLoadBeginRun));
				stateLoadWaitForNativeStart = stateLoadWasRewind = false;
				ramPlayerContinueRecovery = ramPlayerContinueCompletesLoad = false;
			}
			mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO] game=%s profile=runtime-mp2k status=%s audio=fixed coverage=PARTIAL se=unverified fade=unverified confidence=runtime-validated permissive=%d",
				currentProfile->gameCode, runtimeAudioPartial || runtimeAudioPermissive ? "PARTIAL" : "EXPERIMENTAL", (int) runtimeAudioPermissive);
		}
	}
	if (raw->type == GBA_MP2K_NATIVE_FIRST_TICK) {
		for (size_t i = 0; i < candidate->queue.size; ++i) {
			struct mAudioClockEvent* queued = &candidate->queue.events[i];
			if (queued->type == M_AUDIO_CLOCK_PLAY_SONG && !queued->targetFirstTickKnown &&
			    queued->sequence == raw->parentSequence &&
			    queued->playerId == raw->playerId && queued->songId == raw->songId &&
			    raw->audioSampleTimestampKnown) {
				queued->targetFirstTickAudioSample = raw->audioSampleTimestamp;
				queued->targetFirstTickKnown = true;
				break;
			}
		}
		return;
	}
	const struct GBAMP2kProfile* profile = GBAMP2kEventsProfile(core);
	if (profile && raw->playerId >= 0 && (unsigned) raw->playerId < profile->playerCount) {
		if (raw->type == GBA_MP2K_MPLAY_START) {
			candidate->activeSong[raw->playerId] = raw->songId;
		} else if (raw->type == GBA_MP2K_MPLAY_STOP) {
			candidate->activeSong[raw->playerId] = -1;
			if (profile->playerBacking == GBA_MP2K_RAM_PLAYER)
				GBAMP2kPlayerSetState(GBAMP2kEventsPlayers(core), profile, raw->playerId, GBA_MP2K_PLAYER_DISCOVERED);
		}
	}
	struct mAudioClockEvent semantic;
	if (raw->type == GBA_MP2K_SONG_START && candidate->queue.pendingStartValid) {
		mLOG(GBA_MP2K_EVENTS, INFO,
			"[MP2K SEMANTIC] UNKNOWN start seq=%llu song=%d player=%d (no matching MPLAY_START)",
			(unsigned long long) candidate->queue.pendingStart.sequence,
			candidate->queue.pendingStart.songId, candidate->queue.pendingStart.playerId);
		_fixedAudioFallback("unmatched semantic song start");
	}
	if (GBAMP2kSemanticPushRaw(&candidate->queue, raw, &semantic)) {
		if (semantic.type == M_AUDIO_CLOCK_PLAY_SONG) {
			if (!currentProfile->known && !raw->functionAddress) {
				for (size_t i = 0; i < candidate->queue.size; ++i) {
					if (candidate->queue.events[i].sequence == semantic.sequence) {
						candidate->queue.events[i].targetFirstTickKnown = true;
						candidate->queue.events[i].targetFirstTickAudioSample = semantic.audioSampleTimestamp;
					}
				}
			}
			const uint8_t* rom = (const uint8_t*) ((struct GBA*) core->board)->memory.rom;
			uint32_t header = 0;
			if (profile && semantic.songId >= 0 && (unsigned) semantic.songId < profile->songCount) {
				memcpy(&header, rom + profile->songTableOffset + semantic.songId * 8, sizeof(header));
			}
			if (profile && header >= 0x08000000 &&
			    header < 0x08000000 + profile->romSize &&
			    !rom[header - 0x08000000]) {
				for (size_t i = 0; i < candidate->queue.size; ++i) {
					struct mAudioClockEvent* queued = &candidate->queue.events[i];
					if (queued->sequence == semantic.sequence &&
					    queued->type == M_AUDIO_CLOCK_PLAY_SONG) {
						queued->noSequencerTracks = true;
						queued->targetFirstTickKnown = true;
						break;
					}
				}
			}
			for (size_t i = 0; i + 1 < candidate->queue.size;) {
				struct mAudioClockEvent* old = &candidate->queue.events[i];
				if (old->type == M_AUDIO_CLOCK_PLAY_SONG && !old->targetFirstTickKnown &&
				    old->playerId == semantic.playerId && old->sequence != semantic.sequence) {
					--candidate->queue.size;
					memmove(old, old + 1, (candidate->queue.size - i) * sizeof(*old));
				} else {
					++i;
				}
			}
		}
		mLOG(GBA_MP2K_EVENTS, INFO,
			"[MP2K SEMANTIC] seq=%llu src=%llu type=%s song=%d player=%d cycle=%llu audioSample=%llu",
			(unsigned long long) semantic.sequence, (unsigned long long) semantic.sourceSequence,
			semantic.type == M_AUDIO_CLOCK_PLAY_SONG ? "PLAY_SONG" :
			semantic.type == M_AUDIO_CLOCK_FADE_PLAYER ? "FADE_PLAYER" : "STOP_SONG",
			semantic.songId, semantic.playerId, (unsigned long long) semantic.gameCycle,
			(unsigned long long) semantic.audioSampleTimestamp);
	}
}

/* Ownership uncertainty can affect audio compatibility, but cannot relax
 * pointer, table, buffer, clock or render validation. Those failures use
 * _fixedAudioFallback directly, independently of this Preview policy. */
static void _ownershipFallback(const char* reason) {
	bool firstUnverified = !runtimeOwnershipUnverified++;
	if (!runtimeAudioPermissive) {
		_fixedAudioFallback(reason);
		return;
	}
	if (firstUnverified) {
		runtimeAudioPartial = true;
		mLOG(GBA_MP2K_EVENTS, WARN,
			"[FIXED AUDIO] status=PARTIAL reason=unverified-audio-ownership audio=candidate detail=%s", reason);
	}
}

static bool _runtimeFifoOwned(struct GBA* gba, uint32_t source) {
	const struct GBAMP2kProfile* profile = GBAMP2kEventsProfile(core);
	if (!profile || profile->known || !profile->soundInfoPointerOffset ||
	    profile->soundInfoPointerOffset > profile->romSize - 4) return false;
	uint32_t address;
	memcpy(&address, (const uint8_t*) gba->memory.rom + profile->soundInfoPointerOffset, 4);
	if ((address & 3) || address < 0x03000000 || address > 0x03008000 - sizeof(struct GBAMP2kContext)) return false;
	const struct GBAMP2kContext* sound = (const void*) ((const uint8_t*) gba->memory.iwram + address - 0x03000000);
	if ((sound->magic != MP2K_MAGIC && sound->magic != MP2K_MAGIC + 1) ||
	    sound->freq != ((profile->soundMode >> 16) & 15) ||
	    !sound->maxChans || sound->maxChans > 12 || sound->pcmSamplesPerVBlank <= 0 ||
	    sound->pcmSamplesPerVBlank > 4096 || !sound->pcmDmaPeriod || sound->pcmDmaPeriod > 16) return false;
	uint32_t length = sound->pcmSamplesPerVBlank * sound->pcmDmaPeriod;
	uint32_t first = address + sizeof(struct GBAMP2kContext);
	if (length > (0x03008000 - first) / 2) return false;
	/* MP2K's FIFO buffers immediately follow the validated SoundInfo. Their
	 * length comes from the live driver, not a title-specific FIFO address. */
	return source == first || source == first + length;
}

static uint64_t runtimePollingSequence;
static void _pollRuntimeMP2kPlayers(void) {
	const struct GBAMP2kProfile* profile = GBAMP2kEventsProfile(core);
	if (!profile || profile->known || !mp2kCandidate.enabled ||
	    (!fixedAudioOutput.active && !runtimeAudioPending && !fixedAudioOutput.recovering)) return;
	struct GBA* gba = core->board;
	const uint8_t* rom = (const uint8_t*) gba->memory.rom;
	for (unsigned id = 0; id < profile->playerCount; ++id) {
		uint32_t address, expectedTracks;
		memcpy(&address, rom + profile->playerTableOffset + id * 12, 4);
		memcpy(&expectedTracks, rom + profile->playerTableOffset + id * 12 + 4, 4);
		if (!address && !expectedTracks && !rom[profile->playerTableOffset + id * 12 + 8]) continue;
		const struct GBAMP2kMusicPlayerInfo* player = GBAMP2kPlayerAt(gba, profile, id);
		if (!player) { _fixedAudioFallback("polled player pointer outside RAM"); return; }
		if (profile->playerBacking == GBA_MP2K_RAM_PLAYER) {
			struct GBAMP2kPlayerRegistry* registry = GBAMP2kEventsPlayers(core);
			int valid = GBAMP2kPlayerRefresh(registry, gba, profile, id);
			if (valid < 0) { _fixedAudioFallback("RAM player validation failed"); return; }
			if (registry->invalidated) {
				/* A runtime SoundInit reuses these RAM objects. Use the same
				 * bounded rebind pipeline as a native state load, never stale PCM. */
				fixedAudioOutput.active = false;
				fixedAudioOutput.recovering = false;
				fixedAudioOutput.outputRateRefreshPending = true;
				fixedAudioOutput.fallbackReason = "RAM_PLAYER_REINITIALIZED";
				_stateLoadDropCandidate();
				GBAMP2kEventsRebind(core, NULL);
				GBAMP2kEventsSetAudioClock(core, &fixedAudioClock);
				stateLoadStage = STATE_LOAD_REARM_PENDING;
				stateLoadBeginRun = fixedAudioClockRuns;
				stateLoadRetryWait = 0;
				runtimeAudioPending = true;
				return;
			}
			if (!valid) continue;
			/* Initialized unused slots are not active, and cannot start audio. */
			if (!registry->slots[id].startConfirmed) continue;
			if (registry->slots[id].state == GBA_MP2K_PLAYER_ARMED && fixedAudioOutput.active && mp2kCandidate.get_state) {
				struct mp2k_bridge_state state;
				if (!mp2kCandidate.get_state(mp2kCandidate.player, id, &state) &&
				    state.song_header_offset + 0x08000000 == registry->slots[id].lastHeader)
					GBAMP2kPlayerSetState(registry, profile, id, GBA_MP2K_PLAYER_ACTIVE);
			}
		}
		if (player->magic != MP2K_MAGIC) continue; /* SoundInit may not have run. */
		if (player->trackCount > 16 || player->tracks != expectedTracks) {
			_fixedAudioFallback("polled player/track validation failed"); return;
		}
		int song = -1;
		for (unsigned i = 0; i < profile->songCount; ++i) {
			uint32_t header;
			memcpy(&header, rom + profile->songTableOffset + i * 8, 4);
			if (header == player->songHeader && (profile->playerBacking == GBA_MP2K_RAM_PLAYER || rom[profile->songTableOffset + i * 8 + 4] == id)) { song = i; break; }
		}
		bool stopped = (player->status & 0x80000000U) || !player->trackCount;
		if (profile->playerBacking == GBA_MP2K_RAM_PLAYER && !stopped) continue;
		if (song < 0 || (song == mp2kCandidate.activeSong[id] && !stopped) ||
		    (stopped && mp2kCandidate.activeSong[id] < 0)) continue;
		if (stopped && song == 202 && mp2kCandidate.activeSong[id] == 202 &&
		    GBAMP2kEventsTakeAorjNaturalFinish(core, id)) {
			/* Native FINE already owns native completion. At checked 2x/3x it
			 * must not become a Stop on a still-running independent sequence.
			 * No timer or queued STOP is deferred; the bridge reaches its own
			 * FINE. Real native Stop remains observed for this lifetime. */
			mp2kCandidate.activeSong[id] = -1;
			mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K NATURAL COMPLETE] player=%u song=202 nativeClock=%u action=ALLOW_INDEPENDENT_FINE", id, player->clock);
			continue;
		}
		if (!runtimeAudioPartial && fixedAudioOutput.active)
			mLOG(GBA, INFO, "[FIXED AUDIO] game=%s status=PARTIAL audio=fixed semantic=polling phase=frame-level fade=unverified", profile->gameCode);
		runtimeAudioPartial = true;
		struct GBAMP2kEvent event = { 0 };
		event.type = stopped ? GBA_MP2K_MPLAY_STOP : GBA_MP2K_MPLAY_START;
		event.playerId = id;
		event.songId = stopped ? mp2kCandidate.activeSong[id] : song;
		event.playerGuardPassed = true;
		event.audioSampleTimestampKnown = true;
		event.audioSampleTimestamp = fixedAudioClock.runStartSample + fixedAudioClock.runAdvance;
		event.sequence = (UINT64_C(1) << 63) | ++runtimePollingSequence;
		GBAMP2kEventsTracePosition(core, &event.gbaCycle, NULL);
		if (getenv("MGBA_MP2K_LIFETIME_FINISH_PC"))
			mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K LIFETIME POLL] sequence=%llu kind=%s player=%u song=%d status=%08x clock=%u cycle=%llu audioSample=%llu",
				(unsigned long long) event.sequence, stopped ? "STOP" : "START", id, event.songId, player->status, player->clock,
				(unsigned long long) event.gbaCycle, (unsigned long long) event.audioSampleTimestamp);
		if (getenv("MGBA_MP2K_LIFETIME_FINISH_PC")) {
			/* Read-only evidence for evaluating other polling drivers. An
			 * inactive track is not, by itself, authority to suppress Stop. */
			const struct GBAMP2kMusicPlayerTrack* tracks = (const void*) GBAMP2kPlayerRam(gba,
				player->tracks, player->trackCount * sizeof(*tracks));
			for (unsigned track = 0; tracks && track < player->trackCount; ++track)
				mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K LIFETIME POLL TRACK] player=%u track=%u flags=%02x cursor=%08x channel=%08x header=%08x",
					id, track, tracks[track].flags, tracks[track].cmdPtr, tracks[track].chan, player->songHeader);
		}
		_candidateEventSink(&event, &mp2kCandidate);
	}
}

static void _traceMP2kPSGWrite(struct GBA* gba, uint32_t address, uint16_t value) {
	if (!mp2kCandidate.psgOwnerTrace && !fixedAudioOutput.active) {
		return;
	}
	int channel = -1;
	if (address >= GBA_REG_SOUND1CNT_LO && address <= GBA_REG_SOUND1CNT_X) {
		channel = 0;
	} else if (address >= GBA_REG_SOUND2CNT_LO && address <= GBA_REG_SOUND2CNT_HI) {
		channel = 1;
	} else if (address >= GBA_REG_SOUND3CNT_LO && address <= GBA_REG_SOUND3CNT_X) {
		channel = 2;
	} else if (address >= GBA_REG_SOUND4CNT_LO && address <= GBA_REG_SOUND4CNT_HI) {
		channel = 3;
	} else if (address >= GBA_REG_WAVE_RAM0_LO && address <= GBA_REG_WAVE_RAM3_HI) {
		channel = 2;
	} else {
		return;
	}
	uint32_t pc = _ARMPCAddress(gba->cpu);
	const struct GBAMP2kProfile* profile = GBAMP2kEventsProfile(core);
	bool driverPc = profile && pc >= profile->driverCodeStart &&
		pc < profile->driverCodeEnd && !gba->performingDMA;
	bool trigger = (address == GBA_REG_SOUND1CNT_X || address == GBA_REG_SOUND2CNT_HI ||
		address == GBA_REG_SOUND3CNT_X || address == GBA_REG_SOUND4CNT_HI) && (value & 0x8000);
	if (!trigger && driverPc) {
		return;
	}
	uint32_t soundAddress = 0;
	if (profile && profile->soundInfoPointerOffset &&
	    profile->soundInfoPointerOffset <= profile->romSize - sizeof(soundAddress)) {
		memcpy(&soundAddress, (const uint8_t*) gba->memory.rom +
			profile->soundInfoPointerOffset, sizeof(soundAddress));
	}
	int owner = -1;
	int track = -1;
	uint32_t trackPointer = 0;
	uint8_t status = 0;
	uint8_t type = 0;
	uint32_t cgbBase = 0;
	uint32_t ownerBase = 0;
	uint32_t ownerCount = 0;
	if (soundAddress >= 0x03000000 &&
	    soundAddress <= 0x03008000 - sizeof(struct GBAMP2kContext)) {
		const struct GBAMP2kContext* sound = (const void*)
			((const uint8_t*) gba->memory.iwram + soundAddress - 0x03000000);
		cgbBase = sound->cgbChans;
		/* MP2K's SoundChannel entries are 0x40 bytes apart. */
		uint32_t channelAddress = sound->cgbChans + channel * 0x40;
		if (channelAddress >= 0x03000000 &&
		    channelAddress <= 0x03008000 - sizeof(struct GBAMP2kSoundChannel)) {
			const struct GBAMP2kSoundChannel* voice = (const void*)
				((const uint8_t*) gba->memory.iwram + channelAddress - 0x03000000);
			trackPointer = voice->track;
			status = voice->status;
			type = voice->type;
			const uint8_t* rom = (const uint8_t*) gba->memory.rom;
			for (unsigned player = 0; profile && player < profile->playerCount && owner < 0; ++player) {
				uint32_t base;
				uint32_t count;
				memcpy(&base, rom + profile->playerTableOffset + player * 12 + 4, sizeof(base));
				memcpy(&count, rom + profile->playerTableOffset + player * 12 + 8, sizeof(count));
				if (count > 16 || !((base >= 0x02000000 && base < 0x02040000) ||
				                     (base >= 0x03000000 && base < 0x03008000))) {
					continue;
				}
				for (uint32_t i = 0; i < count; ++i) {
					if (trackPointer == base + i * sizeof(struct GBAMP2kMusicPlayerTrack)) {
						owner = player;
						track = i;
						ownerBase = base;
						ownerCount = count;
						break;
					}
				}
			}
		}
	}
	uint64_t cycle = 0;
	GBAMP2kEventsTracePosition(core, &cycle, NULL);
	struct GBAMP2kPsgWriteOwner ownership = {
		.sourcePC = pc, .dma = gba->performingDMA, .channel = channel,
		.voiceType = type, .voiceStatus = status, .activeTrack = trackPointer,
		.playerTrackBase = ownerBase,
		.playerTrackStride = sizeof(struct GBAMP2kMusicPlayerTrack),
		.playerTrackCount = ownerCount
	};
	const char* origin = GBAMP2kIsOwnedPsgWrite(profile, &ownership) ? "mp2k-owned" :
		!driverPc ? "external" : "driver-unowned";
	if (fixedAudioOutput.active && GBAMP2kPsgWriteNeedsNative(profile, &ownership, trigger)) {
		_ownershipFallback("unknown PSG write");
	}
	if (!mp2kCandidate.psgOwnerTrace) return;
	mLOG(GBA_MP2K_EVENTS, INFO,
		"[MP2K PSG WRITE] ch=%d reg=%03x value=%04x pc=%08x cy=%llu smp=%llu cgb=%08x st=%02x ty=%02x ptr=%08x p=%d t=%d s=%d origin=%s dma=%d",
		channel + 1, address, value, pc, (unsigned long long) cycle,
		(unsigned long long) mAudioClockTimestampForCycle(&fixedAudioClock, cycle), cgbBase,
		status, type, trackPointer, owner, track,
		owner >= 0 ? mp2kCandidate.activeSong[owner] : -1, origin, gba->performingDMA);
}

static void _traceMP2kFifoDma(struct GBA* gba, int number, const struct GBADMA* dma) {
	if (number < 0 || number >= 4) return;
	uint32_t pc = _ARMPCAddress(gba->cpu);
	const struct GBAMP2kProfile* profile = GBAMP2kEventsProfile(core);
	bool owned = GBAMP2kIsOwnedDirectSoundDma(profile, pc, gba->performingDMA,
		(unsigned) number, dma->source);
	if (profile && !profile->known) owned = _runtimeFifoOwned(gba, dma->source);
	/* ScheduleFifoDma also runs for every refill, under the interrupted PC. */
	if (fixedAudioOutput.dmaOwned[number] && dma->source == fixedAudioOutput.dmaSource[number]) owned = true;
	if (fixedAudioOutput.dmaOwned[number] != owned || !fixedAudioOutput.dmaSource[number]) {
		mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO FIFO DMA] channel=%d pc=%08x source=%08x owned=%d",
			number, pc, dma->source, owned);
	}
	fixedAudioOutput.dmaOwned[number] = owned;
	fixedAudioOutput.dmaSource[number] = dma->source;
	if (!owned && ((profile && profile->known) || fixedAudioOutput.active)) _ownershipFallback("unknown Direct Sound DMA");
}

static void _traceMP2kFifoWrite(struct GBA* gba, uint32_t address) {
	if (!fixedAudioOutput.active) return;
	if (!gba->performingDMA) {
		_ownershipFallback("CPU Direct Sound FIFO write");
		return;
	}
	int number = gba->memory.activeDMA;
	if (number >= 0 && number < 4 && GBAMP2kEventsProfile(core) && !GBAMP2kEventsProfile(core)->known)
		fixedAudioOutput.dmaOwned[number] = _runtimeFifoOwned(gba, fixedAudioOutput.dmaSource[number]);
	if (number < 0 || number >= 4 || !fixedAudioOutput.dmaOwned[number] ||
	    gba->memory.dma[number].dest != GBA_BASE_IO + address) {
		_ownershipFallback("unknown Direct Sound FIFO source");
	}
}

static void _closeMP2kCandidate(void) {
	if (mp2kCandidate.enabled && core && core->platform(core) == mPLATFORM_GBA) {
		struct GBAAudio* audio = &((struct GBA*) core->board)->audio;
		if (audio->psgWriteTrace == _traceMP2kPSGWrite) audio->psgWriteTrace = NULL;
		if (audio->fifoWriteTrace == _traceMP2kFifoWrite) audio->fifoWriteTrace = NULL;
		if (audio->fifoDmaTrace == _traceMP2kFifoDma) audio->fifoDmaTrace = NULL;
	}
	if (mp2kCandidate.enabled && core && GBAMP2kEventsEnabled(core)) {
		GBAMP2kEventsSetSink(core, NULL, NULL);
	}
	if (mp2kCandidate.pcm) {
		if (mp2kCandidate.queue.pendingStartValid) {
			mLOG(GBA_MP2K_EVENTS, INFO,
				"[MP2K SEMANTIC] UNKNOWN start seq=%llu song=%d player=%d (no matching MPLAY_START)",
				(unsigned long long) mp2kCandidate.queue.pendingStart.sequence,
				mp2kCandidate.queue.pendingStart.songId, mp2kCandidate.queue.pendingStart.playerId);
		}
		mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K CANDIDATE] end frames=%llu deduplicated=%llu unresolved=%llu unmatchedSongStops=%llu dropped=%llu",
			(unsigned long long) mp2kCandidate.frames,
			(unsigned long long) mp2kCandidate.queue.deduplicated,
			(unsigned long long) (mp2kCandidate.queue.unresolvedStarts + mp2kCandidate.queue.pendingStartValid),
			(unsigned long long) (mp2kCandidate.queue.unmatchedSongStops + mp2kCandidate.queue.pendingStopValid),
			(unsigned long long) mp2kCandidate.queue.dropped);
		fclose(mp2kCandidate.pcm);
	}
#ifdef _WIN32
	if (mp2kCandidate.player && mp2kCandidate.destroy) {
		if (stateLoadProfile.enabled) ++stateLoadProfile.destroys;
		mp2kCandidate.destroy(mp2kCandidate.player);
	}
	if (mp2kCandidate.library) {
		FreeLibrary(mp2kCandidate.library);
	}
#endif
	free(mp2kCandidate.buffer);
	memset(&mp2kCandidate, 0, sizeof(mp2kCandidate));
}

static void _openMP2kCandidate(void) {
	_closeMP2kCandidate();
	if (fixedAudioMode == FIXED_AUDIO_DISABLED) return;
	const char* enabled = getenv("MGBA_FIXED_AUDIO_PROTOTYPE");
	const char* output = "1";
	bool outputRequested = output && !strcmp(output, "1");
	if (((!enabled || strcmp(enabled, "1")) && !outputRequested) ||
	    !fixedAudioClock.enabled || !GBAMP2kEventsEnabled(core)) {
		return;
	}
#ifdef _WIN32
	const char* bridgePath = _fixedAudioBridgePath();
	const char* outputPath = getenv("MGBA_MP2K_CANDIDATE_PATH");
	if (!bridgePath || !*bridgePath || (!outputRequested && (!outputPath || !*outputPath))) {
		mLOG(GBA_MP2K_EVENTS, ERROR, "[MP2K CANDIDATE] bridge path required; diagnostic mode also needs output path");
		return;
	}
	mp2kCandidate.library = LoadLibraryA(bridgePath);
	if (!mp2kCandidate.library) {
		mLOG(GBA_MP2K_EVENTS, ERROR, "[MP2K CANDIDATE] cannot load bridge: %s error=%lu",
			bridgePath, (unsigned long) GetLastError());
		goto fail;
	}
uint32_t (*version)(void) = (void*) GetProcAddress(mp2kCandidate.library, "mp2k_bridge_api_version");
	if (!version || version() != 2) {
		mLOG(GBA_MP2K_EVENTS, ERROR, "[MP2K CANDIDATE] bridge ABI v2 required");
		goto fail;
	}
#define BRIDGE_FUNCTION(name) mp2kCandidate.name = (void*) GetProcAddress(mp2kCandidate.library, "mp2k_bridge_" #name)
	BRIDGE_FUNCTION(create);
	BRIDGE_FUNCTION(destroy);
	BRIDGE_FUNCTION(play);
	BRIDGE_FUNCTION(play_at_tick);
	BRIDGE_FUNCTION(stop);
	BRIDGE_FUNCTION(fade_player);
	BRIDGE_FUNCTION(render);
	BRIDGE_FUNCTION(get_state);
	BRIDGE_FUNCTION(get_mode);
	BRIDGE_FUNCTION(get_timing);
	BRIDGE_FUNCTION(get_microframe);
	BRIDGE_FUNCTION(get_fade_state);
	BRIDGE_FUNCTION(rebind_begin);
	BRIDGE_FUNCTION(rebind_step);
	BRIDGE_FUNCTION(error);
#undef BRIDGE_FUNCTION
	if (!mp2kCandidate.create || !mp2kCandidate.destroy || !mp2kCandidate.play ||
	    !mp2kCandidate.play_at_tick ||
	    !mp2kCandidate.fade_player ||
	    !mp2kCandidate.stop || !mp2kCandidate.render || !mp2kCandidate.error) {
		mLOG(GBA_MP2K_EVENTS, ERROR, "[MP2K CANDIDATE] bridge ABI incomplete");
		goto fail;
	}
	const struct GBAMP2kProfile* candidateProfile = GBAMP2kEventsProfile(core);
	uint32_t (*apiVersion)(void) = (void*) GetProcAddress(mp2kCandidate.library, "mp2k_bridge_api_version");
	if (candidateProfile && !candidateProfile->known && (!apiVersion || apiVersion() != 2)) goto fail;
	struct GBA* gba = core->board;
	if (stateLoadProfile.enabled) ++stateLoadProfile.creates;
	mp2kCandidate.player = mp2kCandidate.create((const uint8_t*) gba->memory.rom,
		gba->memory.romSize, (uint32_t) fixedAudioClock.outputSampleRate);
	if (!mp2kCandidate.player) {
		mLOG(GBA_MP2K_EVENTS, ERROR, "[MP2K CANDIDATE] create failed: %s", mp2kCandidate.error());
		goto fail;
	}
	if (candidateProfile && candidateProfile->playerBacking == GBA_MP2K_RAM_PLAYER) {
		uint32_t (*capability)(void) = (void*) GetProcAddress(mp2kCandidate.library, "mp2k_bridge_runtime_players_version");
		int (*routing)(void*, uint8_t) = (void*) GetProcAddress(mp2kCandidate.library, "mp2k_bridge_set_runtime_players");
		if (!capability || capability() != 1 || !routing || !mp2kCandidate.get_state || routing(mp2kCandidate.player, 1)) {
			mLOG(GBA_MP2K_EVENTS, ERROR, "[RAM_PLAYER_REJECTED] reason=bridge-runtime-player-ABI");
			goto fail;
		}
	}
	if (stateLoadCandidatePcm) {
		mp2kCandidate.pcm = stateLoadCandidatePcm;
		stateLoadCandidatePcm = NULL;
	} else if (outputPath && *outputPath) {
		FILE* existing = fopen(outputPath, "rb");
		if (existing) {
			fclose(existing);
			mLOG(GBA_MP2K_EVENTS, ERROR, "[MP2K CANDIDATE] file exists: %s", outputPath);
			goto fail;
		}
		mp2kCandidate.pcm = fopen(outputPath, "wb");
		if (!mp2kCandidate.pcm) {
			mLOG(GBA_MP2K_EVENTS, ERROR, "[MP2K CANDIDATE] cannot open: %s", outputPath);
			goto fail;
		}
	}
	GBAMP2kSemanticReset(&mp2kCandidate.queue);
	for (unsigned i = 0; i < GBA_MP2K_MAX_PLAYERS; ++i) {
		mp2kCandidate.activeSong[i] = -1;
	}
	mp2kCandidate.enabled = true;
	const char* psgTrace = getenv("MGBA_MP2K_PSG_OWNER_TRACE");
	mp2kCandidate.psgOwnerTrace = psgTrace && !strcmp(psgTrace, "1");
	if (mp2kCandidate.psgOwnerTrace || outputRequested) {
		gba->audio.psgWriteTrace = _traceMP2kPSGWrite;
	}
	if (outputRequested) {
		gba->audio.fifoWriteTrace = _traceMP2kFifoWrite;
		gba->audio.fifoDmaTrace = _traceMP2kFifoDma;
	}
	const char* timingTrace = getenv("MGBA_MP2K_TIMING_TRACE");
	mp2kCandidate.timingTrace = timingTrace && !strcmp(timingTrace, "1") && mp2kCandidate.get_timing;
	const char* fadeTrace = getenv("MGBA_MP2K_FADE_TRACE");
	mp2kCandidate.fadeTrace = fadeTrace && !strcmp(fadeTrace, "1");
	const char* stateTrace = getenv("MGBA_MP2K_STATE_TRACE");
	mp2kCandidate.stateTrace = stateTrace && !strcmp(stateTrace, "1") && mp2kCandidate.get_state;
	mp2kCandidate.nextStateSample = (uint64_t) fixedAudioClock.outputSampleRate / 10;
	if (mp2kCandidate.get_mode) {
		struct mp2k_bridge_mode mode;
		if (!mp2kCandidate.get_mode(mp2kCandidate.player, &mode)) {
			mLOG(GBA_MP2K_EVENTS, INFO,
				"[MP2K MODE] candidate volume=%u reverb=%u frequency=%u channels=%u dac=%u",
				mode.volume, mode.reverb, mode.frequency, mode.max_channels, mode.dac_config);
		}
	}
	GBAMP2kEventsSetSink(core, _candidateEventSink, &mp2kCandidate);
	mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K CANDIDATE] file=%s rate=%.0f channels=2 bridge=%s",
		outputPath ? outputPath : "(none)", fixedAudioClock.outputSampleRate, bridgePath);
	return;
fail:
	_closeMP2kCandidate();
#else
	mLOG(GBA_MP2K_EVENTS, ERROR, "[MP2K CANDIDATE] prototype bridge currently supports Windows only");
#endif
}

static bool _stateLoadEventsRebind(const int* songs) {
	if (stateLoadProfile.enabled) { ++stateLoadProfile.validations; ++stateLoadProfile.epochs; }
	return GBAMP2kEventsRebind(core, songs);
}

static const void* _stateLoadRam(struct GBA* gba, uint32_t address, size_t size) {
	if (address >= 0x03000000 && address <= 0x03008000 && size <= 0x03008000 - address)
		return (const uint8_t*) gba->memory.iwram + address - 0x03000000;
	if (address >= 0x02000000 && address <= 0x02040000 && size <= 0x02040000 - address)
		return (const uint8_t*) gba->memory.wram + address - 0x02000000;
	return NULL;
}

static void _stateLoadDropCandidate(void) {
	stateLoadWaitForNativeStart = false;
	ramPlayerContinueRebindPending = false;
	if (mp2kCandidate.pcm) {
		stateLoadCandidatePcm = mp2kCandidate.pcm;
		mp2kCandidate.pcm = NULL;
	}
	_closeMP2kCandidate();
	GBAMP2kSemanticReset(&runtimeMp2kQueue);
	runtimeMp2kValidatedStarts = 0;
	fixedAudioOutput.readFrame = fixedAudioOutput.writeFrame = 0;
	if (fixedAudioOutput.ring)
		memset(fixedAudioOutput.ring, 0, fixedAudioOutput.capacity * 2 * sizeof(int16_t));
	if (fixedAudioOutput.output)
		memset(fixedAudioOutput.output, 0, fixedAudioOutput.outputCapacity * 2 * sizeof(int16_t));
	memset(fixedAudioOutput.dmaOwned, 0, sizeof(fixedAudioOutput.dmaOwned));
	memset(fixedAudioOutput.dmaSource, 0, sizeof(fixedAudioOutput.dmaSource));
}

/* Suspend once per load burst. Intermediate states only deserialize native GBA
 * state; no ROM scan, watch reconstruction, renderer, seek or ring refill. */
static unsigned _fixedAudioSupportedSpeed(double nominal) {
	return fixedAudioSupportedSpeed(fixedAudioMode, mp2kCurrentFrontend.throttleStateKnown,
		mp2kCurrentFrontend.throttleMode == GBA_MP2K_THROTTLE_NORMAL,
		mp2kCurrentFrontend.throttleMode == GBA_MP2K_THROTTLE_FAST_FORWARD,
		nominal, mp2kCurrentFrontend.runRate);
}

static bool _stateLoadRateSupported(void) {
	return _fixedAudioSupportedSpeed(fixedAudioClock.nominalRunRate) != 0;
}

static void _stateLoadSuspend(const char* reason) {
	bool routed = fixedAudioOutput.active;
	fixedAudioOutput.active = false;
	fixedAudioOutput.recovering = false;
	fixedAudioOutput.healthyRuns = 0;
	fixedAudioOutput.recoveryRamp = false;
	fixedAudioOutput.unsupportedRuns = 0;
	if (routed) fixedAudioOutput.outputRateRefreshPending = true;
	if (!fixedAudioOutput.hardFallback) fixedAudioOutput.fallbackReason = reason;
	runtimeAudioPending = !fixedAudioOutput.hardFallback;
	_stateLoadDropCandidate();
	GBAMP2kEventsSuspend(core);
	stateLoadStableRuns = 0;
	stateLoadRetryWait = 0;
}

static void _stateLoadTransportRun(void) {
	if (!fixedAudioOutput.requested || fixedAudioOutput.hardFallback) return;
	bool rewind = mp2kCurrentFrontend.throttleStateKnown && mp2kCurrentFrontend.throttleMode == GBA_MP2K_THROTTLE_REWIND;
	if (ramPlayerContinueRebindPending && !rewind) {
		bool waiting = (stateLoadWaitForNativeStart || stateLoadStage == STATE_LOAD_RECOVERING) &&
			(!ramPlayerContinueRecovery || ramPlayerContinueCompletesLoad);
		_stateLoadSuspend("RAM_PLAYER_CONTINUE_SETTLING");
		ramPlayerContinueRecovery = true;
		ramPlayerContinueCompletesLoad = waiting;
		stateLoadStage = STATE_LOAD_REARM_PENDING;
		stateLoadRetryWait = STATE_LOAD_SETTLE_RUNS;
		stateLoadSeekRuns = 0;
		if (!waiting) stateLoadBeginRun = fixedAudioClockRuns;
		mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO] RAM_PLAYER_CONTINUE_SETTLING nativeRuns=%u sticky=0", STATE_LOAD_SETTLE_RUNS);
	}
	if (rewind) {
		if (stateLoadStage != STATE_LOAD_REWINDING) {
			if (stateLoadStage != STATE_LOAD_RATE_SETTLING && stateLoadStage != STATE_LOAD_REWIND_END_PENDING) _stateLoadSuspend("REWINDING");
			stateLoadStage = STATE_LOAD_REWINDING;
			stateLoadWasRewind = true;
			mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO] REWIND_DETECTED method=frontend state=REWINDING");
		}
		stateLoadStableRuns = 0;
		return;
	}
	if ((stateLoadStage == STATE_LOAD_REARM_PENDING || stateLoadStage == STATE_LOAD_REBIND || stateLoadStage == STATE_LOAD_RECOVERING) && !_stateLoadRateSupported()) {
		_stateLoadSuspend("LOAD_RATE_SETTLING");
		stateLoadStage = STATE_LOAD_RATE_SETTLING;
	}
	if (stateLoadStage == STATE_LOAD_REWINDING) {
		stateLoadStage = STATE_LOAD_REWIND_END_PENDING;
		stateLoadStableRuns = 0;
	}
	if (stateLoadStage != STATE_LOAD_RATE_SETTLING && stateLoadStage != STATE_LOAD_REWIND_END_PENDING) return;
	if (!_stateLoadRateSupported()) { stateLoadStableRuns = 0; return; }
	double rate = mp2kCurrentFrontend.runRate;
	if (!stateLoadStableRuns || rate < stateLoadStableRate * .98 || rate > stateLoadStableRate * 1.02) stateLoadStableRuns = 0;
	stateLoadStableRate = rate;
	if (++stateLoadStableRuns < STATE_LOAD_SETTLE_RUNS) return;
	stateLoadStage = STATE_LOAD_REARM_PENDING;
	if (stateLoadWasRewind) mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO] REWIND_END stableRuns=%u state=REWIND_REBIND", stateLoadStableRuns);
}

static bool _stateLoadFifoOwned(struct GBA* gba, const struct GBAMP2kProfile* profile, unsigned channel, uint32_t source) {
	if (channel != 1 && channel != 2) return false;
	if (!profile->soundInfoPointerOffset || profile->soundInfoPointerOffset > profile->romSize - 4) return false;
	uint32_t address;
	memcpy(&address, (const uint8_t*) gba->memory.rom + profile->soundInfoPointerOffset, 4);
	const struct GBAMP2kContext* sound = _stateLoadRam(gba, address, sizeof(*sound));
	if (!sound || (address & 3) ||
	    (sound->magic != MP2K_MAGIC && sound->magic != MP2K_MAGIC + 1) ||
	    sound->freq != ((profile->soundMode >> 16) & 15) || !sound->maxChans || sound->maxChans > 12 ||
	    sound->pcmSamplesPerVBlank <= 0 || sound->pcmSamplesPerVBlank > 4096 ||
	    !sound->pcmDmaPeriod || sound->pcmDmaPeriod > 16) return false;
	uint32_t length = sound->pcmSamplesPerVBlank * sound->pcmDmaPeriod;
	uint32_t first = address + sizeof(*sound);
	if (!_stateLoadRam(gba, first, length * 2)) return false;
	if (profile->known && profile->fifoSource[channel] != first + (channel == 2 ? length : 0)) return false;
	first += channel == 2 ? length : 0;
	/* A serialized DMA source may be partway through its live PCM buffer. */
	return !(source & 3) && source >= first && source < first + length;
}

enum _StateLoadTrackReason {
    TRACK_VALID, TRACK_CURSOR_OOB, TRACK_STACK_DEPTH_INVALID,
    TRACK_RUNNING_STATUS_INVALID, TRACK_STACK_OOB
};
static const char* const stateLoadTrackReasons[] = {
    "TRACK_VALID", "TRACK_CURSOR_OOB", "TRACK_STACK_DEPTH_INVALID",
    "TRACK_RUNNING_STATUS_INVALID", "TRACK_STACK_OOB"
};
static struct {
    int player, track;
    const char* detailedReason;
    uint32_t pointer, status, value;
} stateLoadFailureContext = { .player = -1, .track = -1 };

static void _stateLoadClearFailureContext(void) {
    stateLoadFailureContext.player = stateLoadFailureContext.track = -1;
    stateLoadFailureContext.detailedReason = NULL;
    stateLoadFailureContext.pointer = stateLoadFailureContext.status = stateLoadFailureContext.value = 0;
}

static const char* _stateLoadPointerRegion(const struct GBA* gba, uint32_t pointer) {
    if (!pointer) return "NULL";
    if (pointer >= 0x08000000 && pointer - 0x08000000 < gba->memory.romSize) return "ROM";
    if (pointer >= 0x02000000 && pointer < 0x02040000) return "EWRAM";
    if (pointer >= 0x03000000 && pointer < 0x03008000) return "IWRAM";
    return "OOB";
}

/* Always emit rejected fields, even with verbose diagnostics disabled. No RAM
 * or sequence bytes are copied into a savestate or persistent diagnostic blob. */
static void _stateLoadTrackDiagnostic(const struct GBAMP2kProfile* profile,
        const struct GBAMP2kMusicPlayerInfo* player, const struct GBAMP2kMusicPlayerTrack* track,
        unsigned song, unsigned id, unsigned index, unsigned used,
        uint32_t address, uint32_t trackAddress, enum _StateLoadTrackReason reason) {
    struct GBA* gba = core->board;
    const struct GBAMP2kSoundChannel* channel = _stateLoadRam(gba, track->chan, sizeof(*channel));
    stateLoadFailureContext.player = id; stateLoadFailureContext.track = index;
    stateLoadFailureContext.detailedReason = stateLoadTrackReasons[reason]; stateLoadFailureContext.pointer = track->cmdPtr;
    stateLoadFailureContext.status = track->flags;
    stateLoadFailureContext.value = reason == TRACK_CURSOR_OOB ? track->cmdPtr :
        reason == TRACK_STACK_DEPTH_INVALID ? track->patternLevel : track->runningStatus;
    mLOG(GBA_MP2K_EVENTS, WARN, "[FIXED AUDIO TRACK DIAG] game=%s profile=%s player=%u song=%u header=%08x headerRegion=%s track=%u trackCount=%u playerTrackCount=%u playerAddress=%08x playerRegion=%s trackAddress=%08x trackRegion=%s detailed_reason=%s reasonEnum=%u",
        profile->gameCode, profile->name, id, song, player->songHeader, _stateLoadPointerRegion(gba, player->songHeader), index, used, player->trackCount,
        address, _stateLoadPointerRegion(gba, address), trackAddress, _stateLoadPointerRegion(gba, trackAddress), stateLoadTrackReasons[reason], reason);
    mLOG(GBA_MP2K_EVENTS, WARN, "[FIXED AUDIO TRACK DIAG] player=%u track=%u playerStatus=%08x playing=%d finished=%d priority=%u magic=%08x processingLock=%d tempo=%u/%u/%u counter=%u clock=%u memory=%08x memoryRegion=%s tone=%08x toneRegion=%s flags=%02x enabled=%d init=%d wait=%u runningStatus=%02x repeat=%u stackDepth=%u cursor=%08x cursorRegion=%s nextData=%08x nextDataRegion=%s",
        id, index, player->status, !(player->status & 0x80000000U), !!(player->status & 0x80000000U), player->priority, player->magic, player->magic == MP2K_MAGIC + 1,
        player->tempoD, player->tempoU, player->tempoI, player->tempoC, player->clock, player->memAccArea, _stateLoadPointerRegion(gba, player->memAccArea), player->tone, _stateLoadPointerRegion(gba, player->tone),
        track->flags, !!(track->flags & 0x80), !!(track->flags & 0x40), track->wait, track->runningStatus, track->repN, track->patternLevel,
        track->cmdPtr, _stateLoadPointerRegion(gba, track->cmdPtr), track->cmdPtr + 1, _stateLoadPointerRegion(gba, track->cmdPtr + 1));
    mLOG(GBA_MP2K_EVENTS, WARN, "[FIXED AUDIO TRACK DIAG] player=%u track=%u stack0=%08x/%s stack1=%08x/%s stack2=%08x/%s pitch=%u/%u/%u keyShift=%d/%d tune=%d bend=%d range=%u volume=%u/%u/%u/%u pan=%d/%d mod=%u modType=%u modValue=%d lfo=%u/%u/%u/%u key=%u velocity=%u gate=%u channel=%08x channelRegion=%s",
        id, index, track->patternStack[0], _stateLoadPointerRegion(gba, track->patternStack[0]), track->patternStack[1], _stateLoadPointerRegion(gba, track->patternStack[1]), track->patternStack[2], _stateLoadPointerRegion(gba, track->patternStack[2]),
        track->keyM, track->pitM, track->pitX, track->keyShift, track->keyShiftX, track->tune, track->bend, track->bendRange,
        track->vol, track->volX, track->volMR, track->volML, track->pan, track->panX, track->mod, track->modT, track->modM,
        track->lfoSpeed, track->lfoSpeedC, track->lfoDelay, track->lfoDelayC, track->key, track->velocity, track->gateTime, track->chan, _stateLoadPointerRegion(gba, track->chan));
    if (channel) mLOG(GBA_MP2K_EVENTS, WARN, "[FIXED AUDIO TRACK DIAG] player=%u track=%u channelStatus=%02x channelType=%u channelTrack=%08x/%s channelNext=%08x/%s channelPrevious=%08x/%s sample=%08x/%s data=%08x/%s frequency=%u",
        id, index, channel->status, channel->type, channel->track, _stateLoadPointerRegion(gba, channel->track), channel->np, _stateLoadPointerRegion(gba, channel->np), channel->pp, _stateLoadPointerRegion(gba, channel->pp),
        channel->waveData, _stateLoadPointerRegion(gba, channel->waveData), channel->cp, _stateLoadPointerRegion(gba, channel->cp), channel->freq);
    else mLOG(GBA_MP2K_EVENTS, WARN, "[FIXED AUDIO TRACK DIAG] player=%u track=%u channelReadable=0 channel=%08x/%s", id, index, track->chan, _stateLoadPointerRegion(gba, track->chan));
}

static void _stateLoadRebindFailed(const char* reason, bool hard) {
	mLOG(GBA_MP2K_EVENTS, WARN, "[FIXED AUDIO] STATE_LOAD_REBIND_FAILED reason=%s class=%s sticky=%d player=%d track=%d detailed_reason=%s pointer=%08x status=%08x value=%08x",
		reason, hard ? "HARD" : "UNSUPPORTED", (int) hard, stateLoadFailureContext.player, stateLoadFailureContext.track,
		stateLoadFailureContext.detailedReason ? stateLoadFailureContext.detailedReason : reason,
		stateLoadFailureContext.pointer, stateLoadFailureContext.status, stateLoadFailureContext.value);
	if (hard) { _fixedAudioFallback(reason); stateLoadStage = STATE_LOAD_NONE; }
	else {
		fixedAudioOutput.recovering = false;
		runtimeAudioPending = true;
		stateLoadStage = STATE_LOAD_REARM_PENDING;
		stateLoadRetryWait = 60; /* Bounded work per attempt; later songs may be reconstructible. */
	}
	_stateLoadDropCandidate();
	GBAMP2kEventsSuspend(core);
}

/* The snapshot and observer are rebuilt at the same native boundary. Nothing
 * from a pre-load player, event watch, ring, or renderer survives this path. */
static void _stateLoadRebindRun(bool validateOnly) {
	if (!validateOnly && stateLoadStage != STATE_LOAD_REARM_PENDING && stateLoadStage != STATE_LOAD_REBIND) return;
	if (fixedAudioOutput.hardFallback) { stateLoadStage = STATE_LOAD_NONE; return; }
#ifdef _WIN32
	if (validateOnly || stateLoadStage == STATE_LOAD_REARM_PENDING) {
		if (!validateOnly && stateLoadRetryWait) { --stateLoadRetryWait; return; }
		_stateLoadClearFailureContext();
		struct GBA* gba = core->board;
		const struct GBAMP2kProfile* profile = GBAMP2kEventsProfile(core);
		if (stateLoadProfile.enabled) ++stateLoadProfile.validations;
		if (!profile || !GBAMP2kProfileValidate(profile, gba)) {
			_stateLoadRebindFailed("STATE_LOAD_INVALID_PROFILE", true); return;
		}
		if (profile->playerBacking == GBA_MP2K_RAM_PLAYER && GBAMP2kEventsPlayers(core)->mismatch) return;
		const uint8_t* rom = (const uint8_t*) gba->memory.rom;
		struct mp2k_bridge_rebind_player states[GBA_MP2K_MAX_PLAYERS] = { 0 };
		int songs[GBA_MP2K_MAX_PLAYERS];
		for (unsigned i = 0; i < GBA_MP2K_MAX_PLAYERS; ++i) songs[i] = -1;
		size_t count = 0;
		for (unsigned id = 0; id < profile->playerCount; ++id) {
			uint32_t address, tracks; uint16_t maximum;
			memcpy(&address, rom + profile->playerTableOffset + id * 12, 4);
			memcpy(&tracks, rom + profile->playerTableOffset + id * 12 + 4, 4);
			memcpy(&maximum, rom + profile->playerTableOffset + id * 12 + 8, 2);
			if (!address && !tracks && !maximum) continue;
			stateLoadFailureContext.player = id; stateLoadFailureContext.track = -1;
			stateLoadFailureContext.pointer = address;
			const struct GBAMP2kMusicPlayerInfo* n = _stateLoadRam(gba, address, sizeof(*n));
			if (!n || (address & 3) || maximum > 16) {
				_stateLoadRebindFailed("STATE_LOAD_INVALID_PLAYER_POINTER", true); return;
			}
			stateLoadFailureContext.status = n->status; stateLoadFailureContext.value = n->magic;
			/* SoundInit or a driver call can be in progress at this boundary. */
			if (!n->magic && profile->playerBacking == GBA_MP2K_RAM_PLAYER) continue;
			if (!n->magic || n->magic == MP2K_MAGIC + 1) return;
			if (n->magic != MP2K_MAGIC || n->tracks != tracks || n->trackCount > maximum) {
				stateLoadFailureContext.detailedReason = n->magic != MP2K_MAGIC ? "PLAYER_MAGIC_INVALID" :
				    n->tracks != tracks ? "PLAYER_TRACK_ALLOCATION_MISMATCH" : "PLAYER_TRACK_COUNT_INVALID";
				stateLoadFailureContext.pointer = n->tracks;
				stateLoadFailureContext.value = n->magic != MP2K_MAGIC ? n->magic : n->tracks != tracks ? tracks : n->trackCount;
				_stateLoadRebindFailed("STATE_LOAD_INVALID_PLAYER_STATE", true); return;
			}
			if ((n->status & 0x80000000U) || !n->songHeader || !n->trackCount) continue;
			int song = -1;
			for (unsigned j = 0; j < profile->songCount; ++j) {
				uint32_t header; uint16_t player;
				memcpy(&header, rom + profile->songTableOffset + j * 8, 4);
				memcpy(&player, rom + profile->songTableOffset + j * 8 + 4, 2);
				if (header == n->songHeader && (profile->playerBacking == GBA_MP2K_RAM_PLAYER || player == id)) { song = j; break; }
			}
			if (song < 0) { stateLoadFailureContext.pointer = n->songHeader; _stateLoadRebindFailed("STATE_LOAD_INVALID_SONG_HEADER", true); return; }
			unsigned used = rom[n->songHeader - 0x08000000];
			const struct GBAMP2kMusicPlayerTrack* nt = _stateLoadRam(gba, tracks, maximum * sizeof(*nt));
			if (!nt || (tracks & 3) || used > maximum) {
				stateLoadFailureContext.pointer = tracks; stateLoadFailureContext.value = used;
				_stateLoadRebindFailed("STATE_LOAD_INVALID_TRACK_POINTER", true); return;
			}
			bool active = false;
			for (unsigned j = 0; j < used; ++j) {
				if (nt[j].flags & 0x40) return; /* Wait for track Init, never restart at the song head. */
				active |= (nt[j].flags & 0x80) != 0;
			}
			if (!active) continue;
			if (!n->tempoI || n->tempoI > 1024 || n->tempoC >= 150 ||
			    (n->fadeOI && (n->fadeOV > 256 || !n->fadeOC || n->fadeOC > n->fadeOI))) {
				_stateLoadRebindFailed("STATE_LOAD_INVALID_TEMPO_OR_FADE", true); return;
			}
			const void* memory = _stateLoadRam(gba, n->memAccArea, 256);
			if (!memory) { stateLoadFailureContext.pointer = n->memAccArea; _stateLoadRebindFailed("STATE_LOAD_INVALID_MEMORY_POINTER", true); return; }
			struct mp2k_bridge_rebind_player* s = &states[count++];
			s->song_header_offset = n->songHeader - 0x08000000;
			s->song = song; s->player = id; s->tracks = used; s->playing = 1;
			s->clock = n->clock; s->priority = n->priority; s->bpm = n->tempoI; s->tempo_counter = n->tempoC;
			s->fade_interval = n->fadeOI; s->fade_counter = n->fadeOC; s->fade_volume = n->fadeOI ? n->fadeOV : 256;
			memcpy(s->memory, memory, sizeof(s->memory));
			for (unsigned j = 0; j < used; ++j) {
				struct mp2k_bridge_rebind_track* t = &s->track[j];
				t->enabled = (nt[j].flags & 0x80) != 0;
				t->position = nt[j].cmdPtr - 0x08000000;
				t->wait = nt[j].wait; t->running_status = nt[j].runningStatus; t->pattern_level = nt[j].patternLevel;
				memcpy(t->controls, &nt[j], sizeof(t->controls));
				enum _StateLoadTrackReason rejected = TRACK_VALID;
				if (t->enabled) {
					if (t->position >= gba->memory.romSize) rejected = TRACK_CURSOR_OOB;
					else if (t->pattern_level > 3) rejected = TRACK_STACK_DEPTH_INVALID;
					else if (t->running_status && t->running_status < 0xBD) rejected = TRACK_RUNNING_STATUS_INVALID;
				}
				/* MODT is a native unsigned command byte, not a 0..2 enum
				 * validity constraint. Preserve e.g. AFEJ MODT 0x7f exactly. */
				if (t->enabled && nt[j].modT > 2 && fixedAudioOutput.diagnostics)
					mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO] STATE_LOAD_TRACK_VALID player=%u track=%u song=%d modType=%u detailed_reason=MODT_BYTE_PRESERVED cursor=%08x", id, j, song, nt[j].modT, nt[j].cmdPtr);
				if (rejected != TRACK_VALID) {
					_stateLoadTrackDiagnostic(profile, n, &nt[j], song, id, j, used, address, tracks + j * sizeof(*nt), rejected);
					_stateLoadRebindFailed("STATE_LOAD_INVALID_TRACK_STATE", true); return;
				}
				for (unsigned k = 0; k < t->pattern_level && k < 3; ++k) {
					t->pattern_stack[k] = nt[j].patternStack[k] - 0x08000000;
					if (t->pattern_stack[k] >= gba->memory.romSize) {
						_stateLoadTrackDiagnostic(profile, n, &nt[j], song, id, j, used, address, tracks + j * sizeof(*nt), TRACK_STACK_OOB);
						stateLoadFailureContext.pointer = nt[j].patternStack[k]; stateLoadFailureContext.value = k;
						_stateLoadRebindFailed("STATE_LOAD_INVALID_PATTERN_POINTER", true); return;
					}
				}
			}
			songs[id] = song;
			mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO] STATE_LOAD_PLAYER_DISCOVERY player=%u address=%08x song=%d header=%08x playing=1 priority=%u tracks=%u",
				id, address, song, n->songHeader, n->priority, used);
			mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO] STATE_LOAD_POSITION player=%u clock=%u tempo=%u/%u/%u/%u fade=%u/%u/%u track0=%08x wait=%u running=%u",
				id, n->clock, n->tempoD, n->tempoU, n->tempoI, n->tempoC,
				n->fadeOI, n->fadeOC, n->fadeOV, used ? nt[0].cmdPtr : 0, used ? nt[0].wait : 0, used ? nt[0].runningStatus : 0);
		}
		mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO] STATE_LOAD_PROFILE game=%s players=%u active=%zu validated=1",
			profile->gameCode, profile->playerCount, count);
		if (validateOnly) return; /* Reject corrupt RAM before the driver can overwrite it during settling. */
		/* Ownership caches belong to the loaded native DMA state. */
		for (unsigned i = 0; i < 4; ++i) {
			const struct GBADMA* dma = &gba->memory.dma[i];
			bool fifo = dma->dest == GBA_BASE_IO + GBA_REG_FIFO_A_LO || dma->dest == GBA_BASE_IO + GBA_REG_FIFO_B_LO;
			if (!fifo) continue;
			bool owned = _stateLoadFifoOwned(gba, profile, i, dma->source);
			fixedAudioOutput.dmaOwned[i] = owned; fixedAudioOutput.dmaSource[i] = dma->source;
			mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO] STATE_LOAD_OWNERSHIP dma=%u source=%08x owned=%d", i, dma->source, (int) owned);
			if (!owned) _ownershipFallback("STATE_LOAD_UNVERIFIED_OWNERSHIP");
			if (fixedAudioOutput.hardFallback) { stateLoadStage = STATE_LOAD_NONE; return; }
		}
		mAudioClockInit(&fixedAudioClock, fixedAudioClock.nominalRunRate, fixedAudioClock.outputSampleRate);
		if (!_stateLoadEventsRebind(songs)) { _stateLoadRebindFailed("STATE_LOAD_INVALID_PROFILE", true); return; }
		if (profile->playerBacking == GBA_MP2K_RAM_PLAYER) {
			struct GBAMP2kPlayerRegistry* registry = GBAMP2kEventsPlayers(core);
			for (unsigned id = 0; id < profile->playerCount; ++id) {
				GBAMP2kPlayerRefresh(registry, gba, profile, id);
				if (songs[id] >= 0) {
					const struct GBAMP2kMusicPlayerInfo* n = GBAMP2kPlayerAt(gba, profile, id);
					if (!GBAMP2kPlayerConfirm(registry, gba, profile, id, songs[id], n->songHeader)) {
						_stateLoadRebindFailed("STATE_LOAD_INVALID_PLAYER_STATE", true); return;
					}
					GBAMP2kPlayerSetState(registry, profile, id, GBA_MP2K_PLAYER_ARMED);
					if (registry->diagnostics) mLOG(GBA_MP2K_EVENTS, INFO, "[RAM_PLAYER_REBIND] generation=%u player=%u address=%08x song=%d", registry->generation, id, registry->slots[id].address, songs[id]);
				}
			}
		}
		GBAMP2kEventsSetAudioClock(core, &fixedAudioClock);
		GBAMP2kSemanticReset(&runtimeMp2kQueue);
		_stateLoadClearFailureContext();
		_openMP2kCandidate();
		if (!mp2kCandidate.enabled) { _stateLoadRebindFailed("STATE_LOAD_BRIDGE_CREATE_FAILED", true); return; }
		uint32_t (*version)(void) = (void*) GetProcAddress(mp2kCandidate.library, "mp2k_bridge_rebind_version");
		if (!version || version() != 1 || !mp2kCandidate.rebind_begin || !mp2kCandidate.rebind_step) {
			_stateLoadRebindFailed("STATE_LOAD_BRIDGE_ABI_MISMATCH", true); return;
		}
		if (profile->playerBacking == GBA_MP2K_RAM_PLAYER && !count) {
			/* No active native track means there is no position to reconstruct.
			 * Use a fresh, unsubmitted renderer and the normal native-start gate.
			 * SoundInit alone must not convert empty recovery PCM into ACTIVE. */
			unsigned initialized = 0;
			struct GBAMP2kPlayerRegistry* registry = GBAMP2kEventsPlayers(core);
			for (unsigned id = 0; id < profile->playerCount; ++id) initialized += registry->slots[id].initialized;
			stateLoadStage = STATE_LOAD_NONE;
			stateLoadWaitForNativeStart = true;
			runtimeAudioPending = true;
			fixedAudioOutput.active = fixedAudioOutput.recovering = false;
			fixedAudioOutput.fallbackReason = NULL;
			/* The discarded timeline already consumed its startup padding.
			 * Prime the fresh ring when a validated native start arrives. */
			fixedAudioOutput.startupRemaining = fixedAudioOutput.lookahead;
			mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO] STATE_LOAD_%s game=%s generation=%u initialized=%u state=%s candidate=0 native=1 sticky=0 rewind=%d",
				initialized ? "WAIT_START" : "WAIT_INIT", profile->gameCode, registry->generation, initialized,
				initialized ? "DISCOVERED" : "WAIT_INIT", (int) stateLoadWasRewind);
			return;
		}
		if (stateLoadProfile.enabled) ++stateLoadProfile.rebinds;
		if (mp2kCandidate.rebind_begin(mp2kCandidate.player, states, count)) {
			_stateLoadRebindFailed("STATE_LOAD_BRIDGE_INVALID_SNAPSHOT", true); return;
		}
		for (unsigned i = 0; i < GBA_MP2K_MAX_PLAYERS; ++i) mp2kCandidate.activeSong[i] = songs[i];
		stateLoadStage = STATE_LOAD_REBIND; stateLoadSeekRuns = 0;
		mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO] STATE_LOAD_REBIND_BEGIN active=%zu state=STATE_LOAD_REBIND", count);
	}
	if (stateLoadStage == STATE_LOAD_REBIND) {
		uint64_t seekStart = stateLoadProfile.enabled ? _stateLoadTimeUs() : 0;
		int result = mp2kCandidate.rebind_step(mp2kCandidate.player, 256);
		if (stateLoadProfile.enabled) { ++stateLoadProfile.seeks; stateLoadProfile.seekUs += _stateLoadTimeUs() - seekStart; }
		++stateLoadSeekRuns;
		if (result < 0) { _stateLoadRebindFailed("STATE_LOAD_RENDERER_FATAL", true); return; }
		/* A valid native clock can require more than 240 bounded chunks.
		 * The bridge caps total reconstruction work at 2^24 SoundMain calls;
		 * retain its 256-call per-run budget and allow that finite work plus
		 * the final boundary check. IN_PROGRESS is not a position mismatch.
		 * Snapshot/clock/pointer/stack guards and result=2 remain unchanged. */
		if (result == 2 || (result == 1 && stateLoadSeekRuns >= (16777216U / 256U + 2U))) {
			_stateLoadRebindFailed("STATE_LOAD_POSITION_NOT_RECONSTRUCTIBLE", false); return;
		}
		if (!result) {
			stateLoadStage = STATE_LOAD_RECOVERING;
			fixedAudioOutput.recovering = true;
			runtimeAudioPending = false;
			fixedAudioOutput.healthyRuns = 0;
			mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO] STATE_LOAD_REBIND_OK seekRuns=%u state=RECOVERING", stateLoadSeekRuns);
		}
	}
#else
	_stateLoadRebindFailed("STATE_LOAD_REBIND_UNAVAILABLE", false);
#endif
}

static void _renderMP2kCandidate(void) {
	if (stateLoadStage != STATE_LOAD_NONE && stateLoadStage != STATE_LOAD_RECOVERING) return;
	if (!mp2kCandidate.enabled || !fixedAudioClock.runAdvance ||
	    (fixedAudioOutput.requested && GBAMP2kEventsProfile(core) && !GBAMP2kEventsProfile(core)->known && !fixedAudioOutput.active && !runtimeAudioPending && !fixedAudioOutput.recovering)) {
		return;
	}
#ifdef _WIN32
	uint64_t start = mp2kCandidate.frames;
	uint64_t end = fixedAudioClock.runStartSample + fixedAudioClock.runAdvance;
	for (size_t i = 0; i < mp2kCandidate.queue.size; ++i) {
		const struct mAudioClockEvent* event = &mp2kCandidate.queue.events[i];
		if (event->type == M_AUDIO_CLOCK_PLAY_SONG && !event->targetFirstTickKnown &&
		    event->audioSampleTimestamp < end) {
			end = event->audioSampleTimestamp;
			break;
		}
	}
	if (end <= start) return;
	size_t frames = (size_t) (end - start);
	if (end - start > 65536) {
		if (stateLoadStage == STATE_LOAD_RECOVERING) { end = start + 65536; frames = 65536; }
		else { _fixedAudioFallback("bridge render frame limit"); return; }
	}
	if (mp2kCandidate.capacity < frames) {
		int16_t* buffer = realloc(mp2kCandidate.buffer, (size_t) frames * 2 * sizeof(int16_t));
		if (!buffer) {
			mLOG(GBA_MP2K_EVENTS, ERROR, "[MP2K CANDIDATE] buffer allocation failed");
			_fixedAudioFallback("bridge render buffer allocation");
			_closeMP2kCandidate();
			return;
		}
		mp2kCandidate.buffer = buffer;
		mp2kCandidate.capacity = frames;
	}
	uint64_t cursor = start;
	while (cursor < end) {
		struct mAudioClockEvent event;
		while (GBAMP2kSemanticPopDue(&mp2kCandidate.queue, cursor, &event)) {
			if (mp2kCandidate.timingTrace && event.songId == 342) {
				struct mp2k_bridge_timing timing;
				if (!mp2kCandidate.get_timing(mp2kCandidate.player, &timing)) {
					mLOG(GBA_MP2K_EVENTS, INFO,
						"[MP2K TIMING] phase=dequeue seq=%llu type=%d event=%llu cursor=%llu bridge=%llu buffered=%u block=%u lastMain=%llu calls=%llu",
						(unsigned long long) event.sequence, event.type,
						(unsigned long long) event.audioSampleTimestamp,
						(unsigned long long) cursor,
						(unsigned long long) timing.rendered_frames,
						timing.buffered, timing.block_frames,
						(unsigned long long) timing.last_sound_main_sample,
						(unsigned long long) timing.sound_main_calls);
					mp2kCandidate.timingTraceUntil = cursor + 1500;
					mp2kCandidate.timingLastCall = timing.sound_main_calls;
				}
			}
			int result;
			switch (event.type) {
			case M_AUDIO_CLOCK_PLAY_SONG:
				result = event.noSequencerTracks ?
					mp2kCandidate.play(mp2kCandidate.player, event.songId, event.playerId) :
					mp2kCandidate.play_at_tick(mp2kCandidate.player, event.songId,
						event.playerId, event.targetFirstTickAudioSample);
				break;
			case M_AUDIO_CLOCK_FADE_PLAYER:
				result = mp2kCandidate.fade_player(mp2kCandidate.player, event.songId,
					event.playerId, event.fadeSpeed);
				break;
			default:
				result = mp2kCandidate.stop(mp2kCandidate.player, event.songId, event.playerId);
				break;
			}
			if (result) {
				mLOG(GBA_MP2K_EVENTS, ERROR, "[MP2K CANDIDATE] event failed seq=%llu: %s",
					(unsigned long long) event.sequence, mp2kCandidate.error());
				_fixedAudioFallback("SEMANTIC_COMMAND_FAILED");
				return;
			}
			if (mp2kCandidate.timingTrace && event.songId == 342) {
				struct mp2k_bridge_timing timing;
				if (!mp2kCandidate.get_timing(mp2kCandidate.player, &timing)) {
					mLOG(GBA_MP2K_EVENTS, INFO,
						"[MP2K TIMING] phase=command seq=%llu cursor=%llu bridge=%llu buffered=%u block=%u nextMain=%llu calls=%llu",
						(unsigned long long) event.sequence,
						(unsigned long long) cursor,
						(unsigned long long) timing.rendered_frames,
						timing.buffered, timing.block_frames,
						(unsigned long long) (timing.rendered_frames + timing.block_frames - timing.buffered),
						(unsigned long long) timing.sound_main_calls);
				}
			}
		}
		uint64_t segmentEnd = end;
		if (mp2kCandidate.queue.size && mp2kCandidate.queue.events[0].audioSampleTimestamp < segmentEnd) {
			segmentEnd = mp2kCandidate.queue.events[0].audioSampleTimestamp;
		}
		if (segmentEnd <= cursor) {
			segmentEnd = cursor + 1;
		}
		size_t offset = (size_t) (cursor - start);
		size_t count = (size_t) (segmentEnd - cursor);
		fixedAudioOutput.bridgeRenderResult = mp2kCandidate.render(mp2kCandidate.player, mp2kCandidate.buffer + offset * 2, count);
		if (fixedAudioOutput.bridgeRenderResult) {
			mLOG(GBA_MP2K_EVENTS, ERROR, "[MP2K CANDIDATE] render failed: %s", mp2kCandidate.error());
			_fixedAudioFallback("BRIDGE_RENDER_FATAL_OR_STATE_UNKNOWN");
			_closeMP2kCandidate();
			return;
		}
		if (mp2kCandidate.timingTrace && cursor < mp2kCandidate.timingTraceUntil) {
			struct mp2k_bridge_timing timing;
			if (!mp2kCandidate.get_timing(mp2kCandidate.player, &timing)) {
				mLOG(GBA_MP2K_EVENTS, INFO,
					"[MP2K TIMING] phase=render from=%llu to=%llu bridge=%llu buffered=%u block=%u lastMain=%llu calls=%llu firstTick=%llu firstTickPlayer=%u",
					(unsigned long long) cursor, (unsigned long long) segmentEnd,
					(unsigned long long) timing.rendered_frames,
					timing.buffered, timing.block_frames,
					(unsigned long long) timing.last_sound_main_sample,
					(unsigned long long) timing.sound_main_calls,
					(unsigned long long) timing.last_phase_tick_sample, timing.last_phase_player);
				if (mp2kCandidate.get_microframe) {
					for (uint64_t call = mp2kCandidate.timingLastCall + 1;
					     call <= timing.sound_main_calls; ++call) {
						struct mp2k_bridge_microframe microframe;
						if (!mp2kCandidate.get_microframe(mp2kCandidate.player, call, &microframe)) {
							mLOG(GBA_MP2K_EVENTS, INFO,
								"[MP2K MICROFRAME] call=%llu sample=%llu p4tick=%llu/%llu track0=%x/%x pcmChannels=%u/%u playing=%u/%u",
								(unsigned long long) microframe.call_index,
								(unsigned long long) microframe.audio_sample,
								(unsigned long long) microframe.player4_tick_before,
								(unsigned long long) microframe.player4_tick_after,
								microframe.player4_track0_before, microframe.player4_track0_after,
								microframe.pcm_channels_before, microframe.pcm_channels_after,
								microframe.player4_playing_before, microframe.player4_playing_after);
						}
					}
					mp2kCandidate.timingLastCall = timing.sound_main_calls;
				}
			}
		}
		cursor = segmentEnd;
	}
	if (mp2kCandidate.pcm &&
	    fwrite(mp2kCandidate.buffer, sizeof(int16_t) * 2, frames, mp2kCandidate.pcm) != frames) {
		mLOG(GBA_MP2K_EVENTS, ERROR, "[MP2K CANDIDATE] PCM write failed");
		_fixedAudioFallback("candidate trace write error");
		_closeMP2kCandidate();
		return;
	}
	_fixedAudioAppend(mp2kCandidate.buffer, frames);
	mp2kCandidate.frames += frames;
#endif
}

static void _traceMP2kState(void) {
#ifdef _WIN32
	if (!mp2kCandidate.stateTrace || !mp2kCandidate.player ||
	    fixedAudioClock.runStartSample + fixedAudioClock.runAdvance < mp2kCandidate.nextStateSample) {
		return;
	}
	mp2kCandidate.nextStateSample += (uint64_t) fixedAudioClock.outputSampleRate / 10;
	struct mp2k_bridge_state candidate;
	if (mp2kCandidate.get_state(mp2kCandidate.player, 0, &candidate)) {
		return;
	}
	struct GBA* gba = core->board;
	const struct GBAMP2kProfile* profile = GBAMP2kEventsProfile(core);
	if (!profile) return;
	if (!mp2kCandidate.nativeModeLogged) {
		uint32_t romMode = profile->soundMode;
		uint32_t soundInfoAddress = 0;
		if (profile->soundInfoPointerOffset &&
		    profile->soundInfoPointerOffset <= profile->romSize - sizeof(soundInfoAddress)) {
			memcpy(&soundInfoAddress, (const uint8_t*) gba->memory.rom +
				profile->soundInfoPointerOffset, sizeof(soundInfoAddress));
		}
		uint32_t runtimeMagic = 0;
		if (soundInfoAddress >= 0x03000000 && soundInfoAddress <= 0x03007FFC) {
			memcpy(&runtimeMagic, (const uint8_t*) gba->memory.iwram +
				soundInfoAddress - 0x03000000, sizeof(runtimeMagic));
		}
		mLOG(GBA_MP2K_EVENTS, INFO,
			"[MP2K MODE] profile=%s ROM mode=%08x volume=%u reverb=%u freq=%u channels=%u dac=%u "
			"soundInfo=%08x runtimeMagic=%08x",
			profile->name, romMode, (romMode >> 12) & 15, romMode & 255, (romMode >> 16) & 15,
			(romMode >> 8) & 15, (romMode >> 20) & 15,
			soundInfoAddress, runtimeMagic);
		if (runtimeMagic == MP2K_MAGIC &&
		    soundInfoAddress <= 0x03008000 - sizeof(struct GBAMP2kContext)) {
			const struct GBAMP2kContext* sound = (const void*)
				((const uint8_t*) gba->memory.iwram + soundInfoAddress - 0x03000000);
			mLOG(GBA_MP2K_EVENTS, INFO,
				"[MP2K MODE] runtime reverb=%u channels=%u volume=%u freq=%u "
				"pcmFreq=%d samplesPerVBlank=%d dmaPeriod=%u dmaCounter=%u",
				sound->reverb, sound->maxChans, sound->masterVolume, sound->freq,
				sound->pcmFreq, sound->pcmSamplesPerVBlank,
				sound->pcmDmaPeriod, sound->pcmDmaCounter);
		}
		mp2kCandidate.nativeModeLogged = true;
	}
	const uint8_t* rom = (const uint8_t*) gba->memory.rom;
	uint32_t playerAddress;
	memcpy(&playerAddress, rom + profile->playerTableOffset, sizeof(playerAddress));
	if (playerAddress < 0x03000000 || playerAddress > 0x03008000 - sizeof(struct GBAMP2kMusicPlayerInfo)) {
		return;
	}
	const struct GBAMP2kMusicPlayerInfo* native = (const void*)
		((const uint8_t*) gba->memory.iwram + playerAddress - 0x03000000);
	char tracks[512];
	size_t written = 0;
	if (native->tracks >= 0x02000000 &&
	    native->tracks <= 0x02040000 - candidate.tracks_used * sizeof(struct GBAMP2kMusicPlayerTrack)) {
		const struct GBAMP2kMusicPlayerTrack* nativeTracks = (const void*)
			((const uint8_t*) gba->memory.wram + native->tracks - 0x02000000);
		for (unsigned i = 0; i < candidate.tracks_used && i < 16; ++i) {
			uint32_t nativePos = nativeTracks[i].cmdPtr;
			if (nativePos >= 0x08000000) {
				nativePos -= 0x08000000;
			}
			int count = snprintf(tracks + written, sizeof(tracks) - written, "%s%u:%x/%x",
				i ? "," : "", i, nativePos, candidate.track_positions[i]);
			if (count < 0 || (size_t) count >= sizeof(tracks) - written) {
				break;
			}
			written += count;
		}
	}
	tracks[written] = 0;
	mLOG(GBA_MP2K_EVENTS, INFO,
		"[MP2K STATE] sample=%llu headers=%x/%x nativeClock=%u "
		"tempo=%u/%u/%u/%u candidateFrame=%llu candidateTick=%llu bpm=%u tracks=%s",
		(unsigned long long) (fixedAudioClock.runStartSample + fixedAudioClock.runAdvance),
		native->songHeader, candidate.song_header_offset + 0x08000000, native->clock,
		native->tempoD, native->tempoU, native->tempoI, native->tempoC,
		(unsigned long long) candidate.frame_count, (unsigned long long) candidate.tick_count,
		candidate.bpm, tracks);
#endif
}

static void _traceMP2kFadeState(void) {
	if (!mp2kCandidate.fadeTrace) {
		return;
	}
	struct GBA* gba = core->board;
	const struct GBAMP2kProfile* profile = GBAMP2kEventsProfile(core);
	if (!profile) return;
	const uint8_t* rom = (const uint8_t*) gba->memory.rom;
	for (unsigned i = 0; i < profile->playerCount; ++i) {
		uint32_t address;
		memcpy(&address, rom + profile->playerTableOffset + i * 12, sizeof(address));
		if (address < 0x03000000 || address > 0x03008000 - sizeof(struct GBAMP2kMusicPlayerInfo)) {
			continue;
		}
		const struct GBAMP2kMusicPlayerInfo* player = (const void*)
			((const uint8_t*) gba->memory.iwram + address - 0x03000000);
		uint16_t current[] = { player->fadeOI, player->fadeOC, player->fadeOV };
		if (memcmp(current, mp2kCandidate.priorFade[i], sizeof(current))) {
			mLOG(GBA_MP2K_EVENTS, INFO,
				"[MP2K FADE STATE] sample=%llu player=%u song=%d header=%08x status=%08x oi=%u oc=%u ov=%u",
				(unsigned long long) (fixedAudioClock.runStartSample + fixedAudioClock.runAdvance),
				i, mp2kCandidate.activeSong[i], player->songHeader, player->status,
				current[0], current[1], current[2]);
			memcpy(mp2kCandidate.priorFade[i], current, sizeof(current));
		}
		if (mp2kCandidate.get_fade_state) {
			struct mp2k_bridge_fade_state candidateFade;
			if (!mp2kCandidate.get_fade_state(mp2kCandidate.player, i, &candidateFade) &&
			    candidateFade.volume != mp2kCandidate.priorCandidateFade[i]) {
				mLOG(GBA_MP2K_EVENTS, INFO,
					"[MP2K CANDIDATE FADE] sample=%llu player=%u song=%d volume=%u speed=%u active=%u",
					(unsigned long long) (fixedAudioClock.runStartSample + fixedAudioClock.runAdvance),
					i, mp2kCandidate.activeSong[i], candidateFade.volume,
					candidateFade.speed, candidateFade.active);
				mp2kCandidate.priorCandidateFade[i] = candidateFade.volume;
			}
		}
	}
}

static void _emitFixedAudioTone(void) {
	if (!fixedAudioToneEnabled || !fixedAudioClock.runAdvance) {
		return;
	}
	uint32_t frames = fixedAudioClock.runAdvance;
	if (audioSampleBufferSize < (size_t) frames * 2) {
		audioSampleBufferSize = (size_t) frames * 2;
		audioSampleBuffer = realloc(audioSampleBuffer, audioSampleBufferSize * sizeof(int16_t));
	}
	uint64_t sampleRate = (uint64_t) fixedAudioClock.outputSampleRate;
	uint64_t clickPeriod = sampleRate / 2;
	uint64_t clickDuration = sampleRate / 10;
	uint64_t carrierPeriod = sampleRate / 512;
	if (clickPeriod < 2 || clickDuration < 1 || carrierPeriod < 2) {
		return;
	}
	for (uint32_t i = 0; i < frames; ++i) {
		uint64_t sample = fixedAudioClock.runStartSample + i;
		int16_t value = 0;
		if (sample % clickPeriod < clickDuration) {
			value = sample % carrierPeriod < carrierPeriod / 2 ? 8000 : -8000;
		}
		audioSampleBuffer[2 * i] = value;
		audioSampleBuffer[2 * i + 1] = value;
	}
	if (mp2kPcmTrace.enabled) {
		if (fwrite(audioSampleBuffer, sizeof(int16_t) * 2, frames, mp2kPcmTrace.pcm) != frames) {
			mLOG(GBA_MP2K_EVENTS, ERROR, "[AUDIO TONE] PCM write failed");
			fclose(mp2kPcmTrace.pcm);
			mp2kPcmTrace.pcm = NULL;
			mp2kPcmTrace.enabled = false;
		}
	}
	_probeAudioCallback(audioSampleBuffer, frames);
	if (mp2kPcmTrace.enabled) {
		mp2kPcmTrace.submittedFrames += frames;
	}
	if (GBAMP2kEventsEnabled(core)) {
		mp2kAudioStats.sent += frames;
		++mp2kAudioStats.audioCalls;
	}
}

static void _b6jjEventTrace(void* context, const struct B6JJAudioEventObservation* e) {
	(void) context;
	mLOG(GBA_MP2K_EVENTS, INFO,
		"[B6JJ EVENT] generation=%llu seq=%llu gameCycle=%llu audioSample=%llu targetCycle=%llu actualCycle=%llu kind=%s id=%u header=%08x",
		(unsigned long long) b6jjGeneration, (unsigned long long) e->sequence,
		(unsigned long long) e->gameCycle, (unsigned long long) e->audioSample,
		(unsigned long long) e->targetCycle, (unsigned long long) e->actualCycle,
		e->se ? "SE" : "BGM", e->id, e->header);
}
static void _b6jjAttachTrace(void) {
	if (b6jjAudio && getenv("MGBA_B6JJ_EVENT_TRACE"))
		B6JJAudioSetDiagnosticSink(b6jjAudio, _b6jjEventTrace, NULL);
}
static void _b6jjDiagnostic(const char* phase) {
	const struct B6JJAudioStats* s = B6JJAudioGetStats(b6jjAudio);
	if (!s) return;
	int64_t drift = s->roots ? (int64_t) (s->lastRoot-s->firstRoot) - (int64_t) (s->roots-1)*280896 : 0;
	mLOG(GBA_MP2K_EVENTS, INFO,
		"[B6JJ CLOCK] phase=%s roots=%llu drift=%lld rootLate=%llu samples=%llu auxCycle=%llu nextRoot=%llu",
		phase, (unsigned long long) s->roots, (long long) drift, (unsigned long long) s->maximumRootLate,
		(unsigned long long) s->samples, (unsigned long long) s->auxCycle, (unsigned long long) s->nextRoot);
	mLOG(GBA_MP2K_EVENTS, INFO,
		"[B6JJ EVENTS] phase=%s events=%llu applied=%llu bgm=%llu se=%llu pending=%zu queuePeak=%zu late=%llu",
		phase,
		(unsigned long long) s->events, (unsigned long long) s->applied,
		(unsigned long long) s->bgm, (unsigned long long) s->se, s->queue, s->queuePeak,
		(unsigned long long) s->maximumLate);
	mLOG(GBA_MP2K_EVENTS, INFO,
		"[B6JJ BUFFER] phase=%s peak=%zu capacity=16384 available=%zu lookahead=%zu underrun=%llu overrun=%llu guard=%llu failure=%s",
		phase, s->bufferPeak, s->available, s->lookaheadSamples,
		(unsigned long long) s->underrun, (unsigned long long) s->overrun,
		(unsigned long long) s->guardFailures, s->failure ? s->failure : "none");
	mLOG(GBA_MP2K_EVENTS, INFO, "[B6JJ FRONTEND] phase=%s accepted=%llu partialBatches=%llu",
		phase, (unsigned long long) b6jjCallbackFrames, (unsigned long long) b6jjPartialBatches);
}
static void _dropB6JJAudio(const char* reason) {
	if (!b6jjAudio) return;
	_b6jjDiagnostic("dispose");
	B6JJAudioDestroy(b6jjAudio); b6jjAudio = NULL;
	memset(b6jjOutput, 0, sizeof(b6jjOutput));
	memset(&fixedAudioClock, 0, sizeof(fixedAudioClock));
	/* Entire stale queue/PCM/private board is gone before primary load/run.
	 * Re-arm requires Reset/reload, not an arbitrary later song request. */
	mLOG(GBA_MP2K_EVENTS, WARN, "[B6JJ AUDIO] NATIVE_FALLBACK reason=%s aux=0 ring=0 queue=0 sticky=1", reason);
}
static bool _openB6JJAudio(void) {
	if (fixedAudioMode == FIXED_AUDIO_DISABLED || !B6JJAudioIdentity(core)) return false;
	fixedAudioBackend = FIXED_AUDIO_BACKEND_B6JJ;
	b6jjRecoveryPending = b6jjRewinding = false;
	b6jjGeneration = b6jjLoads = b6jjRebuilds = 0;
	b6jjRewindRebuildBase = 0;
	_openFixedAudioClock();
	/* B6JJ programs SOUNDBIAS to native 65536Hz during bootstrap. The reset
	 * board still advertises32768Hz; do not lock the aux clock to that default. */
	mAudioClockInit(&fixedAudioClock, (double) core->frequency(core)/core->frameCycles(core), 65536);
	fixedAudioToneEnabled = false;
	b6jjLoggedSpeed = 0;
	b6jjCallbackFrames = b6jjPartialBatches = 0;
	b6jjAudio = B6JJAudioCreate(core, &fixedAudioClock);
	_b6jjAttachTrace();
	if (!b6jjAudio) {
		memset(&fixedAudioClock, 0, sizeof(fixedAudioClock));
		mLOG(GBA_MP2K_EVENTS, WARN, "[B6JJ AUDIO] NATIVE_FALLBACK reason=INITIALIZATION aux=0 ring=0 queue=0 sticky=1");
	} else {
		mLOG(GBA_MP2K_EVENTS, INFO, "[B6JJ AUDIO] backend=FIXED_AUDIO_BACKEND_B6JJ identity=B6JJ/C956FD37/SHA256 rate=65536 format=s16-stereo status=EXPERIMENTAL load=native-fallback rewind=native-fallback");
	}
	return true;
}
static void _recoverB6JJAudio(void) {
	if (!b6jjRecoveryPending || b6jjRewinding || b6jjAudio) return;
	uint64_t start = _stateLoadTimeUs();
	mAudioClockInit(&fixedAudioClock, (double) core->frequency(core)/core->frameCycles(core), 65536);
	++b6jjRebuilds;
	b6jjAudio = B6JJAudioCreateLoaded(core, &fixedAudioClock);
	_b6jjAttachTrace();
	b6jjRecoveryPending = false;
	if (!b6jjAudio) {
		memset(&fixedAudioClock, 0, sizeof(fixedAudioClock));
		mLOG(GBA_MP2K_EVENTS, WARN, "[B6JJ RECOVERY] FAILED generation=%llu native=1 aux=0 ring=0 queue=0",
			(unsigned long long) b6jjGeneration);
		return;
	}
	b6jjLoggedSpeed = 0;
	mAudioBufferClear(core->getAudioBuffer(core));
	const struct B6JJAudioStats* s = B6JJAudioGetStats(b6jjAudio);
	mLOG(GBA_MP2K_EVENTS, INFO,
		"[B6JJ RECOVERY] READY generation=%llu loads=%llu rebuilds=%llu recoveryRuns=0 recoveryRoots=1 recoveryCycles=%llu recoveryUs=%llu start=continuation staleQueue=0 stalePcm=0",
		(unsigned long long) b6jjGeneration, (unsigned long long) b6jjLoads, (unsigned long long) b6jjRebuilds,
		(unsigned long long) s->recoveryCycles, (unsigned long long) (_stateLoadTimeUs()-start));
}
static bool _submitB6JJAudio(void) {
	if (!b6jjAudio) return false;
	size_t frames = fixedAudioClock.runAdvance;
	if (!frames || !B6JJAudioRender(b6jjAudio, b6jjOutput, frames)) {
		const struct B6JJAudioStats* s = B6JJAudioGetStats(b6jjAudio);
		_dropB6JJAudio(s && s->failure ? s->failure : "RENDER_CLOCK");
		return false;
	}
	if (mp2kPcmTrace.enabled) {
		if (fwrite(b6jjOutput, sizeof(int16_t)*2, frames, mp2kPcmTrace.pcm) != frames) {
			fclose(mp2kPcmTrace.pcm); mp2kPcmTrace.pcm = NULL; mp2kPcmTrace.enabled = false;
		} else mp2kPcmTrace.submittedFrames += frames;
	}
	size_t accepted = _probeAudioCallback(b6jjOutput, frames);
	b6jjCallbackFrames += accepted;
	if (fixedAudioRunTraceEnabled) {
		const struct B6JJAudioStats* s = B6JJAudioGetStats(b6jjAudio);
		mLOG(GBA_MP2K_EVENTS, INFO,
			"[B6JJ RUN] run=%llu rate=%.6f callback=%zu accepted=%zu fill=%zu sequence=%llu",
			(unsigned long long) (fixedAudioClockRuns+1), fixedAudioClock.frontendRunRate,
			frames, accepted, s->available-frames, (unsigned long long) s->applied);
	}
	if (accepted != frames) {
		++b6jjPartialBatches;
		_dropB6JJAudio(accepted > frames ? "INVALID_FRONTEND_BATCH_RESULT" : "FRONTEND_PARTIAL_AUDIO_BATCH");
		/* The frontend already consumed this batch. Native resumes next run,
		 * without submitting a second whole batch on this same run. */
		return true;
	}
	if (!(fixedAudioClockRuns % 600)) _b6jjDiagnostic("run");
	return true;
}
static void _beginFixedAudioClockRun(void) {
	if (fixedAudioBackend == FIXED_AUDIO_BACKEND_B6JJ) {
		bool rewind = mp2kCurrentFrontend.throttleStateKnown &&
			mp2kCurrentFrontend.throttleMode == GBA_MP2K_THROTTLE_REWIND;
		if (rewind) {
			if (!b6jjRewinding) {
				_dropB6JJAudio("REWIND_BEGIN");
				b6jjRewindRebuildBase = b6jjRebuilds;
				b6jjRewinding = b6jjRecoveryPending = true;
				mLOG(GBA_MP2K_EVENTS, INFO, "[B6JJ RECOVERY] REWIND_BEGIN intermediateRebuilds=0");
			}
			return;
		}
		if (b6jjRewinding) {
			b6jjRewinding = false;
			mLOG(GBA_MP2K_EVENTS, INFO, "[B6JJ RECOVERY] REWIND_END intermediateRebuilds=%llu finalRebuilds=1",
				(unsigned long long) (b6jjRebuilds-b6jjRewindRebuildBase));
		}
		if (b6jjRecoveryPending) {
			double normal = (double) core->frequency(core)/core->frameCycles(core);
			if (!_fixedAudioSupportedSpeed(normal)) return;
			_recoverB6JJAudio();
		}
	}
	if (!fixedAudioClock.enabled) {
		return;
	}
	double rate = mp2kCurrentFrontend.runRate;
	double normal = fixedAudioClock.nominalRunRate;
	unsigned supportedSpeed = _fixedAudioSupportedSpeed(normal);
	if (fixedAudioBackend == FIXED_AUDIO_BACKEND_B6JJ) {
		if (mp2kCurrentFrontend.throttleMode == GBA_MP2K_THROTTLE_REWIND) {
			_dropB6JJAudio("REWIND_DETECTED"); return;
		}
		if (!supportedSpeed) { _dropB6JJAudio("UNSUPPORTED_FRONTEND_CLOCK"); return; }
		mAudioClockSetFrontendRate(&fixedAudioClock, true, rate);
		uint64_t gameCycle = 0; GBAMP2kEventsTracePosition(core, &gameCycle, NULL);
		mAudioClockBeginRun(&fixedAudioClock, gameCycle, core->frameCycles(core));
		unsigned speed = supportedSpeed;
		if (b6jjLoggedSpeed != speed) {
			mLOG(GBA_MP2K_EVENTS, INFO, "[B6JJ AUDIO] speed=%u rate=%.6f sample=%llu fraction=%.9f", speed, rate,
				(unsigned long long) fixedAudioClock.absoluteAudioSample, fixedAudioClock.fractionalAccumulator);
			b6jjLoggedSpeed = speed;
		}
		return;
	}
	fixedAudioOutput.clockSupported = supportedSpeed != 0;
	bool rateKnown = mp2kCurrentFrontend.throttleStateKnown && rate >= 0;
	if (fixedAudioOutput.requested && !fixedAudioOutput.hardFallback) {
		if (stateLoadStage == STATE_LOAD_RATE_SETTLING || stateLoadStage == STATE_LOAD_REWINDING || stateLoadStage == STATE_LOAD_REWIND_END_PENDING) {
			rate = 0; rateKnown = true; /* Suspended audio timeline, native GBA still runs. */
		} else if (fixedAudioOutput.clockSupported) {
			if (stateLoadStage == STATE_LOAD_REARM_PENDING && rate != fixedAudioOutput.lastSupportedRate) stateLoadRetryWait = 0;
			fixedAudioOutput.lastSupportedRate = rate;
			fixedAudioOutput.unsupportedRuns = 0;
		} else if (fixedAudioOutput.lastSupportedRate &&
		           ++fixedAudioOutput.unsupportedRuns <= FIXED_AUDIO_CLOCK_GRACE_RUNS) {
			_fixedAudioTransient("FRONTEND_THROTTLE_TEMPORARILY_UNSUPPORTED");
			rate = fixedAudioOutput.lastSupportedRate;
			rateKnown = true;
		} else {
			_stateLoadSuspend("FRONTEND_RATE_SETTLING");
			stateLoadStage = STATE_LOAD_RATE_SETTLING;
			stateLoadBeginRun = fixedAudioClockRuns;
			stateLoadWasRewind = false;
			rate = 0; rateKnown = true;
		}
	}
	mAudioClockSetFrontendRate(&fixedAudioClock, rateKnown, rate);
	uint64_t gameCycle = 0;
	GBAMP2kEventsTracePosition(core, &gameCycle, NULL);
	mAudioClockBeginRun(&fixedAudioClock, gameCycle, core->frameCycles(core));
	GBAMP2kEventsSetAudioClock(core, &fixedAudioClock);
	if (fixedAudioOutput.active) {
		if (!fixedAudioClockRuns) {
			const char* psg = getenv("MGBA_FIXED_AUDIO_INJECT_UNKNOWN_PSG");
			if (psg && psg[0] >= '0' && psg[0] <= '3' && !psg[1]) {
				struct GBAMP2kPsgWriteOwner synthetic = {
					.sourcePC = 0x08012340, .channel = (uint8_t) (psg[0] - '0'),
					.voiceType = (uint8_t) (psg[0] - '0' + 1), .voiceStatus = 1
				};
				if (GBAMP2kPsgWriteNeedsNative(GBAMP2kEventsProfile(core), &synthetic, true))
					_ownershipFallback("synthetic unknown PSG write");
			}
			const char* fifo = getenv("MGBA_FIXED_AUDIO_INJECT_UNKNOWN_FIFO");
			if (fifo && !strcmp(fifo, "1") &&
			    !GBAMP2kIsOwnedDirectSoundDma(GBAMP2kEventsProfile(core),
			        0x08012340, false, 1, 0x030033E0))
				_ownershipFallback("synthetic unknown Direct Sound DMA");
		}
		if (fixedAudioOutput.active) {
			int speed = (int) supportedSpeed;
			if (!fixedAudioOutput.loggedSpeed) {
				const struct GBAMP2kProfile* profile = GBAMP2kEventsProfile(core);
				mLOG(GBA_MP2K_EVENTS, INFO,
					"FIXED AUDIO ACTIVE game=%s profile=%s sampleRate=%.0f lookahead=%zu frontendRate=%.3f bridge=loaded",
					profile->gameCode, profile->name, fixedAudioClock.outputSampleRate,
					fixedAudioOutput.lookahead, rate);
			}
			if (speed != fixedAudioOutput.loggedSpeed) {
				mLOG(GBA_MP2K_EVENTS, INFO, "FIXED AUDIO speed %d.0x", speed);
				fixedAudioOutput.loggedSpeed = speed;
			}
		}
	}
}

static void _endFixedAudioClockRun(void) {
	if (!fixedAudioClock.enabled) {
		return;
	}
	mAudioClockEndRun(&fixedAudioClock);
	++fixedAudioClockRuns;
	if (fixedAudioOutput.diagnostics) _fixedAudioDiagnostic("run-end");
	if (fixedAudioRunTraceEnabled) {
		mLOG(GBA_MP2K_EVENTS, INFO,
			"[AUDIO CLOCK] run=%llu rate=%.3f advance=%u sample=%llu frac=%.9f status=%s",
			(unsigned long long) fixedAudioClockRuns, fixedAudioClock.frontendRunRate,
			fixedAudioClock.runAdvance, (unsigned long long) fixedAudioClock.absoluteAudioSample,
			fixedAudioClock.fractionalAccumulator,
			fixedAudioClock.unlimited ? "unsupported-unlimited" :
				fixedAudioClock.frontendRateKnown ? "ok" : "nominal-fallback");
	}
}

static void _sendNativeAudio(size_t produced) {
	if (!produced || fixedAudioToneEnabled) return;
	if (fixedAudioOutput.outputRateRefreshPending) {
		fixedAudioOutput.outputRateRefreshPending = false;
		_audioRateChanged(&stream, core->audioSampleRate(core));
		_fixedAudioDiagnostic("native-rate-refresh");
	}
	if (audioLowPassEnabled) _audioLowPassFilter(audioSampleBuffer, (int) produced);
	if (mp2kPcmTrace.enabled) {
		if (fwrite(audioSampleBuffer, sizeof(int16_t) * 2, produced, mp2kPcmTrace.pcm) != produced) {
			mLOG(GBA_MP2K_EVENTS, ERROR, "[MP2K PCM] write failed at frame=%llu",
				(unsigned long long) mp2kPcmTrace.submittedFrames);
			fclose(mp2kPcmTrace.pcm);
			mp2kPcmTrace.pcm = NULL;
			mp2kPcmTrace.enabled = false;
			GBAMP2kEventsDisablePcmTrace(core);
		}
	}
	size_t accepted = _probeAudioCallback(audioSampleBuffer, produced);
	if (fixedAudioOutput.requested) {
		fixedAudioOutput.lastNativeSample[0] = audioSampleBuffer[2 * (produced - 1)];
		fixedAudioOutput.lastNativeSample[1] = audioSampleBuffer[2 * (produced - 1) + 1];
	}
	if (fixedAudioOutput.recovering && accepted != produced) fixedAudioOutput.healthyRuns = 0;
	if (mp2kPcmTrace.enabled) mp2kPcmTrace.submittedFrames += produced;
	if (GBAMP2kEventsEnabled(core)) {
		mp2kAudioStats.sent += produced;
		++mp2kAudioStats.audioCalls;
	}
	if (fixedAudioOutput.requested) {
		fixedAudioOutput.nativeFrames += produced;
		fixedAudioOutput.callbackFrames += produced;
		if (fixedAudioOutput.callbackPcm) fwrite(audioSampleBuffer, sizeof(int16_t) * 2, produced, fixedAudioOutput.callbackPcm);
	}
}

static void _submitFixedAudio(size_t nativeFrames) {
	const struct GBAMP2kProfile* profile = GBAMP2kEventsProfile(core);
	if ((fixedAudioOutput.active || fixedAudioOutput.recovering) &&
	    (!GBAMP2kEventsEnabled(core) || !profile || !fixedAudioClock.enabled ||
	     fixedAudioClock.outputSampleRate != profile->outputSampleRate))
		_fixedAudioFallback("INVALID_PROFILE_OR_AUDIO_CLOCK");
	if (fixedAudioOutput.requested) fixedAudioOutput.nativeSamplePosition += nativeFrames;
	if (fixedAudioOutput.recovering) {
		bool ready = nativeFrames && fixedAudioOutput.clockSupported && mp2kCandidate.enabled &&
			!mp2kCandidate.queue.dropped && !mp2kCandidate.queue.unresolvedStarts &&
			mp2kCandidate.frames == fixedAudioClock.runStartSample + fixedAudioClock.runAdvance &&
			fixedAudioClock.runAdvance && fixedAudioClock.runAdvance <= fixedAudioOutput.outputCapacity &&
			fixedAudioOutput.writeFrame >= fixedAudioOutput.readFrame &&
			fixedAudioOutput.writeFrame - fixedAudioOutput.readFrame <= fixedAudioOutput.capacity &&
			fixedAudioOutput.writeFrame - fixedAudioOutput.readFrame >= fixedAudioClock.runAdvance + fixedAudioOutput.lookahead;
		fixedAudioOutput.healthyRuns = ready ? fixedAudioOutput.healthyRuns + 1 : 0;
		if (fixedAudioOutput.healthyRuns >= FIXED_AUDIO_RECOVERY_RUNS) {
			/* Keep just the current callback and lookahead; never restart a song. */
			fixedAudioOutput.readFrame = fixedAudioOutput.writeFrame - fixedAudioClock.runAdvance - fixedAudioOutput.lookahead;
			fixedAudioOutput.startupRemaining = 0;
			fixedAudioOutput.active = true;
			fixedAudioOutput.recoveryRamp = true;
			fixedAudioOutput.recovering = false;
			fixedAudioOutput.outputRateRefreshPending = true;
			++fixedAudioOutput.recoveryCount;
			if (stateLoadStage == STATE_LOAD_RECOVERING) {
				mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO] %s runs=%llu state=ACTIVE",
					ramPlayerContinueRecovery && !ramPlayerContinueCompletesLoad ? "RAM_PLAYER_CONTINUE_RECOVERED" : "STATE_LOAD_RECOVERED",
					(unsigned long long) (fixedAudioClockRuns - stateLoadBeginRun));
				if (stateLoadWasRewind) mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO] REWIND_RECOVERED runs=%llu state=ACTIVE",
						(unsigned long long) (fixedAudioClockRuns - stateLoadLastRun));
				stateLoadStage = STATE_LOAD_NONE;
				stateLoadWasRewind = false;
				ramPlayerContinueRecovery = ramPlayerContinueCompletesLoad = false;
			}
			mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO] RECOVERED reason=%s healthyRuns=%u playerPosition=%llu ringFill=%llu",
				fixedAudioOutput.fallbackReason, fixedAudioOutput.healthyRuns,
				(unsigned long long) mp2kCandidate.frames,
				(unsigned long long) (fixedAudioOutput.writeFrame - fixedAudioOutput.readFrame));
			mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO] status=%s audio=fixed reason=recovered",
				profile->known ? "VALIDATED" : runtimeAudioPartial || runtimeAudioPermissive ? "PARTIAL" : "EXPERIMENTAL");
			fixedAudioOutput.fallbackReason = NULL;
			_fixedAudioDiagnostic("recovered");
		}
	}
	if ((fixedAudioOutput.active || fixedAudioOutput.recovering) && (mp2kCandidate.queue.dropped || mp2kCandidate.queue.unresolvedStarts)) {
		_fixedAudioFallback("semantic event queue error");
	}
	if (fixedAudioOutput.outputRateRefreshPending) {
		fixedAudioOutput.outputRateRefreshPending = false;
		_audioRateChanged(&stream, core->audioSampleRate(core));
	}
	/* Snapshot after the recovery AV refresh so interval deltas include its
	 * completed frontend work, not a pending notification from the prior load. */
	if (fixedAudioOutput.active && fixedAudioOutput.recoveryRamp) _stateLoadProfileLog();
	if (!fixedAudioOutput.active) {
		_sendNativeAudio(nativeFrames);
		if (fixedAudioOutput.recovering && fixedAudioOutput.writeFrame - fixedAudioOutput.readFrame > fixedAudioOutput.lookahead)
			fixedAudioOutput.readFrame = fixedAudioOutput.writeFrame - fixedAudioOutput.lookahead;
		return;
	}
	size_t frames = fixedAudioClock.runAdvance;
	if (!fixedAudioOutput.ring || !fixedAudioOutput.output || !fixedAudioOutput.capacity ||
	    fixedAudioOutput.writeFrame < fixedAudioOutput.readFrame ||
	    fixedAudioOutput.writeFrame - fixedAudioOutput.readFrame > fixedAudioOutput.capacity) {
		_fixedAudioFallback("INVALID_RING_BOUNDS"); _sendNativeAudio(nativeFrames); return;
	}
	if (frames > fixedAudioOutput.outputCapacity || !frames) {
		_fixedAudioFallback("unsupported audio frame count");
		_sendNativeAudio(nativeFrames);
		return;
	}
	const char* shortage = getenv("MGBA_FIXED_AUDIO_INJECT_RING_UNDERRUN_AT");
	if (shortage && fixedAudioClockRuns == strtoull(shortage, NULL, 10))
		fixedAudioOutput.readFrame = fixedAudioOutput.writeFrame;
	size_t silence = fixedAudioOutput.startupRemaining < frames ?
		(size_t) fixedAudioOutput.startupRemaining : frames;
	size_t candidate = frames - silence;
	if (fixedAudioOutput.writeFrame - fixedAudioOutput.readFrame < candidate) {
		++fixedAudioOutput.underrunCount;
		_fixedAudioTransient("RING_UNDERRUN_OR_PENDING_FIRST_TICK");
		_sendNativeAudio(nativeFrames);
		return;
	}
	memset(fixedAudioOutput.output, 0, silence * 2 * sizeof(int16_t));
	for (size_t i = 0; i < candidate; ++i) {
		size_t slot = (size_t) ((fixedAudioOutput.readFrame + i) % fixedAudioOutput.capacity);
		fixedAudioOutput.output[2 * (silence + i)] = fixedAudioOutput.ring[2 * slot];
		fixedAudioOutput.output[2 * (silence + i) + 1] = fixedAudioOutput.ring[2 * slot + 1];
	}
	if (fixedAudioOutput.recoveryRamp) {
		/* A short interpolation from the last native sample avoids an abrupt
		 * boundary without replaying either player or mixing two timelines. */
		size_t ramp = frames < 32 ? frames : 32;
		for (size_t i = 0; i < ramp; ++i) {
			for (unsigned ch = 0; ch < 2; ++ch) {
				int32_t previous = fixedAudioOutput.lastNativeSample[ch];
				int32_t current = fixedAudioOutput.output[2 * i + ch];
				fixedAudioOutput.output[2 * i + ch] = previous + (current - previous) * (int32_t) (i + 1) / (int32_t) ramp;
			}
		}
		fixedAudioOutput.recoveryRamp = false;
	}
	if (fixedAudioOutput.callbackPcm &&
	    fwrite(fixedAudioOutput.output, sizeof(int16_t) * 2, frames, fixedAudioOutput.callbackPcm) != frames) {
		_fixedAudioFallback("callback PCM trace write error");
		_sendNativeAudio(nativeFrames);
		return;
	}
	fixedAudioOutput.startupRemaining -= silence;
	fixedAudioOutput.readFrame += candidate;
	fixedAudioOutput.candidateFrames += candidate;
	size_t accepted = _probeAudioCallback(fixedAudioOutput.output, frames);
	fixedAudioOutput.callbackFrames += accepted;
	if (accepted != frames) {
		++fixedAudioOutput.underrunCount;
		if (accepted > frames) _fixedAudioFallback("INVALID_FRONTEND_BATCH_RESULT");
		else _fixedAudioTransient("FRONTEND_PARTIAL_AUDIO_BATCH");
	}
	if (fixedAudioRunTraceEnabled) {
		mLOG(GBA_MP2K_EVENTS, INFO,
			"[FIXED AUDIO RUN] run=%llu rate=%.3f callback=%zu accepted=%zu candidate=%zu startup=%zu fill=%llu underrun=%llu overrun=%llu fallback=%llu",
			(unsigned long long) (fixedAudioClockRuns + 1), fixedAudioClock.frontendRunRate,
			frames, accepted, candidate, silence,
			(unsigned long long) (fixedAudioOutput.writeFrame - fixedAudioOutput.readFrame),
			(unsigned long long) fixedAudioOutput.underrunCount,
			(unsigned long long) fixedAudioOutput.overrunCount,
			(unsigned long long) fixedAudioOutput.fallbackCount);
	}
}

static void _updateMP2kAudioStats(void) {
	if (!GBAMP2kEventsEnabled(core) || !mp2kAudioStats.clock) {
		return;
	}
	retro_time_t now = mp2kAudioStats.clock();
	if (!mp2kAudioStats.start) {
		mp2kAudioStats.start = now;
	}
	++mp2kAudioStats.runs;
	retro_time_t elapsed = now - mp2kAudioStats.start;
	if (elapsed >= 1000000) {
		mLOG(GBA_MP2K_EVENTS, INFO, "[MP2K AUDIO PATH] runs=%u actual=%.2f/s availFrames=%llu sentFrames=%llu calls=%u",
			mp2kAudioStats.runs, mp2kAudioStats.runs * 1000000.0 / elapsed,
			(unsigned long long) mp2kAudioStats.available,
			(unsigned long long) mp2kAudioStats.sent, mp2kAudioStats.audioCalls);
		mp2kAudioStats.start = now;
		mp2kAudioStats.runs = 0;
		mp2kAudioStats.audioCalls = 0;
		mp2kAudioStats.available = 0;
		mp2kAudioStats.sent = 0;
	}
}
#endif

void retro_run(void) {
#ifdef M_CORE_GBA
	retro_time_t probeStart = fixedAudioProbe.clock ? fixedAudioProbe.clock() : 0;
	retro_time_t probeGame = 0, probeAudio = 0;
#endif
	if (deferredSetup) {
		_doDeferredSetup();
	}
#ifdef M_CORE_GBA
	_updateMP2kFrontendState();
	_stateLoadTransportRun();
	_stateLoadRebindRun(false);
	_beginFixedAudioClockRun();
	if (mp2kPcmTrace.enabled && !fixedAudioToneEnabled) {
		GBAMP2kEventsSetPcmSampleBase(core, mp2kPcmTrace.submittedFrames);
	} else if (fixedAudioToneEnabled) {
		GBAMP2kEventsDisablePcmTrace(core);
	}
#endif
	uint16_t keys;

	inputPollCallback();

	bool updated = false;
	if (environCallback(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated) {
		envVarsUpdated = true;

		struct retro_variable var = {
			.key = "mgba_allow_opposing_directions",
			.value = 0
		};
		if (environCallback(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
			mCoreConfigSetIntValue(&core->config, "allowOpposingDirections", strcmp(var.value, "yes") == 0);
			core->reloadConfigOption(core, "allowOpposingDirections", NULL);
		}

		_loadAudioLowPassFilterSettings();
		var.key = "mgba_frameskip";
		var.value = 0;
		if (environCallback(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
			mCoreConfigSetIntValue(&core->config, "frameskip", strtol(var.value, NULL, 10));
			core->reloadConfigOption(core, "frameskip", NULL);
		}

#ifdef M_CORE_GB
		_updateGbPal();
#endif
	}

	keys = 0;
	unsigned i;
	if (useBitmasks) {
		int16_t joypadMask = inputCallback(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
		for (i = 0; i < sizeof(keymap) / sizeof(*keymap); ++i) {
			keys |= ((joypadMask >> keymap[i]) & 1) << i;
		}
	} else {
		for (i = 0; i < sizeof(keymap) / sizeof(*keymap); ++i) {
			keys |= (!!inputCallback(0, RETRO_DEVICE_JOYPAD, 0, keymap[i])) << i;
		}
	}
#ifdef M_CORE_GBA
	_traceMP2kInput(keys);
#endif
	core->setKeys(core, keys);

	if (!luxSensorUsed) {
		static bool wasAdjustingLux = false;
		if (wasAdjustingLux) {
			wasAdjustingLux = inputCallback(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3) ||
			                  inputCallback(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3);
		} else {
			if (inputCallback(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3)) {
				++luxLevelIndex;
				if (luxLevelIndex > 10) {
					luxLevelIndex = 10;
				}
				wasAdjustingLux = true;
			} else if (inputCallback(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3)) {
				--luxLevelIndex;
				if (luxLevelIndex < 0) {
					luxLevelIndex = 0;
				}
				wasAdjustingLux = true;
			}
		}
	}

#ifdef M_CORE_GBA
	if (fixedAudioProbe.clock) probeGame = fixedAudioProbe.clock();
	bool probeGba = fixedAudioProbe.clock && core->platform(core) == mPLATFORM_GBA;
	uint32_t gbaFrameBefore = probeGba ? core->frameCounter(core) : 0;
#endif
	core->runFrame(core);
#ifdef M_CORE_GBA
	if (fixedAudioProbe.clock) probeAudio = fixedAudioProbe.clock();
	uint32_t gbaFrameAfter = probeGba ? core->frameCounter(core) : 0;
	if (probeGba) {
		uint32_t delta = gbaFrameAfter - gbaFrameBefore; /* includes uint32 wrap */
		fixedAudioProbe.gbaFrames += delta;
		if (delta != 1) ++fixedAudioProbe.gbaStepAnomalies;
	}
#endif
	unsigned width, height;
	core->currentVideoSize(core, &width, &height);
	videoCallback(outputBuffer, width, height, BYTES_PER_PIXEL * 256);

#ifdef M_CORE_GBA
	size_t fixedNativeFrames = 0;
	if (core->platform(core) == mPLATFORM_GBA) {
		struct mAudioBuffer *buffer = core->getAudioBuffer(core);
		int samplesAvail            = mAudioBufferAvailable(buffer);
		if (GBAMP2kEventsEnabled(core) && samplesAvail > 0) {
			mp2kAudioStats.available += samplesAvail;
		}
		if (samplesAvail > 0) {
			/* Update 'running average' of number of
			 * samples per frame.
			 * Note that this is not a true running
			 * average, but just a leaky-integrator/
			 * exponential moving average, used because
			 * it is simple and fast (i.e. requires no
			 * window of samples). */
			audioSamplesPerFrameAvg = (SAMPLES_PER_FRAME_MOVING_AVG_ALPHA * (float)samplesAvail) +
					((1.0f - SAMPLES_PER_FRAME_MOVING_AVG_ALPHA) * audioSamplesPerFrameAvg);
			size_t samplesToRead = (size_t)(audioSamplesPerFrameAvg);
			if (b6jjAudio) samplesToRead = (size_t) samplesAvail;
			/* Resize audio output buffer, if required */
			if (audioSampleBufferSize < (samplesToRead * 2)) {
				audioSampleBufferSize = (samplesToRead * 2);
				audioSampleBuffer     = realloc(audioSampleBuffer, audioSampleBufferSize * sizeof(int16_t));
			}
			int produced = mAudioBufferRead(buffer, audioSampleBuffer, samplesToRead);
			fixedNativeFrames = produced > 0 ? (size_t) produced : 0;
			if (produced > 0) {
				_writeGBAStemTrace((size_t) produced);
			}
		}
	}
	if (!_submitB6JJAudio()) {
		_emitFixedAudioTone();
		_pollRuntimeMP2kPlayers();
		_renderMP2kCandidate();
		_submitFixedAudio(fixedNativeFrames);
	}
	_traceMP2kState();
	_traceMP2kFadeState();
	_updateMP2kAudioStats();
	_endFixedAudioClockRun();
	if (fixedAudioProbe.clock) {
		retro_time_t now = fixedAudioProbe.clock();
		++fixedAudioProbe.runs;
		fixedAudioProbe.samples += b6jjAudio || fixedAudioOutput.active ? fixedAudioClock.runAdvance : 0;
		fixedAudioProbe.transportUs += probeGame - probeStart;
		fixedAudioProbe.gameUs += probeAudio - probeGame;
		fixedAudioProbe.audioUs += now - probeAudio;
		if (!(fixedAudioProbe.runs % 120)) {
			if (probeGba) {
				mLOG(GBA_MP2K_EVENTS, INFO, "[GBA FRAME PROBE] runs=%llu us=%lld counter=%u emulatedFrames=%llu stepAnomalies=%llu callbackRequested=%llu callbackAccepted=%llu",
					(unsigned long long) fixedAudioProbe.runs, (long long) now, gbaFrameAfter,
					(unsigned long long) fixedAudioProbe.gbaFrames, (unsigned long long) fixedAudioProbe.gbaStepAnomalies,
					(unsigned long long) fixedAudioProbe.callbackRequested, (unsigned long long) fixedAudioProbe.callbackAccepted);
			}
			mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO PROBE] frames=%llu us=%lld fixedSamples=%llu fixed=%d",
				(unsigned long long) fixedAudioProbe.runs, (long long) now,
				(unsigned long long) fixedAudioProbe.samples, (int) (b6jjAudio || fixedAudioOutput.active));
			mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO PROFILE] frames=%llu transportUs=%llu gameUs=%llu audioUs=%llu",
				(unsigned long long) fixedAudioProbe.runs, (unsigned long long) fixedAudioProbe.transportUs,
				(unsigned long long) fixedAudioProbe.gameUs, (unsigned long long) fixedAudioProbe.audioUs);
		}
	}
#endif
}

static void _setupMaps(struct mCore* core) {
#ifdef M_CORE_GBA
	if (core->platform(core) == mPLATFORM_GBA) {
		struct GBA* gba = core->board;
		struct retro_memory_descriptor descs[11];
		struct retro_memory_map mmaps;
		size_t romSize = gba->memory.romSize + (gba->memory.romSize & 1);

		memset(descs, 0, sizeof(descs));
		size_t savedataSize = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);

		/* Map internal working RAM */
		descs[0].ptr    = gba->memory.iwram;
		descs[0].start  = GBA_BASE_IWRAM;
		descs[0].len    = GBA_SIZE_IWRAM;
		descs[0].select = 0xFF000000;

		/* Map working RAM */
		descs[1].ptr    = gba->memory.wram;
		descs[1].start  = GBA_BASE_EWRAM;
		descs[1].len    = GBA_SIZE_EWRAM;
		descs[1].select = 0xFF000000;

		/* Map save RAM */
		/* TODO: if SRAM is flash, use start=0 addrspace="S" instead */
		descs[2].ptr    = savedataSize ? savedata : NULL;
		descs[2].start  = GBA_BASE_SRAM;
		descs[2].len    = savedataSize;

		/* Map ROM */
		descs[3].ptr    = gba->memory.rom;
		descs[3].start  = GBA_BASE_ROM0;
		descs[3].len    = romSize;
		descs[3].flags  = RETRO_MEMDESC_CONST;

		descs[4].ptr    = gba->memory.rom;
		descs[4].start  = GBA_BASE_ROM1;
		descs[4].len    = romSize;
		descs[4].flags  = RETRO_MEMDESC_CONST;

		descs[5].ptr    = gba->memory.rom;
		descs[5].start  = GBA_BASE_ROM2;
		descs[5].len    = romSize;
		descs[5].flags  = RETRO_MEMDESC_CONST;

		/* Map BIOS */
		descs[6].ptr    = gba->memory.bios;
		descs[6].start  = GBA_BASE_BIOS;
		descs[6].len    = GBA_SIZE_BIOS;
		descs[6].flags  = RETRO_MEMDESC_CONST;

		/* Map VRAM */
		descs[7].ptr    = gba->video.vram;
		descs[7].start  = GBA_BASE_VRAM;
		descs[7].len    = GBA_SIZE_VRAM;
		descs[7].select = 0xFF000000;

		/* Map palette RAM */
		descs[8].ptr    = gba->video.palette;
		descs[8].start  = GBA_BASE_PALETTE_RAM;
		descs[8].len    = GBA_SIZE_PALETTE_RAM;
		descs[8].select = 0xFF000000;

		/* Map OAM */
		descs[9].ptr    = &gba->video.oam; /* video.oam is a structure */
		descs[9].start  = GBA_BASE_OAM;
		descs[9].len    = GBA_SIZE_OAM;
		descs[9].select = 0xFF000000;

		/* Map mmapped I/O */
		descs[10].ptr    = gba->memory.io;
		descs[10].start  = GBA_BASE_IO;
		descs[10].len    = GBA_SIZE_IO;

		mmaps.descriptors = descs;
		mmaps.num_descriptors = sizeof(descs) / sizeof(descs[0]);

		bool yes = true;
		environCallback(RETRO_ENVIRONMENT_SET_MEMORY_MAPS, &mmaps);
		environCallback(RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS, &yes);
	}
#endif
#ifdef M_CORE_GB
	if (core->platform(core) == mPLATFORM_GB) {
		struct GB* gb = core->board;
		struct retro_memory_descriptor descs[12];
		struct retro_memory_map mmaps;

		memset(descs, 0, sizeof(descs));
		size_t savedataSize = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);

		unsigned i = 0;

		/* Map ROM */
		descs[i].ptr    = gb->memory.rom;
		descs[i].start  = GB_BASE_CART_BANK0;
		descs[i].len    = GB_SIZE_CART_BANK0;
		descs[i].flags  = RETRO_MEMDESC_CONST;
		i++;

		descs[i].ptr    = gb->memory.rom;
		descs[i].offset = GB_SIZE_CART_BANK0;
		descs[i].start  = GB_BASE_CART_BANK1;
		descs[i].len    = GB_SIZE_CART_BANK0;
		descs[i].flags  = RETRO_MEMDESC_CONST;
		i++;

		/* Map VRAM */
		descs[i].ptr    = gb->video.vram;
		descs[i].start  = GB_BASE_VRAM;
		descs[i].len    = GB_SIZE_VRAM_BANK0;
		i++;

		/* Map working RAM */
		descs[i].ptr    = gb->memory.wram;
		descs[i].start  = GB_BASE_WORKING_RAM_BANK0;
		descs[i].len    = GB_SIZE_WORKING_RAM_BANK0;
		i++;

		descs[i].ptr    = gb->memory.wram;
		descs[i].offset = GB_SIZE_WORKING_RAM_BANK0;
		descs[i].start  = GB_BASE_WORKING_RAM_BANK1;
		descs[i].len    = GB_SIZE_WORKING_RAM_BANK0;
		i++;

		/* Map OAM */
		descs[i].ptr    = &gb->video.oam; /* video.oam is a structure */
		descs[i].start  = GB_BASE_OAM;
		descs[i].len    = GB_SIZE_OAM;
		descs[i].select = 0xFFFFFF60;
		i++;

		/* Map mmapped I/O */
		descs[i].ptr    = gb->memory.io;
		descs[i].start  = GB_BASE_IO;
		descs[i].len    = GB_SIZE_IO;
		i++;

		/* Map High RAM */
		descs[i].ptr    = gb->memory.hram;
		descs[i].start  = GB_BASE_HRAM;
		descs[i].len    = GB_SIZE_HRAM;
		descs[i].select = 0xFFFFFF80;
		i++;

		/* Map IE Register */
		descs[i].ptr    = &gb->memory.ie;
		descs[i].start  = GB_BASE_IE;
		descs[i].len    = 1;
		i++;

		/* Map External RAM */
		if (savedataSize) {
			descs[i].ptr    = savedata;
			descs[i].start  = GB_BASE_EXTERNAL_RAM;
			descs[i].len    = savedataSize < GB_SIZE_EXTERNAL_RAM ? savedataSize : GB_SIZE_EXTERNAL_RAM;
			i++;

			if ((savedataSize & ~0xFF) > GB_SIZE_EXTERNAL_RAM) {
				descs[i].ptr    = savedata;
				descs[i].offset = GB_SIZE_EXTERNAL_RAM;
				descs[i].start  = 0x16000;
				descs[i].len    = savedataSize - GB_SIZE_EXTERNAL_RAM;
				i++;
			}
		}

		if (gb->model >= GB_MODEL_CGB) {
			/* Map working RAM */
			/* banks 2-7 of wram mapped in virtual address so it can be
			 * accessed without bank switching, GBC only */
			descs[i].ptr    = gb->memory.wram + 0x2000;
			descs[i].start  = 0x10000;
			descs[i].len    = GB_SIZE_WORKING_RAM - 0x2000;
			i++;
		}

		mmaps.descriptors = descs;
		mmaps.num_descriptors = i;

		bool yes = true;
		environCallback(RETRO_ENVIRONMENT_SET_MEMORY_MAPS, &mmaps);
		environCallback(RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS, &yes);
	}
#endif
}

void retro_reset(void) {
#ifdef M_CORE_GBA
	bool resetB6JJ = fixedAudioBackend == FIXED_AUDIO_BACKEND_B6JJ;
	if (resetB6JJ) _dropB6JJAudio("RESET");
	stateLoadSeen = stateLoadWasRewind = stateLoadWaitForNativeStart = false; stateLoadStableRuns = 0;
	ramPlayerContinueRebindPending = ramPlayerContinueRecovery = ramPlayerContinueCompletesLoad = false;
#endif
	core->reset(core);
#ifdef M_CORE_GBA
	if (resetB6JJ) {
		mAudioBufferClear(core->getAudioBuffer(core));
		_openB6JJAudio();
		return;
	}
	stateLoadStage = STATE_LOAD_NONE;
	_detectRuntimeMP2kProfile();
	_openRuntimeMP2kProbe();
	if (mp2kCandidate.enabled) {
		GBAMP2kSemanticReset(&mp2kCandidate.queue);
		mp2kCandidate.frames = 0;
		for (unsigned i = 0; i < GBA_MP2K_MAX_PLAYERS; ++i) {
			mp2kCandidate.activeSong[i] = -1;
		}
		memset(mp2kCandidate.priorFade, 0, sizeof(mp2kCandidate.priorFade));
		memset(mp2kCandidate.priorCandidateFade, 0, sizeof(mp2kCandidate.priorCandidateFade));
		mp2kCandidate.timingTraceUntil = 0;
		mp2kCandidate.timingLastCall = 0;
		#ifdef _WIN32
		if (stateLoadProfile.enabled) ++stateLoadProfile.destroys;
		mp2kCandidate.destroy(mp2kCandidate.player);
		struct GBA* gba = core->board;
		if (stateLoadProfile.enabled) ++stateLoadProfile.creates;
		mp2kCandidate.player = mp2kCandidate.create((const uint8_t*) gba->memory.rom,
			gba->memory.romSize, (uint32_t) fixedAudioClock.outputSampleRate);
		if (!mp2kCandidate.player) {
			mLOG(GBA_MP2K_EVENTS, ERROR, "[MP2K CANDIDATE] reset failed: %s", mp2kCandidate.error());
			_closeMP2kCandidate();
		} else {
			GBAMP2kEventsSetSink(core, _candidateEventSink, &mp2kCandidate);
		}
		#else
		GBAMP2kEventsSetSink(core, _candidateEventSink, &mp2kCandidate);
		#endif
	} else if (fixedAudioOutput.requested) {
		/* A fatal render closes the DLL/player. Reset is a fresh validated
		 * lifecycle, so recreate it instead of keeping that failure latched. */
		_openMP2kCandidate();
	}
	memset(&mp2kAudioStats, 0, sizeof(mp2kAudioStats));
	memset(&fixedAudioProbe, 0, sizeof(fixedAudioProbe));
	if (getenv("MGBA_FIXED_AUDIO_WALL_PROBE")) {
		struct retro_perf_callback perf = { 0 };
		if (environCallback(RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &perf)) fixedAudioProbe.clock = perf.get_time_usec;
	}
	mp2kPcmTrace.previousKeys = 0;
	if (fixedAudioOutput.requested) {
		mAudioClockInit(&fixedAudioClock, fixedAudioClock.nominalRunRate,
			fixedAudioClock.outputSampleRate);
		fixedAudioOutput.recovering = false;
		fixedAudioOutput.recoveryRamp = false;
		fixedAudioOutput.hardFallback = false;
		fixedAudioOutput.fallbackReason = NULL;
		fixedAudioOutput.healthyRuns = 0;
		fixedAudioOutput.unsupportedRuns = 0;
		fixedAudioOutput.lastSupportedRate = 0;
		fixedAudioOutput.nativeSamplePosition = 0;
		fixedAudioOutput.readFrame = 0;
		fixedAudioOutput.writeFrame = 0;
		fixedAudioOutput.startupRemaining = fixedAudioOutput.lookahead;
		memset(fixedAudioOutput.dmaOwned, 0, sizeof(fixedAudioOutput.dmaOwned));
		memset(fixedAudioOutput.dmaSource, 0, sizeof(fixedAudioOutput.dmaSource));
		fixedAudioOutput.active = mp2kCandidate.enabled && fixedAudioOutput.ring && fixedAudioOutput.output &&
			GBAMP2kEventsProfile(core) && GBAMP2kEventsProfile(core)->known;
		runtimeAudioPending = mp2kCandidate.enabled && fixedAudioOutput.ring && fixedAudioOutput.output && !fixedAudioOutput.active;
		runtimeAudioPartial = runtimeAudioPending;
		/* Native fallback may have changed the frontend sample rate. Re-arm
		 * must advertise the new route, even if core audio rate is unchanged. */
		fixedAudioOutput.outputRateRefreshPending = true;
		if (!fixedAudioOutput.active && !runtimeAudioPending) {
			fixedAudioOutput.hardFallback = true;
			fixedAudioOutput.fallbackReason = "RESET_INITIALIZATION_FAILED";
		}
	}
#endif
	mRumbleIntegratorReset(&rumble);
	_setupMaps(core);
}

bool retro_load_game(const struct retro_game_info* game) {
	struct VFile* rom;
	if (game->data) {
		data = anonymousMemoryMap(game->size);
		dataSize = game->size;
		memcpy(data, game->data, game->size);
		rom = VFileFromMemory(data, game->size);
#ifdef ENABLE_VFS
	} else {
		data = NULL;
		rom = VFileOpen(game->path, O_RDONLY);
#endif
	}
	if (!rom) {
		return false;
	}

	core = mCoreFindVF(rom);
	if (!core) {
		rom->close(rom);
		mappedMemoryFree(data, game->size);
		return false;
	}
	mCoreInitConfig(core, NULL);
	core->init(core);

	outputBuffer = malloc(VIDEO_BUFF_SIZE);
	memset(outputBuffer, 0xFF, VIDEO_BUFF_SIZE);
	core->setVideoBuffer(core, outputBuffer, VIDEO_WIDTH_MAX);

	#ifdef M_CORE_GBA
	/* GBA emulation produces a fairly regular number
	 * of audio samples per frame that is consistent
	 * with the set sample rate. We therefore consume
	 * audio samples in retro_run() to achieve the
	 * best possible frame pacing */
	if (core->platform(core) == mPLATFORM_GBA) {
		/* Set initial output audio buffer size
		 * to nominal number of samples per frame.
		 * Buffer will be resized as required in
		 * retro_run(). */
		size_t audioSamplesPerFrame = (size_t)((float) core->audioSampleRate(core) * (float) core->frameCycles(core) /
			(float)core->frequency(core));
		audioSampleBufferSize  = ceil(audioSamplesPerFrame) * 2;
		audioSampleBuffer = malloc(audioSampleBufferSize * sizeof(int16_t));
		audioSamplesPerFrameAvg = (float) audioSamplesPerFrame;
		/* Internal audio buffer size should be
		 * audioSamplesPerFrame, but number of samples
		 * actually generated varies slightly on a
		 * frame-by-frame basis. We therefore allow
		 * for some wriggle room by setting double
		 * what we need (accounting for the hard
		 * coded blip buffer limit of 0x4000). */
		size_t internalAudioBufferSize = audioSamplesPerFrame * 2;
		if (internalAudioBufferSize > 0x4000) {
			internalAudioBufferSize = 0x4000;
		}
		core->setAudioBufferSize(core, internalAudioBufferSize);
	} else
	#endif
	{
		/* GB/GBC emulation does not produce a number
		 * of samples per frame that is consistent with
		 * the set sample rate, and so it is unclear how
		 * best to handle this. We therefore fallback to
		 * using the regular stream-set _postAudioBuffer()
		 * callback with a fixed buffer size, which seems
		 * (historically) to produce adequate results */
		stream.postAudioBuffer = _postAudioBuffer;
		audioSampleBufferSize = GB_SAMPLES * 2;
		audioSampleBuffer = malloc(audioSampleBufferSize * sizeof(int16_t));
		audioSamplesPerFrameAvg = GB_SAMPLES;
		core->setAudioBufferSize(core, GB_SAMPLES);
	}

	core->setAVStream(core, &stream);
	core->setPeripheral(core, mPERIPH_RUMBLE, &rumble);
	core->setPeripheral(core, mPERIPH_ROTATION, &rotation);

	savedata = anonymousMemoryMap(GBA_SIZE_FLASH1M);
	memset(savedata, 0xFF, GBA_SIZE_FLASH1M);

	_reloadSettings();
#ifdef M_CORE_GBA
	_loadFixedAudioMode();
#endif
	core->loadROM(core, rom);
	deferredSetup = true;

	const char* sysDir = 0;
	const char* biosName = 0;
	char biosPath[PATH_MAX];
	environCallback(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sysDir);

#ifdef M_CORE_GBA
	if (core->platform(core) == mPLATFORM_GBA) {
		core->setPeripheral(core, mPERIPH_GBA_LUMINANCE, &lux);
		biosName = "gba_bios.bin";

	}
#endif

#ifdef M_CORE_GB
	if (core->platform(core) == mPLATFORM_GB) {
		memset(&cam, 0, sizeof(cam));
		cam.height = GBCAM_HEIGHT;
		cam.width = GBCAM_WIDTH;
		cam.caps = 1 << RETRO_CAMERA_BUFFER_RAW_FRAMEBUFFER;
		cam.frame_raw_framebuffer = _updateCamera;
		if (environCallback(RETRO_ENVIRONMENT_GET_CAMERA_INTERFACE, &cam)) {
			core->setPeripheral(core, mPERIPH_IMAGE_SOURCE, &imageSource);
		}

		const char* modelName = mCoreConfigGetValue(&core->config, "gb.model");
		struct GB* gb = core->board;

		if (modelName) {
			gb->model = GBNameToModel(modelName);
		} else {
			GBDetectModel(gb);
		}

		switch (gb->model) {
		case GB_MODEL_AGB:
		case GB_MODEL_CGB:
		case GB_MODEL_SCGB:
			biosName = "gbc_bios.bin";
			break;
		case GB_MODEL_SGB:
			biosName = "sgb_bios.bin";
			break;
		case GB_MODEL_DMG:
		default:
			biosName = "gb_bios.bin";
			break;
		}
	}
#endif

#ifdef ENABLE_VFS
	if (core->opts.useBios && sysDir && biosName) {
		snprintf(biosPath, sizeof(biosPath), "%s%s%s", sysDir, PATH_SEP, biosName);
		struct VFile* bios = VFileOpen(biosPath, O_RDONLY);
		if (bios) {
			core->loadBIOS(core, bios, 0);
		}
	}
#endif

	core->reset(core);
#ifdef M_CORE_GBA
	fixedAudioBackend = FIXED_AUDIO_BACKEND_NATIVE;
	bool b6jjSelected = _openB6JJAudio();
	if (!b6jjSelected) _detectRuntimeMP2kProfile();
	memset(&mp2kAudioStats, 0, sizeof(mp2kAudioStats));
	memset(&fixedAudioProbe, 0, sizeof(fixedAudioProbe));
	if (getenv("MGBA_FIXED_AUDIO_WALL_PROBE")) {
		struct retro_perf_callback perf = { 0 };
		if (environCallback(RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &perf)) fixedAudioProbe.clock = perf.get_time_usec;
	}
	_openMP2kPcmTrace();
	_openGBAStemTrace();
	if (!b6jjSelected) {
		_openFixedAudioClock();
		_openMP2kCandidate();
		_openRuntimeMP2kProbe();
		_openFixedAudioOutput();
		if (fixedAudioOutput.requested) fixedAudioBackend = FIXED_AUDIO_BACKEND_MP2K;
	}
#endif
	_setupMaps(core);

	return true;
}

void retro_unload_game(void) {
	if (!core) {
		return;
	}
#ifdef M_CORE_GBA
	_dropB6JJAudio("UNLOAD");
	fixedAudioBackend = FIXED_AUDIO_BACKEND_NATIVE;
	_closeGBAStemTrace();
	_closeFixedAudioOutput();
	_closeMP2kCandidate();
	_closeMP2kPcmTrace();
	memset(&fixedAudioClock, 0, sizeof(fixedAudioClock));
	fixedAudioToneEnabled = false;
#endif
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
	mappedMemoryFree(data, dataSize);
	data = 0;
	mappedMemoryFree(savedata, GBA_SIZE_FLASH1M);
	savedata = 0;
}

size_t retro_serialize_size(void) {
	if (deferredSetup) {
		_doDeferredSetup();
	}
	struct VFile* vfm = VFileMemChunk(NULL, 0);
	mCoreSaveStateNamed(core, vfm, SAVESTATE_SAVEDATA | SAVESTATE_RTC);
	size_t size = vfm->size(vfm);
	vfm->close(vfm);
	return size;
}

bool retro_serialize(void* data, size_t size) {
	if (deferredSetup) {
		_doDeferredSetup();
	}
	struct VFile* vfm = VFileMemChunk(NULL, 0);
	mCoreSaveStateNamed(core, vfm, SAVESTATE_SAVEDATA | SAVESTATE_RTC);
	if ((ssize_t) size > vfm->size(vfm)) {
		size = vfm->size(vfm);
	} else if ((ssize_t) size < vfm->size(vfm)) {
		vfm->close(vfm);
		return false;
	}
	vfm->seek(vfm, 0, SEEK_SET);
	vfm->read(vfm, data, size);
	vfm->close(vfm);
	return true;
}

bool retro_unserialize(const void* data, size_t size) {
#ifdef M_CORE_GBA
	if (fixedAudioBackend == FIXED_AUDIO_BACKEND_B6JJ) {
		_updateMP2kFrontendState();
		bool rewind = mp2kCurrentFrontend.throttleStateKnown &&
			mp2kCurrentFrontend.throttleMode == GBA_MP2K_THROTTLE_REWIND;
		_dropB6JJAudio("STATE_LOAD_OR_REWIND");
		if (rewind && !b6jjRewinding) {
			b6jjRewindRebuildBase = b6jjRebuilds;
			mLOG(GBA_MP2K_EVENTS, INFO, "[B6JJ RECOVERY] REWIND_BEGIN intermediateRebuilds=0");
		}
		b6jjRewinding = rewind;
		b6jjRecoveryPending = false;
		++b6jjLoads; ++b6jjGeneration;
		mAudioBufferClear(core->getAudioBuffer(core));
		if (audioSampleBuffer) memset(audioSampleBuffer, 0, audioSampleBufferSize*sizeof(int16_t));
	}
	_stateLoadClearFailureContext();
	const char* loadProfile = getenv("MGBA_FIXED_AUDIO_LOAD_PROFILE");
	stateLoadProfile.enabled = loadProfile && !strcmp(loadProfile, "1");
	uint64_t loadStart = stateLoadProfile.enabled ? _stateLoadTimeUs() : 0;
	if (stateLoadProfile.enabled && !stateLoadProfile.loads) stateLoadProfile.firstUs = loadStart;
#endif
	if (deferredSetup) {
		_doDeferredSetup();
	}
	#ifdef M_CORE_GBA
	bool stateLoadRequested = fixedAudioOutput.requested && core->platform(core) == mPLATFORM_GBA;
	ramPlayerContinueRebindPending = ramPlayerContinueRecovery = ramPlayerContinueCompletesLoad = false;
	bool rearm = stateLoadRequested && !fixedAudioOutput.hardFallback;
	if (stateLoadRequested) {
		_updateMP2kFrontendState(); /* Query at the load boundary, not a stale prior run. */
		bool rewind = mp2kCurrentFrontend.throttleStateKnown && mp2kCurrentFrontend.throttleMode == GBA_MP2K_THROTTLE_REWIND;
		bool burst = stateLoadSeen && fixedAudioClockRuns - stateLoadLastRun < STATE_LOAD_BURST_RUNS;
		bool suspended = stateLoadStage == STATE_LOAD_REARM_PENDING || stateLoadStage == STATE_LOAD_RATE_SETTLING || stateLoadStage == STATE_LOAD_REWINDING || stateLoadStage == STATE_LOAD_REWIND_END_PENDING;
		mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO] STATE_LOAD_DETECTED run=%llu", (unsigned long long) fixedAudioClockRuns);
		if (!suspended) {
			_stateLoadSuspend("LOAD_RATE_SETTLING");
			stateLoadBeginRun = fixedAudioClockRuns;
			stateLoadWasRewind = false;
			mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO] STATE_LOAD_CANDIDATE_DROPPED ring=0 state=%s sticky=%d", rearm ? "LOAD_RATE_SETTLING" : "HARD", (int) fixedAudioOutput.hardFallback);
		}
		stateLoadSeen = true;
		stateLoadLastRun = fixedAudioClockRuns;
		stateLoadStableRuns = 0;
		stateLoadRetryWait = 0;
		if (rearm) {
			if (rewind || burst || stateLoadWasRewind) {
				if (!stateLoadWasRewind) mLOG(GBA_MP2K_EVENTS, INFO, "[FIXED AUDIO] REWIND_DETECTED method=%s state=REWINDING", rewind ? "frontend" : "load-burst");
				stateLoadWasRewind = true;
				stateLoadStage = STATE_LOAD_REWINDING;
				fixedAudioOutput.fallbackReason = "REWINDING";
			} else {
				/* A confirmed steady non-rewind frontend can retain the single-load
     * reconstruction boundary, including very short SE. Unknown/changing
     * rates wait for several healthy snapshots before any bridge work. */
				bool steady = _stateLoadRateSupported() && (!fixedAudioOutput.lastSupportedRate ||
					(mp2kCurrentFrontend.runRate > fixedAudioOutput.lastSupportedRate * .98 &&
						mp2kCurrentFrontend.runRate < fixedAudioOutput.lastSupportedRate * 1.02));
				stateLoadStage = steady ? STATE_LOAD_REARM_PENDING : STATE_LOAD_RATE_SETTLING;
			}
		} else stateLoadStage = STATE_LOAD_NONE;
	}

	#endif
	struct VFile* vfm = VFileFromConstMemory(data, size);
	bool success = mCoreLoadStateNamed(core, vfm, SAVESTATE_RTC);
	vfm->close(vfm);
	#ifdef M_CORE_GBA
	if (fixedAudioBackend == FIXED_AUDIO_BACKEND_B6JJ) {
		b6jjRecoveryPending = success;
		/* Rebuild at the next supported forward run. A rewind burst performs
		 * no bridge/backend work between unserializes; only its final state
		 * reaches _recoverB6JJAudio. Failed loads stay safely native. */
		mLOG(GBA_MP2K_EVENTS, INFO, "[B6JJ RECOVERY] LOAD generation=%llu success=%d rewind=%d pending=%d aux=0 ring=0 queue=0",
			(unsigned long long) b6jjGeneration, (int) success, (int) b6jjRewinding, (int) b6jjRecoveryPending);
	}
	if (rearm) {
		if (!success) _stateLoadRebindFailed("STATE_LOAD_CORRUPT_STATE", true);
		else {
			/* This host-side native output queue is not part of the serialized
			 * APU. Clear it too, without altering feature-OFF native behavior. */
			struct GBA* gba = core->board;
			mAudioBufferClear(&gba->audio.psg.buffer);
			if (gba->audio.stemTrace) GBAAudioStemTraceClear(gba->audio.stemTrace);
			mAudioClockInit(&fixedAudioClock, fixedAudioClock.nominalRunRate, fixedAudioClock.outputSampleRate);
			GBAMP2kEventsSetAudioClock(core, &fixedAudioClock);
			/* An r0 mismatch belongs to the discarded timeline. Validate the
			 * loaded RAM before creating its one fresh registry generation. */
			struct GBAMP2kPlayerRegistry* registry = GBAMP2kEventsPlayers(core);
			if (registry) registry->mismatch = false;
			if (stateLoadStage == STATE_LOAD_RATE_SETTLING && !stateLoadWasRewind) _stateLoadRebindRun(true);
		}
	}
	#endif
#ifdef M_CORE_GBA
	if (stateLoadProfile.enabled) {
		++stateLoadProfile.loads; stateLoadProfile.loadUs += _stateLoadTimeUs() - loadStart;
		if (stateLoadProfile.loads == 1 || !(stateLoadProfile.loads % 60)) _stateLoadProfileLog();
	}
#endif
	return success;
}

void retro_cheat_reset(void) {
	mCheatDeviceClear(core->cheatDevice(core));
}

void retro_cheat_set(unsigned index, bool enabled, const char* code) {
	UNUSED(index);
	UNUSED(enabled);
	struct mCheatDevice* device = core->cheatDevice(core);
	struct mCheatSet* cheatSet = NULL;
	if (mCheatSetsSize(&device->cheats)) {
		cheatSet = *mCheatSetsGetPointer(&device->cheats, 0);
	} else {
		cheatSet = device->createSet(device, NULL);
		mCheatAddSet(device, cheatSet);
	}
// Convert the super wonky unportable libretro format to something normal
#ifdef M_CORE_GBA
	if (core->platform(core) == mPLATFORM_GBA) {
		char realCode[] = "XXXXXXXX XXXXXXXX";
		size_t len = strlen(code) + 1; // Include null terminator
		size_t i, pos;
		for (i = 0, pos = 0; i < len; ++i) {
			if (isspace((int) code[i]) || code[i] == '+') {
				realCode[pos] = ' ';
			} else {
				realCode[pos] = code[i];
			}
			if ((pos == 13 && (realCode[pos] == ' ' || !realCode[pos])) || pos == 17) {
				realCode[pos] = '\0';
				mCheatAddLine(cheatSet, realCode, 0);
				pos = 0;
				continue;
			}
			++pos;
		}
	}
#endif
#ifdef M_CORE_GB
	if (core->platform(core) == mPLATFORM_GB) {
		char realCode[] = "XXX-XXX-XXX";
		size_t len = strlen(code) + 1; // Include null terminator
		size_t i, pos;
		for (i = 0, pos = 0; i < len; ++i) {
			if (isspace((int) code[i]) || code[i] == '+') {
				realCode[pos] = '\0';
			} else {
				realCode[pos] = code[i];
			}
			if (pos == 11 || !realCode[pos]) {
				realCode[pos] = '\0';
				mCheatAddLine(cheatSet, realCode, 0);
				pos = 0;
				continue;
			}
			++pos;
		}
	}
#endif
	if (cheatSet->refresh) {
		cheatSet->refresh(cheatSet, device);
	}
}

unsigned retro_get_region(void) {
	return RETRO_REGION_NTSC; // TODO: This isn't strictly true
}

void retro_set_controller_port_device(unsigned port, unsigned device) {
	UNUSED(port);
	UNUSED(device);
}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info* info, size_t num_info) {
	UNUSED(game_type);
	UNUSED(info);
	UNUSED(num_info);
	return false;
}

void* retro_get_memory_data(unsigned id) {
	switch (id) {
	case RETRO_MEMORY_SAVE_RAM:
		return savedata;
	case RETRO_MEMORY_RTC:
		switch (core->platform(core)) {
#ifdef M_CORE_GB
		case mPLATFORM_GB:
			switch (((struct GB*) core->board)->memory.mbcType) {
			case GB_MBC3_RTC:
				return &((uint8_t*) savedata)[((struct GB*) core->board)->sramSize];
			default:
				break;
			}
#endif
		default:
			break;
		}
		break;
	default:
		break;
	}
	return NULL;
}

size_t retro_get_memory_size(unsigned id) {
	switch (id) {
	case RETRO_MEMORY_SAVE_RAM:
		switch (core->platform(core)) {
#ifdef M_CORE_GBA
		case mPLATFORM_GBA:
			switch (((struct GBA*) core->board)->memory.savedata.type) {
			case GBA_SAVEDATA_AUTODETECT:
				return GBA_SIZE_FLASH1M;
			default:
				return GBASavedataSize(&((struct GBA*) core->board)->memory.savedata);
			}
#endif
#ifdef M_CORE_GB
		case mPLATFORM_GB:
			return ((struct GB*) core->board)->sramSize;
#endif
		default:
			break;
		}
		break;
	case RETRO_MEMORY_RTC:
		switch (core->platform(core)) {
#ifdef M_CORE_GB
		case mPLATFORM_GB:
			switch (((struct GB*) core->board)->memory.mbcType) {
			case GB_MBC3_RTC:
				return sizeof(struct GBMBCRTCSaveBuffer);
			default:
				return 0;
			}
#endif
		default:
			break;
		}
		break;
	default:
		break;
	}
	return 0;
}

void GBARetroLog(struct mLogger* logger, int category, enum mLogLevel level, const char* format, va_list args) {
	UNUSED(logger);
	if (!logCallback) {
		return;
	}

	char message[2048];
	/* Preserve established trace formatting, but never truncate a rejection. */
	size_t limit = strstr(format, "[FIXED AUDIO TRACK DIAG]") || strstr(format, "STATE_LOAD_REBIND_FAILED") ? sizeof(message) : 256;
	vsnprintf(message, limit, format, args);

	enum retro_log_level retroLevel = RETRO_LOG_INFO;
	switch (level) {
	case mLOG_ERROR:
	case mLOG_FATAL:
		retroLevel = RETRO_LOG_ERROR;
		break;
	case mLOG_WARN:
		retroLevel = RETRO_LOG_WARN;
		break;
	case mLOG_INFO:
		retroLevel = RETRO_LOG_INFO;
		break;
	case mLOG_GAME_ERROR:
	case mLOG_STUB:
#ifdef NDEBUG
		return;
#else
		retroLevel = RETRO_LOG_DEBUG;
		break;
#endif
	case mLOG_DEBUG:
		retroLevel = RETRO_LOG_DEBUG;
		break;
	}
#ifdef NDEBUG
	static int biosCat = -1;
	if (biosCat < 0) {
		biosCat = mLogCategoryById("gba.bios");
	}

	if (category == biosCat) {
		return;
	}
#endif
	logCallback(retroLevel, "%s: %s\n", mLogCategoryName(category), message);
}

/* Used only for GB/GBC content */
static void _postAudioBuffer(struct mAVStream* stream, struct mAudioBuffer* buffer) {
	UNUSED(stream);
	int produced = mAudioBufferRead(buffer, audioSampleBuffer, GB_SAMPLES);
	if (produced > 0) {
		if (audioLowPassEnabled) {
			_audioLowPassFilter(audioSampleBuffer, produced);
		}
		audioCallback(audioSampleBuffer, (size_t)produced);
	}
}

static void _audioRateChanged(struct mAVStream* stream, unsigned rate) {
	UNUSED(stream);
	UNUSED(rate);
#ifdef M_CORE_GBA
	const char* trace = getenv("MGBA_MP2K_RUNTIME_TIMING_TRACE");
	const struct GBAMP2kProfile* profile = GBAMP2kEventsProfile(core);
	if (trace && !strcmp(trace, "1") && profile && !profile->known) {
		uint64_t cycle = 0, sample = 0;
		GBAMP2kEventsTracePosition(core, &cycle, &sample);
		mLOG(GBA_MP2K_EVENTS, INFO,
			"[MP2K RATE PROBE] sampleRate=%u cycle=%llu pcmSample=%llu submitted=%llu soundBias=%04x",
			rate, (unsigned long long) cycle, (unsigned long long) sample,
			(unsigned long long) mp2kPcmTrace.submittedFrames,
			((struct GBA*) core->board)->audio.soundbias);
	}
#endif
	struct retro_system_av_info info;
	retro_get_system_av_info(&info);
#ifdef M_CORE_GBA
	/* Re-announcing an unchanged rate makes RetroArch rebuild its audio/video
	 * drivers. Only Fixed Audio's effective route rate is relevant here. */
	if (fixedAudioOutput.requested && fixedAudioAdvertisedRate == info.timing.sample_rate) return;
	uint64_t avStart = stateLoadProfile.enabled ? _stateLoadTimeUs() : 0;
	fixedAudioAdvertisedRate = info.timing.sample_rate;
#endif
	environCallback(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &info);
#ifdef M_CORE_GBA
	if (stateLoadProfile.enabled) { ++stateLoadProfile.avRefreshes; stateLoadProfile.avUs += _stateLoadTimeUs() - avStart; }
#endif
}

static void _setRumble(struct mRumbleIntegrator* rumble, float level) {
	UNUSED(rumble);
	if (!rumbleInitDone) {
		_initRumble();
	}
	if (!rumbleCallback) {
		return;
	}

	rumbleCallback(0, RETRO_RUMBLE_STRONG, level * 0xFFFF);
	rumbleCallback(0, RETRO_RUMBLE_WEAK, level * 0xFFFF);
}

static void _updateLux(struct GBALuminanceSource* lux) {
	UNUSED(lux);
	struct retro_variable var = {
		.key = "mgba_solar_sensor_level",
		.value = 0
	};
	bool luxVarUpdated = envVarsUpdated;

	if (luxVarUpdated && (!environCallback(RETRO_ENVIRONMENT_GET_VARIABLE, &var) || !var.value)) {
		luxVarUpdated = false;
	}

	if (luxVarUpdated) {
		luxSensorUsed = strcmp(var.value, "sensor") == 0;
	}

	if (luxSensorUsed) {
		_initSensors();
		float fLux = luxSensorEnabled ? sensorGetCallback(0, RETRO_SENSOR_ILLUMINANCE) : 0.0f;
		luxLevel = cbrtf(fLux) * 8;
	} else {
		if (luxVarUpdated) {
			char* end;
			int newLuxLevelIndex = strtol(var.value, &end, 10);

			if (!*end) {
				if (newLuxLevelIndex > 10) {
					luxLevelIndex = 10;
				} else if (newLuxLevelIndex < 0) {
					luxLevelIndex = 0;
				} else {
					luxLevelIndex = newLuxLevelIndex;
				}
			}
		}

		luxLevel = 0x16;
		if (luxLevelIndex > 0) {
			luxLevel += GBA_LUX_LEVELS[luxLevelIndex - 1];
		}
	}

	envVarsUpdated = false;
}

static uint8_t _readLux(struct GBALuminanceSource* lux) {
	UNUSED(lux);
	return 0xFF - luxLevel;
}

static void _updateCamera(const uint32_t* buffer, unsigned width, unsigned height, size_t pitch) {
	if (!camData || width > camWidth || height > camHeight) {
		if (camData) {
			free(camData);
			camData = NULL;
		}
		unsigned bufPitch = pitch / sizeof(*buffer);
		unsigned bufHeight = height;
		if (imcapWidth > bufPitch) {
			bufPitch = imcapWidth;
		}
		if (imcapHeight > bufHeight) {
			bufHeight = imcapHeight;
		}
		camData = malloc(sizeof(*buffer) * bufHeight * bufPitch);
		memset(camData, 0xFF, sizeof(*buffer) * bufHeight * bufPitch);
		camWidth = width;
		camHeight = bufHeight;
		camStride = bufPitch;
	}
	size_t i;
	for (i = 0; i < height; ++i) {
		memcpy(&camData[camStride * i], &buffer[pitch * i / sizeof(*buffer)], pitch);
	}
}

static void _startImage(struct mImageSource* image, unsigned w, unsigned h, int colorFormats) {
	UNUSED(image);
	UNUSED(colorFormats);

	if (camData) {
		free(camData);
	}
	camData = NULL;
	imcapWidth = w;
	imcapHeight = h;
	cam.start();
}

static void _stopImage(struct mImageSource* image) {
	UNUSED(image);
	cam.stop();
}

static void _requestImage(struct mImageSource* image, const void** buffer, size_t* stride, enum mColorFormat* colorFormat) {
	UNUSED(image);
	if (!camData) {
		cam.start();
		*buffer = NULL;
		return;
	}
	size_t offset = 0;
	if (imcapWidth < camWidth) {
		offset += (camWidth - imcapWidth) / 2;
	}
	if (imcapHeight < camHeight) {
		offset += (camHeight - imcapHeight) / 2 * camStride;
	}

	*buffer = &camData[offset];
	*stride = camStride;
	*colorFormat = mCOLOR_XRGB8;
}

static void _updateRotation(struct mRotationSource* source) {
	UNUSED(source);
	tiltX = 0;
	tiltY = 0;
	gyroZ = 0;
	_initSensors();
	if (tiltEnabled) {
		tiltX = sensorGetCallback(0, RETRO_SENSOR_ACCELEROMETER_X) * 3e8f;
		tiltY = sensorGetCallback(0, RETRO_SENSOR_ACCELEROMETER_Y) * -3e8f;
	}
	if (gyroEnabled) {
		gyroZ = sensorGetCallback(0, RETRO_SENSOR_GYROSCOPE_Z) * -5.5e8f;
	}
}

static int32_t _readTiltX(struct mRotationSource* source) {
	UNUSED(source);
	return tiltX;
}

static int32_t _readTiltY(struct mRotationSource* source) {
	UNUSED(source);
	return tiltY;
}

static int32_t _readGyroZ(struct mRotationSource* source) {
	UNUSED(source);
	return gyroZ;
}
