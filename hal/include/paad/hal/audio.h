/*
 * Copyright 2026 AIASK Limited (Green Light Audio)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * HAL service: audio input and output.
 *
 * Sample transfer between the converters (codec, ADC/DAC over I2S/TDM) and
 * memory, period by period, clocked by the media clock. Samples are
 * interleaved 32-bit signed integers, left-justified: a 24-bit converter
 * fills the 24 most significant bits.
 */

#ifndef PAAD_HAL_AUDIO_H
#define PAAD_HAL_AUDIO_H

#include "paad/hal/common.h"
#include "paad/hal/mclk.h"

#ifdef __cplusplus
extern "C" {
#endif

enum paad_hal_audio_dir {
	PAAD_HAL_AUDIO_CAPTURE = 0,  /**< converters to memory (device inputs) */
	PAAD_HAL_AUDIO_PLAYBACK = 1, /**< memory to converters (device outputs) */
};

struct paad_hal_audio_config {
	uint32_t sample_rate;   /**< must match the media clock's sample rate */
	uint8_t channels;
	uint16_t period_frames; /**< frames per callback, e.g. 48 for 1 ms at 48 kHz */
};

struct paad_hal_audio;

/**
 * Called once per period and direction, in interrupt or high-priority task
 * context. CAPTURE: samples hold the frames just recorded. PLAYBACK: the
 * callback fills samples with the next frames to play. mclk_ticks is the
 * media clock counter at the first frame of the period.
 */
typedef void (*paad_hal_audio_cb)(struct paad_hal_audio *audio, enum paad_hal_audio_dir dir,
				  int32_t *samples, uint16_t frames, uint64_t mclk_ticks, void *arg);

struct paad_hal_audio_ops {
	/** Required. */
	int (*configure)(struct paad_hal_audio *audio, enum paad_hal_audio_dir dir,
			 const struct paad_hal_audio_config *config);
	/** Required. */
	int (*start)(struct paad_hal_audio *audio, enum paad_hal_audio_dir dir);
	/** Required. */
	int (*stop)(struct paad_hal_audio *audio, enum paad_hal_audio_dir dir);
};

struct paad_hal_audio {
	const struct paad_hal_audio_ops *ops;
	void *priv;
	const char *name;
	struct paad_hal_mclk *mclk;     /**< media clock driving the converters */
	uint8_t max_channels[2];        /**< per direction, indexed by paad_hal_audio_dir */
	paad_hal_audio_cb callback;     /**< set by the core before start() */
	void *callback_arg;
};

static inline int paad_hal_audio_configure(struct paad_hal_audio *audio, enum paad_hal_audio_dir dir,
					   const struct paad_hal_audio_config *config)
{
	return audio->ops->configure(audio, dir, config);
}

static inline int paad_hal_audio_start(struct paad_hal_audio *audio, enum paad_hal_audio_dir dir)
{
	return audio->ops->start(audio, dir);
}

static inline int paad_hal_audio_stop(struct paad_hal_audio *audio, enum paad_hal_audio_dir dir)
{
	return audio->ops->stop(audio, dir);
}

#ifdef __cplusplus
}
#endif

#endif /* PAAD_HAL_AUDIO_H */
