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
#include <net/if.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>
#include <fcntl.h>
#include <ctype.h>
#include "dvb.h"
#include "minisatip.h"
#ifdef AXE
#include "axe.h"
#include "adapter.h"
#endif

extern struct struct_opts opts;

struct diseqc_cmd
{
	struct dvb_diseqc_master_cmd cmd;
	uint32_t wait;
};

char *fe_pilot[] =
{
	"on",
	"off",
	" ", //auto
	NULL
};

char *fe_rolloff[] =
{
	"0.35",
	"0.20",
	"0.25",
	" ", //auto
	NULL
};

char *fe_delsys[] =
{
	"undefined",
	"dvbc",
	"dvbcb",
	"dvbt",
	"dss",
	"dvbs",
	"dvbs2",
	"dvbh",
	"isdbt",
	"isdbs",
	"isdbc",
	"atsc",
	"atscmh",
	"dmbth",
	"cmmb",
	"dab",
	"dvbt2",
	"turbo",
	"dvbcc",
	"dvbc2",
	NULL
};

char *fe_fec[] =
{
	"none",
	"12",
	"23",
	"34",
	"45",
	"56",
	"67",
	"78",
	"89",
	" ", //auto
	"35",
	"910",
	"25",
	NULL
};

char *fe_modulation[] =
{
	"qpsk",
	"16qam",
	"32qam",
	"64qam",
	"128qam",
	"256qam",
	" ", // auto
	"8vsb",
	"16vsb",
	"8psk",
	"16apsk",
	"32apsk",
	"dqpsk",
	NULL
};

char *fe_tmode[] =
{
	"2k",
	"8k",
	" ", //auto
	"4k",
	"1k",
	"16k",
	"32k",
	"c1",
	"c3780",
	NULL
};

char *fe_gi[] =
{
	"132",
	"116",
	"18",
	"14",
	" ", // auto
	"1128",
	"19128",
	"19256",
	"pn420",
	"pn595",
	"pn945",
	NULL
};

char *fe_hierarchy[] =
{
	"HIERARCHY_NONE",
	"HIERARCHY_1",
	"HIERARCHY_2",
	"HIERARCHY_4",
	"HIERARCHY_AUTO",
	NULL	
};

char *fe_specinv[] =
{
	"off",
	"on",
	" ", // auto
	NULL
};

char *fe_pol[] =
{
	"none",
	"v",
	"h",
	"l",
	"r",
	NULL
};

#define make_func(a) \
char * get_##a(int i) \
{ \
	if(i>=0 && i<sizeof(fe_##a)) \
		if(fe_##a[i][0] == 32)return ""; \
			else return fe_##a[i];  \
	return "NONE"; \
} 
make_func ( pilot );
make_func ( rolloff );
make_func ( delsys );
make_func ( fec );
make_func( modulation );
make_func ( tmode );
make_func ( gi ); 
make_func( specinv );
make_func( pol );



void
msleep (uint32_t msec)
{
	struct timespec req = { msec / 1000, 1000000 * (msec % 1000) };

	while (nanosleep (&req, &req))
		;
}

#ifdef AXE
void axe_wakeup(int fe_fd, int voltage)
{
	int i, mask;
	adapter *a;
	if (opts.axe_power < 2)
		return;
	for (i = 0; i < 4; i++) {
		a = get_adapter(i);
		if (a == NULL || a->force_disable)
			continue;
		if (a->tp.old_pol >= 0)
			return;
	}
	LOG("AXE wakeup");
	for (i = mask = 0; i < 4; i++) {
		/* lowband enabled */
		if (opts.quattro && opts.quattro_hiband == 1 && i < 2) {
			mask = 3;
			continue;
		}
		/* hiband enabled */
		if (opts.quattro && opts.quattro_hiband == 2 && i >= 2) {
			mask = 3<<2;
			continue;
		}
		mask |= 1<<i;
	}
	for (i = 0; i < 4 && mask; i++) {
		if (((1 << i) & mask) == 0)
			continue;
		a = get_adapter2(i);
		if (a == NULL || a->force_disable)
			continue;
		if (ioctl(a->fe, FE_SET_VOLTAGE, voltage) == -1)
			LOG("axe_wakeup: FE_SET_VOLTAGE failed fd %d: %s", a->fe, strerror(errno));
	}
}
#endif

int send_diseqc(int fd, int pos, int pol, int hiband)
{
	struct dvb_diseqc_master_cmd cmd = {
		{0xe0, 0x10, 0x38, 0xf0, 0x00, 0x00}, 4
	};

	cmd.msg[3] = 0xf0 | ( ((pos << 2) & 0x0c) | (hiband ? 1 : 0) | (pol ? 2 : 0));

	LOGL(3, "send_diseqc fd %d, pos = %d, pol = %d, hiband = %d, diseqc => %02x %02x %02x %02x %02x",
                  fd, pos, pol, hiband, cmd.msg[0], cmd.msg[1], cmd.msg[2], cmd.msg[3], cmd.msg[4]);

	
	if (ioctl(fd, FE_SET_TONE, SEC_TONE_OFF) == -1)
		LOG("send_diseqc: FE_SET_TONE failed for fd %d: %s", fd, strerror(errno));
#ifdef AXE
	axe_wakeup(fd, pol ? SEC_VOLTAGE_18 : SEC_VOLTAGE_13);
#endif
	if (ioctl(fd, FE_SET_VOLTAGE, pol ? SEC_VOLTAGE_18 : SEC_VOLTAGE_13) == -1)
		LOG("send_diseqc: FE_SET_VOLTAGE failed for fd %d: %s", fd, strerror(errno));

	msleep(15);
	if (ioctl(fd, FE_DISEQC_SEND_MASTER_CMD, &cmd) == -1)
		LOG( "send_diseqc: FE_DISEQC_SEND_MASTER_CMD failed for fd %d: %s", fd, strerror(errno));	
	
	msleep(15);
	if (ioctl(fd, FE_DISEQC_SEND_BURST, (pos & 1)?SEC_MINI_B : SEC_MINI_A ) == -1)
		LOG("send_diseqc: FE_DISEQC_SEND_BURST failed for fd %d: %s", fd, strerror(errno));
	msleep(15);
	
	if (ioctl(fd, FE_SET_TONE, hiband ? SEC_TONE_ON : SEC_TONE_OFF) == -1)
		LOG("send_diseqc: FE_SET_TONE failed for fd %d: %s", fd, strerror(errno));	
		
	return 0;
}

int send_unicable(int fd, int freq, int pos, int pol, int hiband, int slot, int ufreq)
{
	struct dvb_diseqc_master_cmd cmd = {
		{0xe0, 0x11, 0x5a, 0x00, 0x00}, 5
	};
	int t, o13v = 0;

        if (ufreq < 0) {
        	o13v = 1;
        	ufreq = -ufreq;
	}
	t = (freq + ufreq + 2) / 4 - 350;

	cmd.msg[3] = ((t & 0x0300) >> 8) | 
		(slot << 5) | (pos ? 0x10 : 0) | (hiband ? 4 : 0) | (pol ? 8 : 0);
	cmd.msg[4] = t & 0xff;

	LOGL(3, "send_unicable fd %d, freq %d, ufreq %d, pos = %d, pol = %d, hiband = %d, slot %d, diseqc => %02x %02x %02x %02x %02x",
                  fd, freq, ufreq, pos, pol, hiband, slot, cmd.msg[0], cmd.msg[1], cmd.msg[2], cmd.msg[3], cmd.msg[4]);
#ifdef AXE
	axe_wakeup(fd, SEC_VOLTAGE_13);
#endif
	if (ioctl(fd, FE_SET_VOLTAGE, SEC_VOLTAGE_13) == -1)
		LOG("send_unicable: pre voltage  SEC_VOLTAGE_13 failed for fd %d: %s", fd, strerror(errno));
	msleep(15);
	if (ioctl(fd, FE_SET_TONE, SEC_TONE_OFF) == -1)
		LOG("send_unicable: FE_SET_TONE failed for fd %d: %s", fd, strerror(errno));	
	if (!o13v && ioctl(fd, FE_SET_VOLTAGE, SEC_VOLTAGE_18) == -1)
		LOG("send_unicable: FE_SET_VOLTAGE failed for fd %d: %s", fd, strerror(errno));	
	msleep(15);
	if (ioctl(fd, FE_DISEQC_SEND_MASTER_CMD, &cmd) == -1)
		LOG("send_unicable: FE_DISEQC_SEND_MASTER_CMD failed for fd %d: %s", fd, strerror(errno));
	msleep(15);
	if (ioctl(fd, FE_SET_VOLTAGE, SEC_VOLTAGE_13) == -1)
		LOG("send_unicable: FE_SET_VOLTAGE failed for fd %d: %s", fd, strerror(errno));
	
	return ufreq * 1000;
}

int send_jess(int fd, int freq, int pos, int pol, int hiband, int slot, int ufreq)
{
	struct dvb_diseqc_master_cmd cmd = {
		{0x70, 0x00, 0x00, 0x00, 0x00}, 4
	};
//	int t = (freq / 1000) - 100;
	int t = freq - 100;
	int o13v = ufreq < 0;

	cmd.msg[1] = slot << 3;
	cmd.msg[1] |= ((t << 8) & 0x07);
	cmd.msg[2] = (t & 0xff);
	cmd.msg[3] = ((pos & 0x3f) << 2) | (pol ? 2 : 0) | (hiband ? 1 : 0);

	LOGL(3, "send_jess fd %d, freq %d, ufreq %d, pos = %d, pol = %d, hiband = %d, slot %d, diseqc => %02x %02x %02x %02x %02x",
                  fd, freq, ufreq, pos, pol, hiband, slot, cmd.msg[0], cmd.msg[1], cmd.msg[2], cmd.msg[3], cmd.msg[4]);

#ifdef AXE
	axe_wakeup(fd, SEC_VOLTAGE_13);
#endif
	if (ioctl(fd, FE_SET_VOLTAGE, SEC_VOLTAGE_13) == -1)
		LOG("send_jess: pre voltage  SEC_VOLTAGE_13 failed for fd %d: %s", fd, strerror(errno));
	msleep(15);
	if (ioctl(fd, FE_SET_TONE, SEC_TONE_OFF) == -1)
		LOG("send_jess: FE_SET_TONE failed for fd %d: %s", fd, strerror(errno));
	if (!o13v && ioctl(fd, FE_SET_VOLTAGE, SEC_VOLTAGE_18) == -1)
		LOG("send_jess: FE_SET_VOLTAGE failed for fd %d: %s", fd, strerror(errno));
	msleep(15);
	if (ioctl(fd, FE_DISEQC_SEND_MASTER_CMD, &cmd) == -1)
		LOG("send_jess: FE_DISEQC_SEND_MASTER_CMD failed for fd %d: %s", fd, strerror(errno));
	msleep(15);
	if (ioctl(fd, FE_SET_VOLTAGE, SEC_VOLTAGE_13) == -1)
		LOG("send_jess: FE_SET_VOLTAGE failed for fd %d: %s", fd, strerror(errno));

	return ufreq * 1000;	
}

#ifdef AXE
static inline int extra_quattro(int input, int diseqc, int *equattro)
{
  if (diseqc <= 0)
    *equattro = 0;
  /* lowband allowed - control the hiband inputs independently for positions src=2+ */
  else if (opts.quattro && opts.quattro_hiband == 1 && input < 2)
    *equattro = diseqc;
  /* hiband allowed - control the lowband inputs independently for positions src=2+ */
  else if (opts.quattro && opts.quattro_hiband == 2 && input >= 2 && input < 4)
    *equattro = diseqc;
  else
    *equattro = 0;
  return *equattro;
}

adapter *use_adapter(int input)
{
  adapter *ad = input < 4 ? get_adapter2(input) : NULL;
  char buf[32];
  if (ad) {
    if (ad->fe2 <= 0) {
      sprintf (buf, "/dev/axe/frontend-%d", input);
      ad->fe2 = open(buf, O_RDONLY | O_NONBLOCK);
      LOG("adapter %d force open, fe2: %d", input, ad->fe2);
      if (ad->fe2 < 0)
        ad = NULL;
    }
  }
  return ad;
}
#endif

int setup_switch (int frontend_fd, transponder *tp)
{
	int i;
	int err;
	int hiband = 0;
	int diseqc = (tp->diseqc > 0)? tp->diseqc - 1: 0;
	int freq = tp->freq;
	int pol = (tp->pol - 1) & 1;

	if (freq < SLOF)
	{
		freq = (freq - LOF1);
		hiband = 0;
	} else {
		freq = (freq - LOF2);
		hiband = 1;
	}
	
#ifdef AXE
	adapter *ad, *ad2, *adm;
	int input = 0, aid, equattro = 0;

	for (aid = 0; aid < 4; aid++) {
		ad = get_adapter(aid);
		LOGL(3, "axe adapter %i fe fd %d", aid, ad->fe);
		if (ad && ad->fe == frontend_fd)
			break;
	}
	if (aid >= 4) {
		LOG("axe_fe: unknown adapter for fd %d", frontend_fd);
		return 0;
	}
	if (tp->switch_type != SWITCH_UNICABLE && tp->switch_type != SWITCH_JESS) {
		input = aid;
		if (ad && (!opts.quattro || extra_quattro(input, diseqc, &equattro))) {
			if (equattro > 0)
				diseqc = equattro - 1;
			adm = use_adapter(ad->slave ? ad->slave - 1 : ad->pa);
			if (adm == NULL) {
				LOG("axe_fe: unknown master adapter %d", input);
				return 0;
			}
			if (adm->tp.old_pol >= 0) {
				for (aid = 0; aid < 4; aid++) {
					ad2 = get_adapter(aid);
					if (ad == ad2) continue;
					if (ad2->slave && ad2->slave - 1 != adm->pa) continue;
					if (!ad2->slave && ad2 != adm) continue;
					if (ad2->sid_cnt > 0) break;
				}
				if (adm != ad && aid < 4 &&
				    (adm->tp.old_pol != pol ||
				     adm->tp.old_hiband != hiband ||
				     adm->tp.old_diseqc != diseqc))
					return 0;
			}
			adm->axe_used |= (1 << input);
			if (ad->slave) {
				input = ad->slave - 1;
				if(adm->tp.old_pol != pol ||
				   adm->tp.old_hiband != hiband ||
				   adm->tp.old_diseqc != diseqc) {
					send_diseqc(adm->fe2, diseqc, pol, hiband);
					adm->tp.old_pol = tp->old_pol = pol;
					adm->tp.old_hiband = tp->old_hiband = hiband;
					adm->tp.old_diseqc = tp->old_diseqc = diseqc;
				}
				goto axe;
			}
		} else if (ad && opts.quattro) {
			if (opts.quattro_hiband == 1 && hiband) {
				LOG("axe_fe: hiband is not allowed for quattro config (adapter %d)", input);
				return 0;
			}
			if (opts.quattro_hiband == 2 && !hiband) {
				LOG("axe_fe: lowband is not allowed for quattro config (adapter %d)", input);
				return 0;
			}
			input = ((hiband ^ 1) << 1) | (pol ^ 1);
			adm = use_adapter(input);
			if (adm == NULL) {
				LOG("axe_fe: unknown master adapter %d", input);
				return 0;
			}
			if(adm->tp.old_pol != pol || adm->tp.old_hiband != hiband) {
				send_diseqc(adm->fe2, 0, pol, hiband);
				adm->tp.old_pol = pol;
				adm->tp.old_hiband = hiband;
				adm->tp.old_diseqc = 0;
			}
			adm->axe_used |= (1 << aid);
			goto axe;
		}
	} else {
		input = opts.axe_unicinp[aid];
		ad = use_adapter(input);
		if (ad == NULL) {
			LOGL(3, "axe setup: unable to find adapter %d", input);
			return 0;
		}
		ad->axe_used |= (1 << aid);
	}
#endif

	if(tp->switch_type == SWITCH_UNICABLE)
	{
#ifdef AXE
		if (ad)
			freq = send_unicable(ad->fe2, freq / 1000, diseqc, pol, hiband, tp->uslot, tp->ufreq);
#else
		freq = send_unicable(frontend_fd, freq / 1000, diseqc, pol, hiband, tp->uslot, tp->ufreq);
#endif
	}else if(tp->switch_type == SWITCH_JESS)
	{
#ifdef AXE
		if (ad)
			freq = send_jess(ad->fe2, freq / 1000, diseqc, pol, hiband, tp->uslot, tp->ufreq);
#else
		freq = send_jess(frontend_fd, freq / 1000, diseqc, pol, hiband, tp->uslot, tp->ufreq);
#endif
	}else
	{
		if(tp->old_pol != pol || tp->old_hiband != hiband || tp->old_diseqc != diseqc)
			send_diseqc(frontend_fd, diseqc, pol, hiband);
		else 
			LOGL(3, "Skip sending diseqc commands since the switch position doesn't need to be changed: pol %d, hiband %d, switch position %d", pol, hiband, diseqc);
	}
#ifdef AXE
axe:
	LOGL(3, "axe_fe: reset for fd %d adapter %d input %d", frontend_fd, ad ? ad->pa : -1, input);
	if (axe_fe_reset(frontend_fd) < 0)
		LOG("axe_fe: RESET failed for fd %d: %s", frontend_fd, strerror(errno));
	if (axe_fe_input(frontend_fd, input))
		LOG("axe_fe: INPUT failed for fd %d input %d: %s", frontend_fd, input, strerror(errno));
	if (opts.quattro)
		return freq;
#endif
	
	tp->old_pol = pol;
	tp->old_hiband = hiband;
	tp->old_diseqc = diseqc;
	
	return freq;
}


int
tune_it_s2 (int fd_frontend, transponder * tp)
{
	uint32_t if_freq = 0;
	int res, bclear, bpol;

	struct dvb_frontend_event event;
	
	int freq = tp->freq;
	struct dtv_properties *p;
	struct dvb_frontend_event ev;

	struct dtv_property p_clear[] =
	{
		{.cmd = DTV_CLEAR},
	};

	struct dtv_properties cmdseq_clear =
	{
		.num = 1,
		.props = p_clear
	};

	static struct dtv_property dvbs2_cmdargs[] =
	{
		{.cmd = DTV_DELIVERY_SYSTEM,.u.data = 0},
		{.cmd = DTV_FREQUENCY,.u.data = 0},
		{.cmd = DTV_MODULATION,.u.data = 0},
		{.cmd = DTV_INVERSION,.u.data = 0},
		{.cmd = DTV_SYMBOL_RATE,.u.data = 0},
		{.cmd = DTV_INNER_FEC,.u.data = 0},
#ifndef AXE
		{.cmd = DTV_PILOT,.u.data = 0},
		{.cmd = DTV_ROLLOFF,.u.data = 0},
#endif
		{.cmd = DTV_TUNE},
	};
	static struct dtv_properties dvbs2_cmdseq =
	{
		.num = sizeof (dvbs2_cmdargs) / sizeof (struct dtv_property),
		.props = dvbs2_cmdargs
	};

	static struct dtv_property dvbt_cmdargs[] =
	{
		{.cmd = DTV_DELIVERY_SYSTEM,.u.data = 0},
		{.cmd = DTV_FREQUENCY,.u.data = 0},
		{.cmd = DTV_MODULATION,.u.data = 0},
		{.cmd = DTV_INVERSION,.u.data = 0},
		{.cmd = DTV_BANDWIDTH_HZ,.u.data = 0},
		{.cmd = DTV_CODE_RATE_HP,.u.data = 0},
		{.cmd = DTV_CODE_RATE_LP,.u.data = 0},
		{.cmd = DTV_GUARD_INTERVAL,.u.data = 0},
		{.cmd = DTV_TRANSMISSION_MODE,.u.data = 0},
		{.cmd = DTV_HIERARCHY,.u.data = HIERARCHY_AUTO},
		{.cmd = DTV_STREAM_ID,.u.data = 0},
		{.cmd = DTV_TUNE},
	};
	static struct dtv_properties dvbt_cmdseq =
	{
		.num = sizeof (dvbt_cmdargs) / sizeof (struct dtv_property),
		.props = dvbt_cmdargs
	};
	
	static struct dtv_property dvbc_cmdargs[] = {
    { .cmd = DTV_DELIVERY_SYSTEM, .u.data = SYS_DVBC_ANNEX_A },
    { .cmd = DTV_FREQUENCY,       .u.data = 0 },
    { .cmd = DTV_MODULATION,      .u.data = QAM_AUTO },
    { .cmd = DTV_INVERSION,       .u.data = INVERSION_AUTO },
    { .cmd = DTV_SYMBOL_RATE,     .u.data = 0 },
	{ .cmd = DTV_STREAM_ID,		  .u.data = 0},
    { .cmd = DTV_TUNE },
	};
	
	static struct dtv_properties dvbc_cmdseq = {
    .num = sizeof(dvbc_cmdargs)/sizeof(struct dtv_property),
    .props = dvbc_cmdargs
	};

	static struct dtv_property atsc_cmdargs[] = {
		{ .cmd = DTV_DELIVERY_SYSTEM, .u.data = SYS_ATSC },
		{ .cmd = DTV_FREQUENCY,       .u.data = 0 },
		{ .cmd = DTV_MODULATION,      .u.data = QAM_AUTO },
		{ .cmd = DTV_INVERSION,       .u.data = INVERSION_AUTO },
		{ .cmd = DTV_TUNE },
	};

	static struct dtv_properties atsc_cmdseq = {
		.num = sizeof(atsc_cmdargs)/sizeof(struct dtv_property),
		.props = atsc_cmdargs
	};
	
	bclear = getTick();
	
	if ((ioctl (fd_frontend, FE_SET_PROPERTY, &cmdseq_clear)) == -1)
	{
		LOG ("FE_SET_PROPERTY DTV_CLEAR failed for fd %d: %s", fd_frontend, strerror(errno));
		//        return -1;
	}

	
	switch (tp->sys)
	{
		case SYS_DVBS:
		case SYS_DVBS2:
	
			if (tp->sys == SYS_DVBS2 && tp->mtype == 0)
				tp->mtype = PSK_8;
			if (tp->sys == SYS_DVBS && tp->mtype == 0)
				tp->mtype = QPSK;
			bpol = getTick();
			if_freq = setup_switch (fd_frontend, tp);
			if (!if_freq)
				return -404;
			p = &dvbs2_cmdseq;
			p->props[DELSYS].u.data = tp->sys;
			p->props[MODULATION].u.data = tp->mtype;
#ifndef AXE
			p->props[PILOT].u.data = tp->plts;
			p->props[ROLLOFF].u.data = tp->ro;
#endif
			p->props[INVERSION].u.data = tp->inversion;
			p->props[SYMBOL_RATE].u.data = tp->sr;
			p->props[FEC_INNER].u.data = tp->fec;
			p->props[FREQUENCY].u.data = if_freq;

			LOG("tuning to %d(%d) pol: %s (%d) sr:%d fec:%s delsys:%s mod:%s rolloff:%s pilot:%s, ts clear=%d, ts pol=%d",
				tp->freq, p->props[FREQUENCY].u.data, get_pol(tp->pol), tp->pol, p->props[SYMBOL_RATE].u.data, fe_fec[p->props[FEC_INNER].u.data],
				fe_delsys[p->props[DELSYS].u.data], fe_modulation[p->props[MODULATION].u.data],
#ifdef AXE
				"auto", "auto",
#else
				fe_rolloff[p->props[ROLLOFF].u.data], fe_pilot[p->props[PILOT].u.data],
#endif
				bclear, bpol);
				
			break;

		case SYS_DVBT:
		case SYS_DVBT2:
			if (tp->sys == SYS_DVBT && tp->mtype == 0)
				tp->mtype = QAM_AUTO;
			if (tp->sys == SYS_DVBT2 && tp->mtype == 0)
				tp->mtype = QAM_AUTO;
			p = &dvbt_cmdseq;
			p->props[DELSYS].u.data = tp->sys;
			p->props[FREQUENCY].u.data = freq * 1000;
			p->props[INVERSION].u.data = tp->inversion;
			p->props[MODULATION].u.data = tp->mtype;
			p->props[BANDWIDTH].u.data = tp->bw;
			p->props[FEC_INNER].u.data = tp->fec;
			p->props[FEC_LP].u.data = tp->fec;
			p->props[GUARD].u.data = tp->gi;
			p->props[TRANSMISSION].u.data = tp->tmode;
			p->props[HIERARCHY].u.data = HIERARCHY_AUTO;
			p->props[DSPLPT2].u.data = ((tp->ds & 0xFF) << 8 ) | ( tp->plp & 0xFF);
			
			LOG ("tuning to %d delsys: %s bw:%d inversion:%s mod:%s fec:%s fec_lp:%s guard:%s transmission: %s, ts clear = %d",
					p->props[FREQUENCY].u.data, fe_delsys[p->props[DELSYS].u.data], p->props[BANDWIDTH].u.data, fe_specinv[p->props[INVERSION].u.data],
					fe_modulation[p->props[MODULATION].u.data], fe_fec[p->props[FEC_INNER].u.data], fe_fec[p->props[FEC_LP].u.data], 
					fe_gi[p->props[GUARD].u.data], fe_tmode[p->props[TRANSMISSION].u.data], bclear)
			
			break;
		case SYS_DVBC2:
		case SYS_DVBC_ANNEX_A:
			p = &dvbc_cmdseq;
			if(tp->mtype == 0)
				tp->mtype = QAM_AUTO;
			p->props[DELSYS].u.data = tp->sys;
			p->props[FREQUENCY].u.data = freq * 1000;
			p->props[INVERSION].u.data = tp->inversion;
			p->props[SYMBOL_RATE].u.data = tp->sr;
			p->props[MODULATION].u.data = tp->mtype;
			p->props[DSPLPC2].u.data = ( (tp->ds & 0xFF) << 8 ) | ( tp->plp & 0xFF); // valid for DD DVB-C2 devices

			LOG("tuning to %d sr:%d specinv:%s delsys:%s mod:%s ts clear =%d", p->props[FREQUENCY].u.data, tp->sr, fe_specinv[p->props[INVERSION].u.data], 
					fe_delsys[p->props[DELSYS].u.data], fe_modulation[p->props[MODULATION].u.data], bclear);
			break;
		
		case SYS_ATSC:
		case SYS_DVBC_ANNEX_B:
			p = &atsc_cmdseq;
			if(tp->mtype == 0)
				tp->mtype = QAM_AUTO;
			p->props[DELSYS].u.data = tp->sys;
			p->props[FREQUENCY].u.data = freq * 1000;
			p->props[INVERSION].u.data = tp->inversion;
			p->props[MODULATION].u.data = tp->mtype;

			LOG("tuning to %d specinv:%s delsys:%s mod:%s ts clear=%d", p->props[FREQUENCY].u.data, fe_specinv[p->props[INVERSION].u.data], 
					fe_delsys[p->props[DELSYS].u.data], fe_modulation[p->props[MODULATION].u.data], bclear);
			break;
	}

	/* discard stale QPSK events */
	while (1)
	{
		if (ioctl (fd_frontend, FE_GET_EVENT, &ev) == -1)
			break;
	}

	if ((ioctl (fd_frontend, FE_SET_PROPERTY, p)) == -1)
		if (ioctl (fd_frontend, FE_SET_PROPERTY, p) == -1)
		{
			perror ("FE_SET_PROPERTY TUNE failed");
			LOG ("set property failed");
			return -404;
		}

	return 0;
}


int
set_pid (int hw, int ad, uint16_t i_pid)
{
	char buf[100];
	int fd;

#ifdef AXE
	adapter *a = get_adapter(hw);

	if ( i_pid > 8192 || a == NULL)
		LOG_AND_RETURN(-1, "pid %d > 8192 for ADAPTER %d", i_pid, hw);

	if (axe_dmxts_add_pid(a->dvr, i_pid) < 0)
	{
		LOG ("failed setting filter on PID %d (%s) for ADAPTER %d", i_pid, strerror (errno), hw);
		return -1;
	}
	LOG ("setting filter on PID %d for ADAPTER %d", i_pid, a->pa);
	return (hw << 16) | i_pid;
#else
	if ( i_pid > 8192 )
		LOG_AND_RETURN(-1, "pid %d > 8192 for /dev/dvb/adapter%d/demux%d", i_pid, hw, ad);
		
	sprintf (buf, "/dev/dvb/adapter%d/demux%d", hw, ad);
	if ((fd = open (buf, O_RDWR | O_NONBLOCK)) < 0)
	{
		LOG("Could not open demux device /dev/dvb/adapter%d/demux%d: %s ",hw, ad, strerror (errno));
		return -1;
	}

	struct dmx_pes_filter_params s_filter_params;

	s_filter_params.pid = i_pid;
	s_filter_params.input = DMX_IN_FRONTEND;
	s_filter_params.output = DMX_OUT_TS_TAP;
	s_filter_params.flags = DMX_IMMEDIATE_START;
	s_filter_params.pes_type = DMX_PES_OTHER;

	if (ioctl (fd, DMX_SET_PES_FILTER, &s_filter_params) < 0)
	{
		LOG ("failed setting filter on %d (%s)", i_pid, strerror (errno));
		return -1;
	}

	LOG ("setting filter on PID %d for fd %d", i_pid, fd);
#endif

	return fd;
}


int del_filters (int fd, int pid)
{
#ifdef AXE
	adapter *a = get_adapter(fd >> 16);
	if (a == NULL)
		return 0; /* closed */
	if ((fd & 0xffff) != pid)
		LOG_AND_RETURN(0, "AXE PID remove on an invalid handle %d, pid %d", fd, pid);
	if (axe_dmxts_remove_pid(a->dvr, pid) < 0)
		LOG ("AXE PID remove failed on PID %d ADAPTER %d: %s", pid, a->pa, strerror (errno))
			else
			LOG ("clearing filters on PID %d ADAPTER %d", pid, a->pa);
#else
	if (fd < 0)
		LOG_AND_RETURN(0, "DMX_STOP on an invalid handle %d, pid %d", fd, pid);
	if (ioctl (fd, DMX_STOP) < 0)
		LOG ("DMX_STOP failed on PID %d FD %d: %s", pid, fd, strerror (errno))
			else
			LOG ("clearing filters on PID %d FD %d", pid, fd);
	close (fd);
#endif
	return 0;
}


fe_delivery_system_t
dvb_delsys (int aid, int fd, fe_delivery_system_t *sys)
{
#ifdef AXE
	int i;
	LOG ("Delivery System DVB-S/DVB-S2 (AXE)");
	for(i = 0 ; i < 10 ; i ++)
		sys[i] = 0;
	sys[0] = SYS_DVBS;
	sys[1] = SYS_DVBS2;
	return SYS_DVBS2;
#else
	int i, res, rv = 0;
	struct dvb_frontend_info fe_info;

	static struct dtv_property enum_cmdargs[] =
	{
		{.cmd = DTV_ENUM_DELSYS,.u.data = 0},
	};
	static struct dtv_properties enum_cmdseq =
	{
		.num = sizeof (enum_cmdargs) / sizeof (struct dtv_property),
		.props = enum_cmdargs
	};
	
	for(i = 0 ; i < 10 ; i ++)
		sys[i] = 0;
	if (ioctl (fd, FE_GET_PROPERTY, &enum_cmdseq) < 0)
	{
		LOG ("unable to query frontend, perhaps DVB-API < 5.5 ?");
		//	return 0;
	}

	if ((res = ioctl (fd, FE_GET_INFO, &fe_info) < 0))
	{
		LOG ("FE_GET_INFO failed for adapter %d, fd %d: %s ", aid, fd, strerror(errno));
		//	return -1;
	}
	
	LOG("Detected Adapter %d handle %d DVB Card Name: %s", aid, fd, fe_info.name); 

	int nsys = enum_cmdargs[0].u.buffer.len;

	if (nsys < 1)
	{
	//	LOG ("no available delivery system");
	//	return 0;
		switch ( fe_info.type )
		{
		case FE_OFDM:
			if ( fe_info.caps & FE_CAN_2G_MODULATION )
			{
				LOG ("Delivery System DVB-T2");
				rv=SYS_DVBT2;
			}
			else
			{
				LOG ("Delivery System DVB-T");
				rv=SYS_DVBT;
			}
			break;
		case FE_QAM:
			LOG ("Delivery System DVB-C");
			rv=SYS_DVBC_ANNEX_AC;
			break;
		case FE_QPSK:
			if ( fe_info.caps & FE_CAN_2G_MODULATION )
			{
				LOG ("Delivery System DVB-S2");
				rv=SYS_DVBS2;
			}
			else
			{
				LOG ("Delivery System DVB-S");
				rv=SYS_DVBS;
			}
			break;
		case FE_ATSC:
			if ( fe_info.caps & (FE_CAN_8VSB | FE_CAN_16VSB) )
			{
				LOG ("Delivery System ATSC");
				rv=SYS_ATSC;
			}
			else if ( fe_info.caps & (FE_CAN_QAM_64 | FE_CAN_QAM_256 | FE_CAN_QAM_AUTO) )
			{
				LOG ("Delivery System ATSC/DVBC");
				rv=SYS_DVBC_ANNEX_B;
			}
			else return 0;
			break;
		default:
			LOG ("no available delivery system");
			return 0;
		}
		nsys = 1;
		sys[0] = rv;
	}
	else
	{	
		for (i = 0; i < nsys; i++)
		{
			sys[i] = enum_cmdargs[0].u.buffer.data[i];
			LOG("Detected del_sys[%d] for adapter %d: %s", i, aid, fe_delsys[sys[i]]);
		}
		rv = enum_cmdargs[0].u.buffer.data[0];
	}

	LOG ("returning default from dvb_delsys => %s (count %d)", fe_delsys[rv] , nsys);
	return (fe_delivery_system_t) rv;
#endif

}


#define INVALID_URL(a) {LOG(a);return 0;}
char def_pids[100];

//#define default_pids "0,1,2,3"
#define default_pids "8192"

int
detect_dvb_parameters (char *s, transponder * tp)
{
	char *arg[20];
	int la, i;

	tp->sys = -1;
	tp->freq = -1;
	tp->inversion = -1;
	tp->mod = -1;
	tp->hprate = -1;
	tp->tmode = -1;
	tp->gi = -1;
	tp->bw = -1;
	tp->sm = -1;
	tp->t2id = -1;
	tp->fe = -1;
	tp->ro = -1;
	tp->mtype = -1;
	tp->plts = -1;
	tp->fec = -1;
	tp->sr = -1;
	tp->pol = -1;
	tp->diseqc = -1;
	tp->c2tft = -1;
	tp->ds = -1;
	tp->plp = -1;

	tp->pids = tp->apids = tp->dpids = NULL;

	while (*s > 0 && *s != '?')
		s++;

	if (*s == 0)
		LOG_AND_RETURN (0, "no ? found in URL");

	*s++;
	if (strstr(s, "freq="))
			init_dvb_parameters(tp);

	LOG ("detect_dvb_parameters (S)-> %s", s);
	la = split (arg, s, 20, '&');

	for (i = 0; i < la; i++)
	{
		if (strncmp ("msys=", arg[i], 5) == 0)
			tp->sys = map_int (arg[i] + 5, fe_delsys);
		if (strncmp ("freq=", arg[i], 5) == 0)
			tp->freq = map_float (arg[i] + 5, 1000);
		if (strncmp ("pol=", arg[i], 4) == 0)
			tp->pol = map_int (arg[i] + 4, fe_pol);
		if (strncmp ("sr=", arg[i], 3) == 0)
			tp->sr = map_int (arg[i] + 3, NULL) * 1000;
		if (strncmp ("fe=", arg[i], 3) == 0)
			tp->fe = map_int (arg[i] + 3, NULL);
		if (strncmp ("src=", arg[i], 4) == 0)
			tp->diseqc = map_int (arg[i] + 4, NULL);
		if (strncmp ("ro=", arg[i], 3) == 0)
			tp->ro = map_int (arg[i] + 3, fe_rolloff);
		if (strncmp ("mtype=", arg[i], 6) == 0)
			tp->mtype = map_int (arg[i] + 6, fe_modulation);
		if (strncmp ("fec=", arg[i], 4) == 0)
			tp->fec = map_int (arg[i] + 4, fe_fec);
		if (strncmp ("plts=", arg[i], 5) == 0)
			tp->plts = map_int (arg[i] + 5, fe_pilot);
		if (strncmp ("gi=", arg[i], 3) == 0)
			tp->gi = map_int (arg[i] + 3, fe_gi);
		if (strncmp ("tmode=", arg[i], 6) == 0)
			tp->tmode = map_int (arg[i] + 6, fe_tmode);
		if (strncmp ("bw=", arg[i], 3) == 0)
			tp->bw = map_float (arg[i] + 3, 1000000);
		if (strncmp ("specinv=", arg[i], 8) == 0)
			tp->inversion = map_int (arg[i] + 8, NULL);
		if (strncmp ("c2tft=", arg[i], 6) == 0)
			tp->c2tft = map_int (arg[i] + 6, NULL);
		if (strncmp ("ds=", arg[i], 3) == 0)
			tp->ds = map_int (arg[i] + 3, NULL);
		if (strncmp ("plp=", arg[i], 4) == 0)
			tp->plp = map_int (arg[i] + 4, NULL);
			
		if (strncmp ("pids=", arg[i], 5) == 0)
			tp->pids = arg[i] + 5;
		if (strncmp ("addpids=", arg[i], 8) == 0)
			tp->apids = arg[i] + 8;
		if (strncmp ("delpids=", arg[i], 8) == 0)
			tp->dpids = arg[i] + 8;
	}
	
	if (tp->pids && strstr (tp->pids, "all"))
	{
		strcpy (def_pids, default_pids);
								 // map pids=all to essential pids
		tp->pids = (char *) def_pids;
	}
	
	if (tp->pids && strncmp (tp->pids, "none", 3) == 0)
		tp->pids = NULL;
		
	//      if(!msys)INVALID_URL("no msys= found in URL");
	//      if(freq<10)INVALID_URL("no freq= found in URL or frequency invalid");
	//      if((msys==SYS_DVBS || msys==SYS_DVBS2) && (pol!='H' && pol!='V'))INVALID_URL("no pol= found in URL or pol is not H or V");
	LOG
		("detect_dvb_parameters (E) -> src=%d, fe=%d, freq=%d, fec=%d, sr=%d, pol=%d, ro=%d, msys=%d, mtype=%d, plts=%d, bw=%d, inv=%d, pids=%s - apids=%s - dpids=%s",
		tp->diseqc, tp->fe, tp->freq, tp->fec, tp->sr, tp->pol, tp->ro, tp->sys,
		tp->mtype, tp->plts, tp->bw, tp->inversion, tp->pids ? tp->pids : "NULL",
		tp->apids ? tp->apids : "NULL", tp->dpids ? tp->dpids : "NULL");
	return 0;
}


void
init_dvb_parameters (transponder * tp)
{
	memset(tp, 0, sizeof(transponder));
	tp->inversion = INVERSION_AUTO;
	tp->hprate = FEC_AUTO;
	tp->tmode = TRANSMISSION_MODE_AUTO;
	tp->gi = GUARD_INTERVAL_AUTO;
	tp->bw = 8000000;
	tp->ro = ROLLOFF_AUTO;
	tp->mtype = QPSK;
	tp->plts = PILOT_AUTO;
	tp->fec = FEC_AUTO;
	tp->old_diseqc = tp->old_pol = tp->old_hiband = -1;
}


void
copy_dvb_parameters (transponder * s, transponder * d)
{
	LOG
		("copy_dvb_param start -> src=%d, fe=%d, freq=%d, fec=%d, sr=%d, pol=%d, ro=%d, msys=%d, mtype=%d, plts=%d, bw=%d, inv=%d, pids=%s, apids=%s, dpids=%s",
		d->diseqc, d->fe, d->freq, d->fec, d->sr, d->pol, d->ro, d->sys, d->mtype,
		d->plts, d->bw, d->inversion, d->pids ? d->pids : "NULL",
		d->apids ? d->apids : "NULL", d->dpids ? d->dpids : "NULL");
	if (s->sys != -1)
		d->sys = s->sys;
	if (s->freq != -1)
		d->freq = s->freq;
	if (s->inversion != -1)
		d->inversion = s->inversion;
	if (s->mod != -1)
		d->mod = s->mod;
	if (s->hprate != -1)
		d->hprate = s->hprate;
	if (s->tmode != -1)
		d->tmode = s->tmode;
	if (s->gi != -1)
		d->gi = s->gi;
	if (s->bw != -1)
		d->bw = s->bw;
	if (s->sm != -1)
		d->sm = s->sm;
	if (s->t2id != -1)
		d->t2id = s->t2id;
	if (s->fe != -1)
		d->fe = s->fe;
	if (s->ro != -1)
		d->ro = s->ro;
	if (s->mtype != -1)
		d->mtype = s->mtype;
	if (s->plts != -1)
		d->plts = s->plts;
	if (s->fec != -1)
		d->fec = s->fec;
	if (s->sr != -1)
		d->sr = s->sr;
	if (s->pol != -1)
		d->pol = s->pol;
	if (s->diseqc != -1)
		d->diseqc = s->diseqc;
	if (s->c2tft != -1)
		d->c2tft = s->c2tft;
	if (s->ds != -1)
		d->ds = s->ds;
	if (s->plp != -1)
		d->plp = s->plp;
	
	//      if(s->apids)
	d->apids = s->apids;
	//      if(s->pids)
	d->pids = s->pids;
	//      if(s->dpids)
	d->dpids = s->dpids;

	LOG
		("copy_dvb_parameters -> src=%d, fe=%d, freq=%d, fec=%d sr=%d, pol=%d, ro=%d, msys=%d, mtype=%d, plts=%d, bw=%d, inv=%d, pids=%s, apids=%s, dpids=%s",
		d->diseqc, d->fe, d->freq, d->fec, d->sr, d->pol, d->ro, d->sys, d->mtype,
		d->plts, d->bw, d->inversion, d->pids ? d->pids : "NULL",
		d->apids ? d->apids : "NULL", d->dpids ? d->dpids : "NULL");
}


void
get_signal (int fd, fe_status_t * status, uint32_t * ber, uint16_t * strength,
uint16_t * snr)
{
	*status = *ber = *snr = *strength = 0;

	if (ioctl (fd, FE_READ_STATUS, status) < 0)
	{
		LOG ("ioctl FE_READ_STATUS failed (%s)", strerror (errno));
		*status = 0;
		return;
	}
//	*status = (*status & FE_HAS_LOCK) ? 1 : 0;
	if (*status)
	{
		if (ioctl (fd, FE_READ_BER, ber) < 0)
			LOG ("ioctl FE_READ_BER failed (%s)", strerror (errno));

		if (ioctl (fd, FE_READ_SIGNAL_STRENGTH, strength) < 0)
			LOG ("ioctl FE_READ_SIGNAL_STRENGTH failed (%s)", strerror (errno));

		if (ioctl (fd, FE_READ_SNR, snr) < 0)
			LOG ("ioctl FE_READ_SNR failed (%s)", strerror (errno));
	}
}

int get_signal_new (int fd, fe_status_t * status, uint32_t * ber,
	uint16_t * strength, uint16_t * snr)
{

	*status = *snr = *ber = *strength = 0;

#if DVBAPIVERSION >= 0x050A
	int err = 0;
	static struct dtv_property enum_cmdargs[] =
	{
		{.cmd = DTV_STAT_SIGNAL_STRENGTH,.u.data = 0},
		{.cmd = DTV_STAT_CNR,.u.data = 0},
		{.cmd = DTV_STAT_ERROR_BLOCK_COUNT,.u.data = 0},
	};
	static struct dtv_properties enum_cmdseq =
	{
		.num = sizeof (enum_cmdargs) / sizeof (struct dtv_property),
		.props = enum_cmdargs
	};
	
	
	if (ioctl (fd, FE_GET_PROPERTY, &enum_cmdseq) < 0)
	{
		LOG ("get_signal_new: unable to query frontend %d: %s", fd, strerror (errno));
		err = 100;
	}
	

	if(enum_cmdargs[0].u.st.stat[0].scale ==  FE_SCALE_RELATIVE)
		*strength = enum_cmdargs[0].u.st.stat[0].uvalue >> 8;
	else if(enum_cmdargs[0].u.st.stat[0].scale ==  FE_SCALE_DECIBEL)
		*strength = enum_cmdargs[0].u.st.stat[0].uvalue >> 8;
	else err ++;

	if(enum_cmdargs[1].u.st.stat[0].scale ==  FE_SCALE_RELATIVE)
		*snr = enum_cmdargs[1].u.st.stat[0].uvalue >> 12;
	else if(enum_cmdargs[1].u.st.stat[0].scale ==  FE_SCALE_DECIBEL)
		*snr = enum_cmdargs[1].u.st.stat[0].uvalue >> 12;
	else err ++;

	*ber = enum_cmdargs[2].u.st.stat[0].uvalue & 0xFFFF;

	LOG("get_signal_new returned: Signal (%d): %llu, SNR(%d): %llu, BER: %llu, err %d", enum_cmdargs[0].u.st.stat[0].scale, enum_cmdargs[0].u.st.stat[0].uvalue,
			enum_cmdargs[1].u.st.stat[0].scale, enum_cmdargs[1].u.st.stat[0].uvalue, enum_cmdargs[2].u.st.stat[0].uvalue, err);
	if(err)
		return err;
	
	if (ioctl (fd, FE_READ_STATUS, status) < 0)
	{
		LOG ("ioctl FE_READ_STATUS failed (%s)", strerror (errno));
		*status = 0;
		return -1;
	}
	
	if(*status && !*strength)
		return -1;	
	return 0;
#else
	return -1;
#endif
}

