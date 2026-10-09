// Copyright (c) 2026 granbone
// SPDX-License-Identifier: LGPL-3.0-only
// See LICENSE and COPYING.GPL-3.0.txt in this directory.
#include "bridge.h"
#include "MP2KContext.hpp"
#include "MP2KScanner.hpp"
#include "Rom.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace {
thread_local std::string lastError;
uint32_t crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
uint32_t playerTableOffset(const Rom& rom, const MP2KScanner::Result& result) {
    const uint32_t songPointer = static_cast<uint32_t>(result.songTableInfo.pos + 0x08000000);
    uint32_t found = UINT32_MAX;
    for (size_t i = 4; i + 4 <= rom.Size(); i += 4) {
        if (rom.ReadU32(i) != songPointer) continue;
        const uint32_t playerPointer = rom.ReadU32(i - 4);
        if (!rom.ValidPointer(playerPointer)) continue;
        const size_t offset = playerPointer - 0x08000000;
        if (offset + result.playerTableInfo.size() * 12 > rom.Size()) continue;
        bool matches = true;
        for (size_t j = 0; j < result.playerTableInfo.size(); ++j) {
            if (rom.ReadU16(offset + j * 12 + 8) != result.playerTableInfo[j].maxTracks ||
                rom.ReadU16(offset + j * 12 + 10) != result.playerTableInfo[j].usePriority) {
                matches = false;
                break;
            }
        }
        if (!matches) continue;
        if (found != UINT32_MAX && found != offset) return UINT32_MAX;
        found = static_cast<uint32_t>(offset);
    }
    return found;
}
uint32_t soundModeWord(const MP2KSoundMode& mode) {
    return (uint32_t(mode.vol) << 12) | (uint32_t(mode.rev) << 0) |
        (uint32_t(mode.freq) << 16) | (uint32_t(mode.maxChannels) << 8) |
        (uint32_t(mode.dacConfig) << 20);
}
void validateTables(const Rom& rom, const MP2KScanner::Result& result) {
    const size_t players = result.playerTableInfo.size();
    const auto& mode = result.mp2kSoundMode;
    const size_t table = result.songTableInfo.pos, count = result.songTableInfo.count;
    if (!players || players > 32 || !count || table > rom.Size() || count > (rom.Size() - table) / 8 ||
        !mode.freq || mode.freq > 12 || !mode.maxChannels || mode.maxChannels > 12 ||
        mode.dacConfig < 8 || mode.dacConfig > 11)
        throw std::runtime_error("invalid MP2K table/SoundMode bounds");
    for (const auto& player : result.playerTableInfo)
        if (player.maxTracks > 16)
            throw std::runtime_error("invalid MP2K player track count");
    for (size_t song = 0; song < count; ++song) {
        const size_t entry = table + song * 8;
        const auto player = rom.ReadU16(entry + 4);
        const auto header = rom.ReadU32(entry);
        if (!header && !rom.ReadU32(entry + 4)) continue; // Reserved, non-playable slot.
        if (player >= players || !rom.ValidPointer(header) || (header & 3))
            throw std::runtime_error("invalid song/player table entry");
        const size_t pos = header - 0x08000000;
        if (rom.Size() - pos < 8) throw std::runtime_error("short song header");
        const size_t tracks = rom.ReadU8(pos);
        if (tracks > 16 ||
            tracks > (rom.Size() - pos - 8) / 4)
            throw std::runtime_error("invalid song track count");
        if (tracks && !rom.ValidPointer(rom.ReadU32(pos + 4)))
            throw std::runtime_error("invalid tone bank pointer");
        for (size_t track = 0; track < tracks; ++track)
            if (!rom.ValidPointer(rom.ReadU32(pos + 8 + track * 4)))
                throw std::runtime_error("invalid track pointer");
    }
}
struct Player {
    struct Fade { uint16_t volume = 256, speed = 0, framesLeft = 0; uint8_t microframe = 0; bool active = false; };
    Rom rom;
    // Moved channels retain references to their construction context (scratch
    // buffers/ROM/mode). Keep these bounded, fresh seek contexts alive until
    // the output context and its channels have been destroyed.
    std::vector<std::unique_ptr<MP2KContext>> rebindChannelContexts;
    std::unique_ptr<MP2KContext> context;
    bool runtimePlayers = false;
    size_t buffered = 0;
    uint64_t renderedFrames = 0;
    uint64_t lastSoundMainSample = 0;
    uint64_t soundMainCalls = 0;
    mp2k_bridge_microframe recentMicroframes[16] = {};
    Fade fades[32];
    struct PhasePlay { bool pending = false; uint16_t song = 0; uint64_t tickSample = 0; };
    PhasePlay phasePlays[32];
    uint64_t lastPhaseTickSample = 0;
    uint8_t lastPhasePlayer = 0;
    uint32_t skipNextReaderMask = 0;
    std::vector<mp2k_bridge_rebind_player> rebindStates;
    size_t rebindIndex = 0;
    uint64_t rebindWork = 0;
    std::unique_ptr<MP2KContext> rebindContext;
    PlayerTableInfo playerInfo;
    Player(const uint8_t* data, size_t size, uint32_t rate)
        : rom(Rom::LoadFromBufferCopy({const_cast<uint8_t*>(data), size})) {
        const bool knownAamj = rom.GetROMCode() == "AAMJ" &&
            size == 0x800000 && crc32(data, size) == 0xF3E41D73;
        MP2KScanner scanner(rom);
        auto results = scanner.Scan(nullptr);
        auto found = knownAamj ?
            std::find_if(results.begin(), results.end(), [](const auto& result) {
                return result.songTableInfo.pos == 0x108A3C && result.songTableInfo.count == 455 &&
                    result.playerTableInfo.size() == 9;
            }) : results.size() == 1 && results[0].playerTableInfo.size() <= 32 ?
                results.begin() : results.end();
        if (found == results.end()) throw std::runtime_error("unique supported MP2K table not found");
        validateTables(rom, *found);
        playerInfo = found->playerTableInfo;
        AgbplaySoundMode mode;
        mode.resamplerTypeNormal = ResamplerType::LINEAR;
        mode.resamplerTypeFixed = ResamplerType::LINEAR;
        context = std::make_unique<MP2KContext>(rate, -1, rom, found->mp2kSoundMode,
            mode, found->songTableInfo, found->playerTableInfo);
    }
};
int16_t convert(float v) {
    if (!std::isfinite(v)) throw std::runtime_error("non-finite PCM");
    const float scaled = v * 32767.0f;
    if (scaled >= 32767.f) return 32767;
    if (scaled <= -32768.f) return -32768;
    return static_cast<int16_t>(std::clamp(std::lround(v * 32767.0f), -32768l, 32767l));
}
template<class F> int call(F f) {
    try { f(); lastError.clear(); return 0; }
    catch (const std::exception& e) { lastError = e.what(); return -1; }
    catch (...) { lastError = "unknown C++ exception"; return -1; }
}
}
extern "C" uint32_t mp2k_bridge_api_version(void) { return 2; }
extern "C" mp2k_bridge_handle mp2k_bridge_create(const uint8_t* rom, size_t size, uint32_t rate) {
    Player* result = nullptr;
    if (!rom || size < 0x200 || size > 0x2000000 || rate < 8000 || rate > 192000) { lastError = "invalid ROM or rate"; return nullptr; }
    if (call([&] { result = new Player(rom, size, rate); })) return nullptr;
    return result;
}
extern "C" int mp2k_bridge_scan(const uint8_t* data, size_t size,
    mp2k_bridge_scan_result* output, size_t capacity, size_t* count) {
    if (!data || size < 0x200 || size > 0x2000000 || !count || capacity > 32 || (capacity && !output)) {
        lastError = "invalid scanner arguments";
        return -1;
    }
    *count = 0;
    return call([&] {
        Rom rom = Rom::LoadFromBufferCopy({const_cast<uint8_t*>(data), size});
        MP2KScanner scanner(rom);
        auto candidates = scanner.Scan(nullptr);
        *count = candidates.size();
        for (size_t i = 0; i < std::min(capacity, candidates.size()); ++i) {
            const auto& candidate = candidates[i];
            const uint32_t playerOffset = playerTableOffset(rom, candidate);
            output[i] = {
                .song_table_offset = static_cast<uint32_t>(candidate.songTableInfo.pos),
                .player_table_offset = playerOffset,
                .sound_mode = soundModeWord(candidate.mp2kSoundMode),
                .song_count = candidate.songTableInfo.count,
                .player_count = static_cast<uint8_t>(candidate.playerTableInfo.size()),
                .table_index = candidate.songTableInfo.tableIdx,
                .player_table_reference_valid = static_cast<uint8_t>(playerOffset != UINT32_MAX),
                .driver_variant = 0
            };
        }
    });
}
extern "C" void mp2k_bridge_destroy(mp2k_bridge_handle handle) { delete static_cast<Player*>(handle); }
extern "C" int mp2k_bridge_play(mp2k_bridge_handle handle, uint16_t song, uint8_t player) {
    if (!handle) return -1;
    return call([&] {
        auto& bridge = *static_cast<Player*>(handle);
        auto& ctx = *bridge.context;
        if (player >= ctx.players.size() || song >= ctx.songTableInfo.count || (!bridge.runtimePlayers && ctx.m4aSongNumPlayerGet(song) != player))
            throw std::runtime_error("song/player mismatch");
        if (bridge.runtimePlayers) {
            const auto pos = ctx.rom.ReadAgbPtrToPos(ctx.songTableInfo.pos + song * 8);
            if (ctx.rom.ReadU8(pos) > bridge.playerInfo[player].maxTracks)
                throw std::runtime_error("runtime player track capacity");
            ctx.m4aMPlayStart(player, pos);
        } else ctx.m4aSongNumStart(song);
        bridge.fades[player] = {};
        bridge.phasePlays[player].pending = false;
    });
}
extern "C" int mp2k_bridge_play_at_tick(mp2k_bridge_handle handle, uint16_t song,
                                           uint8_t player, uint64_t first_tick_sample) {
    if (!handle) return -1;
    return call([&] {
        auto& bridge = *static_cast<Player*>(handle);
        auto& ctx = *bridge.context;
        if (song >= ctx.songTableInfo.count || player >= ctx.players.size() ||
            (!bridge.runtimePlayers && ctx.m4aSongNumPlayerGet(song) != player) ||
            first_tick_sample < bridge.renderedFrames)
            throw std::runtime_error("invalid first tick phase");
        if (bridge.runtimePlayers && ctx.rom.ReadU8(ctx.rom.ReadAgbPtrToPos(ctx.songTableInfo.pos + song * 8)) > bridge.playerInfo[player].maxTracks)
            throw std::runtime_error("runtime player track capacity");
        bridge.phasePlays[player] = {true, song, first_tick_sample};
    });
}
extern "C" int mp2k_bridge_stop(mp2k_bridge_handle handle, uint16_t song, uint8_t player) {
    if (!handle) return -1;
    return call([&] {
        auto& bridge = *static_cast<Player*>(handle);
        auto& ctx = *bridge.context;
        if (player >= ctx.players.size() || song >= ctx.songTableInfo.count || (!bridge.runtimePlayers && ctx.m4aSongNumPlayerGet(song) != player))
            throw std::runtime_error("song/player mismatch");
        if (bridge.runtimePlayers) ctx.m4aMPlayStop(player); else ctx.m4aSongNumStop(song);
        bridge.fades[player] = {};
        if (bridge.phasePlays[player].pending && bridge.phasePlays[player].song == song)
            bridge.phasePlays[player].pending = false;
    });
}
extern "C" int mp2k_bridge_fade_player(mp2k_bridge_handle handle, uint16_t song, uint8_t player,
                                          uint16_t speed) {
    if (!handle) return -1;
    return call([&] {
        auto& bridge = *static_cast<Player*>(handle);
        auto& ctx = *bridge.context;
        if (!speed || player >= ctx.players.size() || song >= ctx.songTableInfo.count ||
            (!bridge.runtimePlayers && ctx.m4aSongNumPlayerGet(song) != player) ||
            ctx.players[player].songHeaderPos != static_cast<size_t>(
                ctx.rom.ReadAgbPtrToPos(ctx.songTableInfo.pos + song * 8)))
            throw std::runtime_error("fade song/player mismatch");
        bridge.fades[player] = {256, speed, speed, 0, true};
    });
}
extern "C" int mp2k_bridge_render(mp2k_bridge_handle handle, int16_t* stereo, size_t frames) {
    if (!handle || frames > 65536 || (!stereo && frames)) return -1;
    return call([&] {
        auto& p = *static_cast<Player*>(handle);
        size_t out = 0;
        while (out < frames) {
            for (size_t phasePlayer = 0; phasePlayer < p.context->players.size() && phasePlayer < 32; ++phasePlayer) {
                auto& pending = p.phasePlays[phasePlayer];
                if (!pending.pending || p.renderedFrames != pending.tickSample) continue;
                if (p.runtimePlayers) p.context->m4aMPlayStart(static_cast<uint8_t>(phasePlayer),
                    p.rom.ReadAgbPtrToPos(p.context->songTableInfo.pos + pending.song * 8));
                else p.context->m4aSongNumStart(pending.song);
                p.fades[phasePlayer] = {};
                auto& player = p.context->players[phasePlayer];
                const double correction = p.context->mixer.GetBufferLengthSpeedCorrection();
                const uint64_t step = uint64_t(600) << 32;
                const uint64_t increment = static_cast<uint64_t>(
                    player.bpm * p.context->reader.GetSpeedFactor() * correction * double(uint64_t(1) << 32));
                player.tickProgress_32_32 = step - increment;
                bool playing[32] = {};
                for (size_t i = 0; i < p.context->players.size(); ++i) {
                    playing[i] = p.context->players[i].playing;
                    if (i != phasePlayer) p.context->players[i].playing = false;
                }
                const size_t beforeTick = player.tickCount;
                const uint64_t beforeProgress = player.tickProgress_32_32;
                const auto beforeBpm = player.bpm;
                const bool beforePlaying = player.playing, beforeFinished = player.finished;
                p.context->reader.Process();
                if (const char* tracePath = std::getenv("MGBA_MP2K_STARTUP_TRACE_PATH")) {
                    if (FILE* trace = std::fopen(tracePath, "a")) {
                        std::fprintf(trace, "song=%u player=%zu render=%llu requested=%llu header=%zu tick_before=%zu tick_after=%zu delta=%zu playing=%u/%u finished=%u/%u priority=%u bpm=%u/%u progress=%llu/%llu threshold=%llu increment=%llu correction=%.17g speed=%.9g\n",
                            pending.song, phasePlayer, (unsigned long long)p.renderedFrames,
                            (unsigned long long)pending.tickSample, player.songHeaderPos,
                            beforeTick, player.tickCount, player.tickCount - beforeTick,
                            beforePlaying, player.playing, beforeFinished, player.finished,
                            player.priority, beforeBpm, player.bpm,
                            (unsigned long long)beforeProgress, (unsigned long long)player.tickProgress_32_32,
                            (unsigned long long)step, (unsigned long long)increment, correction,
                            p.context->reader.GetSpeedFactor());
                        std::fclose(trace);
                    }
                }
                // Restore only players we suspended. Process may finish the
                // target; resurrecting playing leaves finished=true and can
                // make its stale priority reject the next song start.
                for (size_t i = 0; i < p.context->players.size(); ++i)
                    if (i != phasePlayer) p.context->players[i].playing = playing[i];
                if (player.tickCount != beforeTick + 1)
                    throw std::runtime_error("first sequencer tick did not fire");
                p.lastPhaseTickSample = p.renderedFrames;
                p.lastPhasePlayer = phasePlayer;
                p.skipNextReaderMask |= 1u << phasePlayer;
                pending.pending = false;
            }
            if (p.buffered >= p.context->masterAudioBuffer.size()) {
                p.lastSoundMainSample = p.renderedFrames;
                ++p.soundMainCalls;
                auto& entry = p.recentMicroframes[p.soundMainCalls % 16];
                const auto* player4 = p.context->players.size() > 4 ? &p.context->players[4] : nullptr;
                entry = {};
                entry.call_index = p.soundMainCalls;
                entry.audio_sample = p.renderedFrames;
                entry.player4_tick_before = player4 ? player4->tickCount : 0;
                entry.player4_track0_before = !player4 || player4->tracks.empty() ?
                    0 : static_cast<uint32_t>(player4->tracks[0].pos);
                entry.pcm_channels_before = static_cast<uint32_t>(p.context->sndChannels.size());
                entry.player4_playing_before = player4 && player4->playing;
                if (p.skipNextReaderMask) {
                    bool playing[32] = {};
                    for (size_t i = 0; i < p.context->players.size(); ++i) {
                        playing[i] = p.context->players[i].playing;
                        if (p.skipNextReaderMask & (1u << i)) p.context->players[i].playing = false;
                    }
                    p.context->m4aSoundMain();
                    for (size_t i = 0; i < p.context->players.size(); ++i)
                        if (p.skipNextReaderMask & (1u << i))
                            p.context->players[i].playing = playing[i];
                    p.skipNextReaderMask = 0;
                } else {
                    p.context->m4aSoundMain();
                }
                for (size_t player = 0; player < p.context->players.size() && player < 32; ++player) {
                    auto& fade = p.fades[player];
                    if (!fade.active && fade.volume == 256) continue;
                    const float reduction = static_cast<float>(256 - fade.volume) / 256.0f;
                    for (const auto& track : p.context->players[player].tracks) {
                        for (size_t i = 0; i < p.context->masterAudioBuffer.size(); ++i) {
                            p.context->masterAudioBuffer[i].left -= track.audioBuffer[i].left * reduction;
                            p.context->masterAudioBuffer[i].right -= track.audioBuffer[i].right * reduction;
                        }
                    }
                    if (fade.active && ++fade.microframe == 4) {
                        fade.microframe = 0;
                        if (--fade.framesLeft == 0) {
                            fade.framesLeft = fade.speed;
                            fade.volume = fade.volume >= 16 ? fade.volume - 16 : 0;
                            if (!fade.volume) {
                                fade.active = false;
                                p.context->players[player].playing = false;
                            }
                        }
                    }
                }
                entry.player4_tick_after = player4 ? player4->tickCount : 0;
                entry.player4_track0_after = !player4 || player4->tracks.empty() ?
                    0 : static_cast<uint32_t>(player4->tracks[0].pos);
                entry.pcm_channels_after = static_cast<uint32_t>(p.context->sndChannels.size());
                entry.player4_playing_after = player4 && player4->playing;
                p.buffered = 0;
            }
            const auto& block = p.context->masterAudioBuffer;
            size_t take = std::min(frames - out, block.size() - p.buffered);
            for (const auto& pending : p.phasePlays)
                if (pending.pending && p.renderedFrames < pending.tickSample)
                    take = std::min<size_t>(take, pending.tickSample - p.renderedFrames);
            for (size_t i = 0; i < take; ++i) {
                stereo[2 * (out + i)] = convert(block[p.buffered + i].left);
                stereo[2 * (out + i) + 1] = convert(block[p.buffered + i].right);
            }
            p.buffered += take;
            out += take;
            p.renderedFrames += take;
        }
    });
}
extern "C" int mp2k_bridge_get_timing(mp2k_bridge_handle handle, struct mp2k_bridge_timing* timing) {
    if (!handle || !timing) return -1;
    return call([&] {
        const auto& p = *static_cast<Player*>(handle);
        *timing = {p.renderedFrames, p.lastSoundMainSample, p.soundMainCalls,
            static_cast<uint32_t>(p.buffered),
            static_cast<uint32_t>(p.context->masterAudioBuffer.size()),
            p.lastPhaseTickSample, p.lastPhasePlayer};
    });
}
extern "C" int mp2k_bridge_get_fade_state(mp2k_bridge_handle handle, uint8_t player,
                                             struct mp2k_bridge_fade_state* state) {
    if (!handle || !state || player >= 32) return -1;
    return call([&] {
        const auto& fade = static_cast<Player*>(handle)->fades[player];
        *state = {fade.volume, fade.speed, static_cast<uint8_t>(fade.active)};
    });
}
extern "C" int mp2k_bridge_get_microframe(mp2k_bridge_handle handle, uint64_t call_index,
                                             struct mp2k_bridge_microframe* microframe) {
    if (!handle || !microframe || !call_index) return -1;
    return call([&] {
        const auto& p = *static_cast<Player*>(handle);
        const auto& stored = p.recentMicroframes[call_index % 16];
        if (stored.call_index != call_index) throw std::runtime_error("microframe history expired");
        *microframe = stored;
    });
}
extern "C" int mp2k_bridge_get_state(mp2k_bridge_handle handle, uint8_t player,
                                        struct mp2k_bridge_state* state) {
    if (!handle || !state) return -1;
    return call([&] {
        const auto& players = static_cast<Player*>(handle)->context->players;
        if (player >= players.size()) throw std::runtime_error("invalid player");
        const auto& p = players[player];
        std::memset(state, 0, sizeof(*state));
        state->frame_count = p.frameCount;
        state->tick_count = p.tickCount;
        state->song_header_offset = static_cast<uint32_t>(p.songHeaderPos);
        state->bpm = p.bpm;
        state->playing = p.playing;
        state->tracks_used = std::min<size_t>(p.tracksUsed, 16);
        for (unsigned i = 0; i < state->tracks_used; ++i)
            state->track_positions[i] = static_cast<uint32_t>(p.tracks[i].pos);
    });
}
extern "C" int mp2k_bridge_get_mode(mp2k_bridge_handle handle, struct mp2k_bridge_mode* mode) {
    if (!handle || !mode) return -1;
    return call([&] {
        const auto& settings = static_cast<Player*>(handle)->context->mp2kSoundMode;
        *mode = {settings.vol, settings.rev, settings.freq, settings.maxChannels, settings.dacConfig};
    });
}
extern "C" const char* mp2k_bridge_error(void) { return lastError.c_str(); }

extern "C" uint32_t mp2k_bridge_rebind_version(void) { return 1; }
extern "C" int mp2k_bridge_rebind_begin(mp2k_bridge_handle handle,
    const mp2k_bridge_rebind_player* states, size_t count) {
    if (!handle || count > 32 || (count && !states)) return -1;
    return call([&] {
        auto& p = *static_cast<Player*>(handle);
        if (p.renderedFrames || p.rebindContext || !p.rebindStates.empty())
            throw std::runtime_error("rebind requires a fresh renderer");
        uint32_t mask = 0;
        for (size_t i = 0; i < count; ++i) {
            const auto& s = states[i];
            if (s.player >= p.context->players.size() || (mask & (1u << s.player)) ||
                s.song >= p.context->songTableInfo.count ||
                (!p.runtimePlayers && p.context->m4aSongNumPlayerGet(s.song) != s.player) ||
                s.song_header_offset != p.rom.ReadAgbPtrToPos(p.context->songTableInfo.pos + s.song * 8) ||
                s.tracks > p.playerInfo[s.player].maxTracks || s.tracks > 16 ||
                !s.playing || !s.bpm || s.bpm > 1024 || s.tempo_counter >= 150 ||
                s.fade_volume > 256 || (s.fade_interval && (!s.fade_counter || s.fade_counter > s.fade_interval)))
                throw std::runtime_error("invalid rebind player snapshot");
            mask |= 1u << s.player;
            // MODT is an unsigned native/sequence byte. Normal TrackMain
            // accepts it unchanged; reconstruction must preserve it too.
            for (size_t j = 0; j < s.tracks; ++j) {
                const auto& t = s.track[j];
                if (t.pattern_level > 3 || (t.enabled && (t.position >= p.rom.Size() ||
                    (t.running_status && t.running_status < 0xBD))))
                    throw std::runtime_error("invalid rebind track snapshot");
                for (size_t k = 0; k < t.pattern_level; ++k)
                    if (t.pattern_stack[k] >= p.rom.Size())
                        throw std::runtime_error("invalid rebind pattern stack");
            }
        }
        if (count) p.rebindStates.assign(states, states + count);
    });
}

extern "C" int mp2k_bridge_rebind_step(mp2k_bridge_handle handle, uint32_t budget) {
    if (!handle || !budget || budget > 4096) return -1;
    int status = 1;
    int result = call([&] {
        auto& p = *static_cast<Player*>(handle);
        while (p.rebindIndex < p.rebindStates.size()) {
            const auto& s = p.rebindStates[p.rebindIndex];
            if (s.clock > 1048576 || p.rebindWork > 16777216) { status = 2; return; }
            if (!p.rebindContext) {
                auto& ctx = *p.context;
                p.rebindContext = std::make_unique<MP2KContext>(ctx.sampleRate, -1, p.rom,
                    ctx.mp2kSoundMode, ctx.agbplaySoundMode, ctx.songTableInfo, p.playerInfo);
                auto& seek = *p.rebindContext;
                if (p.runtimePlayers) seek.m4aMPlayStart(s.player, s.song_header_offset);
                else seek.m4aSongNumStart(s.song);
                seek.memaccArea.assign(s.memory, s.memory + 256);
                // Align the first tick exactly as normal bridge startup does.
                auto& target = seek.players[s.player];
                target.tickProgress_32_32 = (uint64_t(600) << 32) - static_cast<uint64_t>(
                    target.bpm * seek.reader.GetSpeedFactor() * seek.mixer.GetBufferLengthSpeedCorrection() * double(uint64_t(1) << 32));
            }
            auto& seek = *p.rebindContext;
            auto& source = seek.players[s.player];
            // Native clock can trail the first processed tick by one. Select
            // the candidate by command position/wait/running status, not time.
            auto matches = [&] {
                for (size_t j = 0; j < s.tracks; ++j) {
                    const auto& a = source.tracks[j]; const auto& b = s.track[j];
                    if (a.enabled != bool(b.enabled)) return false;
                    if (!b.enabled) continue;
                    if (a.pos != b.position || a.delay != b.wait || a.lastCmd != b.running_status ||
                        a.patternLevel != b.pattern_level) return false;
                    for (size_t k = 0; k < b.pattern_level; ++k)
                        if (a.returnPos[k] != b.pattern_stack[k]) return false;
                }
                return true;
            };
            while (source.tickCount < uint64_t(s.clock) + 1 ||
                   (source.tickCount == uint64_t(s.clock) + 1 && !matches())) {
                if (source.tickCount >= s.clock && matches()) break;
                if (!source.playing || source.tickCount > uint64_t(s.clock) + 1) { status = 2; return; }
                if (!budget) return;
                --budget;
                seek.m4aSoundMain(); ++p.rebindWork;
            }
            if (!matches()) { status = 2; return; }
            auto& dest = p.context->players[s.player];
            // Track storage and channel lists move together. Channel pointers
            // continue to reference the same track allocations after swap.
            dest.tracks.swap(source.tracks);
            dest.songHeaderPos = source.songHeaderPos; dest.bankPos = source.bankPos;
            dest.tracksUsed = source.tracksUsed; dest.reverb = source.reverb;
            dest.tickCount = source.tickCount; dest.frameCount = source.frameCount;
            dest.interframeCount = source.interframeCount;
            dest.bpm = s.bpm; dest.priority = s.priority; dest.playing = true; dest.finished = false;
            dest.tickProgress_32_32 = uint64_t(s.tempo_counter) * 4 * (uint64_t(1) << 32);
            for (size_t j = 0; j < s.tracks; ++j) {
                auto& t = dest.tracks[j]; const auto* c = s.track[j].controls;
                if (!t.enabled) continue;
                t.reptCount = c[3]; t.lastNoteLen = c[4];
                t.lastNoteKey = c[5]; t.lastNoteVel = c[6];
                t.keyShift = int8_t(c[10]) + int8_t(c[11]);
                t.tune = int8_t(c[12]); t.bend = int8_t(c[14]); t.bendr = c[15];
                t.vol = c[18]; t.pan = int8_t(c[20]) + int8_t(c[21]);
                t.mod = c[23]; t.modt = static_cast<MODT>(c[24]);
                t.lfoValue = int8_t(c[22]);
                t.lfos = c[25]; t.lfoPhase = c[26];
                t.lfodl = c[27]; t.lfodlCount = c[28]; t.priority = c[29];
                t.pseudoEchoVol = c[30]; t.pseudoEchoLen = c[31];
                t.updateVolume = t.updatePitch = true;
            }
            auto& ctx = *p.context;
            ctx.sndChannels.splice(ctx.sndChannels.end(), seek.sndChannels);
            ctx.sq1Channels.splice(ctx.sq1Channels.end(), seek.sq1Channels);
            ctx.sq2Channels.splice(ctx.sq2Channels.end(), seek.sq2Channels);
            ctx.waveChannels.splice(ctx.waveChannels.end(), seek.waveChannels);
            ctx.noiseChannels.splice(ctx.noiseChannels.end(), seek.noiseChannels);
            ctx.memaccArea.assign(s.memory, s.memory + 256);
            if (s.fade_interval)
                p.fades[s.player] = {s.fade_volume, s.fade_interval, s.fade_counter, 0, true};
            p.rebindChannelContexts.push_back(std::move(p.rebindContext)); ++p.rebindIndex;
        }
        p.context->reader.Restart();
        p.context->mixer.ResetFade();
        p.context->masterAudioBuffer.clear(); p.buffered = 0;
        status = 0;
    });
    return result ? result : status;
}

extern "C" uint32_t mp2k_bridge_runtime_players_version(void) { return 1; }
extern "C" int mp2k_bridge_set_runtime_players(mp2k_bridge_handle handle, uint8_t enabled) {
    if (!handle || enabled > 1) return -1;
    return call([&] {
        auto& p = *static_cast<Player*>(handle);
        if (p.renderedFrames || !p.rebindStates.empty() ||
            std::any_of(p.context->players.begin(), p.context->players.end(), [](const auto& player) { return player.playing; }) ||
            std::any_of(std::begin(p.phasePlays), std::end(p.phasePlays), [](const auto& phase) { return phase.pending; }))
            throw std::runtime_error("runtime routing requires a fresh renderer");
        p.runtimePlayers = enabled != 0;
    });
}
