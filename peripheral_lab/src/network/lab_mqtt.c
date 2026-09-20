/*
 * lab_mqtt.c — MQTT 协议测试：实现一次 QoS 0 发布/订阅精确回环，网络收发与超时由调用者负责。
 */
#include "lab_mqtt.h"
#include <string.h>
#include <stdio.h>
enum { CONNECT=1, CONNACK, SUBSCRIBE, SUBACK, PUBLISH, ECHO, DISCONNECT, DONE };
static bool fail(lab_mqtt_t *m,const char *why) { m->error=why; return false; }
static size_t string(uint8_t *out,const char *s)
{
    size_t n=strlen(s); out[0]=(uint8_t)(n>>8); out[1]=(uint8_t)n; memcpy(out+2,s,n); return n+2;
}
/* 将固定头、可变长度字段和消息体组装到待发送缓冲。 */
static void packet(lab_mqtt_t *m,uint8_t type,const uint8_t *body,size_t n)
{
    size_t i=1,rest=n; m->tx[0]=type;
    do { uint8_t b=rest%128; rest/=128; m->tx[i++]=b|(rest ? 128 : 0); } while(rest);
    if(n) memcpy(m->tx+i,body,n);
    m->tx_len=i+n; m->tx_sent=0;
}
/* 生成本轮主题和合成载荷，准备 Clean Session CONNECT 报文。 */
void lab_mqtt_start(lab_mqtt_t *m,const char *client,uint32_t nonce)
{
    memset(m,0,sizeof(*m));
    if(!client || !client[0] || strlen(client)>23) { fail(m,"Invalid MQTT client ID"); return; }
    /* 限定 ASCII 字母数字，避免生成通配主题或非法客户端标识。 */
    for(const char *p=client;*p;p++) if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9'))) { fail(m,"Invalid MQTT client ID"); return; }
    snprintf(m->client,sizeof(m->client),"%s",client);
    snprintf(m->topic,sizeof(m->topic),"sifli/lab/%s/echo",client);
    snprintf(m->payload,sizeof(m->payload),"lab-echo-%s-%08lx",client,(unsigned long)nonce);
    uint8_t body[128]={0,4,'M','Q','T','T',4,2,0,60}; /* clean session; keepalive 60 s */
    size_t n=10+string(body+10,client); packet(m,0x10,body,n); m->phase=CONNECT;
}
/* 调用者确认整包发送完毕后，才能进入等待应答或完成阶段。 */
void lab_mqtt_sent(lab_mqtt_t *m)
{
    m->tx_len=m->tx_sent=0;
    if(m->phase==CONNECT) m->phase=CONNACK;
    else if(m->phase==SUBSCRIBE) m->phase=SUBACK;
    else if(m->phase==PUBLISH) { m->published=true; m->phase=ECHO; }
    else if(m->phase==DISCONNECT) { m->phase=DONE; m->done=true; }
}
/* 按当前阶段校验完整报文，收到精确回环后准备 DISCONNECT。 */
static bool frame(lab_mqtt_t *m,uint8_t h,const uint8_t *p,size_t n)
{
    if(m->phase==CONNACK) {
        if(h!=0x20 || n!=2 || p[0]!=0 || p[1]>5) return fail(m,"Malformed MQTT CONNACK");
        if(p[1]) return fail(m,"Broker rejected MQTT CONNECT");
        m->connected=true;
        uint8_t body[80]={0,1}; size_t z=2+string(body+2,m->topic); body[z++]=0;
        packet(m,0x82,body,z); m->phase=SUBSCRIBE; return true;
    }
    if(m->phase==SUBACK) {
        if(h!=0x90 || n!=3 || p[0]!=0 || p[1]!=1) return fail(m,"Malformed or mismatched SUBACK");
        if(p[2]!=0) return fail(m,"Broker did not grant subscription QoS 0");
        m->subscribed=true;
        uint8_t body[160]; size_t z=string(body,m->topic), payload=strlen(m->payload);
        memcpy(body+z,m->payload,payload); packet(m,0x30,body,z+payload); m->phase=PUBLISH; return true;
    }
    if(m->phase==ECHO) {
        if((h>>4)!=3 || (h&0x0e)!=0 || n<2) return fail(m,"Unexpected MQTT echo packet");
        size_t topic=((unsigned)p[0]<<8)|p[1];
        if(!topic || topic>n-2) return fail(m,"Malformed MQTT topic length");
        /* 不接受 retained、别的主题或旧消息；等待本次精确匹配，超时由调用者控制。 */
        if((h&1) || topic!=strlen(m->topic) || memcmp(p+2,m->topic,topic) ||
            n-2-topic!=strlen(m->payload) || memcmp(p+2+topic,m->payload,n-2-topic)) return true;
        m->matched=true; packet(m,0xe0,NULL,0); m->phase=DISCONNECT; return true;
    }
    return fail(m,"Unexpected MQTT packet order");
}
/* 累计 TCP 字节流，处理分包和粘包；缓冲或报文长度超限即失败。 */
bool lab_mqtt_feed(lab_mqtt_t *m,const uint8_t *data,size_t len)
{
    if(m->error) return false;
    if(len>sizeof(m->rx)-m->rx_len) return fail(m,"MQTT receive buffer exceeded");
    if(len) memcpy(m->rx+m->rx_len,data,len); m->rx_len+=len;
    while(m->rx_len>=2 && !m->tx_len) {
        size_t n=0,mul=1,i=1; bool complete=false;
        for(unsigned k=0;k<4;k++) {
            if(i>=m->rx_len) return true;
            uint8_t b=m->rx[i++]; n+=(b&127)*mul;
            if(!(b&128)) { if(k && (b&127)==0) return fail(m,"Noncanonical MQTT length"); complete=true; break; }
            mul*=128;
        }
        if(!complete) return fail(m,"Invalid MQTT remaining length");
        if(n>sizeof(m->rx)-i) return fail(m,"MQTT packet too large");
        if(m->rx_len<i+n) return true;
        if(!frame(m,m->rx[0],m->rx+i,n)) return false;
        m->rx_len-=i+n; memmove(m->rx,m->rx+i+n,m->rx_len);
    }
    return true;
}
