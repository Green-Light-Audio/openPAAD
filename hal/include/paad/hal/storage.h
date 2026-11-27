/*
 * Copyright 2026 AIASK Limited (Green Light Audio)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * HAL service: persistent settings.
 *
 * A small key-value store for what must survive a power cycle: entity and
 * stream names, saved stream bindings, configuration choices. Keys are
 * defined by the core; values are short (tens to hundreds of bytes). The
 * platform decides where they live (flash sector, EEPROM, file on a host).
 * A write either fully succeeds or leaves the previous value in place.
 */

#ifndef PAAD_HAL_STORAGE_H
#define PAAD_HAL_STORAGE_H

#include "paad/hal/common.h"

#ifdef __cplusplus
extern "C" {
#endif

struct paad_hal_storage;

struct paad_hal_storage_ops {
	/** Read a value. Returns its length (may exceed len, in which case
	 * only len bytes are copied), or PAAD_HAL_ERR_INVALID if the key does
	 * not exist. Required. */
	int (*read)(struct paad_hal_storage *st, uint16_t key, void *buf, size_t len);

	/** Create or replace a value atomically. Required. */
	int (*write)(struct paad_hal_storage *st, uint16_t key, const void *data, size_t len);

	/** Remove a value; removing a missing key is not an error. Required. */
	int (*erase)(struct paad_hal_storage *st, uint16_t key);
};

struct paad_hal_storage {
	const struct paad_hal_storage_ops *ops;
	void *priv;
	const char *name;
	size_t max_value_len;
};

static inline int paad_hal_storage_read(struct paad_hal_storage *st, uint16_t key, void *buf, size_t len)
{
	return st->ops->read(st, key, buf, len);
}

static inline int paad_hal_storage_write(struct paad_hal_storage *st, uint16_t key, const void *data, size_t len)
{
	return st->ops->write(st, key, data, len);
}

static inline int paad_hal_storage_erase(struct paad_hal_storage *st, uint16_t key)
{
	return st->ops->erase(st, key);
}

#ifdef __cplusplus
}
#endif

#endif /* PAAD_HAL_STORAGE_H */
