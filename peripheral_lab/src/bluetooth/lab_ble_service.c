/*
 * lab_ble_service.c — BLE 服务：专用任务管理扫描、广播、GATT 和配对，SDK 回调通过队列交接事件。
 */
#include "lab.h"
#include <rthw.h>
#include "lab_ble.h"
#include "bf0_ble_gap.h"
#include "bf0_sibles.h"
#include "bf0_sibles_advertising.h"
#include "ble_connection_manager.h"
#include <string.h>
#include <stddef.h>

/* 单一任务持有协议状态。回调仅向有界队列复制事件，UI 读取快照。 */
typedef struct { unsigned id, length; union { uint32_t align; uint8_t bytes[160]; } data; } event_t;
static struct rt_mutex snapshot_lock;
static rt_mq_t events;
static lab_ble_snapshot_t state, published;
static volatile bool stop_requested, queue_fault;
static volatile unsigned lost_reports, generation;
static bool fault_latched;
static uint8_t rejected_connection = 0xFF;
static bool initialized, adv_initialized, accepting, awaiting_echo, stop_sent;
static rt_tick_t deadline, pair_deadline, echo_deadline, stop_deadline;
static uint8_t pair_request, pair_conn, expected[12];
static uint32_t sequence;
static sibles_hdl service;
static ble_gap_addr_t last_peer;
static bool have_peer;
static uint8_t callback_cccd[2];
SIBLES_ADVERTISING_CONTEXT_DECLAR(adv_context);
enum { EVENT_COMMAND=0xF000, EVENT_SCAN, EVENT_RX, EVENT_CCCD, EVENT_ADV_ON, EVENT_ADV_OFF };
enum { ATTR_SERVICE, ATTR_RX_CHAR, ATTR_RX, ATTR_TX_CHAR, ATTR_TX, ATTR_CCCD, ATTR_STATUS_CHAR, ATTR_STATUS, ATTR_COUNT };
#define SERIAL_UUID_16(x) {((uint8_t)((x)&0xff)),((uint8_t)((x)>>8))}
#define UUID_VALUE(n) {0x01,0x49,0x4c,0x46,0x49,0x53,0x31,0x9c,0x65,0x4c,0x52,0x8a,n,0x00,0xc1,0x7b}
static uint8_t service_uuid[16] = UUID_VALUE(0);
/* 自定义 GATT 属性：RX 接收手机写入，TX 发通知，CCCD 保存订阅开关，Status 提供只读信息。 */
BLE_GATT_SERVICE_DEFINE_128(lab_attributes) {
    BLE_GATT_SERVICE_DECLARE(ATTR_SERVICE, SERIAL_UUID_16_PRI_SERVICE, BLE_GATT_PERM_READ_ENABLE),
    BLE_GATT_CHAR_DECLARE(ATTR_RX_CHAR, SERIAL_UUID_16_CHARACTERISTIC, BLE_GATT_PERM_READ_ENABLE),
    BLE_GATT_CHAR_VALUE_DECLARE(ATTR_RX, UUID_VALUE(1), BLE_GATT_PERM_WRITE_REQ_ENABLE | BLE_GATT_PERM_WRITE_COMMAND_ENABLE,
        BLE_GATT_VALUE_PERM_UUID_128 | BLE_GATT_VALUE_PERM_RI_ENABLE, LAB_BLE_PAYLOAD),
    BLE_GATT_CHAR_DECLARE(ATTR_TX_CHAR, SERIAL_UUID_16_CHARACTERISTIC, BLE_GATT_PERM_READ_ENABLE),
    BLE_GATT_CHAR_VALUE_DECLARE(ATTR_TX, UUID_VALUE(2), BLE_GATT_PERM_NOTIFY_ENABLE,
        BLE_GATT_VALUE_PERM_UUID_128 | BLE_GATT_VALUE_PERM_RI_ENABLE, LAB_BLE_PAYLOAD),
    BLE_GATT_DESCRIPTOR_DECLARE(ATTR_CCCD, SERIAL_UUID_16_CLIENT_CHAR_CFG,
        BLE_GATT_PERM_READ_ENABLE | BLE_GATT_PERM_WRITE_REQ_ENABLE, BLE_GATT_VALUE_PERM_RI_ENABLE, 2),
    BLE_GATT_CHAR_DECLARE(ATTR_STATUS_CHAR, SERIAL_UUID_16_CHARACTERISTIC, BLE_GATT_PERM_READ_ENABLE),
    BLE_GATT_CHAR_VALUE_DECLARE(ATTR_STATUS, UUID_VALUE(3), BLE_GATT_PERM_READ_ENABLE,
        BLE_GATT_VALUE_PERM_UUID_128 | BLE_GATT_VALUE_PERM_RI_ENABLE, LAB_BLE_PAYLOAD),
};
static void message(const char *text) { rt_snprintf(state.message, sizeof(state.message), "%s", text); rt_kprintf("[lab:ble] %s\n", text); }
static void error(const char *op, unsigned status) { char text[128]; rt_snprintf(text, sizeof(text), "%s failed: 0x%02X", op, status); message(text); }
static void address(char text[18], const uint8_t a[6]) { rt_snprintf(text, 18, "%02X:%02X:%02X:%02X:%02X:%02X", a[5],a[4],a[3],a[2],a[1],a[0]); }
static bool elapsed(rt_tick_t at) { return (int32_t)(rt_tick_get()-at) >= 0; }
/* 复制事件到有界队列，避免后台继续引用 SDK 回调的临时数据。 */
static bool enqueue(unsigned id, const void *data, unsigned size)
{
    if (!events || size > sizeof(((event_t *)0)->data.bytes)) return false;
    event_t e = {0}; e.id=id; e.length=size;
    if (size) memcpy(e.data.bytes, data, size);
    return rt_mq_send(events, &e, sizeof(e)) == RT_EOK;
}
/* 读取已发布的协议状态副本，UI 不直接访问任务内部状态。 */
void lab_ble_snapshot(lab_ble_snapshot_t *out)
{
    if (!initialized) { memset(out, 0, sizeof(*out)); strcpy(out->message, "BLE task unavailable"); return; }
    rt_mutex_take(&snapshot_lock, RT_WAITING_FOREVER); *out=published; rt_mutex_release(&snapshot_lock);
}
/* 把界面命令连同当前会话编号入队，由 BLE 任务执行。 */
bool lab_ble_command(lab_ble_command_t cmd, unsigned index)
{
    unsigned args[3] = {cmd,index,generation};
    return initialized && enqueue(EVENT_COMMAND,args,sizeof(args));
}
/* 使旧命令失效并提交停止请求，后续由任务完成无线活动清理。 */
void lab_ble_stop(void)
{
    rt_base_t level=rt_hw_interrupt_disable();
    generation++; stop_requested=true;
    rt_hw_interrupt_enable(level);
}
/* GATT 读回调：返回固定状态或通知订阅配置。 */
static uint8_t *read_value(uint8_t conn, uint8_t index, uint16_t *len)
{
    (void)conn; *len=0;
    static uint8_t info[] = "SiFli Lab BLE v1";
    if (index == ATTR_STATUS) { *len=sizeof(info)-1; return info; }
    if (index == ATTR_CCCD) { *len=2; return callback_cccd; }
    return NULL;
}
/* GATT 写回调：检查长度和订阅配置，复制 RX 数据后交给后台处理。 */
static uint8_t write_value(uint8_t conn, sibles_set_cbk_t *p)
{
    uint8_t value[LAB_BLE_PAYLOAD+1]; value[0]=conn;
    if (p->idx == ATTR_CCCD) {
        if (p->len != 2 || p->value[1] || p->value[0] > 1) return 0x0D;
        memcpy(value+1,p->value,2);
        if (!enqueue(EVENT_CCCD,value,3)) return 0x11;
        memcpy(callback_cccd,p->value,2);
        return 0;
    }
    if (p->idx != ATTR_RX || !p->len || p->len > LAB_BLE_PAYLOAD) return 0x0D;
    memcpy(value+1,p->value,p->len);
    return enqueue(EVENT_RX,value,p->len+1) ? 0 : 0x11;
}
static uint8_t adv_event(uint8_t event, void *context, void *data)
{
    (void)context;
    uint8_t status;
    if (event == SIBLES_ADV_EVT_ADV_STARTED) {
        status=((sibles_adv_evt_startted_t *)data)->status;
        if (!enqueue(EVENT_ADV_ON,&status,1)) queue_fault=true;
    } else if (event == SIBLES_ADV_EVT_ADV_STOPPED) {
        status=((sibles_adv_evt_stopped_t *)data)->reason;
        if (!enqueue(EVENT_ADV_OFF,&status,1)) queue_fault=true;
    }
    return 0;
}
/* 将扫描报告和关键协议事件转为队列消息；扫描丢包与关键事件丢失分别记录。 */
static int stack_event(uint16_t id, uint8_t *data, uint16_t length, uint32_t context)
{
    (void)context;
    if (!events) return 0;
    if (id == BLE_GAP_EXT_ADV_REPORT_IND) {
        if (length < offsetof(ble_gap_ext_adv_report_ind_t,data)) return 0;
        ble_gap_ext_adv_report_ind_t *p=(void *)data;
        if (p->length > length - offsetof(ble_gap_ext_adv_report_ind_t,data)) return 0;
        lab_ble_device_t d={0}; memcpy(d.address,p->addr.addr.addr,6); d.type=p->addr.addr_type;
        d.rssi=p->rssi; d.phy=p->phy_prim; d.info=p->info;
        lab_ble_name(p->data,p->length,d.name,sizeof(d.name));
        if (!enqueue(EVENT_SCAN,&d,sizeof(d))) lost_reports++;
        return 0;
    }
    switch (id) {
    case BLE_POWER_ON_IND: case CONNECTION_MANAGER_CONNCTED_IND: case BLE_GAP_DISCONNECTED_IND:
    case BLE_GAP_SCAN_START_CNF: case BLE_GAP_SCAN_STOPPED_IND: case BLE_GAP_CREATE_CONNECTION_CNF:
    case BLE_GAP_CANCEL_CREATE_CONNECTION_CNF: case SIBLES_MTU_EXCHANGE_IND:
    case BLE_GAP_UPDATE_CONN_PARAM_IND: case CONNECTION_MANAGER_BOND_AUTH_INFOR:
    case CONNECTION_MANAGER_PAIRING_SUCCEED: case CONNECTION_MANAGER_PAIRING_FAILED:
    case CONNECTION_MANAGER_ENCRYPT_IND_EVENT: case BLE_GAP_REMOTE_PHY_IND:
    case BLE_GAP_SET_PHY_CNF:
        if (id == CONNECTION_MANAGER_CONNCTED_IND || id == BLE_GAP_DISCONNECTED_IND) memset(callback_cccd,0,2);
        if (!enqueue(id,data,length)) queue_fault=true;
        break;
    default: break;
    }
    return 0;
}
BLE_EVENT_REGISTER(stack_event,0);

static void disconnect(uint8_t index)
{
    ble_gap_disconnect_t p={0}; p.conn_idx=index; p.reason=CO_ERROR_REMOTE_USER_TERM_CON;
    uint8_t ret=ble_gap_disconnect(&p); if (ret) error("Disconnect",ret);
}
/* 已连接且手机订阅 TX 后才发送通知；提交成功不等于对端回环成功。 */
static bool notify(const uint8_t *data, unsigned length)
{
    if (!state.connected || !state.subscribed) { message("Connect and enable TX notifications first"); return false; }
    sibles_value_t p={0}; p.hdl=service; p.idx=ATTR_TX; p.len=length; p.value=(uint8_t *)data;
    int ret=sibles_write_value(state.conn_index,&p);
    if (ret != (int)length) { error("Notify (retry if busy)",ret); return false; }
    state.tx_bytes+=length;
    return true;
}
/* 注册自定义 GATT 服务、读写回调和广播内容，并启用人工配对确认。 */
static bool init_service(void)
{
    BLE_GATT_SERVICE_INIT_128(svc,lab_attributes,ATTR_COUNT,
        BLE_GATT_SERVICE_PERM_NOAUTH | BLE_GATT_SERVICE_PERM_UUID_128,service_uuid);
    service=sibles_register_svc_128(&svc);
    if (!service) { message("GATT service registration failed"); return false; }
    sibles_register_cbk(service,read_value,write_value);
    bd_addr_t a;
    if (!ble_get_public_address(&a)) {
        address(state.address,a.addr);
        rt_snprintf(state.name,sizeof(state.name),"SiFli-Lab-%02X%02X%02X",a.addr[2],a.addr[1],a.addr[0]);
    } else strcpy(state.name,"SiFli-Lab");
    rt_kprintf("[lab:ble:identity] MAC=%s name=%s\n",state.address,state.name);
    sibles_advertising_para_t p={0};
    p.own_addr_type=GAPM_STATIC_ADDR; p.config.adv_mode=SIBLES_ADV_CONNECT_MODE;
    p.config.mode_config.conn_config.duration=0; p.config.mode_config.conn_config.interval=160;
    p.config.max_tx_pwr=0x7F; p.config.is_auto_restart=0;
    p.rsp_data.completed_name=rt_malloc(sizeof(sibles_adv_type_name_t)+strlen(state.name));
    if (!p.rsp_data.completed_name) { message("No RAM for advertising name"); return false; }
    p.rsp_data.completed_name->name_len=strlen(state.name);
    memcpy(p.rsp_data.completed_name->name,state.name,strlen(state.name));
    p.adv_data.completed_uuid=rt_malloc(sizeof(sibles_adv_type_srv_uuid_t)+sizeof(sibles_adv_uuid_t));
    if (!p.adv_data.completed_uuid) { rt_free(p.rsp_data.completed_name); message("No RAM for service advertisement"); return false; }
    p.adv_data.completed_uuid->count=1;
    p.adv_data.completed_uuid->uuid_list[0].uuid_len=16;
    memcpy(p.adv_data.completed_uuid->uuid_list[0].uuid.uuid_128,service_uuid,16);
    p.evt_handler=adv_event;
    unsigned ret=sibles_advertising_init(adv_context,&p);
    rt_free(p.rsp_data.completed_name);
    rt_free(p.adv_data.completed_uuid);
    adv_initialized=!ret;
    if (ret) { error("Advertising init",ret); return false; }
    connection_manager_set_bond_ack(BOND_PENDING);
    connection_manager_set_bond_cnf_iocap(GAP_IO_CAP_DISPLAY_YES_NO);
    return true;
}
/* 请求停止扫描、广播及连接；最终状态等待 SDK 完成事件确认。 */
static void stop_all(void)
{
    accepting=false; awaiting_echo=false; state.echo=false;
    if (state.pair_pending) connection_manager_bond_ack_reply(pair_conn,pair_request,false);
    state.pair_pending=false;
    if (state.scanning) ble_gap_scan_stop();
    if (state.advertising) sibles_advertising_stop(adv_context);
    if (state.connecting) ble_gap_cancel_create_connection();
    if (state.connected) disconnect(state.conn_index);
    state.stopping=true; stop_sent=true; stop_deadline=rt_tick_get()+rt_tick_from_millisecond(6000);
    message("Stopping radio activities...");
}
/* 在 BLE 任务中校验当前状态，再调用 SDK 执行用户请求。 */
static void command(unsigned cmd, unsigned index)
{
    if (fault_latched) { message("Critical BLE event lost. Restart board before testing."); return; }
    if (!state.ready) { message("Wait for Bluetooth stack ready"); return; }
    if (state.stopping || stop_requested) { message("Wait for radio cleanup"); return; }
    if (cmd == LAB_BLE_SCAN || cmd == LAB_BLE_ADVERTISE || cmd == LAB_BLE_CONNECT) {
        if (state.scanning || state.advertising || state.connected || state.connecting) { message("STOP current activity before starting another"); return; }
    }
    unsigned ret=0;
    switch (cmd) {
    case LAB_BLE_SCAN: {
        ble_gap_scan_start_t p={0}; p.own_addr_type=GAPM_STATIC_ADDR; p.type=GAPM_SCAN_TYPE_OBSERVER;
        p.prop=GAPM_SCAN_PROP_PHY_1M_BIT | GAPM_SCAN_PROP_ACTIVE_1M_BIT;
        p.scan_param_1m.scan_intv=160; p.scan_param_1m.scan_wd=80; p.duration=1000;
        ret=ble_gap_scan_start(&p);
        if (!ret) { state.scanning=true; state.device_count=0; memset(state.devices,0,sizeof(state.devices)); deadline=rt_tick_get()+rt_tick_from_millisecond(12000); message("Scanning for 10 seconds..."); }
        break;
    }
    case LAB_BLE_ADVERTISE:
        ret=adv_initialized ? sibles_advertising_start(adv_context) : 1;
        if (!ret) { accepting=true; state.advertising=true; deadline=rt_tick_get()+rt_tick_from_millisecond(60000); message("Starting advertising; connect within 60 s"); }
        break;
    case LAB_BLE_CONNECT: {
        if (index>=state.device_count) return;
        lab_ble_device_t *d=&state.devices[index];
        ble_gap_connection_create_param_t p={0}; p.own_addr_type=GAPM_STATIC_ADDR;
        p.type=GAPM_INIT_TYPE_DIRECT_CONN_EST; p.conn_to=1000;
        p.conn_param_1m.scan_intv=160; p.conn_param_1m.scan_wd=80;
        p.conn_param_1m.conn_intv_min=24; p.conn_param_1m.conn_intv_max=40;
        p.conn_param_1m.supervision_to=500; p.conn_param_1m.ce_len_max=48;
        memcpy(p.peer_addr.addr.addr,d->address,6); p.peer_addr.addr_type=d->type;
        ret=ble_gap_create_connection(&p);
        if (!ret) { accepting=true; state.connecting=true; deadline=rt_tick_get()+rt_tick_from_millisecond(12000); message("Connecting to selected device..."); }
        break;
    }
    case LAB_BLE_SEND:
        if (awaiting_echo) { message("Waiting for previous packet echo (10 s)"); break; }
        lab_ble_packet(expected,++sequence);
        if (notify(expected,sizeof(expected))) { awaiting_echo=true; echo_deadline=rt_tick_get()+rt_tick_from_millisecond(10000); message("12-byte challenge sent; write it back to RX"); }
        break;
    case LAB_BLE_ECHO: state.echo=!state.echo; message(state.echo ? "RX echo enabled (subscribe TX first)" : "RX echo disabled"); break;
    case LAB_BLE_PAIR:
        if (!state.connected) { message("Connect before pairing"); break; }
        connection_manager_create_bond(state.conn_index); message("Pairing requested; follow prompts on both devices"); break;
    case LAB_BLE_ACCEPT: case LAB_BLE_REJECT:
        if (state.pair_pending) { ret=connection_manager_bond_ack_reply(pair_conn,pair_request,cmd==LAB_BLE_ACCEPT); state.pair_pending=false; message(cmd==LAB_BLE_ACCEPT ? "Pairing accepted; waiting for result" : "Pairing rejected"); }
        break;
    case LAB_BLE_FORGET:
        if (state.connected || state.scanning || state.advertising || state.connecting) { message("STOP before forgetting the last peer"); break; }
        if (!have_peer) { message("Connect to the peer once, then STOP before forgetting"); break; }
        ret=connection_manager_delete_bond(last_peer); if (!ret) message("Last peer bond cleared; forget this board on phone too"); break;
    case LAB_BLE_PHY1: case LAB_BLE_PHY2: {
        if (!state.connected) { message("Connect before changing PHY"); break; }
        ble_gap_update_phy_t p={0}; p.conn_idx=state.conn_index;
        p.tx_phy=cmd==LAB_BLE_PHY1 ? GAP_PHY_LE_1MBPS : GAP_PHY_LE_2MBPS; p.rx_phy=p.tx_phy;
        ret=ble_gap_update_phy(&p); if (!ret) message("PHY requested; waiting for negotiated result"); break;
    }
    }
    if (ret) error("BLE command",ret);
}
/* 消费 SDK 事件和界面命令，推进连接、订阅、配对及回环状态。 */
static void handle(event_t *e)
{
    uint8_t *d=e->data.bytes;
    switch (e->id) {
    case EVENT_COMMAND:
        if (((unsigned *)d)[2]==generation) command(((unsigned *)d)[0],((unsigned *)d)[1]);
        break;
    case BLE_POWER_ON_IND:
        if (!state.ready) { state.ready=init_service(); if (state.ready) message("Ready. Choose Scan or Advertise."); }
        break;
    case EVENT_SCAN:
        if (state.scanning && !state.stopping) lab_ble_add_device(&state,(lab_ble_device_t *)d);
        break;
    case EVENT_ADV_ON:
        if (d[0]) { state.advertising=false; accepting=false; error("Advertise",d[0]); }
        else if (!state.stopping) message("Advertising: use a phone BLE app to connect");
        break;
    case EVENT_ADV_OFF: state.advertising=false; break;
    case BLE_GAP_SCAN_START_CNF:
        if (((ble_gap_start_scan_cnf_t *)d)->status) { state.scanning=false; error("Scan",((ble_gap_start_scan_cnf_t *)d)->status); }
        break;
    case BLE_GAP_SCAN_STOPPED_IND:
        state.scanning=false;
        if (!state.stopping) message(state.device_count ? "Scan complete. Tap a device to select it." : "No reports received; receive test not verified.");
        break;
    case BLE_GAP_CREATE_CONNECTION_CNF:
        if (((ble_gap_create_connection_cnf_t *)d)->status) { state.connecting=false; accepting=false; error("Connect",((ble_gap_create_connection_cnf_t *)d)->status); }
        break;
    case BLE_GAP_CANCEL_CREATE_CONNECTION_CNF:
        if (!((ble_gap_create_connection_cnf_t *)d)->status) state.connecting=false;
        break;
    case CONNECTION_MANAGER_CONNCTED_IND: {
        connection_manager_connect_ind_t *p=(void *)d;
        if (!accepting || stop_requested || state.stopping || state.connected) {
            rejected_connection=p->conn_idx;
            disconnect(p->conn_idx);
            stop_requested=true;
            break;
        }
        state.connected=true; state.connecting=false; state.advertising=false;
        last_peer.addr=p->peer_addr; last_peer.addr_type=p->peer_addr_type; have_peer=true;
        state.conn_index=p->conn_idx; state.interval=p->con_interval; state.mtu=23;
        state.rx_bytes=state.tx_bytes=state.loop_ok=0; state.subscribed=false;
        state.tx_phy=state.rx_phy=1; address(state.peer,p->peer_addr.addr);
        sibles_exchange_mtu(p->conn_idx);
        ble_gap_get_phy_t phy={0}; phy.conn_idx=p->conn_idx; ble_gap_get_remote_physical(&phy);
        message("Connected. Subscribe TX, write RX or request pairing.");
        lab_publish(LAB_BLE,LAB_PASS,"BLE connected","Controller reported a successful BLE link. Data test is separate.");
        break;
    }
    case BLE_GAP_DISCONNECTED_IND: {
        ble_gap_disconnected_ind_t *p=(void *)d;
        if (p->conn_idx == rejected_connection) rejected_connection=0xFF;
        if (state.connected && p->conn_idx == state.conn_index) {
            state.connected=false; state.subscribed=false; state.pair_pending=false;
            state.encrypted=false; state.bonded=false; accepting=false; awaiting_echo=false;
            char text[128]; rt_snprintf(text,sizeof(text),"Disconnected (reason 0x%02X). Restart advertising to reconnect.",p->reason); message(text);
        }
        break;
    }
    case SIBLES_MTU_EXCHANGE_IND: {
        sibles_mtu_exchange_ind_t *p=(void *)d;
        if (state.connected && p->conn_idx==state.conn_index) state.mtu=p->mtu;
        break;
    }
    case BLE_GAP_UPDATE_CONN_PARAM_IND: {
        ble_gap_update_conn_param_ind_t *p=(void *)d;
        if (state.connected && p->conn_idx==state.conn_index) state.interval=p->con_interval;
        break;
    }
    case BLE_GAP_REMOTE_PHY_IND: {
        ble_gap_remote_phy_ind_t *p=(void *)d;
        if (state.connected && p->conn_idx==state.conn_index) { state.tx_phy=p->tx_phy; state.rx_phy=p->rx_phy; message("PHY updated; actual TX/RX PHY shown below"); }
        break;
    }
    case BLE_GAP_SET_PHY_CNF:
        if (((ble_gap_set_phy_cnf_t *)d)->status) error("PHY",((ble_gap_set_phy_cnf_t *)d)->status);
        break;
    case EVENT_CCCD:
        if (state.connected && d[0]==state.conn_index) { state.subscribed=d[1]==1; message(state.subscribed ? "TX notifications enabled" : "TX notifications disabled"); }
        break;
    case EVENT_RX:
        if (state.connected && !state.stopping && d[0]==state.conn_index && e->length>1) {
            unsigned n=e->length-1; state.rx_bytes+=n;
            for (unsigned i=0;i<n;++i) rt_snprintf(state.last_rx+i*3,4,"%02X ",d[i+1]);
            message("RX received; latest bytes shown in hex");
            rt_kprintf("[lab:ble:rx] %s\n",state.last_rx);
            bool matched=awaiting_echo && lab_ble_packet_matches(d+1,n,expected);
            if (matched) { awaiting_echo=false; state.loop_ok++; message("PASS: challenge returned unchanged"); lab_publish(LAB_BLE,LAB_PASS,"BLE loopback verified","Remote returned the exact 12-byte challenge including sequence and checksum."); }
            else if (state.echo) notify(d+1,n);
        }
        break;
    case CONNECTION_MANAGER_BOND_AUTH_INFOR: {
        connection_manager_bond_ack_infor_t *p=(void *)d;
        if (!state.connected || state.stopping || p->conn_idx!=state.conn_index) { connection_manager_bond_ack_reply(p->conn_idx,p->request,false); break; }
        if (p->request==GAPC_TK_EXCH && p->type==GAP_TK_KEY_ENTRY) {
            connection_manager_bond_ack_reply(p->conn_idx,p->request,false); message("Passkey entry unsupported; initiate pairing from phone"); break;
        }
        pair_request=p->request; pair_conn=p->conn_idx; state.passkey=p->confirm_data;
        state.pair_pending=true; pair_deadline=rt_tick_get()+rt_tick_from_millisecond(25000);
        if (p->request==GAPC_NC_EXCH) message("Compare 6-digit code on both devices, then Accept");
        else if (p->request==GAPC_TK_EXCH) message("Enter shown code on phone, then Accept here");
        else { state.passkey=0; message("Peer requests pairing. Accept or Reject below."); }
        break;
    }
    case CONNECTION_MANAGER_PAIRING_SUCCEED: state.pair_pending=false; message("Pairing completed; encryption/bond state shown below"); break;
    case CONNECTION_MANAGER_PAIRING_FAILED: state.pair_pending=false; message("Pairing failed or rejected. Retry on both devices."); break;
    default: break;
    }
}
/* 唯一一次启动共享蓝牙栈，循环处理事件、超时和停止，并发布状态快照。 */
static void task(void *unused)
{
    (void)unused; message("Starting Bluetooth stack...");
    sifli_ble_enable();
    rt_tick_t startup_deadline=rt_tick_get()+rt_tick_from_millisecond(15000);
    event_t e;
    for (;;) {
        if (rt_mq_recv(events,&e,sizeof(e),rt_tick_from_millisecond(50))==RT_EOK) handle(&e);
        if (queue_fault) { queue_fault=false; fault_latched=true; lab_ble_stop(); message("Event queue overflow; stopping test. Restart board if cleanup fails."); }
        if (stop_requested && !stop_sent) stop_all();
        if (state.stopping) {
            if (!state.scanning && !state.advertising && !state.connecting && !state.connected && rejected_connection==0xFF) {
                state.stopping=false; stop_requested=false; stop_sent=false; message(fault_latched ? "Critical event lost. Restart board before another test." : "Stopped. Radio activities closed.");
            } else if (elapsed(stop_deadline)) {
                message("Radio cleanup timeout. Restart board before another test.");
                stop_deadline=rt_tick_get()+rt_tick_from_millisecond(60000);
            }
        } else if ((state.scanning || state.advertising || state.connecting) && elapsed(deadline)) {
            lab_ble_stop();
        }
        if (state.pair_pending && elapsed(pair_deadline)) {
            connection_manager_bond_ack_reply(pair_conn,pair_request,false); state.pair_pending=false; message("Pairing prompt timed out");
        }
        if (awaiting_echo && elapsed(echo_deadline)) { awaiting_echo=false; message("No matching echo in 10 s; TX delivery not verified"); }
        if (state.connected) {
            state.encrypted=connection_manager_get_enc_state(state.conn_index)==ENC_STATE_ON;
            state.bonded=connection_manager_get_bond_state(state.conn_index)==BOND_STATE_BONDED;
        }
        if (!state.ready && elapsed(startup_deadline)) { message("Bluetooth not ready. Check serial initialization log."); startup_deadline=rt_tick_get()+rt_tick_from_millisecond(60000); }
        state.dropped=lost_reports; state.revision++;
        rt_mutex_take(&snapshot_lock,RT_WAITING_FOREVER); published=state; rt_mutex_release(&snapshot_lock);
    }
}
/* 创建事件队列与专用任务；协议服务在栈就绪事件后注册。 */
int lab_ble_init(void)
{
    if (initialized) return RT_EOK;
    if (rt_mutex_init(&snapshot_lock,"lab_ble",RT_IPC_FLAG_PRIO)!=RT_EOK) return -RT_ERROR;
    events=rt_mq_create("ble_evt",sizeof(event_t),32,RT_IPC_FLAG_FIFO);
    if (!events) return -RT_ENOMEM;
    rt_thread_t thread=rt_thread_create("lab_ble",task,NULL,4096,21,10);
    if (!thread) { rt_mq_delete(events); events=NULL; return -RT_ENOMEM; }
    initialized=true;
    return rt_thread_startup(thread);
}
