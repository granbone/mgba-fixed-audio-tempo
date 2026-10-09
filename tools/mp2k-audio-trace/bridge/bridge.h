/* Copyright (c) 2026 granbone
 * SPDX-License-Identifier: LGPL-3.0-only
 * See LICENSE and COPYING.GPL-3.0.txt in this directory. */
#ifndef MGBA_MP2K_BRIDGE_H
#define MGBA_MP2K_BRIDGE_H
#include <stddef.h>
#include <stdint.h>
#ifdef _WIN32
#define MP2K_BRIDGE_API __declspec(dllexport)
#else
#define MP2K_BRIDGE_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef void* mp2k_bridge_handle;
/* Optional rebind capability v1; does not change the existing ABI v2 structs.
 * ROM offsets only, never host or emulated pointers. No old renderer state. */
struct mp2k_bridge_rebind_track {
    uint32_t position;
    uint32_t pattern_stack[3];
    uint8_t enabled, wait, running_status, pattern_level;
    uint8_t controls[32]; /* MP2K track bytes 0..31, copied from loaded RAM. */
};
struct mp2k_bridge_rebind_player {
    uint32_t song_header_offset, clock;
    uint16_t song, bpm, tempo_counter, fade_interval, fade_counter, fade_volume;
    uint8_t player, tracks, priority, playing;
    struct mp2k_bridge_rebind_track track[16];
    uint8_t memory[256];
};
MP2K_BRIDGE_API uint32_t mp2k_bridge_rebind_version(void);
/* begin: 0 success, -1 invalid/fatal; step: 1 pending, 0 ready,
 * 2 position cannot be reconstructed (native continues), -1 fatal. */
MP2K_BRIDGE_API int mp2k_bridge_rebind_begin(mp2k_bridge_handle,
    const struct mp2k_bridge_rebind_player*, size_t count);
MP2K_BRIDGE_API int mp2k_bridge_rebind_step(mp2k_bridge_handle, uint32_t microframes);
/* Additive capability: core has validated actual MPlayStart r0/r1. The
 * supplied slot is authoritative; all song/header/capacity guards remain. */
MP2K_BRIDGE_API uint32_t mp2k_bridge_runtime_players_version(void);
MP2K_BRIDGE_API int mp2k_bridge_set_runtime_players(mp2k_bridge_handle, uint8_t enabled);
MP2K_BRIDGE_API uint32_t mp2k_bridge_api_version(void);
/* ABI v2 scanner evidence. The core validates it before experimental output. */
struct mp2k_bridge_scan_result {
    uint32_t song_table_offset;
    uint32_t player_table_offset; /* UINT32_MAX when the adjacent reference is ambiguous */
    uint32_t sound_mode;
    uint16_t song_count;
    uint8_t player_count;
    uint8_t table_index;
    uint8_t player_table_reference_valid;
    uint8_t driver_variant; /* 0: scanner does not identify the code variant */
};
struct mp2k_bridge_state {
    uint64_t frame_count;
    uint64_t tick_count;
    uint32_t song_header_offset;
    uint16_t bpm;
    uint8_t playing;
    uint8_t tracks_used;
    uint32_t track_positions[16]; /* ROM offsets, or 0 for inactive tracks */
};
struct mp2k_bridge_mode {
    uint8_t volume;
    uint8_t reverb;
    uint8_t frequency;
    uint8_t max_channels;
    uint8_t dac_config;
};
struct mp2k_bridge_timing {
    uint64_t rendered_frames;
    uint64_t last_sound_main_sample;
    uint64_t sound_main_calls;
    uint32_t buffered;
    uint32_t block_frames;
    uint64_t last_phase_tick_sample;
    uint8_t last_phase_player;
};
struct mp2k_bridge_microframe {
    uint64_t call_index;
    uint64_t audio_sample;
    uint64_t player4_tick_before;
    uint64_t player4_tick_after;
    uint32_t player4_track0_before;
    uint32_t player4_track0_after;
    uint32_t pcm_channels_before;
    uint32_t pcm_channels_after;
    uint8_t player4_playing_before;
    uint8_t player4_playing_after;
};
struct mp2k_bridge_fade_state {
    uint16_t volume;
    uint16_t speed;
    uint8_t active;
};
MP2K_BRIDGE_API mp2k_bridge_handle mp2k_bridge_create(const uint8_t* rom, size_t size, uint32_t rate);
MP2K_BRIDGE_API int mp2k_bridge_scan(const uint8_t* rom, size_t size,
                                     struct mp2k_bridge_scan_result* results,
                                     size_t capacity, size_t* count);
MP2K_BRIDGE_API void mp2k_bridge_destroy(mp2k_bridge_handle handle);
MP2K_BRIDGE_API int mp2k_bridge_play(mp2k_bridge_handle handle, uint16_t song, uint8_t player);
MP2K_BRIDGE_API int mp2k_bridge_play_at_tick(mp2k_bridge_handle handle, uint16_t song,
                                              uint8_t player, uint64_t first_tick_sample);
MP2K_BRIDGE_API int mp2k_bridge_stop(mp2k_bridge_handle handle, uint16_t song, uint8_t player);
MP2K_BRIDGE_API int mp2k_bridge_fade_player(mp2k_bridge_handle handle, uint16_t song, uint8_t player,
                                             uint16_t speed);
MP2K_BRIDGE_API int mp2k_bridge_render(mp2k_bridge_handle handle, int16_t* stereo, size_t frames);
MP2K_BRIDGE_API int mp2k_bridge_get_state(mp2k_bridge_handle handle, uint8_t player,
                                           struct mp2k_bridge_state* state);
MP2K_BRIDGE_API int mp2k_bridge_get_mode(mp2k_bridge_handle handle, struct mp2k_bridge_mode* mode);
MP2K_BRIDGE_API int mp2k_bridge_get_timing(mp2k_bridge_handle handle, struct mp2k_bridge_timing* timing);
MP2K_BRIDGE_API int mp2k_bridge_get_fade_state(mp2k_bridge_handle handle, uint8_t player,
                                                struct mp2k_bridge_fade_state* state);
MP2K_BRIDGE_API int mp2k_bridge_get_microframe(mp2k_bridge_handle handle, uint64_t call_index,
                                                struct mp2k_bridge_microframe* microframe);
MP2K_BRIDGE_API const char* mp2k_bridge_error(void);
#ifdef __cplusplus
}
#endif
#endif
