/*
 * lab_pan_service.c — PAN 蓝牙服务：管理手机配对、HID 辅助连接与 PAN 生命周期，并轮询网络测试。
 */
#include "lab.h"
#include "bf0_ble_common.h"
#include "lab_pan_net.h"
#include "bts2_app_inc.h"
#include "bt_connection_manager.h"
#include <string.h>
typedef struct { unsigned generation; uint16_t type,id,len; uint8_t data[32]; } event_t;
static rt_mq_t queue;
static rt_mutex_t mutex;
static lab_pan_snapshot_t state, published;
static uint8_t peer[6];
static volatile bool stop_requested;
static volatile unsigned command_generation;
static rt_tick_t pair_end, discover_end, connect_at, connect_end, dhcp_end, stop_end;
static bool due(rt_tick_t t) { return t && (int32_t)(rt_tick_get()-t)>=0; }
static rt_tick_t after(unsigned ms) { return rt_tick_get()+rt_tick_from_millisecond(ms); }
static void note(const char *s) { rt_snprintf(state.message,sizeof(state.message),"%s",s); rt_kprintf("[lab:pan] %s\n",s); }
static void address(const uint8_t *p)
{
    memcpy(peer,p,6);
    rt_snprintf(state.peer,sizeof(state.peer),"%02X:%02X:%02X:%02X:%02X:%02X",p[5],p[4],p[3],p[2],p[1],p[0]);
}
/* SDK public address is stored least-significant byte first. */
static bool identity(void)
{
    bd_addr_t a;
    if (ble_get_public_address(&a)) {
        state.name[0]=state.address[0]=0;
        note("Bluetooth address unavailable; retry Begin pairing");
        return false;
    }
    rt_snprintf(state.address,sizeof(state.address),"%02X:%02X:%02X:%02X:%02X:%02X",
        a.addr[5],a.addr[4],a.addr[3],a.addr[2],a.addr[1],a.addr[0]);
    rt_snprintf(state.name,sizeof(state.name),"SiFli-Lab-%02X%02X%02X",a.addr[2],a.addr[1],a.addr[0]);
    bt_interface_set_local_name(strlen(state.name),state.name);
    rt_kprintf("[lab:pan:identity] MAC=%s name=%s\n",state.address,state.name);
    return true;
}
/* 经典蓝牙回调只复制有界事件；关键消息入队失败时请求停止。 */
static int event(uint16_t type,uint16_t id,uint8_t *data,uint16_t len)
{
    if(type!=BT_NOTIFY_COMMON && type!=BT_NOTIFY_PAN && type!=BT_NOTIFY_HID) return 0;
    if(type==BT_NOTIFY_HID && id!=BT_NOTIFY_HID_PROFILE_CONNECTED && id!=BT_NOTIFY_HID_PROFILE_DISCONNECTED) return 0;
    event_t e={.type=type,.id=id,.len=len};
    if(len>sizeof(e.data) || (len && !data)) return 0;
    if(len) memcpy(e.data,data,len);
    if(rt_mq_send(queue,&e,sizeof(e))!=RT_EOK) { command_generation++; stop_requested=true; }
    return 0;
}
/* 关闭可发现状态和网络测试，请求断开 HID/PAN/基础链路，保留绑定。 */
static void stop(void)
{
    state.enabled=false; connect_at=connect_end=discover_end=dhcp_end=0;
    if(state.pairing) bt_interface_user_confirm_res(peer,0);
    state.pairing=false; pair_end=0;
    bt_interface_set_scan_mode(0,0);
    lab_pan_net_stop();
    if(state.acl || state.pan || state.hid) {
        bt_interface_cancel_connect_req(peer);
        bt_interface_disc_ext(peer,BT_PROFILE_PAN);
        bt_interface_disc_ext(peer,BT_PROFILE_HID);
        bt_interface_disconnect_req(peer);
    }
    state.stopping=state.acl || state.pan || state.hid; stop_end=state.stopping ? after(10000) : 0;
    note(state.stopping ? "Disconnecting; bonds are retained" : "Stopped; bonds are retained");
}
/* 在基础链路就绪时提交 PAN 连接请求，并启动连接超时计时。 */
static void connect_pan(void)
{
    connect_at=0;
    if(!state.enabled || !state.acl || state.pan || connect_end) return;
    if(bt_interface_conn_ext(peer,BT_PROFILE_PAN)!=0) note("PAN request rejected; retry Connect PAN");
    else { connect_end=after(15000); note("Connecting PAN (15 s)..."); }
}
/* 串行处理用户命令和蓝牙事件，连接成功后才启用 PAN 网络层。 */
static void handle(event_t *e)
{
    if(e->type==0xffff) {
        if(e->generation!=command_generation || stop_requested) return;
        switch(e->id) {
        case PAN_BEGIN:
            if(!state.ready || state.stopping || state.enabled) break;
            if(!identity()) break;
            state.enabled=true; discover_end=after(60000);
            bt_interface_set_scan_mode(1,1);
            note("Enable phone Bluetooth tethering, then pair the name shown above (60 s)"); break;
        case PAN_RECONNECT: connect_pan(); break;
        case PAN_MQTT: if(state.enabled && state.pan) lab_pan_net_mqtt(); break;
        case PAN_TEST: if(state.enabled && state.pan) lab_pan_net_test(); break;
        case PAN_ACCEPT: case PAN_REJECT:
            if(state.pairing) { bt_interface_user_confirm_res(peer,e->id==PAN_ACCEPT); state.pairing=false; pair_end=0; note(e->id==PAN_ACCEPT ? "Accepted; confirm on phone too" : "Pairing rejected"); } break;
        }
        return;
    }
    rt_kprintf("[lab:pan:event] type=%u id=%u len=%u\n",e->type,e->id,e->len);
    if(e->type==BT_NOTIFY_COMMON) {
        if(e->id==BT_NOTIFY_COMMON_BT_STACK_READY) {
            state.ready=true; bt_interface_set_scan_mode(0,0);
            if(identity()) note("Ready. Enable phone hotspot / Bluetooth tethering first");
        } else if(e->id==BT_NOTIFY_COMMON_SCAN_ENB_CFM_IND && e->len==1) {
            state.scan_mode=e->data[0];
            rt_kprintf("[lab:pan:scan] mode=%u (3=discoverable+connectable)\n",state.scan_mode);
        } else if(e->id==BT_NOTIFY_COMMON_IO_CAPABILITY_IND && e->len>=6) {
            bt_interface_io_req_res(e->data,(state.enabled && (!(state.acl || state.pairing) || !memcmp(peer,e->data,6))) ? IO_CAPABILITY_DISPLAY_YES_NO : IO_CAPABILITY_REJECT_REQ,1,1);
        } else if(e->id==BT_NOTIFY_COMMON_USER_CONFIRM_IND && e->len>=sizeof(bt_notify_pair_confirm_t)) {
            bt_notify_pair_confirm_t p; memcpy(&p,e->data,sizeof(p));
            if(!state.enabled || ((state.acl || state.pairing) && memcmp(peer,p.mac.addr,6))) { bt_interface_user_confirm_res(p.mac.addr,0); return; }
            address(p.mac.addr); state.pairing=true; state.code=p.num_val; pair_end=after(25000);
            note("Compare the code on both devices, then Accept (25 s)");
        } else if(e->id==BT_NOTIFY_COMMON_PAIR_IND && e->len>=sizeof(bt_notify_device_base_info_t)) {
            bt_notify_device_base_info_t p; memcpy(&p,e->data,sizeof(p));
            rt_kprintf("[lab:pan:pair] result=0x%02X\n",p.res);
            if(!state.enabled || (state.acl && memcmp(peer,p.mac.addr,6))) return;
            state.pairing=false; pair_end=0;
            if(p.res) { char msg[100]; rt_snprintf(msg,sizeof(msg),"Pair failed 0x%02X; forget phone bond and retry",p.res); note(msg); }
            else { address(p.mac.addr); state.acl=true; discover_end=0; if(!state.pan && !connect_end) connect_at=after(3000); note("Paired; waiting for companion profile / PAN"); }
        } else if(e->id==BT_NOTIFY_COMMON_ENCRYPTION && e->len>=6) {
            if(!state.enabled || (state.acl && memcmp(peer,e->data,6))) { bt_interface_disconnect_req(e->data); return; }
            address(e->data); state.acl=true; state.pairing=false; discover_end=0;
            connect_at=after(3000); bt_interface_set_scan_mode(0,0); note("Encrypted link ready; PAN starts in 3 s");
        } else if(e->id==BT_NOTIFY_COMMON_ACL_CONNECTED && e->len>=sizeof(bt_notify_device_acl_conn_info_t)) {
            bt_notify_device_acl_conn_info_t p; memcpy(&p,e->data,sizeof(p));
            if(p.res) return;
            if(!state.enabled || ((state.acl || state.pairing) && memcmp(peer,p.mac.addr,6))) { bt_interface_disconnect_req(p.mac.addr); return; }
            address(p.mac.addr); state.acl=true;
        } else if(e->id==BT_NOTIFY_COMMON_ACL_DISCONNECTED && e->len>=6 && !memcmp(peer,e->data,6)) {
            state.acl=state.hid=state.pan=state.pairing=state.stopping=false; connect_at=connect_end=dhcp_end=0;
            lab_pan_net_stop();
            unsigned reason=0xff;
            if(e->len>=sizeof(bt_notify_device_base_info_t)) { bt_notify_device_base_info_t p; memcpy(&p,e->data,sizeof(p)); reason=p.res; }
            char msg[128]; rt_snprintf(msg,sizeof(msg),"Phone disconnected (0x%02X). Stop, then Begin to retry",reason); note(msg);
        }
    } else if(e->len>=sizeof(bt_notify_profile_state_info_t)) {
        bt_notify_profile_state_info_t p; memcpy(&p,e->data,sizeof(p));
        rt_kprintf("[lab:pan:profile] type=%u event=%u result=0x%02X\n",e->type,e->id,p.res);
        if(e->type==BT_NOTIFY_HID) {
            if(e->id==BT_NOTIFY_HID_PROFILE_CONNECTED && !p.res) {
                if(!state.enabled || (state.acl && memcmp(peer,p.mac.addr,6))) { bt_interface_disc_ext(p.mac.addr,BT_PROFILE_HID); bt_interface_disconnect_req(p.mac.addr); return; }
                address(p.mac.addr); state.acl=state.hid=true;
                bt_interface_set_scan_mode(0,0); discover_end=0;
                if(!state.pan && !connect_end) connect_at=after(3000);
                note("HID companion ready; PAN connection follows");
            } else if(e->id==BT_NOTIFY_HID_PROFILE_DISCONNECTED && !memcmp(peer,p.mac.addr,6)) state.hid=false;
            return;
        }
        if(e->id==BT_NOTIFY_PAN_PROFILE_CONNECTED && !p.res) {
            if(!state.enabled || ((state.acl || state.pairing) && memcmp(peer,p.mac.addr,6))) { bt_interface_disc_ext(p.mac.addr,BT_PROFILE_PAN); return; }
            address(p.mac.addr); state.acl=state.pan=true; connect_at=connect_end=discover_end=0;
            dhcp_end=after(30000); lab_pan_net_activate();
        } else if(e->id==BT_NOTIFY_PAN_PROFILE_DISCONNECTED && !memcmp(peer,p.mac.addr,6)) {
            state.pan=false; connect_end=dhcp_end=0; lab_pan_net_stop(); note("PAN disconnected; check tethering, then Connect PAN");
        }
    }
}
/* 处理配对、发现和连接超时，轮询网络层并发布带锁的界面快照。 */
static void worker(void *arg)
{
    (void)arg; event_t e; bool passed=false, mqtt_passed=false;
    rt_tick_t startup_end=after(15000);
    while(1) {
        if(stop_requested) { stop_requested=false; stop(); }
        if(rt_mq_recv(queue,&e,sizeof(e),rt_tick_from_millisecond(50))==RT_EOK) handle(&e);
        if(!state.ready && due(startup_end)) { startup_end=after(60000); note("Stack not ready; check serial initialization log"); }
        if(state.stopping && due(stop_end)) { stop_end=0; note("Disconnect timeout; restart board before another test"); }
        if(due(pair_end)) { bt_interface_user_confirm_res(peer,0); pair_end=0; state.pairing=false; note("Pairing confirmation expired"); }
        if(due(discover_end)) stop();
        if(due(connect_at)) connect_pan();
        if(due(connect_end)) { connect_end=0; bt_interface_cancel_connect_req(peer); note("PAN timeout; enable Bluetooth tethering and retry"); }
        lab_pan_snapshot_t net=state; lab_pan_net_poll(&net);
        if(state.pan && !state.pairing) state=net;
        else { state.ip_ok=state.dns_ok=state.tcp_ok=state.http_ok=state.testing=false;
            state.mqtt_connected=state.mqtt_subscribed=state.mqtt_published=state.mqtt_ok=false; }
        if(state.ip_ok) dhcp_end=0;
        if(due(dhcp_end)) { dhcp_end=0; note("No IP after 30 s; check phone tethering and reconnect PAN"); lab_pan_net_stop(); }
        if(state.http_ok && !passed) lab_publish(LAB_PAN,LAB_PASS,"HTTP reachable","DNS + TCP + HTTP HEAD example.com passed via PAN. TLS not tested.");
        if(state.mqtt_ok && !mqtt_passed) lab_publish(LAB_PAN,LAB_PASS,"MQTT loopback OK","Connected, subscribed and received exact current test payload via PAN. Public plaintext broker.");
        mqtt_passed=state.mqtt_ok;
        passed=state.http_ok;
        rt_mutex_take(mutex,RT_WAITING_FOREVER); published=state; rt_mutex_release(mutex);
    }
}
/* 注册经典蓝牙回调并启动 PAN 任务，须先于 BLE 任务启动共享蓝牙栈。 */
int lab_pan_init(void)
{
    mutex=rt_mutex_create("pan",RT_IPC_FLAG_PRIO);
    queue=rt_mq_create("pan",sizeof(event_t),24,RT_IPC_FLAG_FIFO);
    if(!mutex || !queue || lab_pan_net_init()!=RT_EOK) return -RT_ENOMEM;
    rt_thread_t t=rt_thread_create("lab_pan",worker,NULL,4096,22,10);
    if(!t) return -RT_ENOMEM;
    /* 恢复 SDK PAN 示例的 HID 辅助 Profile，PAN 仍由本任务连接。 */
    bt_cm_set_profile_target(BT_CM_HID,BT_LINK_PHONE,0);
    bt_interface_register_bt_event_notify_callback(event);
    rt_thread_startup(t); return RT_EOK;
}
/* 命令携带当前会话编号入队，防止停止前的旧操作重新开启测试。 */
bool lab_pan_command(lab_pan_command_t c) { event_t e={.generation=command_generation,.type=0xffff,.id=c}; return queue && rt_mq_send(queue,&e,sizeof(e))==RT_EOK; }
/* 使排队旧命令失效，并通知任务执行断开清理。 */
void lab_pan_stop(void) { command_generation++; stop_requested=true; }
/* 复制已发布的状态，避免界面与后台同时读写同一结构。 */
void lab_pan_snapshot(lab_pan_snapshot_t *s) { memset(s,0,sizeof(*s)); if(mutex) { rt_mutex_take(mutex,RT_WAITING_FOREVER); *s=published; rt_mutex_release(mutex); } }
