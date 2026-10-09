/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MGBA_FIXED_AUDIO_MODE_H
#define MGBA_FIXED_AUDIO_MODE_H
#include <string.h>
enum FixedAudioMode {
	FIXED_AUDIO_DISABLED,
	FIXED_AUDIO_CONSERVATIVE,
	FIXED_AUDIO_EXPERIMENTAL
};
static enum FixedAudioMode fixedAudioResolveMode(const char* option,
	const char* legacyOption, const char* legacyEnable, const char* legacyAll) {
	/* Preserve every explicit OFF, including old launchers/configurations. */
	if ((option && (!strcmp(option, "disabled") || !strcmp(option, "Disabled"))) ||
	    (legacyOption && (!strcmp(legacyOption, "OFF") || !strcmp(legacyOption, "disabled"))) ||
	    (legacyEnable && !strcmp(legacyEnable, "0"))) return FIXED_AUDIO_DISABLED;
	if (option && !strcmp(option, "conservative")) return FIXED_AUDIO_CONSERVATIVE;
	if (option && !strcmp(option, "experimental")) return FIXED_AUDIO_EXPERIMENTAL;
	if (legacyAll && !strcmp(legacyAll, "1")) return FIXED_AUDIO_EXPERIMENTAL;
	if (legacyEnable && !strcmp(legacyEnable, "1")) return FIXED_AUDIO_CONSERVATIVE;
	return FIXED_AUDIO_EXPERIMENTAL;
}
#endif
