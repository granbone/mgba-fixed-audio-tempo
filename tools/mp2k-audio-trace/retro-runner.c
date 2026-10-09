/* Deterministic libretro host for AAMJ MP2K/PCM diagnostics.
 * Build: gcc -std=c11 -O2 -Wall -Wextra -Isrc/platform/libretro
 *        tools/mp2k-audio-trace/retro-runner.c -o retro-runner.exe
 */
#include "libretro.h"

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>

struct Pulse {
	unsigned start;
	unsigned end;
	unsigned button;
};

static struct Pulse pulses[4096];
static unsigned pulseCount;
static unsigned frameNumber;
static bool fastForward;
static bool rewinding;
static float runRate;
static FILE* logFile;
static FILE* callbackFile;
static bool callbackWriteFailed;
static const char* outputDirectory;
static const char* systemDirectory;
static const char* shotPrefix;
static unsigned shotFrames[128];
static unsigned shotCount;
static const uint8_t* testIwram;
static const uint8_t* testWram;
static struct { unsigned frame, slot; char op[16]; } stateActions[4096];
static unsigned stateActionCount;
static void* stateSlots[16];
static size_t stateSizes[16];
static void* endpointStates[128];
static size_t endpointSize;
static unsigned endpointCount, endpointStart, endpointTarget;
static bool endpointRecovered, endpointHard;
static void** b6jjHistory;
static unsigned b6jjHistoryCount;
static size_t b6jjHistorySize;
static FILE* playerTraceFile;
static unsigned playerTraceCount, playerTraceGeneration;
static uint32_t playerTraceConfirmedMask;
static bool playerTraceCandidateActive;
static uint32_t playerTraceAddresses[32], playerTraceTracks[32];
static const char* playerTraceStates[32];

static uint32_t traceWord(const uint8_t* memory) {
	uint32_t value; memcpy(&value, memory, 4); return value;
}
static const uint8_t* traceRam(uint32_t address, size_t bytes) {
	if (testWram && address >= 0x02000000 && address < 0x02040000 && bytes <= 0x02040000-address)
		return testWram+address-0x02000000;
	if (testIwram && address >= 0x03000000 && address < 0x03008000 && bytes <= 0x03008000-address)
		return testIwram+address-0x03000000;
	return NULL;
}
static void tracePlayers(void) {
	if (!playerTraceFile) return;
	unsigned initialized = 0;
	for (unsigned i=0; i<playerTraceCount; ++i) {
		const uint8_t* p=traceRam(playerTraceAddresses[i],64);
		initialized += p && traceWord(p+0x34)==0x68736d53;
	}
	unsigned confirmed = 0;
	for (unsigned i=0;i<32;++i) confirmed += (playerTraceConfirmedMask >> i)&1u;
	fprintf(playerTraceFile,"{\"frame\":%u,\"generation\":%u,\"candidateActive\":%s,\"initializedCount\":%u,\"confirmedStartCount\":%u,\"slots\":[",
		frameNumber,playerTraceGeneration,playerTraceCandidateActive?"true":"false",initialized,confirmed);
	for (unsigned i=0; i<playerTraceCount; ++i) {
		const uint8_t* p=traceRam(playerTraceAddresses[i],64);
		uint32_t tracks=p?traceWord(p+0x2c):0;
		const uint8_t* t=traceRam(tracks,80);
		fprintf(playerTraceFile,"%s{\"slot\":%u,\"address\":\"%08x\",\"expectedTracks\":\"%08x\",\"tracks\":\"%08x\",\"magic\":\"%08x\",\"header\":\"%08x\",\"status\":\"%08x\",\"playing\":%s,\"clock\":%u,\"track0Flags\":%u,\"track0Cursor\":\"%08x\",\"state\":\"%s\"}",
			i?",":"",i,playerTraceAddresses[i],playerTraceTracks[i],tracks,p?traceWord(p+0x34):0,
			p?traceWord(p):0,p?traceWord(p+4):0,p && traceWord(p) && !(traceWord(p+4)&0x80000000U)?"true":"false",p?traceWord(p+12):0,t?t[0]:0,t?traceWord(t+0x40):0,
			playerTraceStates[i]?playerTraceStates[i]:"WAIT_INIT");
	}
	fputs("]}\n",playerTraceFile);
}


static void freeStateSlots(void) {
	for (unsigned i = 0; i < b6jjHistoryCount; ++i) free(b6jjHistory[i]);
	free(b6jjHistory); b6jjHistory = NULL; b6jjHistoryCount = 0;
	for (unsigned i = 0; i < 16; ++i) { free(stateSlots[i]); stateSlots[i] = NULL; }
	for (unsigned i = 0; i < endpointCount; ++i) { free(endpointStates[i]); endpointStates[i] = NULL; }
	endpointCount = 0;
}

static void RETRO_CALLCONV traceLog(enum retro_log_level level, const char* format, ...) {
	(void) level;
	char message[2048];
	va_list args;
	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	if (playerTraceFile) {
		const char* field=strstr(message,"generation=");
		if (field && strstr(message,"[RAM_PLAYER_GENERATION_CHANGE]")) {
			playerTraceGeneration=strtoul(field+11,NULL,10);
			playerTraceConfirmedMask=0;
			for (unsigned i=0;i<32;++i) playerTraceStates[i]="WAIT_INIT";
		}
		field=strstr(message,"candidate_active=");
		if (field) playerTraceCandidateActive=strtoul(field+17,NULL,10)!=0;
		field=strstr(message,"player=");
		if (field) {
			unsigned id=strtoul(field+7,NULL,10);
			if (id<32 && strstr(message,"[RAM_PLAYER_MATCH]")) playerTraceConfirmedMask |= 1u << id;
			static const char* states[]={"WAIT_INIT","DISCOVERED","START","ARMED","ACTIVE","REJECTED"};
			for (unsigned i=0;id<32 && i<6;++i) {
				char marker[64];snprintf(marker,sizeof(marker),"[RAM_PLAYER_%s]",states[i]);
				if (strstr(message,marker)) playerTraceStates[id]=states[i];
			}
		}
	}
	if (strstr(message, "REWIND_RECOVERED")) endpointRecovered = true;
	if (strstr(message, "STATE_LOAD_REBIND_FAILED") && strstr(message, "sticky=1")) endpointHard = true;
	va_end(args);
	if (strstr(message, "MP2K") || strstr(message, "ERROR") || strstr(message, "FIXED AUDIO") || strstr(message, "AUDIO CLOCK")) {
		fputs(message, logFile);
	}
}

static bool RETRO_CALLCONV environment(unsigned command, void* value) {
	switch (command) {
	case RETRO_ENVIRONMENT_SET_MEMORY_MAPS: {
		const struct retro_memory_map* map = value;
		for (unsigned i = 0; i < map->num_descriptors; ++i) {
			const struct retro_memory_descriptor* d = &map->descriptors[i];
			if (d->start == 0x03000000 && d->len >= 0x8000) testIwram = (const uint8_t*) d->ptr + d->offset;
			if (d->start == 0x02000000 && d->len >= 0x40000) testWram = (const uint8_t*) d->ptr + d->offset;
		}
		return true;
	}
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		((struct retro_log_callback*) value)->log = traceLog;
		return true;
	case RETRO_ENVIRONMENT_GET_THROTTLE_STATE: {
		if (getenv("MGBA_RUNNER_NO_THROTTLE_STATE")) return false;
		struct retro_throttle_state* state = value;
		state->mode = rewinding && !getenv("MGBA_RUNNER_NO_REWIND_MODE") ? RETRO_THROTTLE_REWINDING : fastForward ? RETRO_THROTTLE_FAST_FORWARD : RETRO_THROTTLE_NONE;
		state->rate = rewinding && !getenv("MGBA_RUNNER_NO_REWIND_MODE") ? 0 : runRate;
		const char* fault = getenv("MGBA_RUNNER_RATE_FAULT_AT");
		const char* duration = getenv("MGBA_RUNNER_FAULT_DURATION");
		unsigned count = duration ? strtoul(duration, NULL, 10) : 1;
		if (fault && frameNumber >= strtoul(fault, NULL, 10) &&
		    frameNumber - strtoul(fault, NULL, 10) < count) {
			const char* kind = getenv("MGBA_RUNNER_RATE_FAULT_KIND");
			if (kind && !strcmp(kind, "unknown")) return false;
			state->rate = kind && !strcmp(kind, "zero") ? 0 : 37;
		}
		return true;
	}
	case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
		*(bool*) value = fastForward;
		return true;
	case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
	case RETRO_ENVIRONMENT_GET_CAN_DUPE:
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
	case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
		return true;
	case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
		fprintf(logFile, "[HOST] av_rate=%.0f frame=%u\n", ((struct retro_system_av_info*) value)->timing.sample_rate, frameNumber);
		return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
		*(bool*) value = false;
		return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE: {
		struct retro_variable* variable = value;
		if (!strcmp(variable->key, "mgba_fixed_audio_tempo_mode"))
			variable->value = getenv("MGBA_RUNNER_FIXED_AUDIO_MODE");
		else if (!strcmp(variable->key, "mgba_fixed_audio_tempo"))
			variable->value = getenv("MGBA_RUNNER_LEGACY_FIXED_AUDIO");
		else return false;
		return variable->value != NULL;
	}
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
		*(const char**) value = systemDirectory;
		return true;
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		*(const char**) value = outputDirectory;
		return true;
	case RETRO_ENVIRONMENT_GET_LANGUAGE:
		*(unsigned*) value = RETRO_LANGUAGE_ENGLISH;
		return true;
	default:
		return false;
	}
}

static void RETRO_CALLCONV video(const void* data, unsigned width, unsigned height, size_t pitch) {
	if (!data) {
		return;
	}
	for (unsigned i = 0; i < shotCount; ++i) {
		if (frameNumber != shotFrames[i]) {
			continue;
		}
		char path[1100];
		snprintf(path, sizeof(path), "%s-frame-%u.ppm", shotPrefix, frameNumber);
		FILE* file = fopen(path, "wb");
		if (!file) {
			return;
		}
		fprintf(file, "P6\n%u %u\n255\n", width, height);
		for (unsigned y = 0; y < height; ++y) {
			const uint16_t* row = (const uint16_t*) ((const uint8_t*) data + y * pitch);
			for (unsigned x = 0; x < width; ++x) {
				uint16_t pixel = row[x];
				uint8_t rgb[3] = {
					(uint8_t) (((pixel >> 11) & 31) * 255 / 31),
					(uint8_t) (((pixel >> 5) & 63) * 255 / 63),
					(uint8_t) ((pixel & 31) * 255 / 31)
				};
				fwrite(rgb, 1, 3, file);
			}
		}
		fclose(file);
	}
}

static void RETRO_CALLCONV audioSample(int16_t left, int16_t right) {
	if (callbackFile) {
		const int16_t samples[2] = {left, right};
		if (fwrite(samples, sizeof(samples), 1, callbackFile) != 1) callbackWriteFailed = true;
	}
}

static size_t RETRO_CALLCONV audioBatch(const int16_t* data, size_t frames) {
	if (callbackFile && getenv("MGBA_RUNNER_CALLBACK_INDEX"))
		fprintf(logFile,"[HOST CALLBACK] frame=%u byteOffset=%llu samples=%zu\n",frameNumber,(unsigned long long)_ftelli64(callbackFile),frames);
	if (callbackFile && fwrite(data, sizeof(int16_t) * 2, frames, callbackFile) != frames)
		callbackWriteFailed = true;
	const char* fault = getenv("MGBA_RUNNER_PARTIAL_BATCH_AT");
	const char* duration = getenv("MGBA_RUNNER_FAULT_DURATION");
	unsigned count = duration ? strtoul(duration, NULL, 10) : 1;
	if (fault && frameNumber >= strtoul(fault, NULL, 10) &&
	    frameNumber - strtoul(fault, NULL, 10) < count) return frames / 2;
	return frames;
}

static void RETRO_CALLCONV inputPoll(void) {
}

static int16_t RETRO_CALLCONV inputState(unsigned port, unsigned device, unsigned index, unsigned id) {
	(void) index;
	if (port || device != RETRO_DEVICE_JOYPAD) {
		return 0;
	}
	uint16_t mask = 0;
	for (unsigned i = 0; i < pulseCount; ++i) {
		if (frameNumber >= pulses[i].start && frameNumber < pulses[i].end) {
			mask |= 1U << pulses[i].button;
		}
	}
	return id == RETRO_DEVICE_ID_JOYPAD_MASK ? (int16_t) mask : (int16_t) !!(mask & (1U << id));
}

static int buttonId(const char* name) {
	static const struct { const char* name; int id; } buttons[] = {
		{ "A", RETRO_DEVICE_ID_JOYPAD_A }, { "B", RETRO_DEVICE_ID_JOYPAD_B },
		{ "START", RETRO_DEVICE_ID_JOYPAD_START }, { "SELECT", RETRO_DEVICE_ID_JOYPAD_SELECT },
		{ "UP", RETRO_DEVICE_ID_JOYPAD_UP }, { "DOWN", RETRO_DEVICE_ID_JOYPAD_DOWN },
		{ "LEFT", RETRO_DEVICE_ID_JOYPAD_LEFT }, { "RIGHT", RETRO_DEVICE_ID_JOYPAD_RIGHT },
		{ "L", RETRO_DEVICE_ID_JOYPAD_L }, { "R", RETRO_DEVICE_ID_JOYPAD_R }
	};
	for (unsigned i = 0; i < sizeof(buttons) / sizeof(*buttons); ++i) {
		if (!strcmp(name, buttons[i].name)) {
			return buttons[i].id;
		}
	}
	return -1;
}

static bool readSchedule(const char* path) {
	FILE* file = fopen(path, "r");
	if (!file) {
		perror(path);
		return false;
	}
	char line[128];
	unsigned lineNumber = 0;
	while (fgets(line, sizeof(line), file)) {
		++lineNumber;
		if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
			continue;
		}
		unsigned start, duration;
		char name[24];
		if (sscanf(line, "%u %23s %u", &start, name, &duration) != 3 || !duration ||
		    pulseCount == sizeof(pulses) / sizeof(*pulses)) {
			fprintf(stderr, "bad schedule line %u\n", lineNumber);
			fclose(file);
			return false;
		}
		int button = buttonId(name);
		if (button < 0) {
			fprintf(stderr, "unknown button on line %u: %s\n", lineNumber, name);
			fclose(file);
			return false;
		}
		pulses[pulseCount++] = (struct Pulse) { start, start + duration, (unsigned) button };
	}
	fclose(file);
	return true;
}

#define LOAD(symbol) do { \
	p_##symbol = (void*) GetProcAddress(library, #symbol); \
	if (!p_##symbol) { fprintf(stderr, "missing %s\n", #symbol); goto cleanup; } \
} while (0)

int main(int argc, char** argv) {
	if (argc != 7 && argc != 8) {
		fprintf(stderr, "usage: retro-runner CORE.dll ROM.gba OUTPUT_PREFIX normal|ff|unlimited|switch FRAMES SCHEDULE.txt [SCREENSHOT_FRAMES]\n");
		return 2;
	}
	if (!readSchedule(argv[6])) {
		return 2;
	}
	fastForward = !strcmp(argv[4], "ff") || !strcmp(argv[4], "unlimited");
	if (!fastForward && strcmp(argv[4], "normal") && strcmp(argv[4], "switch")) {
		fprintf(stderr, "mode must be normal, ff, unlimited or switch\n");
		return 2;
	}
	runRate = !strcmp(argv[4], "unlimited") ? 0.f : fastForward ? 119.455f : 59.728f;
	unsigned frames = (unsigned) strtoul(argv[5], NULL, 10);
	const char* rewindAtEnv = getenv("MGBA_RUNNER_B6JJ_REWIND_AT");
	unsigned b6jjRewindAt = rewindAtEnv ? strtoul(rewindAtEnv, NULL, 10) : 0;
	unsigned b6jjRewindLength = getenv("MGBA_RUNNER_B6JJ_REWIND_LENGTH") ? strtoul(getenv("MGBA_RUNNER_B6JJ_REWIND_LENGTH"), NULL, 10) : 600;
	unsigned b6jjRewindCycles = getenv("MGBA_RUNNER_B6JJ_REWIND_CYCLES") ? strtoul(getenv("MGBA_RUNNER_B6JJ_REWIND_CYCLES"), NULL, 10) : 1;
	if (b6jjRewindAt) {
		if (!b6jjRewindLength || b6jjRewindLength > 3600 || b6jjRewindAt < b6jjRewindLength || !b6jjRewindCycles || b6jjRewindCycles > 10) return 2;
		b6jjHistory = calloc(b6jjRewindLength, sizeof(*b6jjHistory)); if (!b6jjHistory) return 2;
	}
	if (!frames) {
		return 2;
	}
	unsigned switchStart = frames / 3;
	unsigned switchEnd = 2 * frames / 3;
	const char* switchStartEnv = getenv("MGBA_RUNNER_SWITCH_START");
	const char* switchEndEnv = getenv("MGBA_RUNNER_SWITCH_END");
	if (switchStartEnv && switchEndEnv) {
		switchStart = (unsigned) strtoul(switchStartEnv, NULL, 10);
		switchEnd = (unsigned) strtoul(switchEndEnv, NULL, 10);
		if (switchStart >= switchEnd || switchEnd >= frames) {
			fprintf(stderr, "invalid switch run boundaries\n");
			return 2;
		}
	}
	shotPrefix = argv[3];
	const char* stateActionsPath = getenv("MGBA_RUNNER_STATE_ACTIONS");
	if (stateActionsPath) {
		FILE* actions = fopen(stateActionsPath, "r");
		if (!actions) { perror(stateActionsPath); return 2; }
		while (stateActionCount < sizeof(stateActions) / sizeof(stateActions[0])) {
			unsigned at, slot; char op[16];
			int read = fscanf(actions, "%u %15s %u", &at, op, &slot);
			if (read == EOF) break;
			if (read != 3 || slot >= 16 || (strcmp(op, "SAVE") && strcmp(op, "LOAD") && strcmp(op, "LOAD_BAD") && strcmp(op, "SPEED") && strcmp(op, "REWIND") && strcmp(op, "RESET"))) {
				fclose(actions); fprintf(stderr, "invalid state action\n"); return 2;
			}
			stateActions[stateActionCount].frame = at;
			stateActions[stateActionCount].slot = slot;
			strcpy(stateActions[stateActionCount++].op, op);
		}
		fclose(actions);
	}
	if (argc == 8) {
		const char* cursor = argv[7];
		while (*cursor && shotCount < sizeof(shotFrames) / sizeof(*shotFrames)) {
			char* end;
			shotFrames[shotCount++] = (unsigned) strtoul(cursor, &end, 10);
			if (end == cursor || (*end && *end != ',')) {
				fprintf(stderr, "invalid screenshot frame list\n");
				return 2;
			}
			cursor = *end ? end + 1 : end;
		}
	}
	char logPath[1024], pcmPath[1024], directory[1024];
	snprintf(logPath, sizeof(logPath), "%s.log", argv[3]);
	snprintf(pcmPath, sizeof(pcmPath), "%s.s16le", argv[3]);
	if (GetFileAttributesA(logPath) != INVALID_FILE_ATTRIBUTES ||
	    GetFileAttributesA(pcmPath) != INVALID_FILE_ATTRIBUTES) {
		fprintf(stderr, "output exists; refusing to overwrite\n");
		return 2;
	}
	logFile = fopen(logPath, "w");
	if (!logFile) {
		perror(logPath);
		return 2;
	}
	const char* playerTable = getenv("MGBA_RUNNER_PLAYER_TABLE_OFFSET");
	const char* playerCount = getenv("MGBA_RUNNER_PLAYER_COUNT");
	if (playerTable && playerCount) {
		unsigned long table=strtoul(playerTable,NULL,0);
		playerTraceCount=strtoul(playerCount,NULL,10);
		FILE* rom=fopen(argv[2],"rb");
		bool valid=rom && playerTraceCount && playerTraceCount<=32;
		for (unsigned i=0;valid && i<playerTraceCount;++i) {
			uint8_t entry[12];
			valid=!fseek(rom,table+i*12,SEEK_SET) && fread(entry,12,1,rom)==1;
			if (valid) { playerTraceAddresses[i]=traceWord(entry);playerTraceTracks[i]=traceWord(entry+4); }
		}
		if (rom) fclose(rom);
		char path[1200];snprintf(path,sizeof(path),"%s-players.jsonl",argv[3]);
		if (valid && GetFileAttributesA(path)==INVALID_FILE_ATTRIBUTES) playerTraceFile=fopen(path,"w");
		if (!playerTraceFile) { fclose(logFile);return 2; }
	}
	const char* callbackPath = getenv("MGBA_RUNNER_CALLBACK_PATH");
	if (callbackPath && *callbackPath) {
		if (GetFileAttributesA(callbackPath) != INVALID_FILE_ATTRIBUTES) {
			fprintf(stderr, "callback output exists; refusing to overwrite\n");
			fclose(logFile);
			return 2;
		}
		callbackFile = fopen(callbackPath, "wb");
		if (!callbackFile) { perror(callbackPath); fclose(logFile); return 2; }
	}
	strncpy(directory, argv[3], sizeof(directory) - 1);
	directory[sizeof(directory) - 1] = 0;
	char* separator = strrchr(directory, '\\');
	char* slash = strrchr(directory, '/');
	if (!separator || (slash && slash > separator)) {
		separator = slash;
	}
	if (separator) {
		*separator = 0;
	} else {
		strcpy(directory, ".");
	}
	outputDirectory = directory;
	systemDirectory = getenv("LIBRETRO_SYSTEM_DIRECTORY");
	if (!systemDirectory) {
		systemDirectory = ".";
	}
	_putenv_s("MGBA_MP2K_AUDIO_TRACE", "1");
	_putenv_s("MGBA_MP2K_AUDIO_TRACE_PATH", pcmPath);
	HMODULE library = LoadLibraryA(argv[1]);
	if (!library) {
		fprintf(stderr, "LoadLibrary failed: %lu\n", GetLastError());
		if (callbackFile) fclose(callbackFile);
		fclose(logFile);
		return 1;
	}
	void (*p_retro_set_environment)(retro_environment_t);
	void (*p_retro_set_video_refresh)(retro_video_refresh_t);
	void (*p_retro_set_audio_sample)(retro_audio_sample_t);
	void (*p_retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
	void (*p_retro_set_input_poll)(retro_input_poll_t);
	void (*p_retro_set_input_state)(retro_input_state_t);
	void (*p_retro_init)(void);
	void (*p_retro_deinit)(void);
	bool (*p_retro_load_game)(const struct retro_game_info*);
	void (*p_retro_unload_game)(void);
	void (*p_retro_run)(void);
	void (*p_retro_reset)(void);
	size_t (*p_retro_serialize_size)(void);
	bool (*p_retro_serialize)(void*, size_t);
	bool (*p_retro_unserialize)(const void*, size_t);
	LOAD(retro_set_environment);
	LOAD(retro_set_video_refresh);
	LOAD(retro_set_audio_sample);
	LOAD(retro_set_audio_sample_batch);
	LOAD(retro_set_input_poll);
	LOAD(retro_set_input_state);
	LOAD(retro_init);
	LOAD(retro_deinit);
	LOAD(retro_load_game);
	LOAD(retro_unload_game);
	LOAD(retro_run);
	LOAD(retro_reset);
	LOAD(retro_serialize_size);
	LOAD(retro_serialize);
	LOAD(retro_unserialize);
	p_retro_set_environment(environment);
	p_retro_set_video_refresh(video);
	p_retro_set_audio_sample(audioSample);
	p_retro_set_audio_sample_batch(audioBatch);
	p_retro_set_input_poll(inputPoll);
	p_retro_set_input_state(inputState);
	p_retro_init();
	struct retro_game_info game = { argv[2], NULL, 0, NULL };
	if (!p_retro_load_game(&game)) {
		fprintf(stderr, "retro_load_game failed\n");
		p_retro_deinit();
		goto cleanup;
	}
	fprintf(logFile, "[HOST] mode=%s rate=%.3f frames=%u schedule=%s\n", argv[4], runRate, frames, argv[6]);
	const char* endpointSetting = getenv("MGBA_RUNNER_ENDPOINT_COUNT");
	if (endpointSetting) {
		endpointTarget = strtoul(endpointSetting, NULL, 10);
		const char* at = getenv("MGBA_RUNNER_ENDPOINT_START");
		endpointStart = at ? strtoul(at, NULL, 10) : 400;
		if (!endpointTarget || endpointTarget > 128 || endpointStart + endpointTarget > frames) goto cleanup;
	}
	const char* initialStatePath = getenv("MGBA_RUNNER_INITIAL_STATE");
	if (initialStatePath) {
		FILE* stream = fopen(initialStatePath, "rb");
		if (!stream) { p_retro_unload_game(); p_retro_deinit(); goto cleanup; }
		fseek(stream, 0, SEEK_END); long length = ftell(stream); rewind(stream);
		void* initial = length > 0 ? malloc((size_t) length) : NULL;
		bool success = initial && fread(initial, (size_t) length, 1, stream) == 1 &&
			p_retro_unserialize(initial, (size_t) length);
		free(initial); fclose(stream);
		fprintf(logFile, "[HOST] initial state success=%d\n", (int) success);
		if (!success) { p_retro_unload_game(); p_retro_deinit(); goto cleanup; }
	}
	/* External/private fixtures may be decoded in memory by the Python host.
	 * Keep the exact bytes in slot 15; never persist them in the repository. */
	if (getenv("MGBA_RUNNER_INITIAL_STATE_STDIN")) {
		_setmode(_fileno(stdin), _O_BINARY);
		uint32_t length;
		if (fread(&length, sizeof(length), 1, stdin) != 1 || !length || length > 16 * 1024 * 1024) goto cleanup;
		stateSlots[15] = malloc(length); stateSizes[15] = length;
		bool success = stateSlots[15] && fread(stateSlots[15], length, 1, stdin) == 1 && p_retro_unserialize(stateSlots[15], length);
		fprintf(logFile, "[HOST] stdin initial state success=%d bytes=%u\n", (int) success, length);
		if (!success) { p_retro_unload_game(); p_retro_deinit(); goto cleanup; }
	}
	for (frameNumber = 0; frameNumber < frames; ++frameNumber) {
		if (!strcmp(argv[4], "switch")) {
			fastForward = frameNumber >= switchStart && frameNumber < switchEnd;
			runRate = fastForward ? 119.455f : 59.728f;
		}
		const char* pauseFrame = getenv("MGBA_RUNNER_PAUSE_AT");
		if (pauseFrame && frameNumber == strtoul(pauseFrame, NULL, 10)) {
			Sleep(100);
			fprintf(logFile, "[HOST] pause/resume without retro_run frame=%u\n", frameNumber);
		}
		const char* resetFrame = getenv("MGBA_RUNNER_RESET_AT");
		if (resetFrame && frameNumber == strtoul(resetFrame, NULL, 10)) {
			p_retro_reset();
			fprintf(logFile, "[HOST] reset frame=%u\n", frameNumber);
		}
		p_retro_run();
		tracePlayers();
		if (endpointTarget && frameNumber >= endpointStart && endpointCount < endpointTarget) {
			endpointSize = p_retro_serialize_size();
			void* state = malloc(endpointSize);
			if (!state || !p_retro_serialize(state, endpointSize)) { free(state); goto cleanup; }
			endpointStates[endpointCount] = state;
			uint64_t digest = 14695981039346656037ULL;
			for (size_t i = 0; i < endpointSize; ++i) digest = (digest ^ ((uint8_t*) state)[i]) * 1099511628211ULL;
			uint32_t header0 = 0, header2 = 0, status2 = 0;
			if (testIwram) { memcpy(&header0, testIwram + 0x62e0, 4); memcpy(&header2, testIwram + 0x6530, 4); memcpy(&status2, testIwram + 0x6534, 4); }
			fprintf(logFile, "[HOST ENDPOINT_CAPTURE] index=%u frame=%u hash=%016llx header0=%08x header2=%08x status2=%08x\n", endpointCount, frameNumber, (unsigned long long) digest, header0, header2, status2);
			++endpointCount;
		}
		const char* ramFrames = getenv("MGBA_RUNNER_RAM_AT");
		if (ramFrames && testIwram && testWram) {
			const char* cursor = ramFrames;
			while (*cursor) {
				char* end;
				unsigned at = strtoul(cursor, &end, 10);
				if (end == cursor) break;
				if (frameNumber == at) {
					char path[1200];
					snprintf(path, sizeof(path), "%s-frame-%u.ram", argv[3], at);
					FILE* dump = GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES ? fopen(path, "wb") : NULL;
					if (!dump || fwrite(testIwram, 0x8000, 1, dump) != 1 || fwrite(testWram, 0x40000, 1, dump) != 1) {
						if (dump) fclose(dump);
						p_retro_unload_game(); p_retro_deinit(); goto cleanup;
					}
					fclose(dump);
				}
				cursor = *end == ',' ? end + 1 : end;
				if (*cursor != ',' && *end && cursor == end) break;
			}
		}
		const char* stateFrame = getenv("MGBA_RUNNER_STATE_ROUNDTRIP_AT");
		for (unsigned i = 0; i < stateActionCount; ++i) {
			if (stateActions[i].frame != frameNumber) continue;
			unsigned slot = stateActions[i].slot;
			bool success;
			if (!strcmp(stateActions[i].op, "RESET")) {
				p_retro_reset(); success = true;
			} else if (!strcmp(stateActions[i].op, "REWIND")) {
				rewinding = slot != 0; success = true;
			} else if (!strcmp(stateActions[i].op, "SPEED")) {
				fastForward = slot >= 2; runRate = slot == 3 ? 179.182f : slot == 2 ? 119.455f : 59.728f; success = slot >= 1 && slot <= 3;
			} else if (!strcmp(stateActions[i].op, "SAVE")) {
				free(stateSlots[slot]); stateSizes[slot] = p_retro_serialize_size();
				stateSlots[slot] = malloc(stateSizes[slot]);
				success = stateSlots[slot] && p_retro_serialize(stateSlots[slot], stateSizes[slot]);
				const char* prefix = getenv("MGBA_RUNNER_STATE_OUTPUT_PREFIX");
				if (success && prefix) {
					char path[1200]; snprintf(path, sizeof(path), "%s-frame-%u-slot-%u.rawstate", prefix, frameNumber, slot);
					FILE* dump = GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES ? fopen(path, "wb") : NULL;
					success = dump && fwrite(stateSlots[slot], stateSizes[slot], 1, dump) == 1;
					if (dump && fclose(dump)) success = false;
				}
			} else if (!strcmp(stateActions[i].op, "LOAD_BAD")) {
				success = stateSlots[slot] && !p_retro_unserialize(stateSlots[slot], 1);
			} else {
				success = stateSlots[slot] && p_retro_unserialize(stateSlots[slot], stateSizes[slot]);
			}
			fprintf(logFile, "[HOST] state %s slot=%u frame=%u success=%d\n", stateActions[i].op, slot, frameNumber, (int) success);
			if (!success) { p_retro_unload_game(); p_retro_deinit(); goto cleanup; }
		}
		if (stateFrame && frameNumber == strtoul(stateFrame, NULL, 10)) {
			size_t size = p_retro_serialize_size();
			void* state = malloc(size);
			if (!state || !p_retro_serialize(state, size) || !p_retro_unserialize(state, size)) {
				fprintf(stderr, "state roundtrip failed\n");
				free(state); p_retro_unload_game(); p_retro_deinit(); goto cleanup;
			}
			free(state);
			fprintf(logFile, "[HOST] state roundtrip frame=%u\n", frameNumber);
		}
		if (b6jjRewindAt && frameNumber >= b6jjRewindAt-b6jjRewindLength && frameNumber < b6jjRewindAt) {
			if (!b6jjHistorySize) b6jjHistorySize = p_retro_serialize_size();
			void* state = malloc(b6jjHistorySize);
			if (!state) goto cleanup;
			b6jjHistory[b6jjHistoryCount++] = state;
			if (!p_retro_serialize(state,b6jjHistorySize)) goto cleanup;
		}
		if (b6jjRewindAt && frameNumber == b6jjRewindAt && !getenv("MGBA_RUNNER_B6JJ_HISTORY_ONLY")) {
			for (unsigned cycle = 0; cycle < b6jjRewindCycles; ++cycle) {
				LARGE_INTEGER start, end, frequency; QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&start);
				FILETIME created, exited, kernelStart, userStart, kernelEnd, userEnd;
				GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernelStart, &userStart);
				rewinding = true;
				fprintf(logFile,"[HOST B6JJ REWIND_BEGIN] cycle=%u states=%u\n",cycle,b6jjHistoryCount);
				for (unsigned i=b6jjHistoryCount; i>0; --i) {
					if (!p_retro_unserialize(b6jjHistory[i-1],b6jjHistorySize)) goto cleanup;
					p_retro_run();
				}
				rewinding = false;
				p_retro_run(); /* Exactly one final-state reconstruction opportunity. */
				QueryPerformanceCounter(&end);
				GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernelEnd, &userEnd);
				ULARGE_INTEGER ks,us,ke,ue;
				ks.LowPart=kernelStart.dwLowDateTime;ks.HighPart=kernelStart.dwHighDateTime;
				us.LowPart=userStart.dwLowDateTime;us.HighPart=userStart.dwHighDateTime;
				ke.LowPart=kernelEnd.dwLowDateTime;ke.HighPart=kernelEnd.dwHighDateTime;
				ue.LowPart=userEnd.dwLowDateTime;ue.HighPart=userEnd.dwHighDateTime;
				fprintf(logFile,"[HOST B6JJ REWIND_END] cycle=%u unserializes=%u wallMs=%.3f cpuMs=%.3f loadsPerSecond=%.3f\n",cycle,b6jjHistoryCount,(end.QuadPart-start.QuadPart)*1000.0/frequency.QuadPart,(ke.QuadPart-ks.QuadPart+ue.QuadPart-us.QuadPart)/10000.0,b6jjHistoryCount*(double)frequency.QuadPart/(end.QuadPart-start.QuadPart));
			}
		}
	}
	if (endpointTarget) {
		for (unsigned i = 0; i < endpointCount; ++i) {
			p_retro_reset(); endpointRecovered = endpointHard = false; rewinding = true;
			fprintf(logFile, "[HOST ENDPOINT_BEGIN] index=%u\n", i);
			for (unsigned j = 5; j > 0; --j) {
				unsigned at = i + j - 1 < endpointCount ? i + j - 1 : endpointCount - 1;
				if (!p_retro_unserialize(endpointStates[at], endpointSize)) goto cleanup;
				p_retro_run();
			}
			rewinding = false;
			unsigned normalRuns = 0;
			while (!endpointRecovered && !endpointHard && normalRuns < 300) { p_retro_run(); ++normalRuns; }
			fprintf(logFile, "[HOST ENDPOINT_END] index=%u active=%d hard=%d runs=%u\n", i, endpointRecovered, endpointHard, normalRuns);
		}
	}
	p_retro_unload_game();
	p_retro_deinit();
	freeStateSlots();
	if (callbackFile && fclose(callbackFile)) callbackWriteFailed = true;
	if (playerTraceFile && fclose(playerTraceFile)) callbackWriteFailed = true;
	fclose(logFile);
	FreeLibrary(library);
	return callbackWriteFailed ? 1 : 0;
cleanup:
	freeStateSlots();
	if (playerTraceFile) fclose(playerTraceFile);
	if (callbackFile) fclose(callbackFile);
	fclose(logFile);
	FreeLibrary(library);
	return 1;
}
