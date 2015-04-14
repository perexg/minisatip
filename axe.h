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

#define DMXTS_ADD_PID     _IOW('o', 1, __u16)
#define DMXTS_REMOVE_PID  _IOW('o', 2, __u16)

static inline int axe_dmxts_add_pid(int fd, __u16 pid)
{
  return ioctl(fd, DMXTS_ADD_PID, &pid);
}

static inline int axe_dmxts_remove_pid(int fd, __u16 pid)
{
  return ioctl(fd, DMXTS_REMOVE_PID, &pid);
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

#endif

#endif
