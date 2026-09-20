/*
 * lab_pan_net.c — PAN 网络测试：tcpip 线程访问网卡和 DNS，PAN 任务推进非阻塞 HTTP/MQTT 收发。
 */
#include "lab.h"
#include "lab_pan_net.h"
#include "lab_mqtt.h"
#include "bf0_ble_common.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/sockets.h"
#include <string.h>
#include <errno.h>

#define TEST_HOST "example.com"
static const char request[]="HEAD / HTTP/1.0\r\nHost: " TEST_HOST "\r\nConnection: close\r\n\r\n";
typedef struct { unsigned generation, kind; bool ok; uint32_t ip, gateway, dns; } net_event_t;
static rt_mq_t replies;
static volatile unsigned generation=1;
static volatile bool active;
static lab_pan_snapshot_t result;
static int socket_fd=-1;
/* 网络阶段：0 空闲，1 DNS，2 TCP 连接，3/4 HTTP 发送/接收，5/6 MQTT 发送/接收。 */
static unsigned phase, sent, received, sequence;
static bool mqtt_mode;
static lab_mqtt_t mqtt;
static rt_tick_t limit, next_sample;
static char response[256];
static struct netif *owned_route, *old_route; /* 只在 tcpip 线程访问。 */
static bool time_up(rt_tick_t t) { return (int32_t)(rt_tick_get()-t)>=0; }
static void note(const char *s) { rt_snprintf(result.message,sizeof(result.message),"%s",s); rt_kprintf("[lab:pan:net] %s\n",s); }
static void close_socket(void) { if (socket_fd>=0) { lwip_close(socket_fd); socket_fd=-1; } }
static void fail(const char *s) { close_socket(); phase=0; result.testing=false; note(s); }
static bool listed(struct netif *p) { for(struct netif *n=netif_list;n;n=n->next) if(n==p) return true; return false; }
/* 在 tcpip 线程恢复仍存在的旧默认网卡，仅恢复本测试接管的路由。 */
static void restore_route(void *arg)
{
    (void)arg;
    if (owned_route && netif_default==owned_route) netif_set_default(listed(old_route) ? old_route : NULL);
    owned_route=old_route=NULL;
}
/* 在 tcpip 线程查找 PAN 网卡，按需启动 DHCP，并通过队列返回地址。 */
static void sample_ip(void *arg)
{
    unsigned epoch=(uintptr_t)arg;
    if (!active || epoch!=generation) return;
    net_event_t e={.generation=epoch,.kind=1};
    for(struct netif *n=netif_list;n;n=n->next) {
        if(n->name[0]!='b' || n->name[1]<'0' || n->name[1]>'9' || !netif_is_link_up(n)) continue;
        if (owned_route!=n) {
            old_route=netif_default; owned_route=n; netif_set_default(n);
            if (!dhcp_supplied_address(n)) dhcp_start(n);
        }
        e.ok=netif_is_up(n) && !ip4_addr_isany_val(*netif_ip4_addr(n));
        e.ip=ip4_addr_get_u32(netif_ip4_addr(n)); e.gateway=ip4_addr_get_u32(netif_ip4_gw(n));
        const ip_addr_t *dns=dns_getserver(0);
        if (IP_IS_V4(dns)) e.dns=ip4_addr_get_u32(ip_2_ip4(dns));
        break;
    }
    rt_mq_send(replies,&e,sizeof(e));
}
/* 把 DNS 结果连同会话编号送回 PAN 任务，供其丢弃迟到结果。 */
static void dns_done(const char *name,const ip_addr_t *ip,void *arg)
{
    (void)name;
    net_event_t e={.generation=(uintptr_t)arg,.kind=2};
    if(ip && IP_IS_V4(ip)) { e.ok=true; e.ip=ip4_addr_get_u32(ip_2_ip4(ip)); }
    rt_mq_send(replies,&e,sizeof(e));
}
/* 在 tcpip 线程启动异步域名解析，缓存命中时也走同一结果入口。 */
static void start_dns(void *arg)
{
    if(!active || (unsigned)(uintptr_t)arg!=generation) return;
    ip_addr_t ip;
    const char *host=mqtt_mode ? LAB_MQTT_HOST : TEST_HOST;
    err_t ret=dns_gethostbyname_addrtype(host,&ip,dns_done,arg,LWIP_DNS_ADDRTYPE_IPV4);
    if(ret==ERR_OK) dns_done(host,&ip,arg);
    else if(ret!=ERR_INPROGRESS) dns_done(host,NULL,arg);
}
static void ip_text(char out[16],uint32_t value)
{
    ip4_addr_t ip; ip4_addr_set_u32(&ip,value); ip4addr_ntoa_r(&ip,out,16);
}
/* 创建 tcpip 回调到 PAN 任务的结果队列。 */
int lab_pan_net_init(void)
{
    replies=rt_mq_create("pan_net",sizeof(net_event_t),8,RT_IPC_FLAG_FIFO);
    return replies ? RT_EOK : -RT_ENOMEM;
}
/* PAN 连接后建立新网络会话，清空上一轮测试状态。 */
void lab_pan_net_activate(void)
{
    generation++; active=true; next_sample=0; phase=0; mqtt_mode=false; memset(&mqtt,0,sizeof(mqtt)); memset(&result,0,sizeof(result));
    note("PAN connected; waiting for DHCP (30 s)");
}
/* 失效旧回调、关闭 socket，并安排 tcpip 线程恢复默认路由。 */
void lab_pan_net_stop(void)
{
    active=false; generation++; close_socket(); phase=0; mqtt_mode=false; memset(&mqtt,0,sizeof(mqtt)); memset(&result,0,sizeof(result));
    tcpip_callback(restore_route,NULL);
}
/* 检查 PAN 地址后启动一次 HTTP 或 MQTT 测试，两种测试共用串行网络状态机。 */
static void begin_test(bool use_mqtt)
{
    if(!active || !result.ip_ok) { note("Wait for PAN interface and a valid IP address"); return; }
    if(result.testing) return;
    mqtt_mode=use_mqtt;
    if(use_mqtt) {
        bd_addr_t address;
        if(ble_get_public_address(&address)!=0) { note("Cannot read local identity for MQTT"); return; }
        uint32_t hash=2166136261u;
        for(unsigned i=0;i<6;i++) hash=(hash^address.addr[i])*16777619u;
        char client[24]; uint32_t nonce=(uint32_t)rt_tick_get();
        rt_snprintf(client,sizeof(client),"sf%08lx%08lx%04x",(unsigned long)hash,(unsigned long)nonce,(++sequence)&0xffff);
        lab_mqtt_start(&mqtt,client,nonce^sequence);
        if(mqtt.error) { note(mqtt.error); return; }
        result.mqtt_connected=result.mqtt_subscribed=result.mqtt_published=result.mqtt_ok=false;
        rt_kprintf("[lab:mqtt] broker=%s:%u client=%s topic=%s payload=%s\n",LAB_MQTT_HOST,LAB_MQTT_PORT,mqtt.client,mqtt.topic,mqtt.payload);
    } else { result.http_ok=false; result.http_status=0; }
    generation++; result.dns_ok=result.tcp_ok=false;
    result.remote[0]=0; result.testing=true; phase=1; sent=received=0;
    note(use_mqtt ? "MQTT: resolving test.mosquitto.org..." : "Resolving example.com..."); limit=rt_tick_get()+rt_tick_from_millisecond(10000);
    if(tcpip_callback(start_dns,(void *)(uintptr_t)generation)!=ERR_OK) fail("Could not queue DNS lookup");
}
/* 启动 DNS、TCP 与 HTTP HEAD 联网检查。 */
void lab_pan_net_test(void) { begin_test(false); }
/* 启动公共 Broker 上的合成消息发布/订阅回环。 */
void lab_pan_net_mqtt(void) { begin_test(true); }
/* 创建非阻塞 socket 并绑定 PAN 本地地址，避免经其他网络完成测试。 */
static void connect_server(uint32_t remote)
{
    socket_fd=lwip_socket(AF_INET,SOCK_STREAM,0);
    if(socket_fd<0) { fail("Socket allocation failed"); return; }
    int nonblocking=1;
    if(lwip_ioctl(socket_fd,FIONBIO,&nonblocking)<0) { fail("Nonblocking socket setup failed"); return; }
    struct sockaddr_in local={0}, dest={0}; local.sin_family=AF_INET; local.sin_addr.s_addr=ipaddr_addr(result.ip);
    if(lwip_bind(socket_fd,(struct sockaddr *)&local,sizeof(local))<0) { fail("Bind to PAN IP failed"); return; }
    dest.sin_family=AF_INET; dest.sin_port=htons(mqtt_mode ? LAB_MQTT_PORT : 80); dest.sin_addr.s_addr=remote;
    int ret=lwip_connect(socket_fd,(struct sockaddr *)&dest,sizeof(dest));
    if(ret<0 && errno!=EINPROGRESS && errno!=EWOULDBLOCK) { fail("TCP connect rejected"); return; }
    phase=2; limit=rt_tick_get()+rt_tick_from_millisecond(8000); note(mqtt_mode ? "MQTT DNS OK; connecting TCP 1883..." : "DNS OK; connecting TCP port 80...");
}
/* 消费网络回调并用零等待 select 推进连接与部分收发，最后汇总结果。 */
void lab_pan_net_poll(lab_pan_snapshot_t *out)
{
    if(!replies) return;
    if(active && time_up(next_sample)) {
        tcpip_callback(sample_ip,(void *)(uintptr_t)generation);
        next_sample=rt_tick_get()+rt_tick_from_millisecond(500);
    }
    net_event_t e;
    while(rt_mq_recv(replies,&e,sizeof(e),0)==RT_EOK) {
        /* 停止或重新测试会更新会话编号，因此旧 DNS/IP 回调不能改变本轮状态。 */
        if(!active || e.generation!=generation) continue;
        if(e.kind==1) {
            if(result.ip_ok && (!e.ok || e.ip!=ipaddr_addr(result.ip))) {
                if(result.testing) fail("PAN IP changed during test");
                result.dns_ok=result.tcp_ok=result.http_ok=false; result.http_status=0;
                result.mqtt_connected=result.mqtt_subscribed=result.mqtt_published=result.mqtt_ok=false;
                mqtt_mode=false; memset(&mqtt,0,sizeof(mqtt));
            }
            bool first=!result.ip_ok && e.ok; result.ip_ok=e.ok;
            ip_text(result.ip,e.ip); ip_text(result.gateway,e.gateway); ip_text(result.dns,e.dns);
            if(first && !result.testing) note("IP ready. Tap Test Internet (DNS + HTTP)");
        } else if(phase==1) {
            if(!e.ok) fail("DNS failed; check phone Internet and DNS server");
            else { result.dns_ok=true; ip_text(result.remote,e.ip); connect_server(e.ip); }
        }
    }
    if(result.testing && time_up(limit)) fail(phase==1 ? "DNS timeout" : phase==2 ? "TCP timeout" : mqtt_mode ? "MQTT timeout; check broker / mobile network" : "HTTP timeout");
    if(socket_fd>=0 && phase>=2) {
        fd_set reads,writes; FD_ZERO(&reads); FD_ZERO(&writes);
        if(phase==4 || phase==6) FD_SET(socket_fd,&reads); else FD_SET(socket_fd,&writes);
        struct timeval tv={0}; int ready=lwip_select(socket_fd+1,&reads,&writes,NULL,&tv);
        if(ready<0) fail("Socket polling failed");
        else if(ready>0 && phase==2) {
            int err=0; socklen_t n=sizeof(err);
            if(lwip_getsockopt(socket_fd,SOL_SOCKET,SO_ERROR,&err,&n)<0 || err) fail("TCP connection failed");
            else { result.tcp_ok=true; phase=mqtt_mode ? 5 : 3; note(mqtt_mode ? "MQTT: sending CONNECT..." : "TCP OK; sending HTTP HEAD..."); limit=rt_tick_get()+rt_tick_from_millisecond(8000); }
        } else if(ready>0 && phase==5) {
            int n=lwip_send(socket_fd,mqtt.tx+mqtt.tx_sent,mqtt.tx_len-mqtt.tx_sent,0);
            if(n>0) {
                mqtt.tx_sent+=n;
                if(mqtt.tx_sent==mqtt.tx_len) {
                    lab_mqtt_sent(&mqtt);
                    if(mqtt.done) { result.mqtt_ok=mqtt.matched; result.testing=false; close_socket(); phase=0; note("PASS: MQTT publish/subscribe exact echo received"); }
                    else {
                        phase=6; limit=rt_tick_get()+rt_tick_from_millisecond(8000);
                        note(mqtt.published ? "MQTT published; waiting for exact echo..." : mqtt.connected ? "MQTT: waiting for SUBACK..." : "MQTT: waiting for CONNACK...");
                    }
                }
            } else if(n==0 || (errno!=EWOULDBLOCK && errno!=EAGAIN)) fail("MQTT send failed");
        } else if(ready>0 && phase==6) {
            uint8_t bytes[128]; int n=lwip_recv(socket_fd,bytes,sizeof(bytes),0);
            if(n>0) {
                if(!lab_mqtt_feed(&mqtt,bytes,n)) fail(mqtt.error);
            } else if(!n) fail("MQTT broker closed the connection");
            else if(errno!=EWOULDBLOCK && errno!=EAGAIN) fail("MQTT receive failed");
        } else if(ready>0 && phase==3) {
            int n=lwip_send(socket_fd,request+sent,sizeof(request)-1-sent,0);
            if(n>0) { sent+=n; if(sent==sizeof(request)-1) { phase=4; note("Waiting for HTTP response..."); } }
            else if(n==0 || (errno!=EWOULDBLOCK && errno!=EAGAIN)) fail("HTTP request send failed");
        } else if(ready>0 && phase==4) {
            int n=lwip_recv(socket_fd,response+received,sizeof(response)-1-received,0);
            if(n>0) {
                received+=n; response[received]=0;
                if(strchr(response,'\n')) {
                    int code=lab_pan_http_status(response,received);
                    result.http_status=code>0 ? code : 0;
                    result.http_ok=code==200 || code==204;
                    close_socket(); phase=0; result.testing=false;
                    char message[128];
                    rt_snprintf(message,sizeof(message),result.http_ok ? "PASS: example.com HTTP %d via PAN" : "HTTP %d: target not verified (redirect/error/filter)",code);
                    note(message);
                } else if(received==sizeof(response)-1) fail("HTTP status line too long");
            } else if(!n) fail("HTTP connection closed before status line");
            else if(errno!=EWOULDBLOCK && errno!=EAGAIN) fail("HTTP receive failed");
        }
    }
    if(mqtt_mode) {
        /* TCP 粘包可能把下一帧留在解析缓冲区，发送完成后继续解析。 */
        if(phase==6 && result.testing) {
            if(!lab_mqtt_feed(&mqtt,NULL,0)) fail(mqtt.error);
            else if(mqtt.tx_len) {
                phase=5; limit=rt_tick_get()+rt_tick_from_millisecond(8000);
                note(mqtt.matched ? "MQTT echo matched; disconnecting..." : mqtt.subscribed ? "MQTT subscribed; publishing probe..." : "MQTT connected; subscribing...");
            }
        }
        result.mqtt_connected=mqtt.connected; result.mqtt_subscribed=mqtt.subscribed; result.mqtt_published=mqtt.published;
    }
    out->mqtt_connected=result.mqtt_connected; out->mqtt_subscribed=result.mqtt_subscribed;
    out->mqtt_published=result.mqtt_published; out->mqtt_ok=result.mqtt_ok;
    out->testing=result.testing; out->ip_ok=result.ip_ok; out->dns_ok=result.dns_ok;
    out->tcp_ok=result.tcp_ok; out->http_ok=result.http_ok; out->http_status=result.http_status;
    memcpy(out->ip,result.ip,16); memcpy(out->gateway,result.gateway,16);
    memcpy(out->dns,result.dns,16); memcpy(out->remote,result.remote,16);
    if(active && result.message[0]) memcpy(out->message,result.message,sizeof(result.message));
}
