/*
 * Copyright 2026 AIASK Limited (Green Light Audio)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * Stream reservation sizes and bandwidth for pro-audio AVB streams.
 *
 * Implements the rules of Milan Specification v1.3, clause 4.3.3.2 and
 * Table 4.4: the Tspec a Talker declares in MSRP, and the bandwidth used to
 * configure the credit-based shaper. Requirement IDs: BW-001 to BW-004 in
 * docs/requirements/matrix.csv.
 */

#ifndef PAAD_BANDWIDTH_H
#define PAAD_BANDWIDTH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Size of the AVTPDU common stream header used by AAF and CRF, in bytes. */
#define PAAD_AVTPDU_HEADER_SIZE 24U

/** Measurement intervals per second for SR class A (125 us). */
#define PAAD_CLASS_A_INTERVALS_PER_SECOND 8000U

/**
 * Tspec MaxFrameSize for an AAF PCM stream: AVTPDU header plus payload.
 * No allowance for clock drift is included here (Milan v1.3 moved it into
 * the bandwidth computation; v1.2 added it to MaxFrameSize).
 */
uint32_t paad_aaf_max_frame_size(unsigned int channels, unsigned int bytes_per_sample,
				 unsigned int samples_per_frame);

/** Tspec MaxFrameSize for a CRF stream carrying one timestamp per PDU. */
uint32_t paad_crf_max_frame_size(unsigned int timestamps_per_pdu);

/**
 * Bandwidth, in bit/s, occupied on the wire by a stream with the given
 * Tspec, including Ethernet header, VLAN tag, FCS, minimum frame size, one
 * byte of clock-drift allowance, preamble and inter-packet gap.
 */
uint64_t paad_stream_bandwidth_bps(uint32_t max_frame_size, uint32_t max_interval_frames,
				   uint32_t intervals_per_second);

#ifdef __cplusplus
}
#endif

#endif /* PAAD_BANDWIDTH_H */
