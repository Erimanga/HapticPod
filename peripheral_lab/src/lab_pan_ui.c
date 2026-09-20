/*
 * lab_pan_ui.c — 蓝牙联网界面：展示配对与逐层联网结果，将操作交给 PAN 服务任务执行。
 */
#include "lab.h"
#include "lab_pan.h"
#include "lab_ui.h"
static lv_obj_t *status, *notice, *accept, *reject, *mqtt_status, *identity_label;
static lv_timer_t *timer;
/* 将界面按钮映射为 PAN 命令，网络请求在后台推进。 */
static void click(lv_event_t *e)
{
    unsigned c=(uintptr_t)lv_event_get_user_data(e);
    if(c==100) lab_pan_stop();
    else if(!lab_pan_command(c)) lv_label_set_text(notice,"Command queue busy; retry shortly");
}
static lv_obj_t *label(lv_obj_t *p,const char *s,unsigned color)
{
    lv_obj_t *o=lv_label_create(p); lv_obj_set_width(o,LV_PCT(100)); lv_label_set_text(o,s);
    lv_obj_set_style_text_font(o,&lv_font_montserrat_14,0);
    lv_obj_set_style_text_color(o,lv_color_hex(color),0); return o;
}
static lv_obj_t *button(lv_obj_t *p,const char *s,unsigned c)
{
    lv_obj_t *b=lv_button_create(p); lv_obj_set_size(b,(lv_display_get_horizontal_resolution(NULL)-54)/2,42);
    lv_obj_set_style_bg_color(b,lv_color_hex(0x22364B),0); lv_obj_set_style_radius(b,12,0); lv_obj_set_style_shadow_width(b,0,0);
    lv_obj_t *l=lv_label_create(b); lv_label_set_text(l,s); lv_obj_center(l); lv_obj_set_style_text_font(l,&lv_font_montserrat_14,0);
    lv_obj_add_event_cb(b,click,LV_EVENT_CLICKED,(void *)(uintptr_t)c); return b;
}
/* 显示蓝牙链路、地址和 HTTP/MQTT 各阶段结果。 */
static void refresh(lv_timer_t *t)
{
    (void)t; lab_pan_snapshot_t s; lab_pan_snapshot(&s);
    lv_label_set_text_fmt(identity_label,"%s\nMAC: %s",s.name[0] ? s.name : "Bluetooth name pending",s.address[0] ? s.address : "--");
    lv_label_set_text_fmt(status,"%s  /  %s\nSCAN %u / BT %s / HID %s\nPAN %s / IP %s\nDNS %s   TCP %s   HTTP %s\nIP: %s\nGateway: %s\nDNS server: %s",s.ready ? "STACK READY" : "STARTING",s.enabled ? "ACTIVE" : "IDLE",s.scan_mode,s.acl ? "OK" : "--",s.hid ? "OK" : "--",s.pan ? "OK" : "--",s.ip_ok ? "OK" : "--",s.dns_ok ? "OK" : "--",s.tcp_ok ? "OK" : "--",s.http_ok ? "OK" : "--",s.ip[0] ? s.ip : "--",s.gateway[0] ? s.gateway : "--",s.dns[0] ? s.dns : "--");
    lv_label_set_text_fmt(mqtt_status,"MQTT / test.mosquitto.org:1883\nCONNECT %s / SUB %s / TX %s\nExact echo: %s",s.mqtt_connected ? "OK" : "--",s.mqtt_subscribed ? "OK" : "--",s.mqtt_published ? "OK" : "--",s.mqtt_ok ? "PASS" : "--");
    if(s.pairing) lv_label_set_text_fmt(notice,"PAIR CODE %06u\nCompare with phone; Accept or Reject",s.code);
    else lv_label_set_text(notice,s.message);
    if(s.pairing) { lv_obj_remove_state(accept,LV_STATE_DISABLED); lv_obj_remove_state(reject,LV_STATE_DISABLED); }
    else { lv_obj_add_state(accept,LV_STATE_DISABLED); lv_obj_add_state(reject,LV_STATE_DISABLED); }
}
/* 离页时取消定时刷新，并请求断开本次联网测试。 */
static void closed(void) { if(timer) { lv_timer_delete(timer); timer=NULL; } lab_pan_stop(); }
/* 创建联网操作与结果区，定时读取 PAN 服务快照。 */
void lab_pan_open(void)
{
    lv_obj_t *root=lab_overlay("BLUETOOTH / INTERNET",closed);
    int w=lv_display_get_horizontal_resolution(NULL), h=lv_display_get_vertical_resolution(NULL);
    lv_obj_t *body=lv_obj_create(root); lv_obj_remove_style_all(body); lv_obj_set_pos(body,22,58); lv_obj_set_size(body,w-44,h-188);
    lv_obj_set_flex_flow(body,LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(body,12,0); lv_obj_set_scroll_dir(body,LV_DIR_VER);
    identity_label=label(body,"",0xFFFFFF);
    status=label(body,"",0x86F5CA);
    lv_obj_t *grid=lv_obj_create(body); lv_obj_remove_style_all(grid); lv_obj_set_size(grid,LV_PCT(100),LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid,LV_FLEX_FLOW_ROW_WRAP); lv_obj_set_style_pad_gap(grid,10,0); lv_obj_remove_flag(grid,LV_OBJ_FLAG_SCROLLABLE);
    button(grid,"Begin pairing",PAN_BEGIN); button(grid,"Connect PAN",PAN_RECONNECT);
    button(grid,"Test Internet",PAN_TEST); button(grid,"Test MQTT",PAN_MQTT); accept=button(grid,"Accept code",PAN_ACCEPT); reject=button(grid,"Reject code",PAN_REJECT);
    mqtt_status=label(body,"",0x86F5CA);
    label(body,"MQTT TEST\nAnonymous public broker / plaintext. Sends a synthetic probe on a per-run topic. CONNECT -> SUBACK -> publish QoS 0 -> exact echo -> disconnect. Topic and payload are printed to serial. No TLS, credentials or retained messages. HTTP and MQTT tests run one at a time.\n\nPHONE SETUP\n1. Enable Android Bluetooth tethering or iPhone Personal Hotspot (Allow Others to Join).\n2. Begin pairing; find the name shown above in phone Bluetooth Settings.\n3. Compare and accept the code on both devices.\n4. Wait for IP, then Test Internet.\n\nAPI\nbt_interface_conn_ext(..., BT_PROFILE_PAN)\nDHCP -> DNS -> TCP -> HTTP HEAD\n\nTarget: example.com:80\nHTTP 200/204 passes. No TLS test. iPhone: forget the old pairing before retrying. HID is a companion profile; automatic mouse/key input is suppressed.  STOP keeps bonds; pair again from the phone after Begin.",0x97A8C0);
    notice=label(root,"",0x80CCFF); lv_obj_set_pos(notice,22,h-120); lv_obj_set_size(notice,w-44,48); lv_label_set_long_mode(notice,LV_LABEL_LONG_DOT);
    lv_obj_t *stop=button(root,LV_SYMBOL_STOP " STOP PAN",100); lv_obj_set_pos(stop,22,h-64); lv_obj_set_style_bg_color(stop,lv_color_hex(0xE84459),0);
    lv_obj_t *back=lv_obj_get_child(root,1); lv_obj_set_size(back,(w-54)/2,42); lv_obj_align(back,LV_ALIGN_BOTTOM_RIGHT,-22,-22);
    refresh(NULL); timer=lv_timer_create(refresh,250,NULL);
}
