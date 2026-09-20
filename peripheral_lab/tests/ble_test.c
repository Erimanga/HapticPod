#include "lab_ble.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void)
{
    char name[5];
    const uint8_t ad[]={2,1,6,7,9,'P','h','o','n','e','!'};
    lab_ble_name(ad,sizeof(ad),name,sizeof(name)); assert(!strcmp(name,"Phon"));
    lab_ble_name(ad,7,name,sizeof(name)); assert(!name[0]);
    const uint8_t short_ad[]={2,8,'A',2,9,'B'};
    lab_ble_name(short_ad,sizeof(short_ad),name,sizeof(name)); assert(!strcmp(name,"B"));
    lab_ble_snapshot_t state={0}; lab_ble_device_t d={.name="Phone",.rssi=-60};
    assert(lab_ble_add_device(&state,&d));
    d.rssi=-45; d.name[0]=0; assert(lab_ble_add_device(&state,&d));
    assert(state.device_count==1 && state.devices[0].reports==2 && !strcmp(state.devices[0].name,"Phone"));
    assert(state.devices[0].rssi==-45);
    d.type=1; assert(lab_ble_add_device(&state,&d)); assert(state.device_count==2);
    for (unsigned i=2;i<LAB_BLE_DEVICES;++i) { d.address[0]=i; assert(lab_ble_add_device(&state,&d)); }
    d.address[0]=99; assert(!lab_ble_add_device(&state,&d));
    assert(state.device_count==LAB_BLE_DEVICES);
    uint8_t a[12], b[12]; lab_ble_packet(a,7); lab_ble_packet(b,7);
    assert(lab_ble_packet_matches(a,12,b));
    assert(!lab_ble_packet_matches(a,11,b));
    lab_ble_packet(b,8); assert(!lab_ble_packet_matches(a,12,b));
    memcpy(b,a,12); b[11]^=1; assert(!lab_ble_packet_matches(a,12,b));
    puts("PASS: BLE AD length checks, name bounds, address/type dedup, list cap, challenge sequence and corruption rejection.");
}
