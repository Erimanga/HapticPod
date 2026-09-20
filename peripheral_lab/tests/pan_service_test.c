#include "../src/network/lab_pan_service.c"
#include <assert.h>
#include "../src/network/lab_pan_identity.c"
#include "../src/network/lab_pan_hid_compat.c"
static unsigned reports;
static bool address_fails;
uint8_t ble_get_public_address(bd_addr_t *a) { if(address_fails) return 1; uint8_t mac[]={0xC3,0xB2,0xA1,0x33,0x22,0x11}; memcpy(a->addr,mac,6); return 0; }
void __real_hid_send_report_req_ext(U16 t,BTS2S_BD_ADDR *b,U16 n,U8 *p,BOOL i) { (void)t;(void)b; assert(!i); reports++; if(n && p[0]==0xa1) for(unsigned k=2;k<n;k++) assert(!p[k]); }
static event_t queued;
static unsigned accepts,rejects,disconnects,connections,net_active,net_stops;
rt_tick_t rt_tick_get(void) { return 100; }
rt_tick_t rt_tick_from_millisecond(unsigned n) { return n; }
rt_mutex_t rt_mutex_create(const char *n,unsigned f) { (void)n;(void)f; return (void *)1; }
int rt_mutex_take(struct rt_mutex *m,int t) { (void)m;(void)t;return 0; }
int rt_mutex_release(struct rt_mutex *m) { (void)m;return 0; }
rt_mq_t rt_mq_create(const char *n,size_t z,unsigned c,unsigned f) { (void)n;(void)z;(void)c;(void)f;return (void *)1; }
int rt_mq_send(rt_mq_t q,const void *d,size_t s) { (void)q;assert(s==sizeof(queued));memcpy(&queued,d,s);return 0; }
int rt_mq_recv(rt_mq_t q,void *d,size_t s,int t) { (void)q;(void)d;(void)s;(void)t;return -1; }
rt_thread_t rt_thread_create(const char *n,void (*f)(void *),void *a,unsigned s,unsigned p,unsigned t) { (void)n;(void)f;(void)a;(void)s;(void)p;(void)t;return (void *)1; }
int rt_thread_startup(rt_thread_t t) { (void)t;return 0; }
void lab_publish(lab_id_t i,lab_status_t s,const char *v,const char *d) { (void)i;(void)s;(void)v;(void)d; }
int lab_pan_net_init(void) { return 0; }
void lab_pan_net_activate(void) { net_active++; }
void lab_pan_net_stop(void) { net_stops++; }
void lab_pan_net_test(void) {}
void lab_pan_net_mqtt(void) {}
void lab_pan_net_poll(lab_pan_snapshot_t *s) { (void)s; }
int bt_interface_user_confirm_res(uint8_t *p,unsigned yes) { (void)p;if(yes) accepts++;else rejects++;return 0; }
int bt_interface_io_req_res(uint8_t *p,unsigned c,unsigned m,unsigned b) { (void)p;(void)c;(void)m;(void)b;return 0; }
int bt_interface_set_scan_mode(unsigned a,unsigned b) { (void)a;(void)b;return 0; }
int bt_interface_cancel_connect_req(uint8_t *p) { (void)p;return 0; }
int bt_interface_disc_ext(uint8_t *p,unsigned r) { (void)p;(void)r;return 0; }
int bt_interface_disconnect_req(uint8_t *p) { (void)p;disconnects++;return 0; }
int bt_interface_conn_ext(uint8_t *p,unsigned r) { (void)p;(void)r;connections++;return 0; }
void bt_interface_set_local_name(unsigned n,void *p) { assert(n==strlen("SiFli-Lab-A1B2C3")); assert(!strcmp(p,"SiFli-Lab-A1B2C3")); }
int bt_interface_register_bt_event_notify_callback(int (*f)(uint16_t,uint16_t,uint8_t *,uint16_t)) { (void)f;return 0; }
void bt_cm_set_profile_target(unsigned p,unsigned l,unsigned a) { (void)p;(void)l;(void)a; }
static void command(unsigned c) { event_t e={.generation=command_generation,.type=0xffff,.id=c};handle(&e); }
int main(void)
{
    queue=(void *)1;
    event(BT_NOTIFY_COMMON,BT_NOTIFY_COMMON_BT_STACK_READY,NULL,0);handle(&queued);
    assert(state.ready && !state.enabled);
    assert(!strcmp(state.name,"SiFli-Lab-A1B2C3"));
    assert(!strcmp(state.address,"11:22:33:A1:B2:C3"));
    address_fails=true;command(PAN_BEGIN);assert(!state.enabled && !state.name[0]);
    address_fails=false;
    lab_pan_command(PAN_BEGIN); lab_pan_stop();stop_requested=false;handle(&queued);assert(!state.enabled);
    command(PAN_BEGIN); assert(state.enabled);
    assert(bt_get_class_of_device()==0x020300);
    uint8_t scan=3; event(BT_NOTIFY_COMMON,BT_NOTIFY_COMMON_SCAN_ENB_CFM_IND,&scan,1);handle(&queued); assert(state.scan_mode==3);
    bt_notify_pair_confirm_t pair={.mac={{1,2,3,4,5,6}},.num_val=123456};
    event(BT_NOTIFY_COMMON,BT_NOTIFY_COMMON_USER_CONFIRM_IND,(void *)&pair,sizeof(pair));handle(&queued);
    assert(state.pairing && state.code==123456 && !accepts);
    command(PAN_ACCEPT);assert(accepts==1 && !state.pairing);
    event(BT_NOTIFY_COMMON,BT_NOTIFY_COMMON_ENCRYPTION,pair.mac.addr,6);handle(&queued);
    assert(state.acl && connect_at);connect_pan();assert(connections==1 && connect_end);
    command(PAN_RECONNECT);assert(connections==1);
    bt_notify_profile_state_info_t pan={.mac=pair.mac};
    event(BT_NOTIFY_PAN,BT_NOTIFY_PAN_PROFILE_CONNECTED,(void *)&pan,sizeof(pan));handle(&queued);
    assert(state.pan && net_active==1);
    stop();assert(!strcmp(state.name,"SiFli-Lab-A1B2C3"));assert(!state.enabled && state.stopping && disconnects==1 && net_stops==1);
    event(BT_NOTIFY_PAN,BT_NOTIFY_PAN_PROFILE_CONNECTED,(void *)&pan,sizeof(pan));handle(&queued);assert(net_active==1);
    event(BT_NOTIFY_COMMON,BT_NOTIFY_COMMON_ACL_DISCONNECTED,pair.mac.addr,6);handle(&queued);assert(!state.stopping && !state.acl);
    event(BT_NOTIFY_COMMON,BT_NOTIFY_COMMON_USER_CONFIRM_IND,(void *)&pair,sizeof(pair));handle(&queued);assert(rejects==1);
    state.enabled=true;
    event(BT_NOTIFY_HID,BT_NOTIFY_HID_PROFILE_CONNECTED,(void *)&pan,sizeof(pan));handle(&queued);
    assert(state.hid && state.acl && connect_at);
    stop();
    unsigned before=disconnects;
    event(BT_NOTIFY_HID,BT_NOTIFY_HID_PROFILE_CONNECTED,(void *)&pan,sizeof(pan));handle(&queued);assert(disconnects==before+1);
    uint8_t report[]={0xa1,1,0xff,0xff,0x77};
    __wrap_hid_send_report_req_ext(0,NULL,sizeof(report),report,1);assert(!reports);
    __wrap_hid_send_report_req_ext(0,NULL,sizeof(report),report,0);assert(reports==1 && report[2]==0xff);
    uint8_t handshake[]={0};
    __wrap_hid_send_report_req_ext(0,NULL,1,handshake,0);assert(reports==2);
    puts("PASS: automatic HID input suppression and neutral control report");
    puts("PASS: HID companion scheduling, late link rejection, previous device class and scan status");
    puts("PASS: production PAN consent, stop cancellation, connection debounce, late connection rejection and disconnect cleanup (mock transport)");
}
