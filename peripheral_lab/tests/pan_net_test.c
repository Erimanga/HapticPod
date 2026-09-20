#include "../src/network/lab_pan_net.c"
#include <assert.h>
static struct netif device={.name={'b','0'}},old;
struct netif *netif_list=&device,*netif_default=&old;
static ip_addr_t dns_server;
static rt_tick_t now=100;
static net_event_t events[32];
static unsigned qhead,qtail,closed,port;
static bool defer_dns,block_send;
static void (*pending_dns)(const char *,const ip_addr_t *,void *);
static void *pending_arg;
static uint8_t incoming[1024];static size_t in_len,in_off;
rt_tick_t rt_tick_get(void) { return now; }
rt_tick_t rt_tick_from_millisecond(unsigned n) { return n; }
rt_mq_t rt_mq_create(const char *n,size_t z,unsigned c,unsigned f) { (void)n;(void)z;(void)c;(void)f;return (void *)1; }
int rt_mq_send(rt_mq_t q,const void *d,size_t n) { (void)q;assert(qtail<32 && n==sizeof(net_event_t));memcpy(&events[qtail++],d,n);return 0; }
int rt_mq_recv(rt_mq_t q,void *d,size_t n,int w) { (void)q;(void)w;if(qhead==qtail){qhead=qtail=0;return -1;}memcpy(d,&events[qhead++],n);return 0; }
uint8_t ble_get_public_address(bd_addr_t *a) { memset(a,1,sizeof(*a));return 0; }
void netif_set_default(struct netif *n) { netif_default=n; }
int dhcp_supplied_address(struct netif *n) { (void)n;return 1; }
void dhcp_start(struct netif *n) { (void)n; }
const ip_addr_t *dns_getserver(unsigned i) { (void)i;return &dns_server; }
err_t tcpip_callback(void (*f)(void *),void *a) { f(a);return 0; }
err_t dns_gethostbyname_addrtype(const char *h,ip_addr_t *ip,void (*cb)(const char *,const ip_addr_t *,void *),void *a,unsigned t)
{ (void)t; assert(!strcmp(h,mqtt_mode ? LAB_MQTT_HOST : TEST_HOST));ip->addr=inet_addr("192.0.2.1");if(defer_dns){pending_dns=cb;pending_arg=a;return ERR_INPROGRESS;}return 0; }
int lwip_socket(int a,int b,int c) { (void)a;(void)b;(void)c;in_len=in_off=0;return 5; }
int lwip_close(int s) { assert(s==5);closed++;return 0; }
int lwip_ioctl(int s,long c,void *v) { (void)s;(void)c;(void)v;return 0; }
int lwip_bind(int s,const struct sockaddr *a,socklen_t n) { (void)s;(void)n;assert(((const struct sockaddr_in *)a)->sin_addr.s_addr==device.ip.addr);return 0; }
int lwip_connect(int s,const struct sockaddr *a,socklen_t n) { (void)s;(void)n;port=ntohs(((const struct sockaddr_in *)a)->sin_port);errno=EINPROGRESS;return -1; }
int lwip_getsockopt(int s,int l,int o,void *e,socklen_t *n) { (void)s;(void)l;(void)o;(void)n;*(int *)e=0;return 0; }
int lwip_select(int n,fd_set *r,fd_set *w,fd_set *e,struct timeval *t) { (void)n;(void)e;(void)t;return FD_ISSET(5,w) || (FD_ISSET(5,r)&&in_off<in_len); }
static void rx(const uint8_t *p,size_t n) { assert(in_off==in_len);memcpy(incoming,p,n);in_off=0;in_len=n; }
int lwip_send(int s,const void *p,size_t n,int f)
{
    (void)s;(void)p;(void)f;
    if(block_send) { errno=EWOULDBLOCK;return -1; }
    size_t chunk=n>2 ? 2 : n; /* 所有写入分成最多 2 字节。 */
    if(chunk==n) {
        if(mqtt_mode) {
            static const uint8_t ack[]={0x20,2,0,0},sub[]={0x90,3,0,1,0};
            if(mqtt.tx[0]==0x10) rx(ack,4);
            else if(mqtt.tx[0]==0x82) rx(sub,5);
            else if(mqtt.tx[0]==0x30) rx(mqtt.tx,mqtt.tx_len);
        } else { const char *line="HTTP/1.1 200 OK\r\n";rx((const uint8_t *)line,strlen(line)); }
    }
    return chunk;
}
int lwip_recv(int s,void *p,size_t n,int f) { (void)s;(void)n;(void)f;if(in_off==in_len){errno=EWOULDBLOCK;return -1;}*(uint8_t *)p=incoming[in_off++];return 1; }
static lab_pan_snapshot_t view;
static void poll(void) { lab_pan_net_poll(&view); }
static void reset(void)
{
    if(socket_fd>=0) lab_pan_net_stop();
    memset(&view,0,sizeof(view));defer_dns=block_send=false;device.ip.addr=inet_addr("192.168.44.2");device.gw.addr=inet_addr("192.168.44.1");device.next=&old;
    qhead=qtail=0;now+=1000;lab_pan_net_activate();poll();assert(view.ip_ok);
}
int main(void)
{
    assert(lab_pan_net_init()==0);reset();lab_pan_net_mqtt();
    for(unsigned i=0;i<600 && result.testing;i++){now+=5;poll();}
    assert(view.mqtt_ok && view.mqtt_connected && view.mqtt_subscribed && view.mqtt_published && !view.testing && socket_fd<0 && port==1883);
    lab_pan_net_test();for(unsigned i=0;i<100 && result.testing;i++){now+=5;poll();}
    assert(view.http_ok && !view.testing && port==80); /* HTTP regression */
    reset();lab_pan_net_mqtt();poll();unsigned before=closed;lab_pan_net_stop();poll();
    assert(closed==before+1 && !view.testing && !view.mqtt_ok && netif_default==&old);
    reset();defer_dns=true;lab_pan_net_mqtt();poll();lab_pan_net_stop();
    ip_addr_t late={inet_addr("192.0.2.1")};pending_dns("ignored",&late,pending_arg);poll();assert(socket_fd<0 && !view.testing);
    reset();block_send=true;lab_pan_net_mqtt();poll();now+=9000;poll();assert(!view.testing && !view.mqtt_ok && socket_fd<0);
    reset();lab_pan_net_mqtt();poll();device.ip.addr=0;now+=600;poll();assert(!view.ip_ok && !view.testing && !view.mqtt_ok && socket_fd<0);
    puts("PASS: production PAN socket state machine: MQTT exact echo with partial sends/reads, HTTP regression, stop/socket close/route restore, stale DNS, send timeout and IP loss");
}
