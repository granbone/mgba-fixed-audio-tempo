"""Private instrumented source model for MP2K PSG allocation. No guard bypass.
SPDX-License-Identifier: MPL-2.0. Production uses no runtime priority override.
"""
import argparse, shutil
from pathlib import Path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--agbplay-source', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--voice-generations', action='store_true', help='Private all-channel request/voice IDs; no lifetime policy change')
    a = p.parse_args(); a.output.mkdir(parents=True, exist_ok=False)
    for name in ('src', 'CMakeLists.txt', 'LICENSE', 'README.md'):
        src = a.agbplay_source / name; dest = a.output / 'agbplay' / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        if src.is_dir(): shutil.copytree(src, dest)
        elif src.exists(): shutil.copy2(src, dest)
    repo = Path(__file__).resolve().parents[2]
    shutil.copytree(repo / 'tools/mp2k-audio-trace/bridge', a.output / 'bridge')
    (a.output / 'bridge/phase4_trace.hpp').write_text('#pragma once\n#include <cstdint>\ninline thread_local uint64_t phase4Sample = 0;\n')
    bridge = a.output / 'bridge/bridge.cpp'; code = bridge.read_text()
    needle = 'p.lastSoundMainSample = p.renderedFrames;'
    assert code.count(needle) == 1
    code = code.replace(needle, needle + '\n                phase4Sample = p.renderedFrames;')
    code = code.replace('p.context->reader.Process();', 'phase4Sample = p.renderedFrames;\n                p.context->reader.Process();')
    bridge.write_text('#include "phase4_trace.hpp"\n' + code)
    reader = a.output / 'agbplay/src/agbplay/SequenceReader.cpp'
    text = reader.read_text()
    needle = '    note.priority = trk.priority;'
    assert text.count(needle) == 1
    # The copied integration CMake replaces the first assignment with combined
    # priority. Override ONLY in this private diagnostic, after that assignment.
    text = text.replace(needle, needle + '''
    const char* policy = std::getenv("MGBA_PHASE4_LEGACY_PRIORITY");
    if (policy) note.priority = static_cast<uint8_t>(unsigned(trk.priority));
''')
    begin = text.index('    auto cgbPolyphonySuppressFunc =')
    finish = text.index('\n    const MP2KChn *chn', begin)
    old = text[begin:finish]
    trace = r'''
        unsigned lane = 0;
        if (static_cast<const void*>(&channels) == &ctx.sq1Channels) lane = 1;
        if (static_cast<const void*>(&channels) == &ctx.sq2Channels) lane = 2;
        if (static_cast<const void*>(&channels) == &ctx.waveChannels) lane = 3;
        if (static_cast<const void*>(&channels) == &ctx.noiseChannels) lane = 4;
        auto emit = [&](const char* decision, const Note* previous) {
            if (const char* path = std::getenv("MGBA_PHASE4_PSG_TRACE")) {
                if (FILE* f = std::fopen(path, "a")) {
                    std::fprintf(f, "sample=%llu tick=%zu header=%zu player=%u track=%u key=%u playerPriority=%u trackPriority=%u priority=%u ch=%u voices=%zu decision=%s oldPlayer=%d oldTrack=%d oldKey=%d oldPriority=%d\n",
                        (unsigned long long)phase4Sample, player.tickCount, player.songHeaderPos, note.playerIdx, note.trackIdx,
                        note.midiKeyPitch, player.priority, trk.priority, note.priority, lane,
                        channels.size(), decision, previous ? int(previous->playerIdx) : -1,
                        previous ? int(previous->trackIdx) : -1, previous ? int(previous->midiKeyPitch) : -1,
                        previous ? int(previous->priority) : -1);
                    std::fclose(f);
                }
            }
        };
        emit("REQUEST", nullptr);
'''
    old = old.replace("// return 'true'", trace + "        // return 'true'", 1)
    old = old.replace('if (playing_note.priority > note.priority)\n                        return false;',
                      'if (playing_note.priority > note.priority) { emit("REJECT_HIGHER", &playing_note); return false; }')
    old = old.replace('if (channels.front().track < &trk)\n                            return false;',
                      'if (channels.front().track < &trk) { emit("REJECT_TIE", &playing_note); return false; }')
    old = old.replace('if (chn.track < &trk)\n                            return false;',
                      'if (chn.track < &trk) { emit("REJECT_TIE", &playing_note); return false; }')
    old = old.replace('            channels.clear();', '''            if (!channels.empty()) emit(channels.front().IsReleasing() ? "REPLACE_RELEASE" : "STEAL", &channels.front().note);
            channels.clear();''')
    old = old.replace('        return true;', '        emit("ACCEPT", nullptr);\n        return true;')
    assert old.count('REJECT_HIGHER') == 2 and old.count('REJECT_TIE') == 2
    text = '#include "phase4_trace.hpp"\n#include <cstdio>\n#include <cstdlib>\n' + text[:begin] + old + text[finish:]
    reader.write_text(text)
    # End of a voice's lifetime, including immediate stealing and normal release.
    chn = a.output / 'agbplay/src/agbplay/MP2KChn.cpp'; text = chn.read_text()
    text = '#include "phase4_trace.hpp"\n#include <cstdio>\n#include <cstdlib>\n' + text
    needle = 'MP2KChn::~MP2KChn()\n{'
    assert text.count(needle) == 1
    text = text.replace(needle, needle + r'''
    if (const char* path = std::getenv("MGBA_PHASE4_PSG_TRACE")) {
        if (FILE* f = std::fopen(path, "a")) {
            std::fprintf(f, "sample=%llu decision=END player=%u track=%u key=%u priority=%u state=%d stop=%u\n",
                (unsigned long long)phase4Sample, note.playerIdx, note.trackIdx, note.midiKeyPitch, note.priority, int(envState), stop);
            std::fclose(f);
        }
    }
''')
    chn.write_text(text)
    if a.voice_generations:
        header=a.output/'bridge/phase4_trace.hpp'
        header.write_text(header.read_text()+'inline thread_local uint64_t phase8VoiceSequence = 0;\n')
        voice_header=a.output/'agbplay/src/agbplay/MP2KChn.hpp'
        text=voice_header.read_text();needle='    Note note;';assert text.count(needle)==1
        voice_header.write_text(text.replace(needle,'    uint64_t phase8VoiceId = 0;\n'+needle))
        text=chn.read_text();needle='    this->track = track;';assert text.count(needle)==1
        text=text.replace(needle,r'''
    phase8VoiceId = ++phase8VoiceSequence;
    if (const char* path = std::getenv("MGBA_PHASE8_VOICE_TRACE")) {
        if (FILE* f = std::fopen(path, "a")) {
            std::fprintf(f, "sample=%llu kind=VOICE_BEGIN id=%llu player=%u track=%u key=%u priority=%u length=%u\n",
                (unsigned long long)phase4Sample, (unsigned long long)phase8VoiceId,
                note.playerIdx, note.trackIdx, note.midiKeyPitch, note.priority, note.length);
            std::fclose(f);
        }
    }
'''+needle)
        needle='MP2KChn::~MP2KChn()\n{';assert text.count(needle)==1
        text=text.replace(needle,needle+r'''
    if (const char* path = std::getenv("MGBA_PHASE8_VOICE_TRACE")) {
        if (FILE* f = std::fopen(path, "a")) {
            std::fprintf(f, "sample=%llu kind=VOICE_END id=%llu player=%u track=%u key=%u freq=%.9g state=%d stop=%u reason=UNKNOWN\n",
                (unsigned long long)phase4Sample, (unsigned long long)phase8VoiceId,
                note.playerIdx, note.trackIdx, note.midiKeyPitch, double(freq), int(envState), stop);
            std::fclose(f);
        }
    }
''');chn.write_text(text)
        text=reader.read_text();needle='    const MP2KChn *chn = nullptr;';assert text.count(needle)==1
        text=text.replace(needle,r'''
    if (const char* path = std::getenv("MGBA_PHASE8_VOICE_TRACE")) {
        if (FILE* f = std::fopen(path, "a")) {
            std::fprintf(f, "sample=%llu kind=NOTE_REQUEST player=%u track=%u key=%u priority=%u length=%u header=%zu tick=%zu\n",
                (unsigned long long)phase4Sample, note.playerIdx, note.trackIdx,
                note.midiKeyPitch, note.priority, note.length, player.songHeaderPos, player.tickCount);
            std::fclose(f);
        }
    }
'''+needle);reader.write_text(text)
    print('Prepared private diagnostic:', a.output)


if __name__ == '__main__': main()
