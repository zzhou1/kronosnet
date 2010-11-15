#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include "ring.h"
#include "utils.h"

#define KNET_MAX_EVENTS 8
#define KNET_PING_TIMERES 200000
#define KNET_DATABUFSIZE 131072 /* 128k */
#define KNET_PINGBUFSIZE (sizeof(struct knet_frame) + sizeof(struct timespec))

struct __knet_handle {
	int sock[2];
	int epollfd;
	struct knet_host *host_head;
	struct knet_listener *listener_head;
	struct knet_frame *databuf;
	struct knet_frame *pingbuf;
	pthread_t control_thread;
	pthread_t heartbt_thread;
	pthread_rwlock_t host_rwlock;
};

static void *knet_control_thread(void *data);
static void *knet_heartbt_thread(void *data);

static inline void knet_tsdiff(
	struct timespec *start, struct timespec *end, suseconds_t *diff);

knet_handle_t knet_handle_new(void)
{
	knet_handle_t knet_h;
	struct epoll_event ev;

	if ((knet_h = malloc(sizeof(struct __knet_handle))) == NULL)
		return NULL;

	memset(knet_h, 0, sizeof(struct __knet_handle));

	if ((knet_h->databuf = malloc(KNET_DATABUFSIZE))== NULL)
		goto exit_fail1;

	memset(knet_h->databuf, 0, KNET_DATABUFSIZE);

	if ((knet_h->pingbuf = malloc(KNET_PINGBUFSIZE))== NULL)
		goto exit_fail2;

	memset(knet_h->pingbuf, 0, KNET_PINGBUFSIZE);

	if (pthread_rwlock_init(&knet_h->host_rwlock, NULL) != 0)
		goto exit_fail3;

	if (socketpair(AF_UNIX, SOCK_STREAM, IPPROTO_IP, knet_h->sock) != 0)
		goto exit_fail4;

	knet_h->epollfd = epoll_create(KNET_MAX_EVENTS);

	if (knet_h->epollfd < 0)
		goto exit_fail5;

	if (knet_fdset_cloexec(knet_h->epollfd) != 0)
		goto exit_fail6;

	memset(&ev, 0, sizeof(struct epoll_event));

	ev.events = EPOLLIN;
	ev.data.fd = knet_h->sock[0];

	if (epoll_ctl(knet_h->epollfd,
				EPOLL_CTL_ADD, knet_h->sock[0], &ev) != 0)
		goto exit_fail6;

	if (pthread_create(&knet_h->control_thread, 0,
				knet_control_thread, (void *) knet_h) != 0)
		goto exit_fail6;

	if (pthread_create(&knet_h->heartbt_thread, 0,
				knet_heartbt_thread, (void *) knet_h) != 0)
		goto exit_fail7;

	return knet_h;

exit_fail7:
	pthread_cancel(knet_h->control_thread);

exit_fail6:
	close(knet_h->epollfd);

exit_fail5:
	close(knet_h->sock[0]);
	close(knet_h->sock[1]);

exit_fail4:
	pthread_rwlock_destroy(&knet_h->host_rwlock);

exit_fail3:
	free(knet_h->databuf);

exit_fail2:
	free(knet_h->pingbuf);

exit_fail1:
	free(knet_h);
	return NULL;
}

int knet_host_acquire(knet_handle_t knet_h, struct knet_host **head, int writelock)
{
	int ret;

	if (writelock != 0)
		ret = pthread_rwlock_wrlock(&knet_h->host_rwlock);
	else
		ret = pthread_rwlock_rdlock(&knet_h->host_rwlock);

	if (head)
		*head = (ret == 0) ? knet_h->host_head : NULL;

	return ret;
}

int knet_host_release(knet_handle_t knet_h)
{
	return pthread_rwlock_unlock(&knet_h->host_rwlock);
}

int knet_handle_getfd(knet_handle_t knet_h)
{
	return knet_h->sock[1];
}

int knet_host_add(knet_handle_t knet_h, struct knet_host *host)
{
	if (pthread_rwlock_wrlock(&knet_h->host_rwlock) != 0)
		return -1;

	/* pushing new host to the front */
	host->next		= knet_h->host_head;
	knet_h->host_head	= host;

	pthread_rwlock_unlock(&knet_h->host_rwlock);
	return 0;
}

int knet_host_remove(knet_handle_t knet_h, struct knet_host *host)
{
	struct knet_host *hp;

	if (pthread_rwlock_wrlock(&knet_h->host_rwlock) != 0)
		return -1;

	/* TODO: use a doubly-linked list? */
	if (host == knet_h->host_head) {
		knet_h->host_head = host->next;
	} else {
		for (hp = knet_h->host_head; hp != NULL; hp = hp->next) {
			if (host == hp->next) {
				hp->next = hp->next->next;
				break;
			}
		}
	}

	pthread_rwlock_unlock(&knet_h->host_rwlock);
	return 0;
}

int knet_listener_add(knet_handle_t knet_h, struct knet_listener *listener)
{
	int value;
	struct epoll_event ev;

	listener->sock = socket(listener->address.ss_family, SOCK_DGRAM, 0);

	if (listener->sock < 0)
		return listener->sock;

	value = KNET_RING_RCVBUFF;
	setsockopt(listener->sock, SOL_SOCKET, SO_RCVBUFFORCE, &value, sizeof(value));

	if (knet_fdset_cloexec(listener->sock) != 0)
		goto exit_fail1;

	if (bind(listener->sock, (struct sockaddr *) &listener->address,
					sizeof(struct sockaddr_storage)) != 0)
		goto exit_fail1;

	memset(&ev, 0, sizeof(struct epoll_event));

	ev.events = EPOLLIN;
	ev.data.fd = listener->sock;

	if (epoll_ctl(knet_h->epollfd, EPOLL_CTL_ADD, listener->sock, &ev) != 0)
		goto exit_fail1;

	if (pthread_rwlock_wrlock(&knet_h->host_rwlock) != 0)
		goto exit_fail2;

	/* pushing new host to the front */
	listener->next		= knet_h->listener_head;
	knet_h->listener_head	= listener;

	pthread_rwlock_unlock(&knet_h->host_rwlock);

	return 0;

 exit_fail2:
	epoll_ctl(knet_h->epollfd, EPOLL_CTL_DEL, listener->sock, &ev);

 exit_fail1:
	close(listener->sock);
	return -1;
}

static void knet_send_data(knet_handle_t knet_h)
{
	ssize_t len, snt;
	struct knet_host *i;
	struct knet_link *j;

	len = read(knet_h->sock[0], knet_h->databuf + 1,
				KNET_DATABUFSIZE - sizeof(struct knet_frame));

	if (len == 0) {
		/* TODO: disconnection, should never happen! */
		close(knet_h->sock[0]); /* FIXME: from here is downhill :) */
		return;
	}

	len += sizeof(struct knet_frame);

	/* TODO: packet inspection */

	knet_h->databuf->type = KNET_FRAME_DATA;

	if (pthread_rwlock_rdlock(&knet_h->host_rwlock) != 0)
		return;

	for (i = knet_h->host_head; i != NULL; i = i->next) {
		for (j = i->link; j != NULL; j = j->next) {
			if (j->enabled != 1) /* link is disabled */
				continue;

			snt = sendto(j->sock, knet_h->databuf, len, MSG_DONTWAIT,
					(struct sockaddr *) &j->address,
					sizeof(struct sockaddr_storage));

			if ((i->active == 0) && (snt == len))
				break;
		}
	}

	pthread_rwlock_unlock(&knet_h->host_rwlock);
}

static void knet_recv_frame(knet_handle_t knet_h, int sockfd)
{
	ssize_t len;
	struct sockaddr_storage address;
	socklen_t addrlen;
	struct knet_host *i;
	struct knet_link *j, *link_src;
	suseconds_t latency_last;

	if (pthread_rwlock_rdlock(&knet_h->host_rwlock) != 0)
		return;

	len = recvfrom(sockfd, knet_h->databuf, KNET_DATABUFSIZE,
		MSG_DONTWAIT, (struct sockaddr *) &address, &addrlen);

	if (len < sizeof(struct knet_frame))
		goto exit_unlock;

	if (ntohl(knet_h->databuf->magic) != KNET_FRAME_MAGIC)
		goto exit_unlock;

	if (knet_h->databuf->version != KNET_FRAME_VERSION)
		goto exit_unlock;

	/* searching host/link, TODO: improve lookup */
	link_src = NULL;

	for (i = knet_h->host_head; i != NULL; i = i->next) {
		for (j = i->link; j != NULL; j = j->next) {
			if (memcmp(&address, &j->address, addrlen) == 0) {
				link_src = j;
				break;
			}
		}
	}

	if (link_src == NULL) /* host/link not found */
		goto exit_unlock;

	switch (knet_h->databuf->type) {
	case KNET_FRAME_DATA:
		write(knet_h->sock[0],
			knet_h->databuf + 1, len - sizeof(struct knet_frame));

		break;
	case KNET_FRAME_PING:
		knet_h->databuf->type = KNET_FRAME_PONG;

		sendto(j->sock, knet_h->databuf, len,
				MSG_DONTWAIT, (struct sockaddr *) &j->address,
				sizeof(struct sockaddr_storage));

		break;
	case KNET_FRAME_PONG:
		clock_gettime(CLOCK_MONOTONIC, &j->pong_last);

		knet_tsdiff((struct timespec *) (knet_h->databuf + 1),
						&j->pong_last, &latency_last);

		j->enabled = 1; /* TODO: might need write lock */
		j->latency *= j->latency_exp;
		j->latency += latency_last * (j->latency_fix - j->latency_exp);
		j->latency /= j->latency_fix;

		break;
	}

 exit_unlock:
	pthread_rwlock_unlock(&knet_h->host_rwlock);
}

static inline void knet_tsdiff(
		struct timespec *start, struct timespec *end, suseconds_t *diff)
{
	*diff = (end->tv_sec - start->tv_sec) * 1000000; /* micro-seconds */
	*diff += (end->tv_nsec - start->tv_nsec) / 1000; /* micro-seconds */
}

static void knet_heartbeat_check_each(knet_handle_t knet_h, struct knet_link *j)
{
	struct timespec clock_now;
	suseconds_t diff_ping, diff_pong;

	if (clock_gettime(CLOCK_MONOTONIC, &clock_now) != 0)
		return;

	knet_tsdiff(&j->ping_last, &clock_now, &diff_ping);

	if (diff_ping >= j->ping_interval) {
		clock_gettime(CLOCK_MONOTONIC, &j->ping_last);

		memmove(knet_h->pingbuf + 1,
			&j->ping_last, sizeof(struct timespec));

		sendto(j->sock, knet_h->pingbuf, KNET_PINGBUFSIZE,
			MSG_DONTWAIT, (struct sockaddr *) &j->address,
			sizeof(struct sockaddr_storage));
	}

	if (j->enabled == 1) {
		knet_tsdiff(&j->pong_last, &clock_now, &diff_pong);

		if (diff_pong >= j->pong_timeout)
			j->enabled = 0; /* TODO: might need write lock */
	}
}

static void *knet_heartbt_thread(void *data)
{
	knet_handle_t knet_h;
	struct knet_host *i;
	struct knet_link *j;

	knet_h = (knet_handle_t) data;

	/* preparing ping buffer */
	knet_h->pingbuf->magic = htonl(KNET_FRAME_MAGIC);
	knet_h->pingbuf->version = KNET_FRAME_VERSION;
	knet_h->pingbuf->type = KNET_FRAME_PING;

	while (1) {
		usleep(KNET_PING_TIMERES);

		if (pthread_rwlock_rdlock(&knet_h->host_rwlock) != 0)
			continue;

		for (i = knet_h->host_head; i != NULL; i = i->next) {
			for (j = i->link; j != NULL; j = j->next)
				knet_heartbeat_check_each(knet_h, j);
		}

		pthread_rwlock_unlock(&knet_h->host_rwlock);
	}

	return NULL;
}

static void *knet_control_thread(void *data)
{
	int i, nev;
	knet_handle_t knet_h;
	struct epoll_event events[KNET_MAX_EVENTS];

	knet_h = (knet_handle_t) data;

	/* preparing data buffer */
	knet_h->databuf->magic = htonl(KNET_FRAME_MAGIC);
	knet_h->databuf->version = KNET_FRAME_VERSION;

	while (1) {
		nev = epoll_wait(knet_h->epollfd, events, KNET_MAX_EVENTS, 0);

		for (i = 0; i < nev; i++) {
			if (events[i].data.fd == knet_h->sock[0]) {
				knet_send_data(knet_h);
			} else {
				knet_recv_frame(knet_h, events[i].data.fd);
			}
		}
	}

	return NULL;
}
