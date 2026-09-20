#pragma once
/* Transport doubles for production lab_pan_net.c. Target build checks SDK ABI. */
#include <stdint.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
typedef int err_t;
#define ERR_OK 0
#define ERR_INPROGRESS -5
#define LWIP_DNS_ADDRTYPE_IPV4 0
typedef struct { uint32_t addr; } ip4_addr_t;
typedef ip4_addr_t ip_addr_t;
struct netif { char name[2]; struct netif *next; ip4_addr_t ip,gw; };
extern struct netif *netif_list,*netif_default;
#define netif_is_link_up(n) 1
#define netif_is_up(n) 1
#define netif_ip4_addr(n) (&(n)->ip)
#define netif_ip4_gw(n) (&(n)->gw)
#define ip4_addr_isany_val(a) (!(a).addr)
#define ip4_addr_get_u32(a) ((a)->addr)
#define ip4_addr_set_u32(a,v) ((a)->addr=(v))
#define IP_IS_V4(a) 1
#define ip_2_ip4(a) (a)
#define ipaddr_addr(s) inet_addr(s)
static inline void ip4addr_ntoa_r(const ip4_addr_t *a,char *out,int n) { struct in_addr ip={a->addr};snprintf(out,n,"%s",inet_ntoa(ip)); }
void netif_set_default(struct netif *);
int dhcp_supplied_address(struct netif *);
void dhcp_start(struct netif *);
const ip_addr_t *dns_getserver(unsigned);
err_t tcpip_callback(void (*)(void *),void *);
err_t dns_gethostbyname_addrtype(const char *,ip_addr_t *,void (*)(const char *,const ip_addr_t *,void *),void *,unsigned);
int lwip_socket(int,int,int);
int lwip_close(int);
int lwip_ioctl(int,long,void *);
int lwip_bind(int,const struct sockaddr *,socklen_t);
int lwip_connect(int,const struct sockaddr *,socklen_t);
int lwip_getsockopt(int,int,int,void *,socklen_t *);
int lwip_select(int,fd_set *,fd_set *,fd_set *,struct timeval *);
int lwip_send(int,const void *,size_t,int);
int lwip_recv(int,void *,size_t,int);
