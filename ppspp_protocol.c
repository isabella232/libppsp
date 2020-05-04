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

#include <stdio.h>
#include <string.h>
#include <endian.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "net.h"
#include "ppspp_protocol.h"
#include "sha1.h"
#include "mt.h"
#include "debug.h"


/*
 * serialize handshake options in memory in form of list
 *
 * in params:
 * 	pos -  pointer to structure with options
 *
 * out params:
 * 	ptr - pointer to buffer where the serialized options will be placed
 */
int make_handshake_options (char *ptr, struct proto_opt_str *pos)
{
	unsigned char *d;
	int ret;

	d = (unsigned char *)ptr;
	if (pos->opt_map & (1 << VERSION)) {
		*d = VERSION;
		d++;
		*d = 1;
		d++;
	} else {
		d_printf("%s", "no version specified - it's obligatory!\n");
		return -1;
	}

	if (pos->opt_map & (1 << MINIMUM_VERSION)) {
		*d = MINIMUM_VERSION;
		d++;
		*d = 1;
		d++;
	} else {
		d_printf("%s", "no minimum_version specified - it's obligatory!\n");
		return -1;
	}

	if (pos->opt_map & (1 << SWARM_ID)) {
		*d = SWARM_ID;
		d++;
		*(uint16_t *)d = htobe16(pos->swarm_id_len & 0xffff);
		d += sizeof(pos->swarm_id_len);
		memcpy(d, pos->swarm_id, pos->swarm_id_len);
		d += pos->swarm_id_len;
	}

	if (pos->opt_map & (1 << CONTENT_PROT_METHOD)) {
		*d = CONTENT_PROT_METHOD;
		d++;
		*d = pos->content_prot_method & 0xff;
		d++;
	} else {
		d_printf("%s", "no content_integrity_protection_method specified - it's obligatory!\n");
		return -1;
	}

	if (pos->opt_map & (1 << MERKLE_HASH_FUNC)) {
		*d = MERKLE_HASH_FUNC;
		d++;
		*d = pos->merkle_hash_func & 0xff;
		d++;
	}

	if (pos->opt_map & (1 << LIVE_SIGNATURE_ALG)) {
		*d = LIVE_SIGNATURE_ALG;
		d++;
		*d = pos->live_signature_alg & 0xff;
		d++;
	}

	if (pos->opt_map & (1 << CHUNK_ADDR_METHOD)) {
		*d = CHUNK_ADDR_METHOD;
		d++;
		*d = pos->chunk_addr_method & 0xff;
		d++;
	} else {
		d_printf("%s", "no chunk_addr_method specified - it's obligatory!\n");
		return -1;
	}

	if (pos->opt_map & (1 << LIVE_DISC_WIND)) {
		*d = LIVE_DISC_WIND;
		d++;
		if ((pos->chunk_addr_method == 0) || (pos->chunk_addr_method == 2)) {		/* 32 or 64 bit addresses */
			*(uint32_t *)d = htobe32(*(uint32_t *)pos->live_disc_wind);
			d += sizeof(uint32_t);
		} else {
			*(uint64_t *)d = htobe64(*(uint64_t *)pos->live_disc_wind);
			d += sizeof(uint64_t);
		}
	} else {
		d_printf("%s", "no chunk_addr_method specified - it's obligatory!\n");
		return -1;
	}

	if (pos->opt_map & (1 << SUPPORTED_MSGS)) {
		*d = SUPPORTED_MSGS;
		d++;
		*d = pos->supported_msgs_len & 0xff;
		d++;
		memcpy(d, pos->supported_msgs, pos->supported_msgs_len & 0xff);
		d += pos->supported_msgs_len & 0xff;
	}

	if (pos->opt_map & (1 << CHUNK_SIZE)) {
		*d = CHUNK_SIZE;
		d++;
		*(uint32_t *)d = htobe32((uint32_t)(pos->chunk_size & 0xffffffff));
		d += sizeof(pos->chunk_size);
	} else {
		d_printf("%s", "no chunk_size specified - it's obligatory!\n");
		return -1;
	}

	/*
	 * extension to original PPSPP protocol
	 * format: 1 + 8 bytes
	 *
	 * uint8_t  = FILE_SIZE = 10
	 * uint64_t = big-endian encoded length of file
	 */
	if (pos->opt_map & (1 << FILE_SIZE)) {
		*d = FILE_SIZE;
		d++;
		*(uint64_t *)d = htobe64(pos->file_size);
		d += sizeof(uint64_t);
	} else {
		d_printf("%s", "no file_size specified - it's obligatory!\n");
		return -1;
	}

	/*
	 * extension to original PPSPP protocol
	 * format: 1 + 1 + max 255 bytes
	 *
	 * uint8_t = FILE_NAME = 11
	 * uint8_t = length of the file name
	 * uint8_t [0..255] file name
	 */
	if (pos->opt_map & (1 << FILE_NAME)) {
		*d = FILE_NAME;
		d++;
		*d = pos->file_name_len & 0xff;
		d++;
		memset(d, 0, 255);
		memcpy(d, pos->file_name, pos->file_name_len);
		d += pos->file_name_len;
	} else {
		d_printf("%s", "no file_name specified - it's obligatory!\n");
		return -1;
	}

	*d = END_OPTION;				/* end the list of options with 0xff marker */
	d++;

	ret = d - (unsigned char *)ptr;
	d_printf("%s returning: %u bytes\n", __func__, ret);

	return ret;
}


/*
 * make structure of handshake request
 *
 * in params:
 * 	dest_chan_id - destination channel id
 * 	src_chan_id - source channel id
 * 	opts - pointer to generated list of PPSPP protocol options
 * 	opt_len - length of the option list in bytes
 * out params:
 * 	ptr - pointer to buffer where data will be stored
 *
 */
int make_handshake_request (char *ptr, uint32_t dest_chan_id, uint32_t src_chan_id, char *opts, int opt_len)
{
	char *d;
	int ret;

	d = ptr;

	*(uint32_t *)d = htobe32(dest_chan_id);
	d += sizeof(uint32_t);

	*d = HANDSHAKE;
	d++;

	*(uint32_t *)d = htobe32(src_chan_id);
	d += sizeof(uint32_t);

	memcpy(d, opts, opt_len);
	d += opt_len;

	ret = d - ptr;
	d_printf("%s: returning %u bytes\n", __func__, ret);

	return ret;
}


/*
 * create HANDSHAKE + HAVE response
 * called by SEEDER
 *
 * in params:
 * 	dest_chan_id - destination channel id
 * 	src_chan_id - source channel id
 * 	opts - pointer to generated list of PPSPP protocol options
 * 	opt_len - length of the option list in bytes
 * 	peer - pointer to struct peer describing SEEDER
 * out params:
 * 	ptr - pointer to buffer where data will be stored
 *
 */
int make_handshake_have (char *ptr, uint32_t dest_chan_id, uint32_t src_chan_id, char *opts, int opt_len, struct peer *peer)
{
	char *d;
	int ret, len;

	/* serialize HANDSHAKE header and options */
	len = make_handshake_request(ptr, dest_chan_id, src_chan_id, opts, opt_len);

	d = ptr + len;

	/* add HAVE header + data */

	*d = HAVE;
	d++;

	*(uint32_t *)d = htobe32(peer->start_chunk);
	d += sizeof(uint32_t);

	*(uint32_t *)d = htobe32(peer->end_chunk);
	d += sizeof(uint32_t);

	ret = d - ptr;
	d_printf("%s: returning %u bytes\n", __func__, ret);

	return ret;
}


/*
 * creates finishing (closing) HANDSHAKE request
 * called by LEECHER
 *
 * in params:
 * 	peer - pointer to structure describing LEECHER
 * out params:
 * 	ptr - pointer to buffer where data will be stored
 *
 */
int make_handshake_finish (char *ptr, struct peer *peer)
{
	unsigned char *d;
	int ret;

	d = (unsigned char *)ptr;

	*(uint32_t *)d = htobe32(0xfeed1234);				/* temporarily */
	d += sizeof(uint32_t);

	*d = HANDSHAKE;
	d++;

	*(uint32_t *)d = htobe32(0x0);
	d += sizeof(uint32_t);

	*d = END_OPTION;
	d++;

	ret = d - (unsigned char *)ptr;
	d_printf("%s: returning %u bytes\n", __func__, ret);

	return ret;
}


/*
 * create REQUEST with range of chunks
 * called by LEECHER
 *
 * in params:
 * 	dest_chan_id - destination channel id
 * 	start_chunk - number of first chunk
 * 	end_chunk - number of end chunk
 *
 * out params:
 * 	ptr - pointer to buffer where data of this request should be placed
 */
int make_request (char *ptr, uint32_t dest_chan_id, uint32_t start_chunk, uint32_t end_chunk)
{
	char *d;
	int ret;

	d = ptr;

	*(uint32_t *)d = htobe32(dest_chan_id);
	d += sizeof(uint32_t);

	*d = REQUEST;
	d++;

	*(uint32_t *)d = htobe32(start_chunk);
	d += sizeof(uint32_t);
	*(uint32_t *)d = htobe32(end_chunk);
	d += sizeof(uint32_t);

	*d = PEX_REQ;
	d++;

	ret = d - ptr;
	d_printf("%s: returning %u bytes\n", __func__, ret);

	return ret;
}


/*
 * make INTEGRITY message
 * called by SEEDER
 *
 * in params:
 * 	peer - pointer to peer structure describing LEECHER
 * 	we - pointer to peer structure describing SEEDER
 *
 * out params:
 * 	ptr - pointer to buffer where INTEGRITY message should be placed
 */
int make_integrity (char *ptr, struct peer *peer, struct peer *we)
{
	char *d;
	int y, ret;
	uint32_t x;

	d = ptr;

	*(uint32_t *)d = htobe32(peer->dest_chan_id);
	d += sizeof(uint32_t);

	*d = INTEGRITY;
	d++;

	*(uint32_t *)d = htobe32(peer->start_chunk);
	d += sizeof(uint32_t);
	*(uint32_t *)d = htobe32(peer->end_chunk);
	d += sizeof(uint32_t);

	y = 0;
	for (x = peer->start_chunk; x <= peer->end_chunk; x++) {
		memcpy(d, we->tree[2 * x].sha, 20);
		d_printf("copying chunk: %u\n", x);
		y++;
		d += 20;
	}

	ret = d - ptr;
	d_printf("%s: returning %u bytes\n", __func__, ret);

	return ret;
}


/*
 * create DATA message with contents of the selected chunk taken from file
 * called by SEEDER
 *
 * in params:
 * 	peer - pointer to structure describing LEECHER
 *
 * out params:
 * 	ptr - pointer to buffer where data will be placed
 */
int make_data (char *ptr, struct peer *peer)
{
	char *d;
	int ret, fd, l;
	uint64_t timestamp;

	d = ptr;

	*(uint32_t *)d = htobe32(peer->dest_chan_id);
	d += sizeof(uint32_t);

	*d = DATA;
	d++;

	*(uint32_t *)d = htobe32(peer->start_chunk);
	d += sizeof(uint32_t);
	*(uint32_t *)d = htobe32(peer->end_chunk);
	d += sizeof(uint32_t);

	timestamp = 0x12345678f11ff00f;		/* temporarily */
	*(uint64_t *)d = htobe64(timestamp);
	d += sizeof(uint64_t);

	fd = open(peer->fname, O_RDONLY);
	if (fd < 0) {
		d_printf("error opening file2: %s\n", peer->fname);
		return -1;
	}

	lseek(fd, peer->curr_chunk * peer->chunk_size, SEEK_SET);

	l = read(fd, d, peer->chunk_size);
	if (l < 0) {
		d_printf("error reading file: %s\n", peer->fname);
		close(fd);
		return -1;
	}

	close(fd);

	d += l;

	ret = d - ptr;
	d_printf("%s: returning %u bytes\n", __func__, ret);

	return ret;
}


/*
 * create ACK message with range of chunks which should be confirmed
 * called by LEECHER
 *
 * in params:
 * 	peer - pointer to struct describing LEECHER
 *
 * out params:
 * 	ptr - pointer to buffer where data shoudl be placed
 */
int make_ack (char *ptr, struct peer *peer)
{
	char *d;
	int ret;
	uint64_t delay_sample;

	d = ptr;

	*(uint32_t *)d = htobe32(peer->dest_chan_id);
	d += sizeof(uint32_t);

	*d = ACK;
	d++;

	*(uint32_t *)d = htobe32(peer->curr_chunk);
	d += sizeof(uint32_t);
	*(uint32_t *)d = htobe32(peer->curr_chunk);
	d += sizeof(uint32_t);

	delay_sample = 0x12345678ABCDEF;		/* temporarily */
	*(uint64_t *)d = htobe64(delay_sample);
	d += sizeof(uint64_t);

	ret = d - ptr;
	/* d_printf("%s: returning %u bytes\n", __func__, ret); */

	return ret;
}


/*
 * parse list of encoded options
 *
 * in params:
 * 	peer - structure describing peer (LEECHER or SEEDER)
 * 	ptr - pointer to data buffer which should be parsed
 */
int dump_options (char *ptr, struct peer *peer)
{
	char *d;
	int swarm_len, x, ret;
	uint8_t chunk_addr_method, supported_msgs_len;
	uint32_t ldw32;
	uint64_t ldw64;

	d = ptr;

	if (*d == VERSION) {
		d++;
		d_printf("version: %u\n", *d);
		if (*d != 1) {
			d_printf("version should be 1 but is: %u\n", *d);
			abort();
		}
		d++;
	}

	if (*d == MINIMUM_VERSION) {
		d++;
		d_printf("minimum_version: %u\n", *d);
		d++;
	}

	if (*d == SWARM_ID) {
		d++;
		swarm_len = be16toh(*((uint16_t *)d) & 0xffff);
		d += 2;
		d_printf("swarm_id[%u]: %s\n", swarm_len, d);
		d += swarm_len;
	}

	if (*d == CONTENT_PROT_METHOD) {
		d++;
		d_printf("%s", "Content integrity protection method: ");
		switch (*d) {
			case 0:	d_printf("%s", "No integrity protection\n"); break;
			case 1: d_printf("%s", "Merkle Hash Tree\n"); break;
			case 2: d_printf("%s", "Hash All\n"); break;
			case 3: d_printf("%s", "Unified Merkle Tree\n"); break;
			default: d_printf("%s", "Unassigned\n"); break;
		}
		d++;
	}

	if (*d == MERKLE_HASH_FUNC) {
		d++;
		d_printf("%s", "Merkle Tree Hash Function: ");
		switch (*d) {
			case 0:	d_printf("%s", "SHA-1\n"); break;
			case 1: d_printf("%s", "SHA-224\n"); break;
			case 2: d_printf("%s", "SHA-256\n"); break;
			case 3: d_printf("%s", "SHA-384\n"); break;
			case 4: d_printf("%s", "SHA-512\n"); break;
			default: d_printf("%s", "Unassigned\n"); break;
		}
		d++;
	}

	if (*d == LIVE_SIGNATURE_ALG) {
		d++;
		d_printf("Live Signature Algorithm: %u\n", *d);
		d++;
	}

	chunk_addr_method = 255;
	if (*d == CHUNK_ADDR_METHOD) {
		d++;
		d_printf("%s", "Chunk Addressing Method: ");
		switch (*d) {
			case 0:	d_printf("%s", "32-bit bins\n"); break;
			case 1:	d_printf("%s", "64-bit byte ranges\n"); break;
			case 2:	d_printf("%s", "32-bit chunk ranges\n"); break;
			case 3:	d_printf("%s", "64-bit bins\n"); break;
			case 4:	d_printf("%s", "64-bit chunk ranges\n"); break;
			default: d_printf("%s", "Unassigned\n"); break;
		}
		chunk_addr_method = *d;
		d++;
	}

	if (*d == LIVE_DISC_WIND) {
		d++;
		d_printf("%s", "Live Discard Window: ");
		switch (chunk_addr_method) {
			case 0:
			case 2:	ldw32 =  be32toh(*(uint32_t *)d); d_printf("32bit: %#x\n", ldw32); d += sizeof(uint32_t); break;
			case 1:
			case 3:
			case 4:	ldw64 =  be64toh(*(uint64_t *)d); d_printf("64bit: %#lx\n", ldw64); d += sizeof(uint64_t); break;
			default: d_printf("%s", "Error\n");
		}
	}

	if (*d == SUPPORTED_MSGS) {
		d++;
		d_printf("%s", "Supported messages mask: ");
		supported_msgs_len = *d;
		d++;
		for (x = 0; x < supported_msgs_len; x++)
			d_printf("%#x ", *(d+x) & 0xff);
		d_printf("%s", "\n");
		d += supported_msgs_len;
	}

	if (*d == CHUNK_SIZE) {
		d++;
		d_printf("Chunk size: %u\n", be32toh(*(uint32_t *)d));
		if (peer->type == LEECHER) {
			peer->chunk_size = be32toh(*(uint32_t *)d);
		}
		d += sizeof(uint32_t);
	}

	if (*d == FILE_SIZE) {
		d++;
		d_printf("File size: %lu\n", be64toh(*(uint64_t *)d));
		if (peer->type == LEECHER) {
			peer->file_size = be64toh(*(uint64_t *)d);
		}
		d += sizeof(uint64_t);
	}

	if (*d == FILE_NAME) {
		d++;
		d_printf("File name size: %u\n", *d & 0xff);
		peer->fname_len = *d & 0xff ;
		d++;
		memcpy(peer->fname, d, peer->fname_len);
		d_printf("File name: %s\n", peer->fname);
		d += peer->fname_len;
	}

	if ((*d & 0xff) == END_OPTION) {
		d_printf("%s", "end option\n");
		d++;
	} else {
		d_printf("error: should be END_OPTION(0xff) but is: d[%lu]: %u\n", d - ptr, *d & 0xff);
		abort();
	}

	d_printf("parsed: %lu bytes\n", d - ptr);

	ret = d - ptr;
	return ret;
}


/*
 * parse HANDSHAKE
 * called by SEEDER
 *
 * in params:
 * 	ptr - pointer to buffer which should be parsed
 * 	req_len - length of buffer pointed by ptr
 * 	peer - pointer to struct describing LEECHER
 */
int dump_handshake_request (char *ptr, int req_len, struct peer *peer)
{
	char *d;
	uint32_t dest_chan_id, src_chan_id;
	int ret, opt_len;

	d = ptr;

	dest_chan_id = be32toh(*(uint32_t *)d);
	d_printf("Destination Channel ID: %#x\n", dest_chan_id);
	d += sizeof(uint32_t);

	if (*d == HANDSHAKE) {
		d_printf("%s", "ok, HANDSHAKE req\n");
	} else {
		d_printf("error - should be HANDSHAKE req (0) but is: %u\n", *d);
		abort();
	}
	d++;

	src_chan_id = be32toh(*(uint32_t *)d);
	d_printf("Source Channel ID: %#x\n", src_chan_id);
	d += sizeof(uint32_t);

	d_printf("%s", "\n");

	opt_len = dump_options(d, peer);

	ret = d + opt_len - ptr;
	d_printf("%s returning: %u bytes\n", __func__, ret);

	return ret;
}


/*
 * parse HANDSHAKE + HAVE
 * called by LEECHER
 *
 * in params:
 * 	ptr - pointer to buffer which should be parsed
 * 	resp_len - length of buffer pointed by ptr
 * 	peer - pointer to struct describing peer
 */
int dump_handshake_have (char *ptr, int resp_len, struct peer *peer)
{
	char *d;
	int req_len, ret;
	uint32_t start_chunk, end_chunk, num_chunks;

	/* dump HANDSHAKE header and protocol options */
	d = ptr;
	req_len = dump_handshake_request(ptr, resp_len, peer);

	d += req_len;
	/* dump HAVE header */
	d_printf("%s", "HAVE header:\n");
	if (*d == HAVE) {
		d_printf("%s", "ok, HAVE header\n");
	} else {
		d_printf("error, should be HAVE header but is: %u\n", *d);
		abort();
	}

	d++;

	start_chunk = be32toh(*(uint32_t *)d);
	d += sizeof(uint32_t);
	peer->start_chunk = start_chunk;
	d_printf("start chunk: %u\n", start_chunk);

	end_chunk = be32toh(*(uint32_t *)d);
	d += sizeof(uint32_t);
	peer->end_chunk = end_chunk;
	d_printf("end chunk: %u\n", end_chunk);

	/* calculate how many chunks seeder has */
	num_chunks = end_chunk - start_chunk + 1;
	d_printf("seeder have %u chunks\n", num_chunks);
	peer->nc = num_chunks;

	/* calculate number of leaves */
	peer->nl = 1 << order2(peer->nc);
	d_printf("------------------nc: %u nl: %u\n", peer->nc, peer->nl);

	if (peer->chunk == NULL) {
		peer->chunk = malloc(peer->nl * sizeof(struct chunk));
		memset(peer->chunk, 0, peer->nl * sizeof(struct chunk));
	} else {
		d_printf("%s", "error - peer->chunk has already allocated memory, HAVE should be send only once\n");
		abort();
	}

	ret = d - ptr;
	d_printf("%s returning: %u bytes\n", __func__, ret);

	return ret;
}


/*
 * parse REQUEST
 * called by SEEDER
 *
 * in params:
 * 	ptr - pointer to buffer which should be parsed
 * 	req_len - length of buffer pointed by ptr
 * 	peer - pointer to struct describing LEECHER
 */
int dump_request (char *ptr, int req_len, struct peer *peer)
{
	char *d;
	int ret;
	uint32_t dest_chan_id, start_chunk, end_chunk;

	d = ptr;

	dest_chan_id = be32toh(*(uint32_t *)d);
	d_printf("Destination Channel ID: %#x\n", dest_chan_id);
	d += sizeof(uint32_t);

	if (*d == REQUEST) {
		d_printf("%s", "ok, REQUEST header\n");
	} else {
		d_printf("error, should be REQUEST header but is: %u\n", *d);
		abort();
	}
	d++;

	start_chunk = be32toh(*(uint32_t *)d);
	d += sizeof(uint32_t);
	d_printf("  start chunk: %u\n", start_chunk);

	end_chunk = be32toh(*(uint32_t *)d);
	d += sizeof(uint32_t);
	d_printf("  end chunk: %u\n", end_chunk);

	_assert(peer->type == LEECHER, "%s\n", "Only leecher is allowed to run this procedure");

	if (peer->type == LEECHER) {
		peer->start_chunk = start_chunk;
		peer->end_chunk = end_chunk;
	}

	if (d - ptr < req_len) {
		d_printf("  here do in the future maintenance of rest of messages: %lu bytes left\n" ,req_len - (d - ptr));
	}

	ret = d - ptr;
	d_printf("%s returning: %u bytes\n", __func__, ret);

	return ret;
}


/*
 * parse INTEGRITY
 * called by LEECHER
 *
 * in params:
 * 	ptr - pointer to buffer which should be parsed
 * 	req_len - length of buffer pointed by ptr
 * 	peer - pointer to struct describing LEECHER
 */
int dump_integrity (char *ptr, int req_len, struct peer *peer)
{
	char *d;
	int ret;
	uint32_t x, dest_chan_id, start_chunk, end_chunk;

	d = ptr;

	dest_chan_id = be32toh(*(uint32_t *)d);
	d_printf("Destination Channel ID: %#x\n", dest_chan_id);
	d += sizeof(uint32_t);

	if (*d == INTEGRITY) {
		d_printf("%s", "ok, INTEGRITY header\n");
	} else {
		d_printf("error, should be INTEGRITY header but is: %u\n", *d);
		abort();
	}
	d++;

	start_chunk = be32toh(*(uint32_t *)d);
	d += sizeof(uint32_t);
	d_printf("  start chunk: %u\n", start_chunk);

	end_chunk = be32toh(*(uint32_t *)d);
	d += sizeof(uint32_t);
	d_printf("  end chunk: %u\n", end_chunk);

	for (x = start_chunk; x <= end_chunk; x++) {
		memcpy(peer->chunk[x].sha, d, 20);
		peer->chunk[x].state = CH_ACTIVE;
		peer->chunk[x].offset = (x - start_chunk) * peer->chunk_size;
		peer->chunk[x].len = peer->chunk_size;

/*
		s = 0;
		for (y = 0; y < 20; y++)
			s += sprintf(sha_buf + s, "%02x", peer->chunk[x].sha[y] & 0xff);
		sha_buf[40] = '\0';
		d_printf("dumping chunk %u:  %s\n", x, sha_buf);
*/

		d += 20;
	}

	if (req_len - (d - ptr) > 0)
		d_printf("  %lu bytes left, parse them\n", req_len - (d - ptr));

	ret = d - ptr;
	d_printf("%s returning: %u bytes\n", __func__, ret);

	return ret;
}


/*
 * parse ACK
 * called by SEEDER
 *
 * in params:
 * 	ptr - pointer to buffer which should be parsed
 * 	ack_len - length of buffer pointed by ptr
 * 	peer - pointer to struct describing peer
 */
int dump_ack (char *ptr, int ack_len, struct peer *peer)
{
	char *d;
	int ret;
	uint32_t dest_chan_id, start_chunk, end_chunk;
	uint64_t delay_sample;

	d = ptr;

	dest_chan_id = be32toh(*(uint32_t *)d);
	d_printf("Destination Channel ID: %#x\n", dest_chan_id);
	d += sizeof(uint32_t);

	if (*d == ACK) {
		d_printf("%s", "ok, ACK header\n");
	} else {
		d_printf("error, should be ACK header but is: %u\n", *d);
	}
	d++;

	start_chunk = be32toh(*(uint32_t *)d);
	d += sizeof(uint32_t);
	d_printf("start chunk: %u\n", start_chunk);

	end_chunk = be32toh(*(uint32_t *)d);
	d += sizeof(uint32_t);
	d_printf("end chunk: %u\n", end_chunk);

	delay_sample = be64toh(*(uint64_t *)d);
	d += sizeof(uint64_t);
	d_printf("delay_sample: %#lx\n", delay_sample);

	ret = d - ptr;
	d_printf("%s returning: %u bytes\n", __func__, ret);

	return ret;
}


/*
 * return type of message
 */
uint8_t message_type (char *ptr)
{
	return ptr[4];			/* skip first 4 bytes - there is destination channel id */
}


/*
 * return type of HANDSHAKE: INIT, FINISH, ERROR
 */
uint8_t handshake_type (char *ptr)
{
	char * d;
	uint32_t dest_chan_id, src_chan_id;
	uint8_t ret;

	d = ptr;

	dest_chan_id = be32toh(*(uint32_t *)d);
	d_printf("Destination Channel ID: %#x\n", dest_chan_id);
	d += sizeof(uint32_t);

	if (*d == HANDSHAKE) {
		d_printf("%s", "ok, HANDSHAKE header\n");
	} else {
		d_printf("error, should be HANDSHAKE header but is: %u\n", *d);
		abort();
	}
	d++;

	src_chan_id = be32toh(*(uint32_t *)d);
	d_printf("Destination Channel ID: %#x\n", dest_chan_id);
	d += sizeof(uint32_t);

	if ((dest_chan_id == 0x0) && (src_chan_id != 0x0)) {
		d_printf("%s", "handshake_init\n");
		ret = HANDSHAKE_INIT;
	}

	if ((dest_chan_id != 0x0) && (src_chan_id == 0x0)) {
		d_printf("%s", "handshake_finish\n");
		ret = HANDSHAKE_FINISH;
	}
	if ((dest_chan_id == 0x0) && (src_chan_id == 0x0)) {
		d_printf("%s", "handshake_error1\n");
		ret = HANDSHAKE_ERROR;
	}

	if ((dest_chan_id != 0x0) && (src_chan_id != 0x0)) {
		d_printf("%s", "handshake_error2\n");
		ret = HANDSHAKE_ERROR;
	}

	return ret;
}


/*
 * test procedure
 */
void proto_test (struct peer *peer)
{
	if (peer->type == SEEDER) {
		net_seeder(peer);					/* run server sharing file */
	} else {
		net_leecher(peer);					/* run client receiving file */
	}
}
