/* SPDX-License-Identifier: MPL-2.0
 * B6JJ exact-identity experimental native-derived audio backend.
 * Timing is autonomous: no oracle, future trace, MP2K ABI, or time stretch.
 */
#include <mgba/internal/gba/b6jj-audio.h>
#include <mgba/core/audio-clock.h>
#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include <mgba/gba/core.h>
#include <mgba/internal/arm/isa-inlines.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/serialize.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/vfs.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

#define B6JJ_PERIOD 280896U
#define B6JJ_CAPACITY 16384U
#define B6JJ_STARTUP_LATENCY 64U
struct B6JJEvent {
	uint64_t sequence, gameCycle, audioCycle;
	uint32_t id, header;
	bool se;
};
struct B6JJAudio {
	struct mCore* primary;
	struct mCore* aux;
	const struct mAudioClock* clock;
	struct B6JJAudioStats stats;
	struct B6JJEvent queue[16];
	uint32_t bgmId, seId, lastPrimaryCycle, lastAuxCycle;
	uint64_t primaryEpoch, auxEpoch, nextRoot, lastApplied;
	int64_t origin;
	mColor* pixels;
	bool isolated;
	bool recovering, recoveryReady, loaded;
	uint32_t recoveryReturn;
	uint32_t primaryCallEntry, primaryCallReturn;
	void (*sink)(void*, const struct B6JJAudioEventObservation*);
	void* sinkContext;
	void (*primaryRegion)(struct ARMCore*, uint32_t);
	void (*auxRegion)(struct ARMCore*, uint32_t);
	void (*store32)(struct ARMCore*, uint32_t, int32_t, int*);
	void (*store16)(struct ARMCore*, uint32_t, int16_t, int*);
	void (*store8)(struct ARMCore*, uint32_t, int8_t, int*);
	uint32_t (*storeMultiple)(struct ARMCore*, uint32_t, int, enum LSMDirection, int*);
};
/* Libretro owns a single machine. No GBA/serialized-state layout extension. */
static struct B6JJAudio* instance;

bool B6JJAudioIdentity(const struct mCore* primary) {
	if (!primary || primary->platform(primary) != mPLATFORM_GBA) return false;
	const struct GBA* g = primary->board;
	if (g->memory.romSize != 16777216 || g->romCrc32 != 0xC956FD37 ||
	    memcmp((const uint8_t*) g->memory.rom + 0xAC, "B6JJ", 4)) return false;
#ifdef _WIN32
	static const uint8_t expected[32] = {
		0xA6,0xD5,0xE2,0x2F,0x0E,0x93,0x78,0x65,0xBB,0xC3,0xB0,0x93,0x26,0xCF,0x74,0x71,
		0xC9,0x0E,0x47,0xAE,0x00,0xBC,0x06,0x0E,0x63,0xA5,0xB0,0x60,0x5F,0xB9,0xD2,0x45};
	HCRYPTPROV provider = 0;
	HCRYPTHASH hash = 0;
	uint8_t digest[32]; DWORD size = sizeof(digest);
	bool ok = CryptAcquireContext(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
		CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) &&
		CryptHashData(hash, (const BYTE*) g->memory.rom, (DWORD) g->memory.romSize, 0) &&
		CryptGetHashParam(hash, HP_HASHVAL, digest, &size, 0) && size == 32 &&
		!memcmp(digest, expected, 32);
	if (hash) CryptDestroyHash(hash);
	if (provider) CryptReleaseContext(provider, 0);
	return ok;
#else
	/* This DLL experiment is explicitly Windows-only; fail closed elsewhere. */
	return false;
#endif
}
static void fail(struct B6JJAudio* b, const char* reason) {
	if (!b->stats.failure) { b->stats.failure = reason; ++b->stats.guardFailures; }
}
static uint64_t boardCycle(struct GBA* g, uint32_t* last, uint64_t* epoch) {
	uint32_t now = mTimingCurrentTime(&g->timing);
	if (now < *last && *last - now > 0x80000000U) *epoch += UINT64_C(0x100000000);
	*last = now;
	return *epoch + now;
}
static uint64_t auxCycle(struct B6JJAudio* b) {
	return boardCycle(b->aux->board, &b->lastAuxCycle, &b->auxEpoch);
}
static bool headerValid(struct B6JJAudio* b, uint32_t header) {
	const struct GBA* g = b->primary->board;
	if (header < 0x08000000 || header > 0x09000000 - 14) return false;
	const uint8_t* h = (const uint8_t*) g->memory.rom + header - 0x08000000;
	unsigned length = h[0] | h[1] << 8;
	if (length < 14 || length > 0x09000000 - header) return false;
	for (unsigned i = 0; i < 6; ++i) {
		unsigned offset = h[2+i*2] | h[3+i*2] << 8;
		if (offset && offset >= length) return false;
	}
	return true;
}
static void primaryObserve(struct ARMCore* c, uint32_t pc) {
	struct B6JJAudio* b = instance;
	if (!b->stats.failure) {
		if (pc == b->primaryCallReturn) b->primaryCallEntry = b->primaryCallReturn = 0;
		if (pc == 0x08008440) b->bgmId = c->gprs[0];
		if (pc == 0x08008498) b->seId = c->gprs[0];
		bool se = pc == 0x0807D2B8 && (c->gprs[ARM_LR] & ~1U) == 0x080084BE;
		bool bgm = pc == 0x0807D250 && (c->gprs[ARM_LR] & ~1U) == 0x0800846A;
		/* setActiveRegion also observes IRQ return to a not-yet-executed
		 * callee entry. That resumes an existing BL; it is not a new Start. */
		if ((se || bgm) && pc != b->primaryCallEntry) {
			if (b->primaryCallEntry) fail(b, "NESTED_PRIMARY_START");
			b->primaryCallEntry = pc;
			b->primaryCallReturn = se ? 0x080084BE : 0x0800846A;
			uint64_t game = boardCycle(b->primary->board, &b->lastPrimaryCycle, &b->primaryEpoch);
			uint64_t sample = mAudioClockTimestampForCycle(b->clock, game);
			int64_t target = b->origin + (int64_t) sample * 256;
			if (!b->clock->runActive || target < 0 || b->stats.queue == 16 ||
			    !headerValid(b, c->gprs[0])) fail(b, "EVENT_HEADER_CLOCK_OR_CAPACITY");
			else {
				struct B6JJEvent e = {++b->stats.events, game, (uint64_t) target,
					se ? b->seId : b->bgmId, c->gprs[0], se};
				if (b->stats.queue && e.audioCycle < b->queue[b->stats.queue-1].audioCycle)
					fail(b, "EVENT_ORDER");
				else {
					b->queue[b->stats.queue++] = e;
					if (se) ++b->stats.se; else ++b->stats.bgm;
					if (b->stats.queue > b->stats.queuePeak) b->stats.queuePeak = b->stats.queue;
				}
			}
		}
		/* These lifecycle semantics have not been validated. Stop fixed audio
		 * on an unforwarded wrapper, rather than continue an obsolete song. */
		if (pc == 0x08008474 || pc == 0x08008480 || pc == 0x080084D0 ||
		    pc == 0x080084DC || pc == 0x080084E8 ||
	    (pc == 0x080083D0 && (b->stats.events || b->loaded))) fail(b, "UNSUPPORTED_AUDIO_LIFECYCLE");
	}
	b->primaryRegion(c, pc);
}
static void auxObserve(struct ARMCore* c, uint32_t pc) {
	struct B6JJAudio* b = instance;
	if (b->recovering) {
		if (pc == 0x0807EEE4 && !b->recoveryReturn) b->recoveryReturn = c->gprs[ARM_LR] & ~1U;
		else if (b->recoveryReturn && pc == b->recoveryReturn) {
			b->recoveryReady = true;
			c->nextEvent = c->cycles;
			((struct GBA*) b->aux->board)->earlyExit = true;
		}
		if (pc == 0x08008440 || pc == 0x08008498 || pc == 0x080083D0 ||
		    pc == 0x08008474 || pc == 0x08008480 || pc == 0x080084D0 ||
		    pc == 0x080084DC || pc == 0x080084E8) fail(b, "RECOVERY_GAME_AUDIO_CALL");
	}
	if (!b->isolated && pc == 0x0807EEE4) b->stats.lastRoot = auxCycle(b);
	if (b->isolated && pc != 0x08000000 && pc >= 0x4000 &&
	    !(pc >= 0x0807CE10 && pc < 0x08080800)) fail(b, "AUX_PC");
	b->auxRegion(c, pc);
}
static bool writable(struct ARMCore* c, uint32_t address, unsigned bytes) {
	struct B6JJAudio* b = instance;
	struct GBA* g = b->aux->board;
	if (!b->isolated || g->performingDMA) return true;
	if (address >= 0x08000000) { fail(b, "AUX_ROM_WRITE"); return false; }
	if (address < 0x02000000 || address >= 0x04000000) return true;
	if ((address >= 0x03000000 && address <= 0x030013A0 - bytes) ||
	    (address >= 0x03007C00 && address <= 0x03007E00 - bytes)) return true;
	(void) c;
	fail(b, "AUX_RAM_OR_STACK_WRITE"); return false;
}
static void store32(struct ARMCore* c, uint32_t a, int32_t v, int* cy) {
	if (writable(c, a & ~3U, 4)) instance->store32(c, a, v, cy);
}
static void store16(struct ARMCore* c, uint32_t a, int16_t v, int* cy) {
	if (writable(c, a & ~1U, 2)) instance->store16(c, a, v, cy);
}
static void store8(struct ARMCore* c, uint32_t a, int8_t v, int* cy) {
	if (writable(c, a, 1)) instance->store8(c, a, v, cy);
}
static uint32_t storeMultiple(struct ARMCore* c, uint32_t a, int mask, enum LSMDirection dir, int* cy) {
	unsigned n = 0; for (unsigned m = (unsigned) mask; m; m >>= 1) n += m & 1;
	uint32_t start = a & ~3U;
	if (dir == LSM_IB) start += 4;
	else if (dir == LSM_DA && n) start -= (n-1)*4;
	else if (dir == LSM_DB) start -= n*4;
	if (n && !writable(c, start, n*4)) return a;
	return instance->storeMultiple(c, a, mask, dir, cy);
}
static bool peripheralsUntil(struct B6JJAudio* b, uint64_t target) {
	struct ARMCore* c = ((struct GBA*) b->aux->board)->cpu;
	for (unsigned guard = 0; auxCycle(b) < target && !b->stats.failure; ++guard) {
		if (guard == 1000000) { fail(b, "PERIPHERAL_PROGRESS"); break; }
		int32_t distance = c->nextEvent - c->cycles;
		uint64_t remain = target - auxCycle(b);
		if (distance > 0) c->cycles += (int32_t) (remain < (uint32_t) distance ? remain : (uint32_t) distance);
		if (c->cycles >= c->nextEvent) c->irqh.processEvents(c);
	}
	return !b->stats.failure;
}
static bool call(struct B6JJAudio* b, uint32_t entry, uint32_t argument) {
	struct ARMCore* c = ((struct GBA*) b->aux->board)->cpu;
	c->halted = 0; c->cpsr.i = 1;
	_ARMSetMode(c, MODE_THUMB);
	c->gprs[ARM_SP] = 0x03007E00; c->gprs[ARM_LR] = 0x08000001;
	c->gprs[0] = c->gprs[1] = argument; c->gprs[2] = c->gprs[3] = 0;
	c->gprs[ARM_PC] = entry;
	int32_t prefetch = ThumbWritePC(c);
	c->cycles += prefetch + 1 + c->memory.activeSeqCycles16;
	uint64_t begin = auxCycle(b);
	for (unsigned instructions = 0; !b->stats.failure; ++instructions) {
		uint32_t pc = (uint32_t) c->gprs[ARM_PC] - _ARMInstructionLength(c);
		if (pc == 0x08000000) break;
		if ((pc >= 0x4000 && !(pc >= 0x0807CE10 && pc < 0x08080800)) ||
		    instructions == 200000 || auxCycle(b) - begin > 100000) { fail(b, "AUX_CALL_BUDGET_OR_PC"); break; }
		ARMRun(c);
	}
	return !b->stats.failure;
}
static bool apply(struct B6JJAudio* b) {
	struct B6JJEvent e = b->queue[0];
	if (e.sequence != b->stats.applied + 1 || e.audioCycle < b->lastApplied) {
		fail(b, "APPLY_SEQUENCE"); return false;
	}
	if (!peripheralsUntil(b, e.audioCycle)) return false;
	uint64_t late = auxCycle(b) - e.audioCycle;
	if (late > b->stats.maximumLate) b->stats.maximumLate = late;
	if (b->sink) {
		struct B6JJAudioEventObservation observation = {e.sequence, e.gameCycle, e.audioCycle, auxCycle(b), e.id, e.header, e.se};
		b->sink(b->sinkContext, &observation);
	}
	if (!call(b, e.se ? 0x0807D2B8 : 0x0807D250, e.header)) return false;
	++b->stats.applied; b->lastApplied = e.audioCycle;
	--b->stats.queue;
	memmove(b->queue, b->queue+1, b->stats.queue*sizeof(*b->queue));
	return true;
}
static struct B6JJAudio* create(struct mCore* primary, const struct mAudioClock* clock, bool loaded) {
	if (instance || !clock || !clock->enabled || clock->outputSampleRate != 65536 || !B6JJAudioIdentity(primary)) return NULL;
	struct B6JJAudio* b = calloc(1, sizeof(*b));
	if (!b) return NULL;
	b->primary = primary; b->clock = clock;
	b->loaded = loaded;
	b->aux = GBACoreCreate();
	if (!b->aux || !b->aux->init(b->aux)) { free(b); return NULL; }
	mCoreInitConfig(b->aux, "b6jj-private-audio");
	b->aux->opts.skipBios = primary->opts.skipBios;
	b->aux->setAudioBufferSize(b->aux, B6JJ_CAPACITY);
	b->pixels = calloc(256*160, sizeof(mColor));
	if (!b->pixels) { B6JJAudioDestroy(b); return NULL; }
	b->aux->setVideoBuffer(b->aux, b->pixels, 256);
	struct GBA* p = primary->board;
	struct VFile* rom = VFileMemChunk(p->memory.rom, p->memory.romSize);
	if (!rom || !b->aux->loadROM(b->aux, rom)) {
		if (rom) rom->close(rom);
		B6JJAudioDestroy(b); return NULL;
	}
	if (p->biosVf) {
		struct VFile* bios = VFileMemChunk(p->memory.bios, 16384);
		if (!bios || !b->aux->loadBIOS(b->aux, bios, 0)) {
			if (bios) bios->close(bios);
			B6JJAudioDestroy(b); return NULL;
		}
	}
	b->aux->reset(b->aux);
	struct GBA* g = b->aux->board;
	uint64_t loadedCycle = 0;
	if (loaded) {
		struct GBASerializedState* snapshot = calloc(1, sizeof(*snapshot));
		if (!snapshot) { B6JJAudioDestroy(b); return NULL; }
		GBASerialize(p, snapshot);
		bool restored = GBADeserialize(g, snapshot);
		free(snapshot);
		if (!restored) { B6JJAudioDestroy(b); return NULL; }
		/* Exact identity fixes the driver/asset layout. Validate every live
		 * native cursor before running a reconstructed context. */
		for (unsigned slot = 0x60; slot < 0x780; slot += 0x98) {
			uint32_t cursor;
			memcpy(&cursor, (const uint8_t*) g->memory.iwram + slot + 4, sizeof(cursor));
			if (cursor && (cursor < 0x08000000 || cursor >= 0x09000000)) {
				B6JJAudioDestroy(b); return NULL;
			}
		}
		if (g->audio.sampleInterval != 256 ||
		    g->audio.chA.internalRemaining < 0 || g->audio.chA.internalRemaining > 4 ||
		    g->audio.chB.internalRemaining < 0 || g->audio.chB.internalRemaining > 4) {
			B6JJAudioDestroy(b); return NULL;
		}
		b->lastPrimaryCycle = mTimingCurrentTime(&p->timing);
		loadedCycle = auxCycle(b);
		/* The host PCM queue is outside the savestate. Only newly generated
		 * continuation PCM is permitted in this fresh generation. */
		mAudioBufferClear(b->aux->getAudioBuffer(b->aux));
	}
	b->primaryRegion = p->cpu->memory.setActiveRegion;
	b->auxRegion = g->cpu->memory.setActiveRegion;
	b->store32 = g->cpu->memory.store32; b->store16 = g->cpu->memory.store16;
	b->store8 = g->cpu->memory.store8; b->storeMultiple = g->cpu->memory.storeMultiple;
	instance = b;
	g->cpu->memory.setActiveRegion = auxObserve;
	g->cpu->memory.store32 = store32; g->cpu->memory.store16 = store16;
	g->cpu->memory.store8 = store8; g->cpu->memory.storeMultiple = storeMultiple;
	if (loaded) {
		b->recovering = true;
		for (unsigned loops = 0; !b->recoveryReady && !b->stats.failure; ++loops) {
			if (loops == 100000 || auxCycle(b) - loadedCycle > B6JJ_PERIOD + 100000) {
				fail(b, "RECOVERY_ROOT_BUDGET"); break;
			}
			ARMRunLoop(g->cpu);
		}
		b->recovering = false;
		b->stats.recoveryCycles = auxCycle(b) - loadedCycle;
		if (b->stats.failure || !b->recoveryReady) { B6JJAudioDestroy(b); return NULL; }
		g->earlyExit = false;
	} else for (unsigned i = 0; i < 5; ++i) b->aux->runFrame(b->aux);
	uint64_t end = auxCycle(b);
	if (!b->stats.lastRoot || end < b->stats.lastRoot || end - b->stats.lastRoot >= B6JJ_PERIOD) {
		B6JJAudioDestroy(b); return NULL;
	}
	b->origin = loaded ? (int64_t) loadedCycle - (int64_t) clock->absoluteAudioSample*256 :
		(int64_t) end - (int64_t) B6JJ_PERIOD*5;
	b->nextRoot = b->stats.lastRoot + B6JJ_PERIOD;
	b->stats.firstRoot = b->stats.lastRoot = 0;
	b->isolated = true; g->cpu->halted = 0; g->cpu->cpsr.i = 1;
	/* runFrame begins at a partial board interval. Align the frontend epoch
	 * with five nominal frames using startup silence only; no music resample. */
	struct mAudioBuffer* buffer = b->aux->getAudioBuffer(b->aux);
	size_t existing = mAudioBufferAvailable(buffer);
	/* A loaded packet may end immediately before a root deadline. The same
	 * fixed 64-sample transport reserve absorbs that boundary without advancing
	 * past a root or changing any loaded voice/peripheral state. */
	size_t nominal = loaded ? existing + B6JJ_STARTUP_LATENCY : (uint64_t) B6JJ_PERIOD*5/256 + B6JJ_STARTUP_LATENCY;
	if (existing > nominal || nominal > B6JJ_CAPACITY) { B6JJAudioDestroy(b); return NULL; }
	int16_t* boot = calloc(nominal*2, sizeof(int16_t));
	if (!boot) { B6JJAudioDestroy(b); return NULL; }
	mAudioBufferRead(buffer, boot+(nominal-existing)*2, existing);
	mAudioBufferWrite(buffer, boot, nominal); free(boot);
	b->stats.bufferPeak = nominal;
	p->cpu->memory.setActiveRegion = primaryObserve;
	return b;
}
struct B6JJAudio* B6JJAudioCreate(struct mCore* primary, const struct mAudioClock* clock) {
	return create(primary, clock, false);
}
struct B6JJAudio* B6JJAudioCreateLoaded(struct mCore* primary, const struct mAudioClock* clock) {
	return create(primary, clock, true);
}
void B6JJAudioDestroy(struct B6JJAudio* b) {
	if (!b) return;
	if (b->primaryRegion && ((struct GBA*) b->primary->board)->cpu->memory.setActiveRegion == primaryObserve)
		((struct GBA*) b->primary->board)->cpu->memory.setActiveRegion = b->primaryRegion;
	/* No callbacks may run during destruction. Queue and PCM are gone before
	 * returning to a loaded/rewound primary timeline. */
	memset(b->queue, 0, sizeof(b->queue));
	if (b->aux) { mAudioBufferClear(b->aux->getAudioBuffer(b->aux)); mCoreConfigDeinit(&b->aux->config); b->aux->deinit(b->aux); }
	free(b->pixels);
	if (instance == b) instance = NULL;
	memset(b, 0, sizeof(*b)); free(b);
}
bool B6JJAudioRender(struct B6JJAudio* b, int16_t* output, size_t frames) {
	if (!b || b->stats.failure || frames > 2048 || !b->clock->runActive) return false;
	int64_t horizon = b->origin + (int64_t) (b->clock->runStartSample + frames)*256;
	if (horizon > 0) {
		uint64_t target = (uint64_t) horizon;
		while (b->nextRoot <= target && !b->stats.failure) {
			while (b->stats.queue && b->queue[0].audioCycle <= b->nextRoot) if (!apply(b)) return false;
			if (!peripheralsUntil(b, b->nextRoot)) return false;
			uint64_t root = auxCycle(b);
			uint64_t late = root-b->nextRoot;
			if (late > b->stats.maximumRootLate) b->stats.maximumRootLate = late;
			if (late > 100000) { fail(b, "ROOT_DEADLINE_BUDGET"); return false; }
			if (!b->stats.roots) b->stats.firstRoot = root;
			b->stats.lastRoot = root; ++b->stats.roots;
			if (!call(b, 0x0807EEE4, 0)) return false;
			b->nextRoot += B6JJ_PERIOD;
		}
		while (b->stats.queue && b->queue[0].audioCycle < target) if (!apply(b)) return false;
		if (!peripheralsUntil(b, target)) return false;
	}
	struct mAudioBuffer* buffer = b->aux->getAudioBuffer(b->aux);
	size_t available = mAudioBufferAvailable(buffer);
	if (available > b->stats.bufferPeak) b->stats.bufferPeak = available;
	if (mAudioBufferFull(buffer)) { ++b->stats.overrun; fail(b, "PCM_BUFFER_FULL"); return false; }
	/* Native callbacks produce four-sample batches. Advance at most one batch
	 * to make the current frontend batch available; root deadlines stay fixed. */
	for (unsigned batches = 0; mAudioBufferAvailable(buffer) < frames && batches < 4 &&
	     !b->stats.queue && auxCycle(b)+1024 < b->nextRoot; ++batches) {
		if (!peripheralsUntil(b, auxCycle(b)+1024)) break;
		if ((batches+1)*4 > b->stats.lookaheadSamples) b->stats.lookaheadSamples = (batches+1)*4;
	}
	b->stats.available = mAudioBufferAvailable(buffer);
	b->stats.auxCycle = auxCycle(b); b->stats.nextRoot = b->nextRoot;
	if (mAudioBufferAvailable(buffer) < frames || b->stats.failure) {
		++b->stats.underrun; fail(b, "PCM_UNDERRUN"); return false;
	}
	mAudioBufferRead(buffer, output, frames); b->stats.samples += frames;
	return true;
}
const struct B6JJAudioStats* B6JJAudioGetStats(const struct B6JJAudio* b) { return b ? &b->stats : NULL; }
struct mCore* B6JJAudioDiagnosticCore(struct B6JJAudio* b) { return b ? b->aux : NULL; }
void B6JJAudioSetDiagnosticSink(struct B6JJAudio* b,
	void (*sink)(void*, const struct B6JJAudioEventObservation*), void* context) {
	if (b) { b->sink = sink; b->sinkContext = context; }
}
