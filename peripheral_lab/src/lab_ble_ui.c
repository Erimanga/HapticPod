/*
 * lab_ble_ui.c — BLE 测试界面：提交扫描、连接和配对命令，通过快照展示异步执行结果。
 */
#include "lab.h"
#include "lab_ble.h"
#include "lab_ui.h"
#include <string.h>
#define BG 0x0C1220
#define PANEL 0x172235
#define GREEN 0x86F5CA
static lv_obj_t *body, *info, *notice, *rows[LAB_BLE_DEVICES], *row_text[LAB_BLE_DEVICES];
static lv_timer_t *timer;
static lab_ble_snapshot_t view;
static unsigned page, selected_device;
static lv_obj_t *tab_buttons[4];
static bool last_pair, confirm_forget;
static void show_page(unsigned which);
static lv_obj_t *text(lv_obj_t *parent, const char *s, unsigned color)
{
    lv_obj_t *o=lv_label_create(parent);
    lv_obj_set_width(o,LV_PCT(100)); lv_label_set_text(o,s);
    lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    lv_obj_set_style_text_font(o,&lv_font_montserrat_14,0);
    return o;
}
/* 将按钮操作转换为 BLE 命令，STOP 使用独立停止入口。 */
static void click(lv_event_t *e)
{
    unsigned cmd=(uintptr_t)lv_event_get_user_data(e);
    if (cmd==100) { lab_ble_stop(); return; }
    if (cmd==LAB_BLE_FORGET && !confirm_forget) {
        confirm_forget=true; lv_label_set_text(notice,"Tap Forget peer again to delete the last peer bond"); return;
    }
    confirm_forget=false;
    if (!lab_ble_command(cmd,selected_device)) lv_label_set_text(notice,"Command queue busy; retry shortly");
}
static lv_obj_t *button(lv_obj_t *parent,const char *name,unsigned cmd,int width)
{
    lv_obj_t *b=lv_button_create(parent); lv_obj_set_size(b,width,44);
    lv_obj_set_style_bg_color(b,lv_color_hex(PANEL),0); lv_obj_set_style_radius(b,12,0);
    lv_obj_set_style_shadow_width(b,0,0);
    lv_obj_t *l=lv_label_create(b); lv_label_set_text(l,name); lv_obj_center(l);
    lv_obj_set_style_text_font(l,&lv_font_montserrat_14,0);
    lv_obj_add_event_cb(b,click,LV_EVENT_CLICKED,(void *)(uintptr_t)cmd); return b;
}
static lv_obj_t *grid(lv_obj_t *parent)
{
    lv_obj_t *g=lv_obj_create(parent); lv_obj_remove_style_all(g);
    lv_obj_set_size(g,LV_PCT(100),LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(g,LV_FLEX_FLOW_ROW_WRAP); lv_obj_set_style_pad_gap(g,10,0);
    lv_obj_remove_flag(g,LV_OBJ_FLAG_SCROLLABLE); return g;
}
static void choose(lv_event_t *e)
{
    selected_device=(uintptr_t)lv_event_get_user_data(e);
    for (unsigned i=0;i<LAB_BLE_DEVICES;++i) {
        lv_obj_set_style_border_width(rows[i],i==selected_device ? 2 : 0,0);
        lv_obj_set_style_border_color(rows[i],lv_color_hex(GREEN),0);
    }
}
/* 按所选标签页建立扫描、数据或配对控件。 */
static void show_page(unsigned which)
{
    page=which; confirm_forget=false; lv_obj_clean(body);
    for (unsigned i=0;i<4;++i) {
        lv_obj_set_style_bg_color(tab_buttons[i],lv_color_hex(i==which ? GREEN : PANEL),0);
        lv_obj_set_style_text_color(tab_buttons[i],lv_color_hex(i==which ? BG : 0xEDF4FC),0);
    }
    info=text(body,"",0xEDF4FC);
    lv_obj_t *g=grid(body); int w=(lv_display_get_horizontal_resolution(NULL)-54)/2;
    if (page==0) {
        button(g,"Scan 10 s",LAB_BLE_SCAN,w); button(g,"Advertise",LAB_BLE_ADVERTISE,w);
        button(g,"Use 1M PHY",LAB_BLE_PHY1,w); button(g,"Use 2M PHY",LAB_BLE_PHY2,w);
        text(body,"LEARN THE API\nsifli_ble_enable()\nBLE_POWER_ON_IND\nsibles_advertising_start()\nConnect using a phone BLE app, not the system audio pairing page. Advertising stops after 60 s without a connection.",0x97A8C0);
    } else if (page==1) {
        button(g,"Scan 10 s",LAB_BLE_SCAN,w); button(g,"Connect selected",LAB_BLE_CONNECT,w);
        text(body,"Select a device after scanning ends. Connect only to your test device. This page tests GAP links; remote GATT browsing is not included.",0x97A8C0);
        for (unsigned i=0;i<LAB_BLE_DEVICES;++i) {
            rows[i]=button(body,"",0,lv_display_get_horizontal_resolution(NULL)-44);
            lv_obj_set_height(rows[i],66); lv_obj_remove_event_cb(rows[i],click);
            lv_obj_add_event_cb(rows[i],choose,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
            row_text[i]=lv_obj_get_child(rows[i],0); lv_obj_set_width(row_text[i],LV_PCT(100));
            lv_obj_set_style_text_font(row_text[i],&lv_font_montserrat_12,0);
            lv_obj_add_flag(rows[i],LV_OBJ_FLAG_HIDDEN);
        }
    } else if (page==2) {
        button(g,"Send challenge",LAB_BLE_SEND,w); button(g,"Toggle RX echo",LAB_BLE_ECHO,w);
        text(body,"PHONE SETUP\nConnect, then enable TX notifications. Write 1-20 bytes to RX. To verify round trip, return the exact challenge bytes to RX within 10 s.\n\nSERVICE UUID\n" LAB_BLE_SERVICE_UUID "\nRX: 7bc10001-...\nTX: 7bc10002-...\nStatus (read): 7bc10003-...\n\nLEARN THE API\nsibles_register_svc_128()\nsibles_write_value()\nTX counts accepted bytes, not confirmed delivery.",0x97A8C0);
    } else {
        button(g,"Pair",LAB_BLE_PAIR,w); button(g,"Forget peer",LAB_BLE_FORGET,w);
        button(g,"Accept",LAB_BLE_ACCEPT,w); button(g,"Reject",LAB_BLE_REJECT,w);
        text(body,"Compare the code on both devices before accepting. Prompts expire after 25 s. STOP disconnects but keeps bonds. Forget peer deletes only the last connected peer after a second tap while idle; also forget this board on your phone.\n\nLEARN THE API\nconnection_manager_create_bond()\nconnection_manager_bond_ack_reply()\nconnection_manager_get_enc_state()",0x97A8C0);
    }
}
static void tab(lv_event_t *e) { show_page((uintptr_t)lv_event_get_user_data(e)); }
/* 读取协议快照，刷新连接信息、扫描列表与配对提示。 */
static void refresh_ble(lv_timer_t *t)
{
    (void)t; lab_ble_snapshot(&view);
    if (view.pair_pending && !last_pair) show_page(3);
    last_pair=view.pair_pending;
    if (!confirm_forget) lv_label_set_text(notice,view.message);
    char s[400];
    if (page==0) rt_snprintf(s,sizeof(s),"%s\n%s\n%s | %s\nMTU %u / interval %u.%02u ms\nPHY TX %u / RX %u",view.name,view.address,
        view.ready ? "Stack ready" : "Starting",view.connected ? "Connected" : view.advertising ? "Advertising" : "Idle",
        view.mtu,view.interval*125/100,view.interval*125%100,view.tx_phy,view.rx_phy);
    else if (page==1) {
        rt_snprintf(s,sizeof(s),"%s / %u devices\nSelected: %u | dropped reports: %u",view.scanning ? "Scanning" : "Scan idle",view.device_count,selected_device+1,view.dropped);
        for (unsigned i=0;i<LAB_BLE_DEVICES;++i) {
            if (i>=view.device_count) { lv_obj_add_flag(rows[i],LV_OBJ_FLAG_HIDDEN); continue; }
            lab_ble_device_t *d=&view.devices[i]; lv_obj_remove_flag(rows[i],LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text_fmt(row_text[i],"%u. %s  %d dBm\n%02X:%02X:%02X:%02X:%02X:%02X  type %u / PHY %u",i+1,d->name[0] ? d->name : "(no name)",d->rssi,d->address[5],d->address[4],d->address[3],d->address[2],d->address[1],d->address[0],d->type,d->phy);
        }
    } else if (page==2) rt_snprintf(s,sizeof(s),"%s / notify %s / echo %s\nTX %u B  RX %u B\nVerified round trips: %u\nRX: %s",view.connected ? view.peer : "Not connected",view.subscribed ? "ON" : "OFF",view.echo ? "ON" : "OFF",view.tx_bytes,view.rx_bytes,view.loop_ok,view.last_rx);
    else rt_snprintf(s,sizeof(s),"%s\nEncrypted: %s / Bonded: %s\n%s\nCode: %06u",view.connected ? view.peer : "Not connected",view.encrypted ? "YES" : "NO",view.bonded ? "YES" : "NO",view.pair_pending ? "CONFIRM ON BOTH DEVICES" : "No pending pairing request",view.passkey);
    lv_label_set_text(info,s);
}
/* 删除刷新定时器并停止本页无线测试，保留已保存的绑定。 */
static void closed(void)
{
    if (timer) { lv_timer_delete(timer); timer=NULL; }
    lab_ble_stop();
}
/* 创建 BLE 测试层，启动定时快照刷新。 */
void lab_ble_open(void)
{
    last_pair=false; selected_device=0;
    lv_obj_t *root=lab_overlay("BLUETOOTH / BLE",closed);
    lv_obj_t *tabs=grid(root); lv_obj_set_width(tabs,lv_display_get_horizontal_resolution(NULL)-44);
    lv_obj_set_pos(tabs,22,58); lv_obj_set_style_pad_gap(tabs,6,0);
    const char *names[]={"Status","Scan","Data","Pair"};
    for (unsigned i=0;i<4;++i) {
        lv_obj_t *b=tab_buttons[i]=button(tabs,names[i],0,(lv_display_get_horizontal_resolution(NULL)-62)/4);
        lv_obj_set_height(b,36); lv_obj_remove_event_cb(b,click); lv_obj_add_event_cb(b,tab,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
    }
    body=lv_obj_create(root); lv_obj_remove_style_all(body);
    lv_obj_set_pos(body,22,106); lv_obj_set_size(body,lv_display_get_horizontal_resolution(NULL)-44,lv_display_get_vertical_resolution(NULL)-236);
    lv_obj_set_flex_flow(body,LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(body,12,0);
    lv_obj_set_scroll_dir(body,LV_DIR_VER); lv_obj_set_scrollbar_mode(body,LV_SCROLLBAR_MODE_AUTO);
    notice=text(root,"",0x80CCFF); lv_obj_set_width(notice,lv_display_get_horizontal_resolution(NULL)-44);
    lv_obj_set_pos(notice,22,lv_display_get_vertical_resolution(NULL)-120);
    lv_obj_set_height(notice,44); lv_label_set_long_mode(notice,LV_LABEL_LONG_DOT);
    lv_obj_t *stop=button(root,LV_SYMBOL_STOP " STOP BLE",100,(lv_display_get_horizontal_resolution(NULL)-54)/2);
    lv_obj_set_style_bg_color(stop,lv_color_hex(0xE84459),0);
    lv_obj_set_pos(stop,22,lv_display_get_vertical_resolution(NULL)-64);
    lv_obj_t *back=lv_obj_get_child(root,1);
    lv_obj_set_size(back,(lv_display_get_horizontal_resolution(NULL)-54)/2,44);
    lv_obj_align(back,LV_ALIGN_BOTTOM_RIGHT,-22,-20);
    show_page(0); refresh_ble(NULL); timer=lv_timer_create(refresh_ble,250,NULL);
}
