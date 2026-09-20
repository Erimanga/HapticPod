/* Real SDK declarations and production state machine; only transport/RTOS calls are mocked. */
#include "bf0_ble_gap.h"
#include "bf0_sibles_advertising.h"
#include "ble_connection_manager.h"
#undef BLE_EVENT_REGISTER
#define BLE_EVENT_REGISTER(fn, context)
#include "../src/bluetooth/lab_ble_service.c"
#include <assert.h>
static event_t queue[40];
static unsigned head, tail, notify_calls, disconnect_calls, scan_calls;
static unsigned accepted, rejected;
static bool full, notify_full;
static rt_tick_t now;
rt_base_t rt_hw_interrupt_disable(void) { return 0; }
void rt_hw_interrupt_enable(rt_base_t level) { (void)level; }
rt_tick_t rt_tick_get(void) { return now; }
rt_tick_t rt_tick_from_millisecond(unsigned ms) { return ms; }
int rt_mutex_init(struct rt_mutex *m,const char *n,unsigned f) { (void)m;(void)n;(void)f;return 0; }
int rt_mutex_take(struct rt_mutex *m,int t) { (void)m;(void)t;return 0; }
int rt_mutex_release(struct rt_mutex *m) { (void)m;return 0; }
rt_mq_t rt_mq_create(const char *n,size_t z,unsigned c,unsigned f) { (void)n;(void)z;(void)c;(void)f;return (void *)1; }
int rt_mq_delete(rt_mq_t q) { (void)q;return 0; }
int rt_mq_send(rt_mq_t q,const void *data,size_t size) { assert(q && size==sizeof(event_t)); if(full) return -1; assert(tail<40); memcpy(&queue[tail++],data,size);return 0; }
int rt_mq_recv(rt_mq_t q,void *data,size_t size,int wait) { (void)q;(void)wait; if(head==tail) return -1;memcpy(data,&queue[head++],size);return 0; }
rt_thread_t rt_thread_create(const char *n,void (*f)(void *),void *a,unsigned s,unsigned p,unsigned t) { (void)n;(void)f;(void)a;(void)s;(void)p;(void)t;return (void *)1; }
int rt_thread_startup(rt_thread_t t) { (void)t;return 0; }
void lab_publish(lab_id_t id,lab_status_t s,const char *v,const char *d) { (void)id;(void)s;(void)v;(void)d; }
void sifli_ble_enable(void) {}
sibles_hdl sibles_register_svc_128(sibles_register_svc_128_t *s) { (void)s;return (void *)1; }
void sibles_register_cbk(sibles_hdl h,sibles_get_cbk g,sibles_set_cbk s) { (void)h;(void)g;(void)s; }
int sibles_write_value(uint8_t c,sibles_value_t *v) { (void)c;notify_calls++;return notify_full ? 0 : v->len; }
uint8_t sibles_exchange_mtu(uint8_t c) { (void)c;return 0; }
uint8_t ble_get_public_address(bd_addr_t *a) { memset(a,0,sizeof(*a));return 0; }
uint8_t sibles_advertising_init(sibles_advertising_context_t *c,sibles_advertising_para_t *p) { (void)c;assert(!p->config.is_auto_restart);return 0; }
uint8_t sibles_advertising_start(sibles_advertising_context_t *c) { (void)c;return 0; }
uint8_t sibles_advertising_stop(sibles_advertising_context_t *c) { (void)c;return 0; }
int sibles_advertising_evt_handler(uint16_t e,uint8_t *d,uint16_t n,uint32_t c) { (void)e;(void)d;(void)n;(void)c;return 0; }
uint8_t ble_gap_scan_start(ble_gap_scan_start_t *p) { assert(p->duration==1000);scan_calls++;return 0; }
uint8_t ble_gap_scan_stop(void) { return 0; }
uint8_t ble_gap_create_connection(ble_gap_connection_create_param_t *p) { (void)p;return 0; }
uint8_t ble_gap_cancel_create_connection(void) { return 0; }
uint8_t ble_gap_disconnect(ble_gap_disconnect_t *p) { (void)p;disconnect_calls++;return 0; }
uint8_t ble_gap_update_phy(ble_gap_update_phy_t *p) { (void)p;return 0; }
uint8_t ble_gap_get_remote_physical(ble_gap_get_phy_t *p) { (void)p;return 0; }
uint8_t connection_manager_set_bond_ack(uint8_t s) { (void)s;return 0; }
uint8_t connection_manager_set_bond_cnf_iocap(uint8_t s) { (void)s;return 0; }
void connection_manager_create_bond(uint8_t c) { (void)c; }
uint8_t connection_manager_bond_ack_reply(uint8_t c,uint8_t r,bool a) { (void)c;(void)r;if(a)accepted++;else rejected++;return 0; }
uint8_t connection_manager_delete_bond(ble_gap_addr_t a) { (void)a;return 0; }
uint8_t connection_manager_get_enc_state(uint8_t c) { (void)c;return ENC_STATE_NONE; }
uint8_t connection_manager_get_bond_state(uint8_t c) { (void)c;return BOND_STATE_NONE; }
static void drain(void) { while(head<tail) handle(&queue[head++]);head=tail=0; }
static void reset(void)
{
    memset(&state,0,sizeof(state)); state.ready=true; initialized=true; events=(void *)1;
    stop_requested=stop_sent=accepting=awaiting_echo=full=notify_full=fault_latched=false;
    rejected_connection=255; head=tail=notify_calls=disconnect_calls=scan_calls=accepted=rejected=0;
}
int main(void)
{
    reset(); assert(init_service());
    assert(lab_ble_command(LAB_BLE_SCAN,0)); lab_ble_stop();
    stop_requested=false; drain(); assert(scan_calls==0); /* old UI command cancelled by session generation */
    command(LAB_BLE_SCAN,0); assert(state.scanning && scan_calls==1);
    stop_all(); assert(state.stopping && state.scanning); /* wait for controller event */
    event_t event={.id=BLE_GAP_SCAN_STOPPED_IND}; handle(&event); assert(!state.scanning);
    reset(); state.connected=true; state.conn_index=3;
    command(LAB_BLE_SEND,0); assert(!notify_calls && !awaiting_echo);
    state.subscribed=true; notify_full=true;
    command(LAB_BLE_SEND,0); assert(!awaiting_echo && state.tx_bytes==0);
    notify_full=false; command(LAB_BLE_SEND,0); assert(awaiting_echo && state.tx_bytes==12);
    event=(event_t){.id=EVENT_RX,.length=13}; event.data.bytes[0]=3;
    memcpy(event.data.bytes+1,expected,12); event.data.bytes[12]^=1; handle(&event); assert(!state.loop_ok);
    memcpy(event.data.bytes+1,expected,12); handle(&event); assert(state.loop_ok==1 && !awaiting_echo);
    state.echo=true; event.data.bytes[0]=4; unsigned before=notify_calls;handle(&event);assert(notify_calls==before);
    reset(); accepting=false;
    connection_manager_connect_ind_t conn={.conn_idx=5};
    stack_event(CONNECTION_MANAGER_CONNCTED_IND,(uint8_t *)&conn,sizeof(conn),0);drain();
    assert(disconnect_calls==1 && rejected_connection==5 && stop_requested);
    reset(); state.connected=true; state.conn_index=1;
    connection_manager_bond_ack_infor_t pair={.conn_idx=1,.request=GAPC_NC_EXCH,.confirm_data=123456};
    stack_event(CONNECTION_MANAGER_BOND_AUTH_INFOR,(uint8_t *)&pair,sizeof(pair),0);drain();
    assert(state.pair_pending && !accepted && state.passkey==123456);
    command(LAB_BLE_REJECT,0);assert(!state.pair_pending && rejected==1);
    uint8_t bytes[21]={0}; sibles_set_cbk_t write={.idx=ATTR_RX,.len=21,.value=bytes};
    assert(write_value(1,&write)==0x0D);
    write.len=1; full=true;assert(write_value(1,&write)==0x11);
    puts("PASS: production BLE command cancellation, asynchronous stop, notify backpressure, exact echo, late link rejection, pairing consent and RX bounds.");
}
