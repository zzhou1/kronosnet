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

typedef struct nozzle_iface *nozzle_t;

/**
 * nozzle_open
 * @brief create a new tap device on the system.
 *
 * devname - pointer to device name of at least size IFNAMSIZ.
 *           if the dev strlen is 0, then the system will assign a name automatically.
 *           if a string is specified, the system will try to create a device with
 *           the specified name.
 *           NOTE: on FreeBSD the tap device names can only be tapX where X is a
 *           number from 0 to 255. On Linux such limitation does not apply.
 *           The name must be unique to the system. If an interface with the same
 *           name is already configured on the system, an error will be returned.
 *
 * devname_size - lenght of the buffer provided in dev (has to be at least IFNAMSIZ).
 *
 * updownpath - nozzle supports the typical filesystem structure to execute
 *              actions for: down.d  post-down.d  pre-up.d  up.d
 *              in the form of:
 *              updownpath/<action>/<interface_name>
 *              updownpath specifies where to find those directories on the
 *              filesystem and it must be an absolute path.
 *
 * @return
 * nozzle_open returns
 * a pointer to a nozzle struct on success
 * NULL on error and errno is set.
 */

nozzle_t nozzle_open(char *devname, size_t devname_size, const char *updownpath);

/**
 * nozzle_close
 * @brief deconfigure and destroy a nozzle device
 *
 * nozzle - pointer to the nozzle struct to destroy
 *
 * error_down - pointers to string to record errors from executing down.d
 *              when configured. The string is malloc'ed, the caller needs to free those
 *              buffers.
 *
 * error_postdown - pointers to string to record errors from executing post-down.d
 *                  when configured. The string is malloc'ed, the caller needs to free
 *                  those buffers.
 *
 * @return
 * 0 on success
 * -1 on error and error is set.
 * error_down / error_postdown are set to NULL if execution of external scripts
 * is sucessful
 * error_down / error_postdown will contain strings recording the execution error.
 */

int nozzle_close(nozzle_t nozzle, char **error_down, char **error_postdown);

/**
 * nozzle_set_up
 * @brief equivalent of ifconfig up, executes pre-up.d up.d if configured
 *
 * nozzle - pointer to the nozzle struct
 *
 * error_preup - pointers to string to record errors from executing pre-up.d
 *               when configured. The string is malloc'ed, the caller needs to free those
 *               buffers.
 *
 * error_up - pointers to string to record errors from executing up.d
 *            when configured. The string is malloc'ed, the caller needs to free those
 *            buffers.
 *
 * @return
 * 0 on success
 * -1 on error and error is set.
 * error_preup / error_up are set to NULL if execution of external scripts
 * is sucessful
 * error_preup / error_up will contain strings recording the execution error.
 */

int nozzle_set_up(nozzle_t nozzle, char **error_preup, char **error_up);

/**
 * nozzle_set_down
 * @brief equivalent of ifconfig down, executes down.d post-down.d
 *
 * nozzle - pointer to the nozzle struct
 *
 * error_down - pointers to string to record errors from executing down.d
 *              when configured. The string is malloc'ed, the caller needs to free those
 *              buffers.
 *
 * error_postdown - pointers to string to record errors from executing post-down.d
 *                  when configured. The string is malloc'ed, the caller needs to free
 *                  those buffers.
 *
 * @return
 * 0 on success
 * -1 on error and error is set.
 * error_down / error_postdown are set to NULL if execution of external scripts
 * is sucessful
 * error_down / error_postdown will contain strings recording the execution error.
 */

int nozzle_set_down(nozzle_t nozzle, char **error_down, char **error_postdown);

nozzle_t nozzle_find(char *dev, size_t dev_size);

int nozzle_get_fd(const nozzle_t nozzle);

const char *nozzle_get_name(const nozzle_t nozzle);

int nozzle_get_mtu(const nozzle_t nozzle);
int nozzle_set_mtu(nozzle_t nozzle, const int mtu);
int nozzle_reset_mtu(nozzle_t nozzle);

int nozzle_get_mac(const nozzle_t nozzle, char **ether_addr);
int nozzle_set_mac(nozzle_t nozzle, const char *ether_addr);
int nozzle_reset_mac(nozzle_t nozzle);

int nozzle_add_ip(nozzle_t nozzle, const char *ip_addr, const char *prefix, char **error_string);
int nozzle_del_ip(nozzle_t nozzle, const char *ip_addr, const char *prefix, char **error_string);
int nozzle_get_ips(const nozzle_t nozzle, char **ip_addr_list, int *entries);

#endif
