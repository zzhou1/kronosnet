#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include "libknet-private.h"

int knet_host_get(knet_handle_t knet_h, uint16_t node_id, struct knet_host **host)
{
	int ret;

	if ((ret = pthread_rwlock_rdlock(&knet_h->list_rwlock)) != 0)
		return ret;

	*host = knet_h->host_index[node_id];

	if (*host == NULL) {
		pthread_rwlock_unlock(&knet_h->list_rwlock);
		errno = ENOENT;
		return ENOENT;
	}

	return 0;
}

int knet_host_acquire(knet_handle_t knet_h, struct knet_host **host)
{
	int ret;

	if ((ret = pthread_rwlock_rdlock(&knet_h->list_rwlock)) != 0)
		return ret;

	*host = knet_h->host_head;

	return 0;
}

int knet_host_release(knet_handle_t knet_h, struct knet_host **host)
{
	int ret;

	*host = NULL;

	if ((ret = pthread_rwlock_unlock(&knet_h->list_rwlock)) != 0)
		return ret;

	return 0;
}

int knet_host_foreach(knet_handle_t knet_h, knet_link_fn_t linkfun, struct knet_host_search *data)
{
	int lockstatus;
	struct knet_host *host;

	lockstatus = pthread_rwlock_rdlock(&knet_h->list_rwlock);

	if ((lockstatus != 0) && (lockstatus != EDEADLK))
		return lockstatus;

	for (host = knet_h->host_head; host != NULL; host = host->next) {
		if ((linkfun(knet_h, host, data)) != KNET_HOST_FOREACH_NEXT)
			break;
	}

	if (lockstatus == 0)
		pthread_rwlock_unlock(&knet_h->list_rwlock);

	return 0;
}

int knet_host_add(knet_handle_t knet_h, uint16_t node_id)
{
	int i, ret = 0; /* success */
	struct knet_host *host;

	if ((ret = pthread_rwlock_wrlock(&knet_h->list_rwlock)) != 0)
		goto exit_clean;

	if (knet_h->host_index[node_id] != NULL) {
		errno = ret = EEXIST;
		goto exit_unlock;
	}

	if ((host = malloc(sizeof(struct knet_host))) == NULL)
		goto exit_unlock;

	memset(host, 0, sizeof(struct knet_host));

	host->node_id = node_id;

	for (i = 0; i < KNET_MAX_LINK; i++)
		host->link[i].link_id = i;

	/* adding new host to the index */
	knet_h->host_index[node_id] = host;

	/* TODO: keep hosts ordered */
	/* pushing new host to the front */
	host->next		= knet_h->host_head;
	knet_h->host_head	= host;

 exit_unlock:
	pthread_rwlock_unlock(&knet_h->list_rwlock);

 exit_clean:
	return ret;
}

int knet_host_remove(knet_handle_t knet_h, uint16_t node_id)
{
	int ret = 0; /* success */
	struct knet_host *i, *removed;

	if ((ret = pthread_rwlock_wrlock(&knet_h->list_rwlock)) != 0)
		goto exit_clean;

	if (knet_h->host_index[node_id] == NULL) {
		errno = ret = EINVAL;
		goto exit_unlock;
	}

	removed = NULL;

	/* removing host from list */
	if (knet_h->host_head->node_id == node_id) {
		removed = knet_h->host_head;
		knet_h->host_head = removed->next;
	} else {
		for (i = knet_h->host_head; i->next != NULL; i = i->next) {
			if (i->next->node_id == node_id) {
				removed = i->next;
				i->next = removed->next;
				break;
			}
		}
	}

	if (removed != NULL) {
		knet_h->host_index[node_id] = NULL;
		free(removed);
	}

 exit_unlock:
	pthread_rwlock_unlock(&knet_h->list_rwlock);

 exit_clean:
	return ret;
}

/* bcast = 0 -> unicast packet | 1 -> broadcast|mcast */

/* make this bcast/ucast aware */
int knet_should_deliver(struct knet_host *host, int bcast, seq_num_t seq_num)
{
	size_t i, j; /* circular buffer indexes */
	seq_num_t seq_dist;

	seq_dist = (seq_num < host->bcast_seq_num_rx) ?
		(SEQ_MAX - seq_num) + host->bcast_seq_num_rx : host->bcast_seq_num_rx - seq_num;

	j = seq_num % KNET_CBUFFER_SIZE;

	if (seq_dist < KNET_CBUFFER_SIZE) { /* seq num is in ring buffer */
		return (host->bcast_circular_buffer[j] == 0) ? 1 : 0;
	} else if (seq_dist <= SEQ_MAX - KNET_CBUFFER_SIZE) {
		memset(host->bcast_circular_buffer, 0, KNET_CBUFFER_SIZE);
		host->bcast_seq_num_rx = seq_num;
	}

	/* cleaning up circular buffer */
	i = (host->bcast_seq_num_rx + 1) % KNET_CBUFFER_SIZE;

	if (i > j) {
		memset(host->bcast_circular_buffer + i, 0, KNET_CBUFFER_SIZE - i);
		memset(host->bcast_circular_buffer, 0, j + 1);
	} else {
		memset(host->bcast_circular_buffer + i, 0, j - i + 1);
	}

	host->bcast_seq_num_rx = seq_num;

	return 1;
}

void knet_has_been_delivered(struct knet_host *host, int bcast, seq_num_t seq_num)
{

	if (bcast) {
		host->bcast_circular_buffer[seq_num % KNET_CBUFFER_SIZE] = 1;
	} else {
		host->ucast_circular_buffer[seq_num % KNET_CBUFFER_SIZE] = 1;
	}

	return;
}
