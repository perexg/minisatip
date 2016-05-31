#ifndef __AXE_H
#define __AXE_H

#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>

typedef struct fe_frontend_status fe_frontend_status_t;

struct fe_frontend_status {
  __u32 val0;
  __u32 val1;
  __u32 val2;
  __u32 modulation;
  __u32 val4;
  __u32 frequency;
  __u32 val6;
  __u32 val7;
  __u32 symbol_rate;
  __u32 val9;
  __u32 fec;
  __u32 rolloff;
  __u32 val12;
  __u32 val13;
} __attribute__ ((packed));

#define FE_FRONTEND_STANDBY     _IOW('o', 91, __u32)
#define FE_FRONTEND_RESET	_IO('o', 93)
#define FE_FRONTEND_STATUS      _IOR('o', 96, fe_frontend_status_t)
#define FE_FRONTEND_INPUT	_IOW('o', 97, __u8)

static inline int axe_fe_standby(int fd, __u32 stdby)
{
  return ioctl(fd, FE_FRONTEND_STANDBY, &stdby);
}

static inline int axe_fe_reset(int fd)
{
  return ioctl(fd, FE_FRONTEND_RESET, 0x54);
}

static inline int axe_fe_input(int fd, __u8 in)
{
  return ioctl(fd, FE_FRONTEND_INPUT, &in);
}

typedef struct dmx_stream_params_s {
  __u32  srcIp;
  __u16  srcPort;
  __u16  dstPort;
  __u32  dstIp;
  __u32  ts;     /* timestamp              */
  __u32  ssrc;   /* synchronization source */
  __u16  seq;
} dmx_stream_params_t;

typedef struct rtp_state {
  __u32 ssrc;
  __u32 ts;
  __u32 spc;
  __u32 soc;
  __u16 seq;
} rtp_state_t;

#define DMXTS_ADD_PID            _IOW('o', 1, __u16)
#define DMXTS_REMOVE_PID         _IOW('o', 2, __u16)

#define DMXTS_TRANSFER_START      _IO('o', 5)
#define DMXTS_TRANSFER_START_RTP _IOW('o', 6, dmx_stream_params_t)
#define DMXTS_TRANSFER_STOP       _IO('o', 7)
#define DMXTS_RTP_SETUP_SSRC     _IOW('o', 8, __u32)
#define DMXTS_TRANSFER_PAUSE      _IO('o', 9)
#define DMXTS_TRANSFER_RESUME     _IO('o', 10)

#define DMXTS_GET_RTP_STREAM_STATE _IOR('o', 11, rtp_state_t)

static inline int axe_dmxts_add_pid(int fd, __u16 pid)
{
  return ioctl(fd, DMXTS_ADD_PID, &pid);
}

static inline int axe_dmxts_remove_pid(int fd, __u16 pid)
{
  return ioctl(fd, DMXTS_REMOVE_PID, &pid);
}

static inline int axe_dmxts_start(int fd)
{
  return ioctl(fd, DMXTS_TRANSFER_START);
}

static inline int axe_dmxts_stop(int fd)
{
  return ioctl(fd, DMXTS_TRANSFER_STOP);
}

#ifdef AXE_MAIN

int axe_fp_fd = -1;

static inline axe_fp_fd_open(void)
{
  if (axe_fp_fd < 0)
    axe_fp_fd = open("/dev/axe/fp-0", O_WRONLY);
}

static inline axe_fp_fd_write(const char *s)
{
  const char *b;
  size_t len;
  ssize_t r;

  axe_fp_fd_open();
  len = strlen(b = s);
  while (len > 0) {
    r = write(axe_fp_fd, b, len);
    if (r > 0) {
      len -= r;
      b += r;
    }
  }
}

void axe_set_tuner_led(int tuner, int on)
{
  static int state = 0;
  char buf[16];
  if (((state >> tuner) & 1) != !!on) {
    sprintf(buf, "T%d_LED %d\n", tuner, on ? 1 : 0);
    axe_fp_fd_write(buf);
    if (on)
      state |= 1 << tuner;
    else
      state &= ~(1 << tuner);
  }
}

void axe_set_network_led(int on)
{
  static int state = -1;
  if (state != on) {
    axe_fp_fd_write(on ? "NET_LED 1\n" : "NET_LED 0\n");
    state = on;
  }
}

#else

void axe_set_tuner_led(int tuner, int on);
void axe_set_network_led(int on);
void axe_status(char *buf, size_t buflen);

#endif

#endif
