#include "network/lab_mqtt.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const uint8_t connack[]={0x20,2,0,0},suback[]={0x90,3,0,1,0};
static void ready(lab_mqtt_t *m)
{
    lab_mqtt_start(m,"sf1234567890",123);
    assert(!m->error && m->tx[0]==0x10 && m->tx[9]==2);
    lab_mqtt_sent(m);
    assert(lab_mqtt_feed(m,connack,1) && !m->connected);
    assert(lab_mqtt_feed(m,connack+1,3) && m->connected && m->tx[0]==0x82);
    lab_mqtt_sent(m);
    for(unsigned i=0;i<sizeof(suback);i++) assert(lab_mqtt_feed(m,suback+i,1));
    assert(m->subscribed && m->tx[0]==0x30 && !m->published);
}
int main(void)
{
    lab_mqtt_t m; ready(&m);
    uint8_t echo[256];size_t n=m.tx_len;memcpy(echo,m.tx,n);lab_mqtt_sent(&m);
    assert(m.published && !m.matched);
    echo[0]|=1;assert(lab_mqtt_feed(&m,echo,n) && !m.matched); /* retained */
    echo[0]=0x30;echo[n-1]^=1;assert(lab_mqtt_feed(&m,echo,n) && !m.matched);echo[n-1]^=1;
    for(size_t i=0;i<n;i++) assert(lab_mqtt_feed(&m,echo+i,1));
    assert(m.matched && !m.done && m.tx_len==2 && m.tx[0]==0xe0);
    lab_mqtt_sent(&m);assert(m.done);
    ready(&m);n=m.tx_len;memcpy(echo,m.tx,n);lab_mqtt_sent(&m);
    uint8_t batch[512];memcpy(batch,echo,n);batch[n-1]^=1;memcpy(batch+n,echo,n);
    assert(lab_mqtt_feed(&m,batch,2*n) && m.matched); /* multiple frames in one TCP read */
    lab_mqtt_start(&m,"test",1);lab_mqtt_sent(&m);
    uint8_t denied[]={0x20,2,0,5};assert(!lab_mqtt_feed(&m,denied,sizeof(denied)) && !m.connected);
    lab_mqtt_start(&m,"test",1);lab_mqtt_sent(&m);assert(lab_mqtt_feed(&m,connack,4));lab_mqtt_sent(&m);
    uint8_t badsub[]={0x90,3,0,2,0};assert(!lab_mqtt_feed(&m,badsub,5));
    lab_mqtt_start(&m,"test",1);lab_mqtt_sent(&m);uint8_t huge[]={0x30,0xff,0x7f};assert(!lab_mqtt_feed(&m,huge,3));
    lab_mqtt_start(&m,"test",1);lab_mqtt_sent(&m);uint8_t badlen[]={0x30,0xff,0xff,0xff,0xff};assert(!lab_mqtt_feed(&m,badlen,5));
    lab_mqtt_start(&m,"test",1);lab_mqtt_sent(&m);uint8_t noncanonical[]={0x20,0x82,0,0,0};assert(!lab_mqtt_feed(&m,noncanonical,5));
    lab_mqtt_start(&m,"bad/#",1);assert(m.error);
    lab_mqtt_start(&m,"123456789012345678901234",1);assert(m.error);
    ready(&m);lab_mqtt_sent(&m);uint8_t truncatedtopic[]={0x30,3,0,99,0};assert(!lab_mqtt_feed(&m,truncatedtopic,5));
    puts("PASS: production MQTT CONNECT/SUBSCRIBE/PUBLISH/DISCONNECT, split/coalesced frames, exact echo, retained/mismatch rejection, denied/malformed ACKs and length bounds");
}
