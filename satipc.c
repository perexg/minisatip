/*
 * Copyright (C) 2014-2020 Catalin Toda <catalinii@yahoo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 */
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <net/if.h>
#include <fcntl.h>
#include <ctype.h>
#include "utils.h"
#include "dvbapi.h"
#include "satipc.h"
#include "ca.h"
#include "minisatip.h"
#include "dvb.h"

#define TCP_DATA_SIZE ((ADAPTER_BUFFER/1316)*(1316+16)*3)
#define TCP_DATA_MAX (TCP_DATA_SIZE * 8)
extern char *fe_delsys[];
extern struct struct_opts opts;

#define SATIP_LOG 5


typedef struct struct_satipc
{
	char enabled;
	int id;
	int lap, ldp; // number of pids to add, number of pids to delete
	uint16_t apid[MAX_PIDS]; // pids to add
	uint16_t dpid[MAX_PIDS]; // pids to delete
	// satipc
	char sip[40];
	int sport;
	char session[18];
	int stream_id;
	int listen_rtp;
	int rtcp, rtcp_sock, cseq;
	int err;
	int wp, qp; // written packet, queued packet
	char ignore_packets; // ignore packets coming from satip server while tuning
	char satip_fe, last_cmd;
	char use_tcp;
	char expect_reply, force_commit, want_commit, want_tune, sent_transport;
	int64_t last_setup, last_connect;
	uint8_t addpids, setup_pids;
	unsigned char *tcp_data;
	int tcp_size, tcp_pos, tcp_len;
	char use_fe, option_no_session, option_no_setup, option_no_option;
	uint32_t rcvp, repno, rtp_miss, rtp_ooo;   // rtp statstics
	uint16_t rtp_seq;

} satipc;

satipc *satip[MAX_ADAPTERS];

satipc * get_satip1(int aid, char *file, int line)
{
	if (aid < 0 || aid >= MAX_ADAPTERS || !satip[aid] || !satip[aid]->enabled)
	{
		LOG("%s:%d: %s returns NULL for id %d", file, line, __FUNCTION__, aid);
		return NULL;
	}
	return satip[aid];

}
#define get_satip(a) get_satip1(a, __FILE__, __LINE__)

#define get_ad_and_sipr(id, val) { ad = get_adapter(id); \
																																			sip = get_satip(id); \
																																			if(!ad || !sip) \
																																				return val; \
}

#define get_ad_and_sip(id) { ad = get_adapter(id); \
																													sip = get_satip(id); \
																													if(!ad || !sip) \
																														return; \
}

int http_request(adapter *ad, char *url, char *method);

#define SATIPC_ITEM (0x3000000000000)
#define MAKE_ITEM(a,b) ((SATIPC_ITEM | (a<<24) | (b)))
void satipc_commit(adapter *ad);
void set_adapter_signal(adapter *ad, char *b, int rlen);

int satipc_reply(sockets * s)
{
	int rlen = s->rlen;
	adapter *ad;
	satipc *sip;
	char *arg[50], *sess, *es, *sid, *timeout, *sep;
	int la, i, rc;
	__attribute__((unused)) int rv;
	get_ad_and_sipr(s->sid, 1);
	s->rlen = 0;
	LOG("satipc_reply (sock %d) handle %d, adapter %d:\n%s", s->id, s->sock,
					s->sid, s->buf);

	if ((timeout = strstr((char *)s->buf, "timeout=")))
	{
		int tmout;
		timeout += strlen("timeout=");
		tmout = map_intd(timeout, NULL, 30);
		sockets_timeout(ad->fe_sock, tmout * 500); // 2 times 30s
	}

	sess = strstr((char *)s->buf, "Session:");

	if (sip->last_cmd == RTSP_DESCRIBE)
	{
		set_adapter_signal(ad, (char *)s->buf, rlen);
	}

	sep = strstr((char *)s->buf, "minisatip");
	if(sep)
	{
		sip->option_no_session = 1;
		sip->option_no_setup = 1;
		sip->option_no_option = 1;
	}
	sep = strstr((char *)s->buf, "enigma_minisatip");
	if(sep && !ad->restart_when_tune)
	{
		LOGL(3, "Setting adapter %d to restart every time the transponder is changed", ad->id);
		ad->restart_when_tune = 1;
		if(sip->use_tcp == 0)
		{
			sip->use_tcp = 1;
			if(ad)
			{
				LOG("adapter %d is not RTSP over TCP, switching", ad->id);
				ad->restart_needed = 1;
			}
		}
	}

	la = split(arg, (char *) s->buf, 50, ' ');
	rc = map_int(arg[1], NULL);

	if (sip->option_no_session && sip->last_cmd == RTSP_OPTIONS && !sess && sip->session[0])
		rc = 454;

	if(rc == 404)
		ad->restart_needed = 1;

	if (rc == 454 || rc == 503 || rc == 405)
	{
		sip->sent_transport = 0;
		sip->want_tune = 1;
		sip->want_commit = 1;
		sip->force_commit = 1;
	}
	else if (rc != 200)
		sip->err = 1;
	sid = NULL;
	if (rc == 200 && !sip->want_tune && sip->last_cmd == RTSP_PLAY && sip->ignore_packets)
	{
		LOG("accepting packets from RTSP server");
		sip->ignore_packets = 0;
		ad->wait_new_stream = 1;
	}
	sess = NULL;
	for (i = 0; i < la; i++)
		if (strncasecmp("Session:", arg[i], 8) == 0)
			sess = header_parameter(arg, i);
		else if (strncasecmp("com.ses.streamID:", arg[i], 17) == 0)
			sid = header_parameter(arg, i);
		else if (strncasecmp("Server:", arg[i], 7) == 0)
		{
			char *ua = header_parameter(arg, i);
			if (!strncmp(ua, app_name, strlen(app_name)))
			{
				sip->addpids = 1;
				sip->setup_pids = 1;
			}
		}

	if (!sip->err && !sip->session[0] && sess)
	{
		if ((es = strchr(sess, ';')))
			*es = 0;
		strncpy(sip->session, sess, sizeof(sip->session));
		sip->session[sizeof(sip->session) - 1] = 0;
		LOG("satipc: session set for adapter %d to %s", ad->id, sip->session);

		if (sid && sip->stream_id == -1)
			sip->stream_id = map_int(sid, NULL);

		sip->expect_reply = 0;
		sip->force_commit = 1;
//		http_request(ad, NULL, "PLAY");
		satipc_commit(ad);
		return 0;

	}

	if (sip->wp >= sip->qp)
		sip->expect_reply = 0;
	else
	{
		char *np = (char *) getItem(MAKE_ITEM(ad->id, sip->wp));
		if (np)
		{
			int len = strlen(np);
			if (sip->session[0] && !strstr(np, "Session:"))
				sprintf(np + len - 2, "Session: %s\r\n\r\n", sip->session);

			LOG("satipc_reply: sending next packet:\n%s", np);
			rv = write(s->sock, np, strlen(np));
			delItem(MAKE_ITEM(ad->id, sip->wp++));
		}
	}
	if (!sip->expect_reply && (sip->wp >= sip->qp)
					&& (sip->want_commit || sip->force_commit)) // we do not expect reply and no other events in the queue, we commit a
	{
		satipc_commit(ad);
	}

	if (!sip->expect_reply && sip->last_cmd == RTSP_PLAY)
		http_request(ad, NULL, "DESCRIBE");

	return 0;
}

int satipc_timeout(sockets *s)
{
	adapter *ad;
	satipc *sip;
	get_ad_and_sipr(s->sid, 1);
	if(sip->want_tune || sip->lap || sip->ldp)
	{
		LOG("no timeout will be performed as we have operations in queue");
		return 0;
	}
	LOG(
		"satipc: Sent keep-alive to the satip server %s:%d, adapter %d, socket_id %d, handle %d, timeout %d",
		ad ? sip->sip : NULL, ad ? sip->sport : 0, s->sid, s->id, s->sock,
		s->timeout_ms);
	if(sip->wp == sip->qp)
		http_request(ad, NULL, "OPTIONS");

	s->rtime = getTick();
	return 0;
}

int satipc_close(sockets * s)
{
	LOG("satip_close called for adapter %d, socket_id %d, handle %d timeout %d",
					s->sid, s->id, s->sock, s->timeout_ms);
	close_adapter(s->sid);
	return 0;
}

void set_adapter_signal(adapter *ad, char *b, int rlen)
{
	int i, strength, status, snr;
	char *ver, *tun, *signal = NULL;
	for (i = 0; i < rlen - 4; i++)
		if (b[i] == 'v' && b[i + 1] == 'e' && b[i + 2] == 'r'
						&& b[i + 3] == '=')
		{
			ver = b + i;
			tun = strstr((const char*) ver, "tuner=");
			if (tun)
				signal = strchr(tun, ',');
			if (signal)
			{
				sscanf(signal + 1, "%d,%d,%d", &strength, &status, &snr);
				if (ad->strength != strength && ad->snr != snr)
					LOGL(3,
										"satipc: Received signal status from the server for adapter %d, stength=%d status=%d snr=%d",
										ad->id, strength, status, snr);
				ad->strength = strength;
				ad->status = status ? FE_HAS_LOCK : 0;
				ad->snr = snr;
			}
		}

}

int satipc_rtcp_reply(sockets * s)
{
	unsigned char *b = s->buf;
	int rlen = s->rlen;
	adapter *ad;
	satipc *sip;
	get_ad_and_sipr(s->sid, 0);
	uint32_t rp;

	s->rlen = 0;
//	LOG("satip_rtcp_reply called");
	if (b[0] == 0x80 && b[1] == 0xC8)
	{
		copy32r(rp, b, 20);

		if (!sip->use_tcp && ((++sip->repno % 100) == 0))  //every 20s
			LOG(
				"satipc: rtp report, adapter %d: rtcp missing packets %d, rtp missing %d, rtp ooo %d, pid err %d",
				ad->id, rp - sip->rcvp, sip->rtp_miss, sip->rtp_ooo,
				ad->pid_err - ad->dec_err);
	}
	set_adapter_signal(ad, (char *)b, rlen);
	return 0;
}

int satipc_open_device(adapter *ad)
{
	satipc *sip = satip[ad->id];
	if (!sip || !sip->sip)
		return 1;

	int64_t ctime = getTick();
	if ((sip->last_connect > 0) && (ctime - sip->last_connect < 30000))
		return 3;

	sip->last_connect = ctime;
	ad->fe = tcp_connect(sip->sip, sip->sport, NULL, 0); // non-blockin socket
	if (ad->fe < 0)
		return 2;

	LOG("satipc: connected to SAT>IP server %s port %d %s handle %d", sip->sip,
					sip->sport, sip->use_tcp ? "[RTSP OVER TCP]" : "", ad->fe);
	if (!sip->use_tcp)
	{
		sip->listen_rtp = opts.start_rtp + 1000 + ad->id * 2;
		ad->dvr = udp_bind(NULL, sip->listen_rtp);
		sip->rtcp = udp_bind(NULL, sip->listen_rtp + 1);

		ad->fe_sock = sockets_add(ad->fe, NULL, ad->id, TYPE_TCP,
																												(socket_action) satipc_reply, (socket_action) satipc_close,
																												(socket_action) satipc_timeout);
		sip->rtcp_sock = sockets_add(sip->rtcp, NULL, ad->id, TYPE_TCP,
																															(socket_action) satipc_rtcp_reply, (socket_action) satipc_close,
																															NULL);
		sockets_timeout(ad->fe_sock, 30000); // 30s
		set_socket_receive_buffer(ad->dvr, opts.dvr_buffer);
		if (ad->fe_sock < 0 || ad->dvr < 0 || sip->rtcp < 0
						|| sip->rtcp_sock < 0)
		{
			sockets_del(sip->rtcp_sock);
			sockets_del(ad->fe_sock);
			close(sip->rtcp);
			close(ad->dvr);
			close(ad->fe);
		}
	}
	else
	{
		ad->dvr = ad->fe;
		ad->fe = -1;
		ad->fe_sock = sockets_add(SOCK_TIMEOUT, NULL, ad->id, TYPE_UDP,
																												NULL, NULL, (socket_action) satipc_timeout);
		sockets_timeout(ad->fe_sock, 30000); // 30s

	}
	sip->session[0] = 0;
	sip->lap = 0;
	sip->ldp = 0;
	sip->cseq = 1;
	sip->err = 0;
	sip->tcp_pos = sip->tcp_len = 0;
	sip->expect_reply = 0;
	sip->last_connect = 0;
	sip->sent_transport = 0;
	sip->session[0] = 0;
	sip->stream_id = -1;
	sip->wp = sip->qp = sip->want_commit = 0;
	sip->rcvp = sip->repno = 0;
	sip->rtp_miss = sip->rtp_ooo = 0;
	sip->rtp_seq = 0xFFFF;
	sip->ignore_packets = 1;
	sip->force_commit = 0;
	sip->last_setup = -10000;
	sip->last_cmd = 0;
	sip->enabled = 1;
	if(!sip->option_no_option)
		http_request(ad, NULL, "OPTIONS");

	return 0;

}

void satip_close_device(adapter *ad)
{
	satipc *sip = get_satip(ad->id);
	http_request(ad, NULL, "TEARDOWN");
	sip->session[0] = 0;
	sip->sent_transport = 0;
	if (ad->fe_sock > 0)
		sockets_del(ad->fe_sock);
	if (sip->rtcp_sock > 0)
		sockets_del(sip->rtcp_sock);
	ad->fe_sock = -1;
	sip->rtcp_sock = -1;
	sip->enabled = 0;

}

int satipc_read(int socket, void *buf, int len, sockets *ss, int *rb)
{
	unsigned char buf1[20];
	uint16_t seq;
	adapter *ad;
	satipc *sip;
	get_ad_and_sipr(ss->sid, 0);
	struct iovec iov[3] =
	{
		{ .iov_base = buf1, .iov_len = 12 },
		{ .iov_base = buf, .iov_len = len },
		{ NULL, 0 }
	};
	*rb = readv(socket, iov, 2);  // stripping rtp header
	if (*rb > 0)
	{
		ad = get_adapter(ss->sid);
		sip->rcvp++;

		copy16r(seq, buf1, 2);
		if (sip->rtp_seq == 0xFFFF)
			sip->rtp_seq = seq;
		if (seq > sip->rtp_seq)
			sip->rtp_miss++;
		else if (seq < sip->rtp_seq)
			sip->rtp_ooo++;
		sip->rtp_seq = (seq + 1) & 0xFFFF;
	}
	if (!ad)
		ad = get_adapter(ss->sid);
	if (ad && sip->ignore_packets)
	{
		*rb = 0;
		return 1;
	}
	*rb -= 12;
	return (*rb >= 0);
}

int process_rtsp_tcp(sockets *ss, unsigned char *rtsp, int rtsp_len, void *buf,
																					int len)
{
	int nl = 0;
	unsigned char tmp_char;
	satipc *sip = get_satip(ss->sid);
	adapter *ad = get_adapter(ss->sid);
	if (!ad || !sip)
		return 0;

	if (sip->ignore_packets)
		return 0;

	if (rtsp[1] == 1)
	{
		tmp_char = rtsp[rtsp_len + 4];
		rtsp[rtsp_len + 4] = 0;
		set_adapter_signal(ad, (char *)rtsp + 4, rtsp_len);
		rtsp[rtsp_len + 4] = tmp_char;
		return 0;
	}
	else if (rtsp[1] == 0)
	{
		nl = rtsp_len - 12;
		if (nl > len)
			nl = len;

		if (nl > 0)
			memcpy(buf, rtsp + 16, nl);

	}
	else
		LOG("Not processing packet as the type is %02X (not 0 or 1)", rtsp[1]);

	return nl;
}

int first;
int satipc_tcp_read(int socket, void *buf, int len, sockets *ss, int *rb)
{
	unsigned char *rtsp;
	sockets tmp_sock;
	int pos;
	int rtsp_len;
	int tmp_len = 0;
	adapter *ad;
	satipc *sip;
	int skipped_bytes = 0;
	get_ad_and_sipr(ss->sid, 0);
	*rb = 0;
//	LOG("start satipc_tcp_read");
	if (!sip->tcp_data)
	{
		sip->tcp_size = TCP_DATA_SIZE;
		sip->tcp_data = malloc1(sip->tcp_size + 3);
		if (!sip->tcp_data)
			LOG_AND_RETURN(-1, "Cannot alloc memory for tcp_data with size %d",
																		sip->tcp_size);
		memset(sip->tcp_data, 0, sip->tcp_size + 2);
	}

//	if (sip->tcp_len == sip->tcp_size && sip->tcp_pos == 0)
//	{
//		LOG("Probably the buffer needs to be increased, as it is full");
//		sip->tcp_len = 0;
//	}
	if (sip->tcp_len == sip->tcp_size && sip->tcp_pos)
	{
		int nl = sip->tcp_len - sip->tcp_pos;
		memmove(sip->tcp_data, sip->tcp_data + sip->tcp_pos, nl);
		LOGL(SATIP_LOG, "Moved from the position %d, length %d", sip->tcp_pos, nl);
		sip->tcp_pos = 0;
		sip->tcp_len = nl;

	}
	uint8_t *tmp_b = sip->tcp_data + sip->tcp_len;
	int expected_len = sip->tcp_size - sip->tcp_len;
	if(expected_len > 0 )
	{
		tmp_len = read(socket, sip->tcp_data + sip->tcp_len, expected_len);
		LOGL(SATIP_LOG, "read %d (from %d) from rtsp socket %d (id %d) [%02X %02X, %02X %02X]", tmp_len, sip->tcp_size - sip->tcp_len, socket, ss->id, tmp_b[0],tmp_b[1],tmp_b[2],tmp_b[3]);
		if (tmp_len <= 0)
			LOG_AND_RETURN(0, "read %d from RTSP socket, errno %d, %s", tmp_len, errno, strerror(errno));
	}else
	{
		tmp_b = sip->tcp_data + sip->tcp_pos;
		LOGL(3, "buffer is full, skipping read, %d, pos %d [%02X %02X, %02X %02X]", sip->tcp_size, sip->tcp_pos, tmp_b[0],tmp_b[1],tmp_b[2],tmp_b[3]);
/*		if(!first)
   {
   first = 1;
   LOG("tcp_size %d tcp_len %d", sip->tcp_size, sip->tcp_len);
   hexdump("buffer full", sip->tcp_data, sip->tcp_len);
   }
   if(sip->tcp_size * 2 > TCP_DATA_MAX)
   {
   int new_size = sip->tcp_size * 2;
   LOG("allocating %d", new_size);
   void *p = realloc(sip->tcp_data, new_size + 10);
   if(p)
   {
    sip->tcp_data = p;
    memset(sip->tcp_data + sip->tcp_size, 0, new_size - sip->tcp_size);
    sip->tcp_size = new_size;

   }
   else LOG("reallocation failed for %d bytes", new_size);
   }
 */
	}
	pos = 0;
	sip->tcp_len += tmp_len;
	while (sip->tcp_pos < sip->tcp_len - 6)
	{
		rtsp = sip->tcp_data + sip->tcp_pos;
		if ((rtsp[0] == 0x24) && (rtsp[1] < 2) && (rtsp[4] == 0x80)
						&& ((rtsp[5] == 0x21) || (rtsp[5] == 0xC8)))
		{
			copy16r(rtsp_len, rtsp, 2);
//			LOG("found at pos %d, rtsp_len %d, len %d", sip->tcp_pos, rtsp_len, sip->tcp_len);
			if(skipped_bytes)
			{
				LOG("%s: skipped %d bytes", __FUNCTION__, skipped_bytes);
				skipped_bytes = 0;
			}
			// debug
//			if((rtsp[1] == 0) && (((rtsp_len - 12) > 26320) || (((rtsp_len - 12) % 188) != 0)))
//				LOG("invalid rtsp_len %d", rtsp_len);

			if (rtsp_len + 4 + sip->tcp_pos > sip->tcp_len) // expecting more data in the buffer
			{
				LOGL(SATIP_LOG, "satip buffer is full @ pos %d, tcp_pos %d, required %d len %d tcp_len %d, tcp_size %d, left to read %d",
									pos, sip->tcp_pos, rtsp_len - 12, len, sip->tcp_len, sip->tcp_size, rtsp_len + 4 + sip->tcp_pos - sip->tcp_len);
				break;
			}

			if (rtsp[1] == 0 && (rtsp_len - 12 + pos > len)) // destination buffer full
			{
				ad->flush = 1;
				LOGL(SATIP_LOG, "Destination buffer is full @ pos %d, tcp_pos %d, required %d len %d",
									pos, sip->tcp_pos, rtsp_len - 12, len );
				break;
			}
			sip->tcp_pos += rtsp_len + 4;
			LOGL(SATIP_LOG, "ad %d processed %d, socket %d", ad->id, rtsp_len, socket);
			pos += process_rtsp_tcp(ss, rtsp, rtsp_len, buf + pos, len - pos);
			*rb = pos;

		}
		else if (!strncmp((char *)rtsp, "RTSP", 4))
		{
			unsigned char *nlnl, *cl;
			int bytes, icl = 0;
			unsigned char tmp_char;
			if(skipped_bytes)
			{
				LOG("%s: skipped %d bytes", __FUNCTION__, skipped_bytes);
				skipped_bytes = 0;
			}
			nlnl = (unsigned char *)strstr((char *)rtsp, "\r\n\r\n");
//			LOG("found RTSP nlnl %d, len %d", nlnl - rtsp, sip->tcp_len);
			if(nlnl > sip->tcp_data + sip->tcp_len)
			{
				LOGL(3, "Unlikely, found newline after the end of string, tcp_pos %d", sip->tcp_pos);
				nlnl = NULL;
				sip->tcp_data[sip->tcp_size + 1] = 0;
			}
			if (nlnl && (cl = (unsigned char *)strcasestr((char *)rtsp, "content-length:")))
			{
				cl += 15;
				while (*cl == 0x20)
					cl++;

				icl = map_intd((char *)cl, NULL, 0);
				nlnl += icl;
			}
			if (!nlnl)
			{
				LOGL(3, "End of rtsp rtsp message not found in this block, pos %d, tcp_pos %d, len %d, size %d", pos, sip->tcp_pos, len, sip->tcp_size);
				break;
			}else if(nlnl > sip->tcp_data + sip->tcp_len)
			{
				LOGL(3, "Found newline after the end of string, tcp_pos %d", sip->tcp_pos);
				break;
			}
			memset(&tmp_sock, 0, sizeof(tmp_sock));
			bytes = nlnl - rtsp;
			sip->tcp_pos += bytes + 4;
			tmp_sock.buf = rtsp;
			tmp_sock.rlen = bytes;
			tmp_sock.sid = ss->sid;
			tmp_sock.sock = ad->dvr;
			tmp_sock.id = ss->id;
			tmp_char = rtsp[bytes + 4];
			rtsp[bytes + 4] = 0;
			LOGL(3, "sending %d bytes to satipc_reply, cl %d, pos %d, tcp_len %d, left %d", bytes + 4, icl, sip->tcp_pos - bytes - 4, sip->tcp_len, pos);
			satipc_reply(&tmp_sock);
			rtsp[bytes + 4] = tmp_char;
		}
		else
		{
			LOGL(SATIP_LOG + 1, "ignoring byte %02X", rtsp[0]);
			skipped_bytes++;
			sip->tcp_pos++;
		}
	}

	if (sip->tcp_pos == sip->tcp_len)
		sip->tcp_pos = sip->tcp_len = 0;
	LOGL(SATIP_LOG, "%s: returning %d bytes", __FUNCTION__, *rb);
	return (*rb >= 0);
}

void satip_post_init(adapter *ad)
{
	satipc *sip;
	get_ad_and_sip(ad->id);
	if (sip->use_tcp)
		sockets_setread(ad->sock, satipc_tcp_read);
	else
	{
		sockets_setread(ad->sock, satipc_read);
		set_socket_thread(sip->rtcp_sock, get_socket_thread(ad->sock));
	}
	set_socket_thread(ad->fe_sock, get_socket_thread(ad->sock));
}

int satipc_set_pid(adapter *ad, uint16_t pid)
{
	satipc *sip;
	sip = get_satip(ad->id);
	int aid = ad->id;
	if (sip->err) // error reported, return error
		return 0;
	LOG("satipc: set_pid for adapter %d, pid %d, err %d", aid, pid, sip->err);
	if (sip->err) // error reported, return error
		return 0;
	sip->apid[sip->lap] = pid;
	sip->lap++;
	return aid + 100;
}

int satipc_del_filters(int fd, int pid)
{
	adapter *ad;
	satipc *sip;
	fd -= 100;
	get_ad_and_sipr(fd, 0);
	LOG("satipc: del_pid for aid %d, pid %d, err %d", fd, pid, sip->err);
	if (sip->err) // error reported, return error
		return 0;
	sip->dpid[sip->ldp] = pid;
	sip->ldp++;
	return 0;
}

void get_s2_url(adapter *ad, char *url)
{
#define FILL(req, val, def, met) if((val != def) && (val!=-1)) len +=sprintf(url + len, req, met);
	int len = 0, plts, ro;
	transponder * tp = &ad->tp;
	satipc *sip = get_satip(ad->id);
	if (!sip)
		return;
	url[0] = 0;
	plts = tp->plts;
	ro = tp->ro;
//	if (plts == PILOT_AUTO)
//		plts = PILOT_OFF;
//	if (ro == ROLLOFF_AUTO)
//		ro = ROLLOFF_35;
	FILL("src=%d", tp->diseqc, 0, tp->diseqc);
	if (sip->use_fe && (sip->satip_fe > 0) && (sip->satip_fe < 128))
		FILL("&fe=%d", sip->satip_fe, 0, sip->satip_fe);
	FILL("&freq=%d", tp->freq, 0, tp->freq / 1000);
	FILL("&msys=%s", tp->sys, 0, get_delsys(tp->sys));
	FILL("&mtype=%s", tp->mtype, -1, get_modulation(tp->mtype));
	FILL("&pol=%s", tp->pol, -1, get_pol(tp->pol));
	FILL("&sr=%d", tp->sr, -1, tp->sr / 1000);
	FILL("&fec=%s", tp->fec, FEC_AUTO, get_fec(tp->fec));
	FILL("&ro=%s", ro, ROLLOFF_AUTO, get_rolloff(ro));
	FILL("&plts=%s", plts, PILOT_AUTO, get_pilot(plts));
	url[len] = 0;
	return;
}

void get_c2_url(adapter *ad, char *url)
{
	int len = 0;
	transponder * tp = &ad->tp;
	satipc *sip = get_satip(ad->id);
	if (!sip)
		return;
	url[0] = 0;
	FILL("freq=%.1f", tp->freq, 0, tp->freq / 1000.0);
	if (sip->use_fe)
		FILL("&fe=%d", sip->satip_fe, 0, sip->satip_fe);
	FILL("&sr=%d", tp->sr, -1, tp->sr / 1000);
	FILL("&msys=%s", tp->sys, 0, get_delsys(tp->sys));
	FILL("&mtype=%s", tp->mtype, -1, get_modulation(tp->mtype));
	FILL("&gi=%s", tp->gi, GUARD_INTERVAL_AUTO, get_gi(tp->gi));
	FILL("&fec=%s", tp->fec, FEC_AUTO, get_fec(tp->fec));
	FILL("&tmode=%s", tp->tmode, TRANSMISSION_MODE_AUTO, get_tmode(tp->tmode));
	FILL("&specinv=%d", tp->inversion, INVERSION_AUTO, tp->inversion);
	FILL("&t2id=%d", tp->t2id, 0, tp->t2id);
	FILL("&sm=%d", tp->sm, 0, tp->sm);
	FILL("&plp=%d", tp->plp, 0, tp->plp);
	url[len] = 0;
	return;
}

void get_t2_url(adapter *ad, char *url)
{
	int len = 0;
	transponder * tp = &ad->tp;
	satipc *sip = get_satip(ad->id);
	if (!sip)
		return;
	url[0] = 0;
	FILL("freq=%.1f", tp->freq, 0, tp->freq / 1000.0);
	if (sip->use_fe)
		FILL("&fe=%d", sip->satip_fe, 0, sip->satip_fe);
	FILL("&bw=%d", tp->bw, BANDWIDTH_AUTO, tp->bw / 1000000);
	FILL("&msys=%s", tp->sys, 0, get_delsys(tp->sys));
	FILL("&mtype=%s", tp->mtype, -1, get_modulation(tp->mtype));
	FILL("&gi=%s", tp->gi, GUARD_INTERVAL_AUTO, get_gi(tp->gi));
	FILL("&tmode=%s", tp->tmode, TRANSMISSION_MODE_AUTO, get_tmode(tp->tmode));
	FILL("&specinv=%d", tp->inversion, INVERSION_AUTO, tp->inversion);
	FILL("&c2tft=%d", tp->c2tft, 0, tp->c2tft);
	FILL("&ds=%d", tp->ds, 0, tp->ds);
	FILL("&plp=%d", tp->plp, 0, tp->plp);
	url[len] = 0;
	return;
}

int http_request(adapter *ad, char *url, char *method)
{
	char session[200];
	char buf[2048];
	char sid[40];
	char *qm;
	int lb, remote_socket;
	char format[] = "%s rtsp://%s:%d/%s%s%s RTSP/1.0\r\nCSeq: %d%s\r\n\r\n";
	__attribute__((unused)) int rv;
	satipc *sip = get_satip(ad->id);
	if (!sip)
		return 0;

	session[0] = 0;
	sid[0] = 0;
	remote_socket = sip->use_tcp ? ad->dvr : ad->fe;

	if (!sip->option_no_setup && !method && sip->sent_transport == 0)
		method = "SETUP";

	if (!method)
		method = "PLAY";

	if (sip->sent_transport == 0 && (method[0] == 'S' || method[0] == 'P'))
	{
		sip->last_setup = getTick();
		sip->sent_transport = 1;
		sip->stream_id = -1;
		sip->session[0] = 0;
		if (sip->use_tcp)
			sprintf(session, "\r\nTransport: RTP/AVP/TCP;interleaved=0-1");
		else
			sprintf(session, "\r\nTransport:RTP/AVP;unicast;client_port=%d-%d",
											sip->listen_rtp, sip->listen_rtp + 1);
	}
	else
	{
		if (sip->session[0])
			sprintf(session, "\r\nSession: %s", sip->session);
		else
			session[0] = 0;
	}

	if (strcmp(method, "OPTIONS") == 0)
	{
		sprintf(session + strlen(session), "\r\nUser-Agent: %s %s", app_name,
										version);
		sip->last_cmd = RTSP_OPTIONS;
	}

	if (!strcmp(method, "PLAY"))
		sip->last_cmd = RTSP_PLAY;
	else if (!strcmp(method, "TEARDOWN"))
		sip->last_cmd = RTSP_TEARDOWN;
	else if (!strcmp(method, "DESCRIBE"))
		sip->last_cmd = RTSP_DESCRIBE;
	else if (!strcmp(method, "SETUP"))
		sip->last_cmd = RTSP_SETUP;

	qm = "?";
	if (!url || !url[0])
		qm = "";

	if (!url)
		url = "";

	if (sip->stream_id != -1)
		sprintf(sid, "stream=%d", sip->stream_id);

	lb = snprintf(buf, sizeof(buf), format, method, sip->sip, sip->sport, sid,
															qm, url, sip->cseq++, session);

	LOG("satipc_http_request (ad %d): %s to handle %d: \n%s", ad->id,
					sip->expect_reply ? "queueing" : "sending", remote_socket, buf);
	if (sip->expect_reply)
	{
		setItem(MAKE_ITEM(ad->id, sip->qp++), (unsigned char *) buf, lb + 1, 0);
	}
	else
	{
		sip->wp = sip->qp = 0;
		rv = write(remote_socket, buf, lb);
		if(rv != lb)
			LOG("satipc: write on socket %d, error %d: %d from %d written (%s)", remote_socket, errno, rv, lb, strerror(errno));
	}
	sip->expect_reply = 1;
	return 0;
}

void tune_url(adapter *ad, char *url)
{
	switch (ad->sys[0])
	{
	case SYS_DVBS2:
	case SYS_DVBS:
		get_s2_url(ad, url);
		break;
	case SYS_DVBC2:
	case SYS_DVBC_ANNEX_A:
		get_c2_url(ad, url);
		break;
	case SYS_DVBT2:
	case SYS_DVBT:
	case SYS_ISDBT:
		get_t2_url(ad, url);
		break;
	}
}

void satipc_commit(adapter *ad)
{
	char url[400];
	char tmp_url[400];
	int send_pids = 1, send_apids = 1, send_dpids = 1;
	int len = 0, i;
	satipc *sip = get_satip(ad->id);
	if (!sip)
		return;

	url[0] = 0;
	LOG(
		"satipc: commit for adapter %d pids to add %d, pids to delete %d, expect_reply %d, force_commit %d want_tune %d",
		ad->id, sip->lap, sip->ldp, sip->expect_reply, sip->force_commit,
		sip->want_tune);

	if (ad->do_tune && !sip->want_tune)
	{
		sip->lap = sip->ldp = 0;
		return;
	}
	if (sip->lap + sip->ldp == 0)
		if (!sip->force_commit || !ad->tp.freq)
			return;

	if (sip->expect_reply)
	{
		sip->want_commit = 1;
		return;
	}

	if (!sip->addpids)
	{
		send_apids = 0;
		send_dpids = 0;
	}

	if (!sip->sent_transport)
	{
		send_apids = 0;
		send_dpids = 0;
		if (!sip->setup_pids)
			send_pids = 0;
	}

	if (sip->force_commit && sip->sent_transport)
		send_pids = 1;

	if (send_apids && send_pids)
		send_pids = 0;

	send_apids = send_apids && sip->lap > 0;
	send_dpids = send_dpids && sip->ldp > 0;

	if (getTick() - sip->last_setup < 10000 && !sip->sent_transport)
	{
		LOG(
			"satipc: last setup less than 10 seconds ago for server %s, maybe an error ?",
			sip->sip);
		return;
	}

//	if (sip->last_cmd != RTSP_OPTIONS && sip->sent_transport == 0)
//	{
//		http_request(ad, NULL, "OPTIONS");
//		sip->force_commit = 1;
//		return;
//	}

	if (ad->do_tune && sip->want_tune) // subsequent PLAY command should have pids
	{
		send_pids = 1;
		send_apids = 0;
		send_dpids = 0;
	}
	if (sip->sent_transport == 0)
		sip->want_tune = 1;

	if(!sip->sent_transport && sip->option_no_setup)
	{
		send_pids = 1;
		send_apids = 0;
		send_dpids = 0;
	}
	sip->want_commit = 0;
	if (sip->want_tune + send_pids + send_apids + send_dpids == 0)
	{
		LOG("satipc: nothing to commit");
		sip->force_commit = 0;
		if (sip->last_cmd == RTSP_SETUP && !sip->setup_pids)
			send_apids = 1; // force send addpids=
	}
	if (sip->want_tune)
	{
		tune_url(ad, url);
		len = strlen(url);
		sip->ignore_packets = 1; // ignore all the packets until we get 200 from the server
		sip->want_tune = 0;
		if (!sip->setup_pids)
			sprintf(url + len, "&pids=none");
	}

	get_adapter_pids(ad->id, tmp_url, sizeof(tmp_url));
	if ((send_apids || send_dpids)
					&& (!strcmp(tmp_url, "all") || !strcmp(tmp_url, "none")))
	{
		send_apids = send_dpids = 0;
		send_pids = 1;
	}

	if (send_pids)
	{
		if (len > 0)
			len += sprintf(url + len, "&");
		len += sprintf(url + len, "pids=");
//		get_adapter_pids(ad->id, url + len, sizeof(url) - len);
		strncpy(url + len, tmp_url, sizeof(url) - len);
		url[sizeof(url) - 1] = 0;
		sip->lap = 0;
		sip->ldp = 0;
		sip->force_commit = 0;
	}

	if (send_apids)
	{
		int i;
		if (len > 0)
			len += sprintf(url + len, "&");
		len += sprintf(url + len, "addpids=");
		for (i = 0; i < sip->lap; i++)
			len += sprintf(url + len, "%d,", sip->apid[i]);
		if (sip->lap == 0)
			get_adapter_pids(ad->id, url + len, sizeof(url) - len);
		else
			url[--len] = 0;
		sip->lap = 0;
		sip->force_commit = 0;
	}

	if (send_dpids)
	{
		if (len > 0)
			len += sprintf(url + len, "&");
		len += sprintf(url + len, "delpids=");
		for (i = 0; i < sip->ldp; i++)
			len += sprintf(url + len, "%d,", sip->dpid[i]);
		url[--len] = 0;
		sip->ldp = 0;
		sip->force_commit = 0;
	}

	http_request(ad, url, NULL);

	return;
}

int satipc_tune(int aid, transponder * tp)
{
	adapter *ad;
	satipc *sip;
	get_ad_and_sipr(aid, 1);
	LOG("satipc: tuning to freq %d, sys %d for adapter %d", ad->tp.freq / 1000,
					ad->tp.sys, aid);
	sip->err = 0;
	ad->strength = 0;
	ad->status = 0;
	ad->snr = 0;
	sip->want_commit = 0;
	sip->want_tune = 1;
	sip->ignore_packets = 1; // ignore all the packets until we get 200 from the server
	sip->lap = 0;
	sip->ldp = 0;
	sip->use_fe = 0;
	if (tp->fe > 0)
		sip->use_fe = 1;
	/*	if(sip->sent_transport == 0)
	   {
	   tune_url(ad, url);
	   url[strlen(url) - 6] = 0;
	   //		sprintf(url + strlen(url), "0");
	   http_request(ad, url, "SETUP");
	   sip->want_tune = 0;
	   }
	 */
	return 0;
}

fe_delivery_system_t satipc_delsys(int aid, int fd, fe_delivery_system_t *sys)
{
	return 0;
}

uint8_t determine_fe(adapter **a, int pos, char *csip, int sport)
{
	int i = pos;
	while (i > 0)
	{
		i--;
		adapter *ad = a[i];
		satipc *sip = satip[i];
		if (!ad || !sip)
			continue;
		if (sport == sip->sport && !strcmp(sip->sip, csip))
			return sip->satip_fe + 1;
	}
	return 1;

}

void find_satip_adapter(adapter **a)
{
	int i, la, j, k;
	char *sep1, *sep2, *sep;
	char *arg[50];
	adapter *ad;
	satipc *sip;

	if (!opts.satip_servers || !opts.satip_servers[0])
		return;
	char satip_servers[strlen(opts.satip_servers)+10];
	strcpy(satip_servers, opts.satip_servers);
	la = split(arg, satip_servers, 50, ',');
	j = 0;
	for (i = 0; i < MAX_ADAPTERS; i++)
		if(!a[i] || a[i]->type == ADAPTER_SATIP || a[i]->type == 0)
		{
			if (j >= la)
				break;

			if (is_adapter_disabled(i))
				continue;
			if (!a[i])
				a[i] = adapter_alloc();
			if (!satip[i])
			{
				satip[i] = malloc1(sizeof(satipc));
				if(satip[i])
					memset(satip[i], 0, sizeof(satipc));
			}
			sip = satip[i];
			ad = a[i];
			mutex_lock(&ad->mutex);
			ad->open = (Open_device) satipc_open_device;
			ad->set_pid = (Set_pid) satipc_set_pid;
			ad->del_filters = (Del_filters) satipc_del_filters;
			ad->commit = (Adapter_commit) satipc_commit;
			ad->tune = (Tune) satipc_tune;
			ad->delsys = (Dvb_delsys) satipc_delsys;
			ad->post_init = (Adapter_commit) satip_post_init;
			ad->close = (Adapter_commit) satip_close_device;
			ad->type = ADAPTER_SATIP;

			for (k = 0; k < 10; k++)
				ad->sys[k] = 0;

			sep = NULL;
			sep1 = NULL;
			sep2 = NULL;
			sep1 = strchr(arg[j], ':');
			if (sep1)
				sep2 = strchr(sep1 + 1, ':');
			if (map_intd(arg[j], fe_delsys, -1) != -1)
				sep = arg[j];

			if (sep1)
				*sep1++ = 0;
			if (sep2)
				*sep2++ = 0;

			if (sep)
			{
				if (!sep1)
				{
					LOG("Found only the system for satip adapter %s", arg[j]);
					continue;
				}
			}
			else
			{
				if (sep1)
					sep2 = sep1;
				sep1 = arg[j];

			}

			if (!sep)
				sep = "dvbs2";
			if (!sep2)
				sep2 = "554";
			memset(sip->sip, 0, sizeof(sip->sip));
			ad->sys[0] = ad->tp.sys = map_int(sep, fe_delsys);
			strncpy(sip->sip, sep1, sizeof(sip->sip) - 1);
			sip->satip_fe = -1;
			if(strchr(sip->sip, '@'))
			{
				sip->satip_fe = map_int(sep1, NULL);
				memmove(sip->sip, strchr(sip->sip, '@') + 1, sizeof(sip->sip) - 1);
			}
			sip->sport = map_int(sep2, NULL);
			if (ad->sys[0] == SYS_DVBS2)
				ad->sys[1] = SYS_DVBS;
			if (ad->sys[0] == SYS_DVBT2)
				ad->sys[1] = SYS_DVBT;
			if (ad->sys[0] == SYS_DVBC2)
				ad->sys[1] = SYS_DVBC_ANNEX_A;
			if(sip->satip_fe == -1)
				sip->satip_fe = determine_fe(a, i, sip->sip, sip->sport);
			sip->addpids = opts.satip_addpids;
			sip->setup_pids = opts.satip_setup_pids;
			sip->tcp_size = 0;
			sip->tcp_data = NULL;
			sip->use_tcp = opts.satip_rtsp_over_tcp;

			if(i + 1 > a_count)
				a_count = i + 1; // update adapter counter

			j++;
			LOG("AD%d: Satip device %s port %d delsys %d: %s %s, total number of devices %d", ad->id, sip->sip,
							sip->sport, ad->sys[0], get_delsys(ad->sys[0]),
							get_delsys(ad->sys[1]), a_count);
			mutex_unlock(&ad->mutex);
		}
}


_symbols satipc_sym[] =
{
	{ "ad_satip", VAR_AARRAY_STRING, satip, 1, MAX_ADAPTERS, offsetof(
				satipc, sip) },
	{ NULL, 0, NULL, 0, 0 }
};
