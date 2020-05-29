/*
 * Copyright (c) 2020 Conclusive Engineering Sp. z o.o.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


#ifndef _PEER_H_
#define _PEER_H_

#include <netinet/in.h>
#include <semaphore.h>
#include <time.h>
#include <stdint.h>
#include <dirent.h>
#include <string.h>
#include <sys/queue.h>

#include "mt.h"

#define INTERNAL_LINKAGE __attribute__((__visibility__("hidden")))

struct schedule_entry {
	uint64_t begin, end;
};


struct peer {
	enum {
		LEECHER,
		SEEDER
	} type;

	enum {
		SM_NONE = 0,
		SM_HANDSHAKE_INIT,
		SM_SEND_HANDSHAKE_HAVE,
		SM_WAIT_REQUEST,
		SM_REQUEST,
		SM_SEND_PEX_RESP,
		SM_SEND_INTEGRITY,
		SM_SEND_DATA,
		SM_WAIT_ACK,
		SM_ACK,SM_WAIT_FINISH
	} sm_seeder;

	enum {
		SM_HANDSHAKE = 1,
		SM_WAIT_HAVE,
		SM_PREPARE_REQUEST,
		SM_SEND_REQUEST,
		SM_WAIT_PEX_RESP,
		SM_PEX_RESP,
		SM_WAIT_INTEGRITY,
		SM_INTEGRITY,
		SM_WAIT_DATA,
		SM_DATA,
		SM_SEND_ACK,
		SM_INC_Z,
		SM_WHILE_REQUEST,
		SM_SEND_HANDSHAKE_FINISH,
		SM_SWITCH_SEEDER
	} sm_leecher;

	uint32_t src_chan_id;
	uint32_t dest_chan_id;
	struct peer *seeder;		/* pointer to seeder peer struct - used on seeder side in threads */
	struct peer *local_leecher;	/* pointer to local leecher peer struct - used on leecher side in threads */
	struct node *tree;		/* pointer to beginning (index 0) array with tree nodes */
	struct node *tree_root;		/* pointer to root of the tree */
	struct chunk *chunk;		/* array of chunks */
	uint32_t nl;			/* number of leaves */
	uint32_t nc;			/* number of chunks */
	uint64_t num_series;		/* number of series */
	uint64_t hashes_per_mtu;	/* number of hashes that fit MTU size, for example if == 5 then series are 0..4, 5..9, 10..14 */
	uint8_t sha_demanded[20];

	uint8_t fetch_schedule;				/* 0 = peer is not allowed to get next schedule from download_schedule array */
	uint8_t after_seeder_switch;			/* 0 = still downloading from primary seeder, 1 = switched to another seeder after connection lost */
	struct schedule_entry *download_schedule;	/* leecher side: array of elements of pair: begin,end of chunks, max number of index: peer->nl-1 */
	uint64_t download_schedule_len;			/* number of indexes of allocated array "download_schedule", 0 = all chunks downloaded */
	volatile uint64_t download_schedule_idx;	/* index (iterator) for download_schedule array */
	pthread_mutex_t download_schedule_mutex;	/* mutex for "download_schedule" array protection */

	/* for thread */
	uint8_t finishing;
	pthread_t thread;
	uint8_t thread_num;				/* only for debugging - thread number */

	uint32_t timeout;

	/* timestamp of last received and sent message */
	struct timespec ts_last_recv, ts_last_send;

	/* last received and sent message */
	uint8_t d_last_recv, d_last_send;

	/* network things */
	uint16_t port;					/* seeder: udp port number to bind to */
	struct sockaddr_in leecher_addr;		/* leecher address: IP/PORT from seeder point of view */
	struct sockaddr_in seeder_addr;			/* primary seeder IP/PORT address from leecher point of view */
	char *recv_buf;
	char *send_buf;

	uint16_t recv_len;
	int sockfd, fd;
	pthread_mutex_t fd_mutex;

	/* synchronization */
	sem_t *sem;
	char sem_name[64];
	uint8_t to_remove;
	pthread_mutex_t mutex;
	pthread_cond_t mtx_cond;
	enum { C_TODO = 1, C_DONE = 2 } cond;

	uint32_t chunk_size;
	uint32_t start_chunk;
	uint32_t end_chunk;
	uint64_t curr_chunk;		/* currently serviced chunk */
	uint64_t file_size;
	char fname[256];
	char fname_len;
	uint8_t pex_required;		/* leecher side: 1=we want list of seeders from primary seeder */

	struct peer *current_seeder;	/* leecher side: points to one element of the list seeders in ->next */

	struct file_list_entry *file_list_entry;	/* seeder side: pointer to file choosen by leecher using SHA1 hash */

	struct peer *next;		/* list of peers - leechers from seeder point of view or seeders from leecher pov */
};


/* list of files shared by seeder */
SLIST_HEAD(slisthead, file_list_entry);
extern struct slisthead file_list_head;
struct file_list_entry {
	char path[1024];		/* full path to file: directory name + file name */
	char sha[20];			/* do we need this? */
	uint64_t file_size;
	uint32_t nl;			/* number of leaves */
	uint32_t nc;			/* number of chunks */
	struct chunk *tab_chunk;	/* array of chunks for this file */
	struct node *tree;		/* tree of the file */
	struct node *tree_root;		/* pointer to root node of the tree */
	uint32_t start_chunk;
	uint32_t end_chunk;

	SLIST_ENTRY(file_list_entry) next;
};


/* list of other (alternative) seeders maintained by primary seeder */
SLIST_HEAD(slist_seeders, other_seeders_entry);
extern struct slist_seeders other_seeders_list_head;
struct other_seeders_entry {
	struct sockaddr_in sa;
	SLIST_ENTRY(other_seeders_entry) next;
};


extern struct peer peer_list_head;
extern uint8_t remove_dead_peers;

void add_peer_to_list (struct peer *, struct peer *);
int remove_peer_from_list (struct peer *, struct peer *);
struct peer * ip_port_to_peer (struct peer *, struct sockaddr_in *);
struct peer * new_peer (struct sockaddr_in *, int, int);
struct peer * new_seeder (struct sockaddr_in *, int);
void cleanup_peer (struct peer *);
void cleanup_all_dead_peers (struct peer *);
void create_download_schedule (struct peer *);
int all_chunks_downloaded (struct peer *);
void create_file_list (char *);

#endif /* _PEER_H_ */
