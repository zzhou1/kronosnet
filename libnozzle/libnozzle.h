/*
 * Copyright (C) 2010-2018 Red Hat, Inc.  All rights reserved.
 *
 * Author: Fabio M. Di Nitto <fabbione@kronosnet.org>
 *
 * This software licensed under GPL-2.0+, LGPL-2.0+
 */

#ifndef __LIBNOZZLE_H__
#define __LIBNOZZLE_H__

#include <sys/types.h>
#include <net/if.h>

/**
 *
 * @file libnozzle.h
 * @brief tap interfaces management API include file
 * @copyright Copyright (C) 2010-2017 Red Hat, Inc.  All rights reserved.
 *
 * nozzle is a commodity library to manage tap (ethernet) interfaces
 */

typedef struct _iface *tap_t;

tap_t tap_open(char *dev, size_t dev_size, const char *updownpath);
int tap_close(tap_t tap);

tap_t tap_find(char *dev, size_t dev_size);

int tap_get_fd(const tap_t tap);

const char *tap_get_name(const tap_t tap);

int tap_get_mtu(const tap_t tap);
int tap_set_mtu(tap_t tap, const int mtu);
int tap_reset_mtu(tap_t tap);

int tap_get_mac(const tap_t tap, char **ether_addr);
int tap_set_mac(tap_t tap, const char *ether_addr);
int tap_reset_mac(tap_t tap);

int tap_set_up(tap_t tap, char **error_preup, char **error_up);
int tap_set_down(tap_t tap, char **error_down, char **error_postdown);

int tap_add_ip(tap_t tap, const char *ip_addr, const char *prefix, char **error_string);
int tap_del_ip(tap_t tap, const char *ip_addr, const char *prefix, char **error_string);
int tap_get_ips(const tap_t tap, char **ip_addr_list, int *entries);

#endif
