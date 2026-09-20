#pragma once
/* Host transport double; firmware compilation checks the actual SDK ABI. */
#include <stdint.h>
enum { BT_NOTIFY_COMMON=1,BT_NOTIFY_PAN,BT_NOTIFY_COMMON_BT_STACK_READY,
 BT_NOTIFY_COMMON_IO_CAPABILITY_IND,BT_NOTIFY_COMMON_USER_CONFIRM_IND,
 BT_NOTIFY_COMMON_ENCRYPTION,BT_NOTIFY_COMMON_ACL_CONNECTED,BT_NOTIFY_COMMON_ACL_DISCONNECTED,
 BT_NOTIFY_PAN_PROFILE_CONNECTED,BT_NOTIFY_PAN_PROFILE_DISCONNECTED, BT_NOTIFY_HID, BT_NOTIFY_HID_PROFILE_CONNECTED, BT_NOTIFY_HID_PROFILE_DISCONNECTED, BT_NOTIFY_COMMON_PAIR_IND, BT_NOTIFY_COMMON_SCAN_ENB_CFM_IND };
enum { IO_CAPABILITY_DISPLAY_YES_NO,IO_CAPABILITY_REJECT_REQ,BT_PROFILE_PAN,BT_PROFILE_HID };
typedef struct { uint8_t addr[6]; } bt_notify_device_mac_t;
typedef struct { bt_notify_device_mac_t mac; uint32_t num_val; } bt_notify_pair_confirm_t;
typedef struct { bt_notify_device_mac_t mac; uint16_t handle; uint8_t res,acl_dir; uint32_t dev_cls; void *acl_info; } bt_notify_device_acl_conn_info_t;
typedef struct { bt_notify_device_mac_t mac; uint8_t profile_type,profile_role; uint16_t profile_channel; uint8_t res; } bt_notify_profile_state_info_t;
int bt_interface_user_confirm_res(uint8_t *,unsigned);
int bt_interface_io_req_res(uint8_t *,unsigned,unsigned,unsigned);
int bt_interface_set_scan_mode(unsigned,unsigned);
int bt_interface_cancel_connect_req(uint8_t *);
int bt_interface_disc_ext(uint8_t *,unsigned);
int bt_interface_disconnect_req(uint8_t *);
int bt_interface_conn_ext(uint8_t *,unsigned);
void bt_interface_set_local_name(unsigned,void *);
int bt_interface_register_bt_event_notify_callback(int (*)(uint16_t,uint16_t,uint8_t *,uint16_t));

typedef struct { bt_notify_device_mac_t mac; uint16_t handle; uint8_t res; } bt_notify_device_base_info_t;
typedef uint8_t U8;
typedef uint16_t U16;
typedef unsigned BOOL;
typedef struct { unsigned nap,uap,lap; } BTS2S_BD_ADDR;
#define BT_SRVCLS_NETWORK 0x020000
#define BT_DEVCLS_LAP 0x0300
#define BT_LAP_FULLY 0
