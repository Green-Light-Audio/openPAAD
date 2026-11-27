/*
 * Copyright 2026 AIASK Limited, trading as Green Light Audio
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Host network interface through libpcap: BPF on macOS, packet sockets on
 * Linux. libpcap restores VLAN tags that Linux strips on receive, and gives
 * the time the kernel received each frame, which is far better than the
 * time the simulation gets round to reading it.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <time.h>
#include <unistd.h>

#include <pcap/pcap.h>

#include "paad/hal/common.h"
#include "paad/sim/clock.h"

#include "host_if.h"

struct paad_sim_host_if {
	pcap_t *pcap;
	char name[IFNAMSIZ];
	bool nano; /* timestamps in ns (else us) */
	int ioctl_fd;
};

int host_if_open(const char *ifname, struct paad_sim_host_if **out, char *err, size_t err_len)
{
	char errbuf[PCAP_ERRBUF_SIZE] = "";
	struct paad_sim_host_if *h;
	int rc;

	if (strlen(ifname) >= IFNAMSIZ) {
		snprintf(err, err_len, "interface name too long");
		return PAAD_HAL_ERR_INVALID;
	}

	h = calloc(1, sizeof(*h));
	if (!h) {
		snprintf(err, err_len, "out of memory");
		return PAAD_HAL_ERR_NO_MEMORY;
	}
	strncpy(h->name, ifname, sizeof(h->name) - 1);
	h->ioctl_fd = -1;

	h->pcap = pcap_create(ifname, errbuf);
	if (!h->pcap) {
		snprintf(err, err_len, "%s", errbuf);
		free(h);
		return PAAD_HAL_ERR_IO;
	}

	pcap_set_snaplen(h->pcap, 2048);
	pcap_set_promisc(h->pcap, 1);
	pcap_set_immediate_mode(h->pcap, 1);
	pcap_set_timeout(h->pcap, 1);
	(void)pcap_set_tstamp_precision(h->pcap, PCAP_TSTAMP_PRECISION_NANO); /* not everywhere */

	rc = pcap_activate(h->pcap);
	if (rc < 0) {
		if (rc == PCAP_ERROR_PERM_DENIED)
			snprintf(err, err_len, "%s: permission denied (needs root, or capture rights on the interface)",
				 ifname);
		else if (rc == PCAP_ERROR_NO_SUCH_DEVICE)
			snprintf(err, err_len, "%s: no such interface", ifname);
		else
			snprintf(err, err_len, "%s: %s", ifname, pcap_geterr(h->pcap));
		pcap_close(h->pcap);
		free(h);
		return rc == PCAP_ERROR_NO_SUCH_DEVICE ? PAAD_HAL_ERR_INVALID : PAAD_HAL_ERR_IO;
	}

	if (pcap_datalink(h->pcap) != DLT_EN10MB) {
		snprintf(err, err_len, "%s: not an Ethernet interface", ifname);
		pcap_close(h->pcap);
		free(h);
		return PAAD_HAL_ERR_INVALID;
	}

	if (pcap_setnonblock(h->pcap, 1, errbuf) < 0) {
		snprintf(err, err_len, "%s: %s", ifname, errbuf);
		pcap_close(h->pcap);
		free(h);
		return PAAD_HAL_ERR_IO;
	}

	h->nano = pcap_get_tstamp_precision(h->pcap) == PCAP_TSTAMP_PRECISION_NANO;
	h->ioctl_fd = socket(AF_INET, SOCK_DGRAM, 0);

	*out = h;

	return PAAD_HAL_OK;
}

void host_if_close(struct paad_sim_host_if *h)
{
	if (!h)
		return;

	pcap_close(h->pcap);
	if (h->ioctl_fd >= 0)
		close(h->ioctl_fd);
	free(h);
}

void host_if_filter(struct paad_sim_host_if *h, const uint8_t addr[6])
{
	struct bpf_program prog;
	char expr[128];

	snprintf(expr, sizeof(expr),
		 "(ether multicast or ether dst %02x:%02x:%02x:%02x:%02x:%02x) and not ether src "
		 "%02x:%02x:%02x:%02x:%02x:%02x",
		 addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], addr[0], addr[1], addr[2], addr[3], addr[4],
		 addr[5]);

	if (pcap_compile(h->pcap, &prog, expr, 1, PCAP_NETMASK_UNKNOWN) < 0)
		return;

	(void)pcap_setfilter(h->pcap, &prog);
	pcap_freecode(&prog);
}

int host_if_send(struct paad_sim_host_if *h, const uint8_t *data, uint16_t len)
{
	return pcap_inject(h->pcap, data, len) == (int)len ? PAAD_HAL_OK : PAAD_HAL_ERR_IO;
}

/* A host time (the realtime clock pcap stamps frames with) on the
 * simulation's monotonic clock: subtract their current difference. */
static uint64_t realtime_to_monotonic(uint64_t real_ns)
{
	struct timespec ts;
	uint64_t real_now, mono_now;

	clock_gettime(CLOCK_REALTIME, &ts);
	mono_now = paad_sim_monotonic_ns(NULL);
	real_now = (uint64_t)ts.tv_sec * PAAD_HAL_NSEC_PER_SEC + (uint64_t)ts.tv_nsec;

	if (real_ns > real_now)
		return mono_now; /* stamped "in the future": clock adjusted meanwhile */

	return mono_now - (real_now - real_ns);
}

int host_if_recv(struct paad_sim_host_if *h, uint8_t *buf, uint16_t size, uint64_t *rx_ns)
{
	struct pcap_pkthdr *hdr;
	const u_char *data;
	uint64_t real_ns;
	int rc;

	rc = pcap_next_ex(h->pcap, &hdr, &data);
	if (rc == 0)
		return 0;
	if (rc < 0)
		return PAAD_HAL_ERR_IO;

	if (hdr->caplen > size || hdr->caplen != hdr->len)
		return 0; /* truncated or oversized: not a frame we could use */

	memcpy(buf, data, hdr->caplen);

	real_ns = (uint64_t)hdr->ts.tv_sec * PAAD_HAL_NSEC_PER_SEC +
		  (uint64_t)hdr->ts.tv_usec * (h->nano ? 1ULL : 1000ULL);
	*rx_ns = hdr->ts.tv_sec ? realtime_to_monotonic(real_ns) : 0;

	return (int)hdr->caplen;
}

bool host_if_up(struct paad_sim_host_if *h)
{
	struct ifreq ifr;

	if (h->ioctl_fd < 0)
		return true; /* cannot tell: assume up */

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, h->name, sizeof(ifr.ifr_name) - 1);
	if (ioctl(h->ioctl_fd, SIOCGIFFLAGS, &ifr) < 0)
		return false;

	return (ifr.ifr_flags & IFF_UP) && (ifr.ifr_flags & IFF_RUNNING);
}
