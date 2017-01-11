/*
 * Copyright (C) 2016 Red Hat, Inc.  All rights reserved.
 *
 * Authors: Fabio M. Di Nitto <fabbione@kronosnet.org>
 *
 * This software licensed under GPL-2.0+, LGPL-2.0+
 */

#ifndef __TRANSPORTS_H__
#define __TRANSPORTS_H__

knet_transport_ops_t *get_udp_transport(void);
knet_transport_ops_t *get_sctp_transport(void);

int _configure_transport_socket(knet_handle_t knet_h, int sock, struct sockaddr_storage *address, const char *type);
int _set_fd_tracker(knet_handle_t knet_h, int sockfd, uint8_t transport, uint8_t data_type, void *data, int do_lock);

int _transport_addrtostr(const struct sockaddr *sa, socklen_t salen, char *str[2]);
void _transport_addrtostr_free(char *str[2]);

#endif