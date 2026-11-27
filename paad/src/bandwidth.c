/*
 * Copyright 2026 AIASK Limited, trading as Green Light Audio
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "paad/bandwidth.h"

/* Ethernet framing around an AVTPDU, in bytes. */
#define ETH_HEADER_WITH_VLAN 18U /* destination, source, 802.1Q tag, EtherType */
#define ETH_FCS              4U
#define ETH_MIN_FRAME        68U /* minimum frame including VLAN tag and FCS */
#define ETH_PREAMBLE_SFD     8U
#define ETH_INTER_PACKET_GAP 12U
#define CLOCK_DRIFT_ALLOWANCE 1U /* sampling clock may run slightly fast */

#define CRF_TIMESTAMP_SIZE 8U

uint32_t paad_aaf_max_frame_size(unsigned int channels, unsigned int bytes_per_sample,
				 unsigned int samples_per_frame)
{
	return PAAD_AVTPDU_HEADER_SIZE + channels * bytes_per_sample * samples_per_frame;
}

uint32_t paad_crf_max_frame_size(unsigned int timestamps_per_pdu)
{
	/* CRF header is 20 bytes, followed by 64-bit timestamps. */
	return 20U + timestamps_per_pdu * CRF_TIMESTAMP_SIZE;
}

uint64_t paad_stream_bandwidth_bps(uint32_t max_frame_size, uint32_t max_interval_frames,
				   uint32_t intervals_per_second)
{
	uint64_t frame = (uint64_t)max_frame_size + ETH_HEADER_WITH_VLAN + ETH_FCS;

	if (frame < ETH_MIN_FRAME)
		frame = ETH_MIN_FRAME;

	frame += CLOCK_DRIFT_ALLOWANCE;
	frame += ETH_PREAMBLE_SFD + ETH_INTER_PACKET_GAP;

	return frame * max_interval_frames * intervals_per_second * 8U;
}
