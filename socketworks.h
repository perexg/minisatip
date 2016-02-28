#ifndef SOCKETWORKS_H
#define SOCKETWORKS_H
#define MAX_SOCKS 100
#include <netinet/in.h>

typedef int (*socket_action) (void *s);

typedef struct struct_sockets
{
	int sock;					 // socket - <0 for invalid/not used, 0 for end of the list
	struct sockaddr_in sa;		 //remote address - set on accept or recvfrom on udp sockets
	socket_action action;
	socket_action close;
	socket_action timeout;
	int type;					 //0 - udp; 1 -> tcp(client); 2 -> server ; 3 -> http; 4-> rtsp
	int sid;					 //stream_id if set >=0 or adapter_id for dvb handles
	int rtime;					 // read time
	unsigned char *buf;
	int lbuf;
	int rlen;
	int skiplen;
	int close_sec;
	int sock_id;				 // socket id
	int err;
	int flags; // 1 - buf is allocated dynamically
} sockets;

#define TYPE_UDP 0
#define TYPE_TCP 1
#define TYPE_SERVER 2
#define TYPE_HTTP 3
#define TYPE_RTSP 4
#define TYPE_DVR 5
#define TYPE_RTCP 6

#define MAX_HOST 50

char *setlocalip ();
char *getlocalip ();
int udp_connect (char *addr, int port, struct sockaddr_in *serv);
int udp_bind_connect(char *src, int sport, char *dest, int dport, struct sockaddr_in *serv);
char *get_sock_host(int fd);
int get_sock_port(int fd);
int sockets_add (int sock, struct sockaddr_in *sa, int sid, int type,
socket_action a, socket_action c, socket_action t);
int sockets_del (int sock);
void sockets_set_poll (int sock, int enable);
void sockets_reset (int sock);
int no_action (int s);
int select_and_execute ();
int get_mac (char *mac);
int fill_sockaddr (struct sockaddr_in *serv, char *host, int port);
int sockets_del_for_sid (int ad);
char *get_current_timestamp ();
char *get_current_timestamp_log ();
void set_socket_buffer (int sid, unsigned char *buf, int len);
void sockets_timeout (int i, int t);
void free_all ();

#endif
