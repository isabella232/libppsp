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

#include "peer.h"
#include "debug.h"
#include "sha1.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/*
 * add element to the end of the list
 */
INTERNAL_LINKAGE
void
add_peer_to_list(struct slist_peers *list_head, struct peer *p)
{
  struct peer *pd;

  d_printf("add new peer to list: %#lx  %s:%u\n", (uint64_t)p, inet_ntoa(p->leecher_addr.sin_addr),
           ntohs(p->leecher_addr.sin_port));

  /* check for possible duplicates */
  SLIST_FOREACH(pd, list_head, snext)
  {
    if (pd == p) {
      d_printf("%s", "this element already exist in the list\n");
      return;
    }
  }

  /* check whether the list is empty */
  if (SLIST_EMPTY(list_head)) {
    SLIST_INSERT_HEAD(list_head, p, snext);
  } else {
    struct peer *last = NULL;
    /* look for last element in the list */
    SLIST_FOREACH(pd, list_head, snext)
    {
      if (SLIST_NEXT(pd, snext) == NULL) {
	last = pd;
      }
    }
    if (last == NULL) {
      abort();
    }
    /* add element to the end of the list */
    SLIST_INSERT_AFTER(last, p, snext);
  }
}

INTERNAL_LINKAGE
int
remove_peer_from_list(struct slist_peers *list_head, struct peer *p)
{
  SLIST_REMOVE(list_head, p, peer, snext);

  return 0;
}

INTERNAL_LINKAGE
struct peer *
ip_port_to_peer(struct peer *seeder, struct slist_peers *list_head, struct sockaddr_in *client)
{
  struct peer *p;

  SLIST_FOREACH(p, list_head, snext)
  {
    if (memcmp(&p->leecher_addr, client, sizeof(struct sockaddr_in)) == 0) {
      return p;
    }
  }

  return NULL;
}

/* seeder side: create new remote peer (LEECHER) */
INTERNAL_LINKAGE
struct peer *
new_peer(struct sockaddr_in *sa, int n, int sockfd)
{
  struct peer *p;

  p = malloc(sizeof(struct peer));
  if (p == NULL) {
    return NULL;
  }

  memset(p, 0, sizeof(struct peer));
  memcpy(&p->leecher_addr, sa, sizeof(struct sockaddr_in));

  d_printf("new peer[%u]: %#lx   IP: %s:%u\n", p->thread_num, (uint64_t)p, inet_ntoa(p->leecher_addr.sin_addr),
           ntohs(p->leecher_addr.sin_port));

  p->recv_buf = malloc(n); /* allocate receiving buffer */
  p->send_buf = malloc(n); /* allocate sending buffer */

  p->sockfd = sockfd;
  p->type = LEECHER;
  p->seeder = NULL;
  p->finishing = 0;
  clock_gettime(CLOCK_MONOTONIC, &p->ts_last_recv);
  clock_gettime(CLOCK_MONOTONIC, &p->ts_last_send);

  return p;
}

/* leecher side: create new remote peer (SEEDER) */
INTERNAL_LINKAGE
struct peer *
new_seeder(struct sockaddr_in *sa, int n)
{
  struct peer *p;

  p = malloc(sizeof(struct peer));
  if (p == NULL) {
    return NULL;
  }

  memset(p, 0, sizeof(struct peer));

  memcpy(&p->leecher_addr, sa, sizeof(struct sockaddr_in));

  p->recv_buf = malloc(n); /* allocate receiving buffer */
  p->send_buf = malloc(n); /* allocate sending buffer */

  p->type = SEEDER;
  p->seeder = NULL;
  p->finishing = 0;
  p->thread = 0;
  clock_gettime(CLOCK_MONOTONIC, &p->ts_last_recv);
  clock_gettime(CLOCK_MONOTONIC, &p->ts_last_send);

  return p;
}

INTERNAL_LINKAGE
void
cleanup_peer(struct peer *p)
{
  d_printf("cleaning up peer[%u]: %#lx   IP: %s:%u\n", p->thread_num, (uint64_t)p, inet_ntoa(p->leecher_addr.sin_addr),
           ntohs(p->leecher_addr.sin_port));

  /* method1 only - wait for pthread, destroy mutex and condition variable */
  if (p->to_remove == 1) {
    pthread_join(p->thread, NULL);

    d_printf("cleaning up peer: %#lx\n", (uint64_t)p);
    if (p->seeder != NULL) { /* are we seeder? */
      (void)remove_peer_from_list(&p->seeder->peers_list_head, p);
    } else if (p->local_leecher != NULL) { /* are we leecher? */
      (void)remove_peer_from_list(&p->local_leecher->peers_list_head, p);
    }

    /* destroy the semaphore */
    pthread_mutex_destroy(&p->seeder_mutex);
    pthread_cond_destroy(&p->seeder_mtx_cond);
  }

  /* free allocated memory */
  if (p->recv_buf) {
    free(p->recv_buf);
  }
  if (p->send_buf) {
    free(p->send_buf);
  }
  p->recv_buf = p->send_buf = NULL;
  d_printf("freeing peer: %#lx\n", (uint64_t)p);
  free(p);
}

/* remove all the marked peers */
INTERNAL_LINKAGE
void
cleanup_all_dead_peers(struct slist_peers *list_head)
{
  struct peer *p;

  SLIST_FOREACH(p, list_head, snext)
  {
    if (p->to_remove != 0) { /* is this peer (leecher) marked to remove? */
      cleanup_peer(p);
    }
  }
  remove_dead_peers = 0;
}

/*
 * basing on chunk array - create download schedule (array)
 */
INTERNAL_LINKAGE
void
create_download_schedule(struct peer *p)
{
  uint64_t o;
  uint64_t old_o;

  d_printf("creating schedule for %u chunks\n", p->nc);

  p->download_schedule_len = 0;
  o = 0;
  while (o < p->nc) {
    /* find first/closest not yet downloaded chunk */
    while ((p->chunk[o].downloaded == CH_YES) && (o < p->nc)) {
      o++;
    }
    if (o >= p->nc) {
      break;
    }

    old_o = o;
    uint64_t y = 0;
    while ((y < p->hashes_per_mtu) && (o < p->nc)) {
      if (p->chunk[o].downloaded == CH_NO) {
	o++;
      } else {
	break;
      }
      y++;
    }
    d_printf("%lu-%lu   %lu\n", old_o, o - 1, o - old_o);

    p->download_schedule[p->download_schedule_len].begin = old_o;
    p->download_schedule[p->download_schedule_len].end = o - 1;
    p->download_schedule_len++;

    _assert((p->chunk[o].downloaded == CH_NO) || (p->chunk[o].downloaded == CH_YES),
            "p->chunk[o].downloaded should have CH_NO or CH_YES, but have: %d\n", p->chunk[o].downloaded);
    _assert(p->download_schedule_len <= p->nc,
            "p->download_schedule_len should be <= p->nc, but "
            "p->download_schedule_len=%lu and p->nc=%u\n",
            p->download_schedule_len, p->nc);
  }
}

/*
 * basing on chunk array - create download schedule (array)
 */
INTERNAL_LINKAGE
int32_t
create_download_schedule_sbs(struct peer *p, uint32_t start_chunk, uint32_t end_chunk)
{
  int32_t ret;
  uint32_t last_chunk;
  uint64_t o;
  uint64_t old_o;

  d_printf("creating schedule for %u chunks\n", p->nc);
  p->download_schedule_len = 0;
  o = start_chunk;
  last_chunk = start_chunk;

  if (start_chunk > p->end_chunk) {
    d_printf("error: range: %u-%u is outside of the allowed range (%u-%u)\n", start_chunk, end_chunk, p->start_chunk,
             p->end_chunk);
    return -1;
  }

  p->hashes_per_mtu = 256;

  while ((o < p->nc) && (o <= end_chunk)) {
    /* find first/closest not yet downloaded chunk */
    while ((p->chunk[o].downloaded == CH_YES) && (o < p->nc)) {
      o++;
    }
    if (o >= p->nc) {
      break;
    }

    old_o = o;
    uint64_t y = 0;
    while ((y < p->hashes_per_mtu) && (o < p->nc) && (o <= end_chunk)) {
      if (p->chunk[o].downloaded == CH_NO) {
	o++;
      } else {
	break;
      }
      y++;
    }
    d_printf("range of chunks: %lu-%lu   %lu\n", old_o, o - 1, o - old_o);

    last_chunk = o - 1;
    p->download_schedule[p->download_schedule_len].begin = old_o;
    p->download_schedule[p->download_schedule_len].end = o - 1;
    p->download_schedule_len++;
  }

  ret = (last_chunk - start_chunk + 1) * p->chunk_size;

  return ret;
}

INTERNAL_LINKAGE
int32_t
swift_create_download_schedule_sbs(struct peer *p, uint32_t start_chunk, uint32_t end_chunk)
{
  int32_t ret;
  int32_t hci;
  uint32_t last_chunk;
  uint32_t ec;
  uint64_t o;
  uint64_t old_o;
  uint64_t y;

  d_printf("creating schedule for %u chunks\n", p->nc);
  p->download_schedule_len = 0;
  o = start_chunk;
  last_chunk = start_chunk;

  if (start_chunk > p->end_chunk) {
    d_printf("error: range: %u-%u is outside of the allowed range (%u-%u)\n", start_chunk, end_chunk, p->start_chunk,
             p->end_chunk);
    return -1;
  }

  p->hashes_per_mtu = 256;

  _assert(p->num_have_cache > 0, "%s\n", "peer->num_have_cache must be > 0, but it isn't");

  hci = 0;
  while (hci < p->num_have_cache) {
    ec = p->have_cache[hci].end_chunk;
    while ((o < p->nc) && (o <= ec)) {
      /* find first/closest not yet downloaded chunk */
      while ((p->chunk[o].downloaded == CH_YES) && (o < p->nc)) {
	o++;
      }
      if (o >= p->nc) {
	break;
      }

      old_o = o;
      y = 0;
      while ((y < p->hashes_per_mtu) && (o < p->nc) && (o <= ec)) {
	if (p->chunk[o].downloaded == CH_NO) {
	  o++;
	} else {
	  break;
	}
	y++;
      }
      d_printf("range of chunks: %lu-%lu   %lu\n", old_o, o - 1, o - old_o);

      last_chunk = o - 1;
      p->download_schedule[p->download_schedule_len].begin = old_o;
      p->download_schedule[p->download_schedule_len].end = o - 1;
      p->download_schedule_len++;
    }
    hci++;
  }

  ret = (last_chunk - start_chunk + 1) * p->chunk_size;

  return ret;
}

INTERNAL_LINKAGE
int
all_chunks_downloaded(struct peer *p)
{
  int ret;
  uint64_t x;

  d_printf("%s", "checking whether all of chunks have been downloaded\n");

  ret = 1;
  x = 0;
  while ((x < p->nc)) {
    if (p->chunk[x].downloaded == CH_NO) {
      ret = 0;
      break;
    }
    x++;
  }
  return ret;
}

INTERNAL_LINKAGE
void
list_dir(struct peer *peer, char *dname)
{
  DIR *dir;
  char newdir[1024];
  struct file_list_entry *f;
  struct stat stat;

  dir = opendir(dname);
  if (dir == NULL) {
    return;
  }

  while (1) {
    struct dirent *dirent = readdir(dir);
    if (dirent == NULL) {
      break;
    }

    if (dirent->d_type == DT_REG) {
      f = malloc(sizeof(struct file_list_entry));
      memset(f->path, 0, sizeof(f->path));
      sprintf(f->path, "%s/%s", dname, dirent->d_name);
      lstat(f->path, &stat);
      f->file_size = stat.st_size;
      SLIST_INSERT_HEAD(&peer->file_list_head, f, next);
    }

    if ((dirent->d_type == DT_DIR) && (strcmp(dirent->d_name, ".") != 0) && (strcmp(dirent->d_name, "..") != 0)) {
      snprintf(newdir, sizeof(newdir) - 1, "%s/%s", dname, dirent->d_name);
      list_dir(peer, newdir);
    }
  }
  closedir(dir);
}

INTERNAL_LINKAGE
void
create_file_list(struct peer *peer, char *dname)
{
  list_dir(peer, dname);
}

INTERNAL_LINKAGE
void
process_file(struct file_list_entry *file_entry, struct peer *peer)
{
  char *buf;
  unsigned char digest[20 + 1];
  int fd;
  int r;
  uint64_t x;
  uint64_t nc;
  uint64_t nl;
  uint64_t c;
  uint64_t rd;
  struct stat stat;
  SHA1Context context;
  struct node *ret;
  struct node *root8;
  uint32_t chunk_size;

  chunk_size = peer->chunk_size;
  fd = open(file_entry->path, O_RDONLY);
  if (fd < 0) {
    printf("error opening file: %s\n", file_entry->path);
    exit(1);
  }
  fstat(fd, &stat);

  buf = malloc(chunk_size);

  nc = stat.st_size / chunk_size;
  if ((stat.st_size - stat.st_size / chunk_size * chunk_size) > 0) {
    nc++;
  }
  file_entry->nc = nc;
  d_printf("number of chunks [%u]: %lu\n", chunk_size, nc);

  /* compute number of leaves - it is not the same as number of chunks */
  nl = 1 << (order2(nc));
  d_printf("number of leaves %lu\n", nl);
  file_entry->nl = nl;

  file_entry->start_chunk = 0;
  file_entry->end_chunk = nc - 1;

  /* allocate array of chunks which will be linked to leaves later */
  file_entry->tab_chunk = malloc(nl * sizeof(struct chunk));
  memset(file_entry->tab_chunk, 0, nl * sizeof(struct chunk));

  /* initialize array of chunks */
  for (x = 0; x < nl; x++) {
    file_entry->tab_chunk[x].state = CH_EMPTY;
  }

  root8 = build_tree(nc, &ret);
  file_entry->tree_root = root8;
  file_entry->tree = ret;

  /* compute SHA hash for every chunk for given file */
  rd = 0;
  c = 0;
  while (rd < (uint64_t)stat.st_size) {
    r = read(fd, buf, chunk_size);

    SHA1Reset(&context);
    SHA1Input(&context, (uint8_t *)buf, r);
    SHA1Result(&context, digest);

    file_entry->tab_chunk[c].state = CH_ACTIVE;
    file_entry->tab_chunk[c].offset = c * chunk_size;
    file_entry->tab_chunk[c].len = r;
    memcpy(file_entry->tab_chunk[c].sha, digest, 20);
    memcpy(ret[2 * c].sha, digest, 20);
    ret[2 * c].state = ACTIVE;
    rd += r;
    c++;
  }
  close(fd);

  /* link array of chunks to leaves */
  for (x = 0; x < nl; x++) {
    ret[x * 2].chunk = &file_entry->tab_chunk[x];
    file_entry->tab_chunk[x].node = &ret[x * 2];
  }

  /* print the tree for given file */
  show_tree_root_based(&ret[root8->number]);

  /* print array tab_chunk */
  dump_chunk_tab(file_entry->tab_chunk, nl);

  /* update all the SHAs in the tree */
  update_sha(ret, nl);

  dump_tree(ret, nl);

  free(buf);
}
