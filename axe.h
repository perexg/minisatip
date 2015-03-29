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
#define FE_FRONTEND_THREAD_UP	_IOW('o', 97, __u8)

static inline int axe_fe_standby(int fd, __u32 stdby)
{
  return ioctl(fd, FE_FRONTEND_STANDBY, &stdby);
}

static inline int axe_fe_reset(int fd)
{
  return ioctl(fd, FE_FRONTEND_RESET, 0x54);
}

static inline int axe_fe_thread_up(int fd, __u8 up)
{
  return ioctl(fd, FE_FRONTEND_THREAD_UP, &up);
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

#endif
