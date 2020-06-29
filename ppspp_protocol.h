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

#ifndef _PPSPP_PROTOCOL_H_
#define _PPSPP_PROTOCOL_H_

#include "mt.h"
#include "peer.h"


/* handshake protocol options */
enum proto_options {
	VERSION = 0,
	MINIMUM_VERSION,
	SWARM_ID,
	CONTENT_PROT_METHOD,
	MERKLE_HASH_FUNC,
	LIVE_SIGNATURE_ALG,
	CHUNK_ADDR_METHOD,
	LIVE_DISC_WIND,
	SUPPORTED_MSGS,
	CHUNK_SIZE,
	FILE_SIZE,
	FILE_NAME,
	FILE_HASH,
	END_OPTION = 255
};

enum message {
	HANDSHAKE = 0,
	DATA,
	ACK,
	HAVE,
	INTEGRITY,
	PEX_RESV4,
	PEX_REQ,
	SIGNED_INTEGRITY,
	REQUEST,
	CANCEL,
	CHOKE,
	UNCHOKE,
	PEX_RESV6,
	PEX_RESCERT
};

enum handshake_type {
	HANDSHAKE_INIT = 1,
	HANDSHAKE_FINISH,
	HANDSHAKE_ERROR
};

struct proto_opt_str {
	uint8_t version;
	uint8_t minimum_version;
	uint16_t swarm_id_len;
	uint8_t *swarm_id;
	uint8_t content_prot_method;
	uint8_t merkle_hash_func;
	uint8_t live_signature_alg;
	uint8_t chunk_addr_method;
	uint8_t live_disc_wind[8];
	uint8_t supported_msgs_len;
	uint8_t supported_msgs[256];
	uint32_t chunk_size;
	uint64_t file_size;
	uint8_t file_name[256];
	uint8_t file_name_len;
	uint8_t sha_demanded[20];
	uint32_t opt_map;				/* bitmap - which of the fields above have any data */
};

struct ppsp_msg_handshake
{
	uint32_t src_channel_id;
	uint8_t protocol_options[];
} __attribute__((packed));

struct ppsp_msg_have
{
	uint32_t start_chunk;
	uint32_t end_chunk;
} __attribute__((packed));

struct ppsp_msg_data
{
	uint32_t start_chunk;
	uint32_t end_chunk;
	uint64_t timestamp;
	uint8_t data[];
} __attribute__((packed));

struct ppsp_msg_ack
{
	uint32_t start_chunk;
	uint32_t end_chunk;
	uint64_t sample;
};

struct ppsp_msg_integrity
{
	uint32_t end_chunk;
	uint8_t hash[256];
} __attribute__((packed));

struct ppsp_msg_signed_integrity
{
	uint32_t start_chunk;
	uint32_t end_chunk;
	uint64_t timestamp;
	uint8_t signature[];
} __attribute__((packed));

struct ppsp_msg_request
{
	uint32_t start_chunk;
	uint32_t end_chunk;
} __attribute__((packed));

struct ppsp_msg_cancel
{
	uint32_t start_chunk;
	uint32_t end_chunk;
} __attribute__((packed));

struct ppsp_msg
{
	uint32_t dst_channel_id; /* ??? */
	uint8_t message_type;
	union {
		struct ppsp_msg_handshake handshake;
		struct ppsp_msg_have have;
		struct ppsp_msg_data data;
		struct ppsp_msg_ack ack;
		struct ppsp_msg_integrity integrity;
		struct ppsp_msg_request request;
		struct ppsp_msg_cancel cancel;
	};
} __attribute__((packed));

int make_handshake_options (char *, struct proto_opt_str *);
int make_handshake_request (char *, uint32_t, uint32_t, char *, int);
int make_handshake_have (char *, uint32_t, uint32_t, char *, int, struct peer *);
int make_handshake_finish (char *, struct peer *);
int make_request (char *, uint32_t, uint32_t, uint32_t, struct peer*);
int make_pex_resp (char *, struct peer *, struct peer *);
int make_integrity (char *, struct peer *, struct peer *);
int make_data (char *, struct peer *);
int make_ack (char *, struct peer *);
int dump_options (char *ptr, struct peer *);
int dump_handshake_request (char *, int, struct peer *);
int dump_handshake_have (char *, int, struct peer *);
int dump_request (char *, int, struct peer *);
int dump_pex_resp (char *, int, struct peer *, int);
int dump_integrity (char *, int, struct peer *);
int dump_ack (char *, int, struct peer *);
uint8_t message_type (char *);
uint8_t handshake_type (char *);

#endif /* _PPSPP_PROTOCOL_H_ */
