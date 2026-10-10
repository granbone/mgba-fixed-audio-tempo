/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MGBA_FIXED_AUDIO_RATE_H
#define MGBA_FIXED_AUDIO_RATE_H
#include "fixed_audio_mode.h"
#include <math.h>
#include <stdbool.h>

/* Keep the v0.3 1x/2x windows verbatim. 3x is a bounded Experimental
 * trial, never inferred from GET_FASTFORWARDING or an unlimited rate. */
static unsigned fixedAudioSupportedSpeed(enum FixedAudioMode mode, bool known,
	bool normalThrottle, bool fastForwardThrottle, double nominal, double rate) {
	if (mode == FIXED_AUDIO_DISABLED || !known || !isfinite(nominal) ||
	    nominal <= 0 || !isfinite(rate) || rate <= 0) return 0;
	if (normalThrottle && rate > nominal * .98 && rate < nominal * 1.02) return 1;
	if (!fastForwardThrottle) return 0;
	if (rate > nominal * 1.98 && rate < nominal * 2.02) return 2;
	if (mode == FIXED_AUDIO_EXPERIMENTAL &&
	    rate > nominal * 2.98 && rate < nominal * 3.02) return 3;
	return 0;
}
#endif
