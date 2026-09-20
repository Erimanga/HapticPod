/* Host-only backend: never accesses a board. Tests use production UI and LVGL. */
#include "lab.h"
#include "lab_game.h"
#include "lab_game_sound.h"
#include "lab_ui.h"
#include "lab_ble.h"
#include "lab_pan.h"
#include <assert.h>
#include "src/draw/lv_draw_buf_private.h"
#include "src/core/lv_obj_draw_private.h"
#include <string.h>

/* Fail immediately instead of allowing LVGL's retry loop to hide a regression.
 * This guards pixel buffers, independently of the desktop's available heap. */
static bool forbid_pixel_buffers;
static lv_draw_buf_malloc_cb_t original_draw_malloc;
static unsigned game_buffer_attempts;
static void *checked_draw_malloc(size_t size, lv_color_format_t format)
{
    if (forbid_pixel_buffers) {
        game_buffer_attempts++;
        fprintf(stderr, "Unexpected game pixel buffer: %zu bytes\n", size);
        abort();
    }
    return original_draw_malloc(size, format);
}
static void assert_no_layers(lv_obj_t *o)
{
    assert(lv_obj_get_layer_type(o)==LV_LAYER_TYPE_NONE);
    for (unsigned i=0; i<lv_obj_get_child_count(o); ++i) assert_no_layers(lv_obj_get_child(o,i));
}
static lab_ble_snapshot_t ble_view = {.ready=true, .mtu=23, .tx_phy=1, .rx_phy=1, .name="SiFli-Lab-1234", .address="12:34:56:78:90:AB", .message="Ready. Choose Scan or Advertise."};
static unsigned ble_cmd, ble_stops;
void lab_ble_snapshot(lab_ble_snapshot_t *s) { *s=ble_view; }
bool lab_ble_command(lab_ble_command_t cmd, unsigned index) { (void)index; ble_cmd=cmd; return true; }
void lab_ble_stop(void) { ble_stops++; }
static lab_pan_snapshot_t pan_view={.ready=true,.name="SiFli-Lab-A1B2C3",.address="11:22:33:A1:B2:C3",.message="Enable Android Bluetooth tethering first"};
static unsigned pan_cmd,pan_stops;
void lab_pan_snapshot(lab_pan_snapshot_t *s) { *s=pan_view; }
bool lab_pan_command(lab_pan_command_t c) { pan_cmd=c; return true; }
void lab_pan_stop(void) { pan_stops++; }
static lab_result_t results[LAB_COUNT];
static bool busy, continuous, stopped;
static unsigned sound_events[GAME_SOUND_LOSE+1], sound_quiets;
static lab_motion_t motion = {.a={0,0,1}, .valid=true};
static bool motion_frozen;
void lab_motion_snapshot(lab_motion_t *s) { if (!motion_frozen) motion.sequence++; *s=motion; }
void lab_motion_publish(const lab_motion_t *s) { motion=*s; }
static unsigned speaker_volume = 6, speaker_frequency = 440;
void lab_speaker_settings(unsigned *v, unsigned *f) { *v = speaker_volume; *f = speaker_frequency; }
void lab_speaker_configure(unsigned v, unsigned f) { if (!busy) { speaker_volume = v; speaker_frequency = f; } }
static lab_touch_frame_t raw_frame;
bool lab_continuous(void) { return busy && continuous; }
void lab_stop(void) { stopped = true; busy = continuous = false; }
bool lab_cancelled(void) { return stopped; }
void lab_touch_frame_publish(const lab_touch_frame_t *f) { raw_frame = *f; }
void lab_touch_frame_snapshot(lab_touch_frame_t *f) { *f = raw_frame; }
static uint8_t framebuffer[390 * 450 * 4];
static _Alignas(64) uint8_t drawbuffer[390 * 450 * 4];
void lab_snapshot(lab_id_t id, lab_result_t *r) { *r = results[id]; }
bool lab_busy(void) { return busy; }
void lab_publish(lab_id_t id, lab_status_t state, const char *value, const char *detail)
{
    results[id].status = state;
    results[id].revision++;
    snprintf(results[id].value, sizeof(results[id].value), "%s", value);
    snprintf(results[id].detail, sizeof(results[id].detail), "%s", detail);
}
bool lab_start(lab_id_t id, unsigned action)
{
    if (busy) return false;
    busy = true;
    stopped = false;
    continuous = action == 1 && (id == LAB_ADC || id == LAB_LIGHT || id == LAB_IMU || id == LAB_MAG);
    lab_publish(id, LAB_RUNNING, "Working...", "Simulated I/O for host UI verification.");
    return true;
}
void lab_confirm(lab_id_t id, bool pass)
{
    if (!busy && results[id].status == LAB_OBSERVE)
        lab_publish(id, pass ? LAB_PASS : LAB_FAIL, pass ? "Confirmed" : "Needs attention", "Simulated confirmation.");
}
#define STUB(name) void lab_##name##_run(unsigned a) { (void)a; }
STUB(rgb) STUB(gpio) STUB(key) STUB(adc) STUB(uart) STUB(charger)
STUB(game) STUB(imu) STUB(light) STUB(mag) STUB(rtc) STUB(touch) STUB(mic) STUB(speaker)

static void flush(lv_display_t *display, const lv_area_t *a, uint8_t *pixels)
{
    for (int y = a->y1; y <= a->y2; ++y) {
        memcpy(framebuffer + (y * 390 + a->x1) * 4, pixels, (a->x2 - a->x1 + 1) * 4);
        pixels += (a->x2 - a->x1 + 1) * 4;
    }
    lv_display_flush_ready(display);
}
static void settle(void)
{
    for (int i = 0; i < 30; ++i) { lv_tick_inc(20); lv_timer_handler(); }
    lv_refr_now(NULL);
}
static void screenshot(const char *name)
{
    settle();
    FILE *f = fopen(name, "wb");
    assert(f);
    fprintf(f, "P6\n390 450\n255\n");
    for (unsigned i = 0; i < sizeof(framebuffer); i += 4) {
        fputc(framebuffer[i+2], f); fputc(framebuffer[i+1], f); fputc(framebuffer[i], f);
    }
    fclose(f);
}
static lv_obj_t *find_text(lv_obj_t *root, const char *text)
{
    if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), text)) return root;
    for (unsigned i = 0; i < lv_obj_get_child_count(root); ++i) {
        lv_obj_t *found = find_text(lv_obj_get_child(root, i), text);
        if (found) return found;
    }
    return NULL;
}
static lv_obj_t *find_size(lv_obj_t *root, int w, int h)
{
    if (lv_obj_get_width(root)==w && lv_obj_get_height(root)==h) return root;
    for (unsigned i=0; i<lv_obj_get_child_count(root); ++i) {
        lv_obj_t *o=find_size(lv_obj_get_child(root,i),w,h); if (o) return o;
    }
    return NULL;
}
static lv_obj_t *click_text(lv_obj_t *root, const char *text)
{
    lv_obj_t *l = find_text(root, text);
    assert(l);
    lv_obj_t *button = lv_obj_get_parent(l);
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
    settle();
    return button;
}
static lv_indev_data_t pointer_data;
static lv_indev_t *pointer;
static void read_pointer(lv_indev_t *input, lv_indev_data_t *data)
{
    (void)input;
    *data = pointer_data;
}
static void swipe(int x, int y, int dx, int dy, unsigned duration)
{
    settle();
    pointer_data.state = LV_INDEV_STATE_PRESSED;
    pointer_data.point.x = x; pointer_data.point.y = y;
    lv_indev_read(pointer);
    for (int i = 1; i <= 10; ++i) {
        lv_tick_inc(duration / 10);
        pointer_data.point.x = x + dx * i / 10;
        pointer_data.point.y = y + dy * i / 10;
        lv_indev_read(pointer);
        lv_timer_handler();
    }
    pointer_data.state = LV_INDEV_STATE_RELEASED;
    lv_indev_read(pointer);
    settle();
}
static void check_decoder(void)
{
    lab_touch_frame_t frame;
    uint8_t data[13] = {2, 0, 100, 0, 120, 0, 0, 0x80, 220, 0x11, 0x2c, 0, 0};
    assert(lab_touch_decode(data, &frame) && frame.count == 2);
    assert(frame.points[0].x == 100 && frame.points[1].y == 300 && frame.points[1].id == 1);
    data[9] = 1; assert(!lab_touch_decode(data, &frame)); /* duplicate ID */
    data[9] = 0x11; data[0] = 3; assert(!lab_touch_decode(data, &frame));
    data[0] = 2; data[7] = 0x40; assert(!lab_touch_decode(data, &frame)); /* UP */
    data[7] = 0x8f; assert(!lab_touch_decode(data, &frame)); /* out of range */
    data[0] = 0; assert(lab_touch_decode(data, &frame) && frame.count == 0);
    /* 第一个槽位释放后，仍可从第二槽位读到存活的单个触点。 */
    data[0] = 1; data[1] = 0x40; data[7] = 0x80;
    assert(lab_touch_decode(data, &frame) && frame.count == 1 && frame.points[0].id == 1);
}
int main(void)
{
    setbuf(stdout, NULL);
    puts("Initializing host LVGL");
    lv_init();
    original_draw_malloc=lv_draw_buf_get_handlers()->buf_malloc_cb;
    lv_draw_buf_get_handlers()->buf_malloc_cb=checked_draw_malloc;
    lv_display_t *display = lv_display_create(390, 450);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display, drawbuffer, NULL, sizeof(drawbuffer), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush);
    for (int i = 0; i < LAB_COUNT; ++i)
        lab_publish(i, LAB_IDLE, "Ready to test", "Choose an action below to begin.");
    pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer, read_pointer);
    pointer_data.state = LV_INDEV_STATE_RELEASED;
    lab_ui_create();
    check_decoder();
    puts("Rendering dashboard");
    screenshot("dashboard.ppm");
    click_text(lv_screen_active(), "RGB light");
    screenshot("rgb-detail.ppm");
    lv_obj_t *cycle = click_text(lv_screen_active(), "Cycle RGB");
    assert(busy && lv_obj_has_state(cycle, LV_STATE_DISABLED));
    busy = false;
    lab_publish(LAB_RGB, LAB_OBSERVE, "Cycle complete", "Red, green and blue shown for 1 second each; LED is now off.");
    screenshot("rgb-result.ppm");
    click_text(lv_screen_active(), "Looks right");
    assert(results[LAB_RGB].status == LAB_PASS);
    click_text(lv_screen_active(), LV_SYMBOL_LEFT);
    click_text(lv_screen_active(), "Microphone");
    click_text(lv_screen_active(), "Record 3 s");
    lab_publish(LAB_MIC, LAB_RUNNING, "-24.321 dBFS", "Recording: 1600 ms / 3000 ms\nRaw peak 3420, RMS 1992, clipped 0\n16 kHz / mono / 16-bit. Speak or clap; not calibrated dB SPL.");
    screenshot("mic-live.ppm");
    busy = false;
    lab_publish(LAB_MIC, LAB_OBSERVE, "Recording ready", "Play recording and confirm your voice.");
    click_text(lv_screen_active(), LV_SYMBOL_LEFT);
    click_text(lv_screen_active(), "Speaker");
    for (unsigned i = 0; i < LAB_ACTION_COUNT; ++i)
        assert(find_text(lv_screen_active(), lab_modules[LAB_SPEAKER].actions[i]));
    screenshot("speaker-notes.ppm");
    lv_obj_t *controls = lv_obj_get_parent(find_text(lv_screen_active(), "Volume  6 / 15"));
    lv_obj_t *vol_slider = lv_obj_get_child(controls, 1);
    lv_obj_t *freq_slider = lv_obj_get_child(controls, 3);
    lv_slider_set_value(vol_slider, 0, LV_ANIM_OFF);
    lv_slider_set_value(freq_slider, 4000, LV_ANIM_OFF);
    lv_obj_send_event(freq_slider, LV_EVENT_VALUE_CHANGED, NULL);
    assert(speaker_volume == 0 && speaker_frequency == 4000);
    assert(find_text(controls, "Volume  0 / 15 (mute)"));
    assert(find_text(controls, "Frequency  4000 Hz"));
    assert(find_text(lv_screen_active(), "Play WAV"));
    click_text(lv_screen_active(), "Play WAV");
    assert(busy);
    busy = false; settle();
    click_text(lv_screen_active(), "Play custom");
    assert(lv_obj_has_state(vol_slider, LV_STATE_DISABLED));
    assert(lv_obj_has_state(freq_slider, LV_STATE_DISABLED));
    busy = false; settle();
    assert(!lv_obj_has_state(vol_slider, LV_STATE_DISABLED));
    lv_slider_set_value(vol_slider, 6, LV_ANIM_OFF);
    lv_obj_send_event(vol_slider, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *last_note = find_text(lv_screen_active(), lab_modules[LAB_SPEAKER].actions[7]);
    lv_obj_scroll_to_view(last_note, LV_ANIM_OFF);
    screenshot("speaker-notes-bottom.ppm");
    click_text(lv_screen_active(), lab_modules[LAB_SPEAKER].actions[7]);
    assert(busy);
    busy = false;
    click_text(lv_screen_active(), LV_SYMBOL_LEFT);
    click_text(lv_screen_active(), "6-axis IMU");
    click_text(lv_screen_active(), "Continuous");
    results[LAB_IMU].samples = 3;
    lab_publish(LAB_IMU, LAB_RUNNING, "Accel (g)\nX 0.003\nY -0.005   Z 0.999", "Gyro (deg/s)\nX 0.026 / Y -0.018 / Z 0.009\nRaw A: 49 -82 16384\nRaw G: 3 -2 1\nNominal scale; not calibrated.");
    screenshot("imu-live.ppm");
    click_text(lv_screen_active(), LV_SYMBOL_STOP "  STOP TEST");
    assert(stopped);
    click_text(lv_screen_active(), LV_SYMBOL_LEFT);
    /* All detail screens must open and return without stale pointers. */
    for (int i = 1; i < LAB_COUNT; ++i) {
        click_text(lv_screen_active(), lab_modules[i].title);
        assert(find_text(lv_screen_active(), "LEARN THE API"));
        click_text(lv_screen_active(), LV_SYMBOL_LEFT);
    }
    click_text(lv_screen_active(), "Bluetooth");
    click_text(lv_screen_active(), "Open BLE tests");
    screenshot("ble-status.ppm");
    click_text(lv_layer_top(), "Advertise"); assert(ble_cmd == LAB_BLE_ADVERTISE);
    click_text(lv_layer_top(), "Scan");
    ble_view.device_count=1; strcpy(ble_view.devices[0].name,"Test phone"); ble_view.devices[0].rssi=-48;
    screenshot("ble-scan.ppm");
    click_text(lv_layer_top(), "Data");
    click_text(lv_layer_top(), "Send challenge"); assert(ble_cmd == LAB_BLE_SEND);
    ble_view.connected=true; ble_view.pair_pending=true; ble_view.passkey=123456;
    settle(); screenshot("ble-pair.ppm");
    click_text(lv_layer_top(), "Accept"); assert(ble_cmd == LAB_BLE_ACCEPT);
    click_text(lv_layer_top(), LV_SYMBOL_STOP " STOP BLE"); assert(ble_stops==1);
    click_text(lv_layer_top(), "Done / Back"); assert(ble_stops==2);
    click_text(lv_screen_active(), LV_SYMBOL_LEFT);
    click_text(lv_screen_active(), "Bluetooth Internet");
    click_text(lv_screen_active(), "Open PAN tests");
    screenshot("pan-status.ppm");
    click_text(lv_layer_top(), "Begin pairing"); assert(pan_cmd==PAN_BEGIN);
    pan_view.pairing=true; pan_view.code=123456; settle();
    screenshot("pan-pair.ppm");
    click_text(lv_layer_top(), "Accept code"); assert(pan_cmd==PAN_ACCEPT);
    pan_view.pairing=false; pan_view.enabled=pan_view.acl=pan_view.pan=pan_view.ip_ok=true;
    strcpy(pan_view.ip,"192.168.44.2"); strcpy(pan_view.gateway,"192.168.44.1"); strcpy(pan_view.dns,"192.168.44.1");
    strcpy(pan_view.message,"IP ready. Tap Test Internet (DNS + HTTP)"); settle();
    click_text(lv_layer_top(), "Test Internet"); assert(pan_cmd==PAN_TEST);
    screenshot("pan-ip.ppm");
    click_text(lv_layer_top(), "Test MQTT"); assert(pan_cmd==PAN_MQTT);
    pan_view.mqtt_connected=pan_view.mqtt_subscribed=pan_view.mqtt_published=pan_view.mqtt_ok=true;
    strcpy(pan_view.message,"PASS: MQTT publish/subscribe exact echo received"); settle();
    screenshot("pan-mqtt.ppm");
    click_text(lv_layer_top(), LV_SYMBOL_STOP " STOP PAN"); assert(pan_stops==1);
    click_text(lv_layer_top(), "Done / Back"); assert(pan_stops==2);
    click_text(lv_screen_active(), LV_SYMBOL_LEFT);
    lab_display_open();
    lab_overlay_close();
    assert(results[LAB_DISPLAY].status == LAB_IDLE);
    lab_display_open();
    lv_obj_t *overlay = lv_obj_get_child(lv_layer_top(), -1);
    lv_obj_t *surface = lv_obj_get_child(overlay, -1);
    for (int i = 0; i < 4; ++i) lv_obj_send_event(surface, LV_EVENT_CLICKED, NULL);
    lab_overlay_close();
    assert(results[LAB_DISPLAY].status == LAB_OBSERVE);
    lab_touch_open();
    click_text(lv_layer_top(), "1");
    click_text(lv_layer_top(), "1");
    assert(results[LAB_TOUCH].status == LAB_RUNNING);
    lab_overlay_close();
    assert(results[LAB_TOUCH].status == LAB_IDLE);
    lab_touch_open();
    swipe(60, 108, 0, 0, 100);
    assert(strstr(results[LAB_TOUCH].detail, "Target 1 HIT") && strstr(results[LAB_TOUCH].detail, "x 60   y 108"));
    screenshot("touch.ppm"); click_text(lv_layer_top(), "2");
    click_text(lv_layer_top(), "3"); click_text(lv_layer_top(), "4");
    assert(results[LAB_TOUCH].status == LAB_PASS);
    lab_overlay_close();
    assert(results[LAB_TOUCH].status == LAB_PASS);
    click_text(lv_screen_active(), "Light sensor");
    click_text(lv_screen_active(), "Continuous");
    results[LAB_LIGHT].samples = 8;
    lab_publish(LAB_LIGHT, LAB_RUNNING, "2534.567 lux", "Raw CH0 1234 / CH1 312\nGain code 0, rate 0x03\nEstimated illuminance; no cover-glass calibration.");
    screenshot("light-live.ppm");
    lv_obj_t *stop = lv_obj_get_parent(find_text(lv_screen_active(), LV_SYMBOL_STOP "  STOP TEST"));
    assert(stop && lv_obj_get_y(stop) + lv_obj_get_height(stop) <= 450);
    swipe(8, 160, 170, 0, 900);
    assert(stopped && !find_text(lv_screen_active(), "LEARN THE API"));
    click_text(lv_screen_active(), "Touch panel");
    click_text(lv_screen_active(), "Single point");
    swipe(195, 442, 0, -180, 900);
    assert(!lv_obj_get_child_count(lv_layer_top()) && !find_text(lv_screen_active(), "LEARN THE API"));
    click_text(lv_screen_active(), "Touch panel");
    click_text(lv_screen_active(), "Two fingers");
    raw_frame = (lab_touch_frame_t){2, {{100, 180, 0}, {280, 300, 1}}};
    lab_publish(LAB_TOUCH, LAB_RUNNING, "Checking real contact count", "Chip 00 / FW 00. Reported 2, decoded 2, max 2.\nHost-simulated frame for UI preview.");
    screenshot("touch-multi.ppm");
    swipe(195, 442, 0, -180, 900);
    assert(lv_obj_get_child_count(lv_layer_top())); /* two-finger motion cannot navigate */
    raw_frame.count = 0;
    swipe(8, 180, 170, 0, 200);
    assert(stopped && !lv_obj_get_child_count(lv_layer_top()) && find_text(lv_screen_active(), "LEARN THE API"));
    /* 右边缘已不再返回；中部快滑/慢拖也不触发返回。 */
    swipe(382, 160, -170, 0, 200);
    assert(find_text(lv_screen_active(), "LEARN THE API"));
    swipe(300, 160, -170, 0, 200);
    assert(find_text(lv_screen_active(), "LEARN THE API"));
    swipe(195, 340, 0, -140, 200);
    assert(find_text(lv_screen_active(), "LEARN THE API"));
    swipe(195, 340, 0, -140, 900);
    assert(find_text(lv_screen_active(), "LEARN THE API"));
    swipe(8, 160, 170, 0, 900);
    assert(!find_text(lv_screen_active(), "LEARN THE API"));
    puts("PASS: gestures, scroll, live-stop, raw two-point decoder and overlay; 17 detail screens; busy state; manual confirmation; display coverage; touch repeat/cancel/complete.");
    click_text(lv_screen_active(), "Play Lab");
    forbid_pixel_buffers=true;
    click_text(lv_screen_active(), "Pelican ride");
    assert(!find_text(lv_layer_top(), "A LITTLE BALANCE. A BIG CATCH."));
    assert(!find_text(lv_layer_top(), "Flip F/B") && !find_text(lv_layer_top(), "Flip L/R") && !find_text(lv_layer_top(), "Swap XY"));
    assert(!find_text(lv_layer_top(), "Done / Back"));
    screenshot("pelican-entry.ppm");
    lv_obj_t *rider=find_size(lv_layer_top(),220,180); assert(rider);
    int initial_y=lv_obj_get_y(rider), initial_x=lv_obj_get_x(rider);
    motion.a[0]=-0.28f; motion.a[2] = 0.96f;
    settle(); settle();
    assert(lv_obj_get_y(rider)<initial_y-5 && lv_obj_get_x(rider)==initial_x);
    int forward_y=lv_obj_get_y(rider);
    screenshot("pelican-forward.ppm");
    forward_y=lv_obj_get_y(rider);
    motion.a[0]=0.28f;
    settle(); settle(); settle();
    assert(lv_obj_get_y(rider)>forward_y+5);
    screenshot("pelican-reverse.ppm");
    motion.a[0]=0; settle(); settle(); settle();
    motion.a[1]=-0.22f; motion.a[2]=0.975f;
    settle(); settle();
    assert(lv_obj_get_x(rider)>initial_x+5);
    assert_no_layers(lv_layer_top());
    screenshot("pelican-turn.ppm");
    motion.a[1]=0; motion.a[0]=-0.28f; motion.a[2]=0.96f;
    settle(); settle();
    screenshot("pelican-ride.ppm");
    click_text(lv_layer_top(), LV_SYMBOL_PAUSE);
    assert(find_text(lv_layer_top(), "Seaside siesta\nTake a breath. Your ride can wait."));
    lv_obj_t *difficulty=find_size(lv_layer_top(),258,12); assert(difficulty);
    assert(lv_slider_get_value(difficulty)==1);
    lv_slider_set_value(difficulty,2,LV_ANIM_OFF); lv_obj_send_event(difficulty,LV_EVENT_VALUE_CHANGED,NULL); settle();
    assert(find_text(lv_layer_top(),"Hard / lively"));
    lv_slider_set_value(difficulty,0,LV_ANIM_OFF); lv_obj_send_event(difficulty,LV_EVENT_VALUE_CHANGED,NULL); settle();
    assert(find_text(lv_layer_top(),"Easy / relaxed"));
    lv_slider_set_value(difficulty,1,LV_ANIM_OFF); lv_obj_send_event(difficulty,LV_EVENT_VALUE_CHANGED,NULL); settle();
    lv_obj_t *sound_slider=find_size(lv_layer_top(),258,10); assert(sound_slider);
    lv_slider_set_value(sound_slider,0,LV_ANIM_OFF); lv_obj_send_event(sound_slider,LV_EVENT_VALUE_CHANGED,NULL); settle();
    assert(find_text(lv_layer_top(),"Sound off / muted"));
    lv_slider_set_value(sound_slider,8,LV_ANIM_OFF); lv_obj_send_event(sound_slider,LV_EVENT_VALUE_CHANGED,NULL); settle();
    assert(find_text(lv_layer_top(),"Sound volume  8 / 15"));
    assert(speaker_volume==6); /* Game volume is independent of the Speaker test. */
    screenshot("pelican-paused.ppm");
    click_text(lv_layer_top(), "Resume");
    int centered_y=lv_obj_get_y(rider); settle(); assert(lv_obj_get_y(rider)==centered_y);
    click_text(lv_layer_top(), LV_SYMBOL_PAUSE);
    click_text(lv_layer_top(), "Center");
    assert(lv_obj_has_flag(lv_obj_get_parent(lv_obj_get_parent(find_text(lv_layer_top(), "Resume"))), LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(lv_obj_get_parent(find_text(lv_layer_top(), LV_SYMBOL_PAUSE)), LV_OBJ_FLAG_HIDDEN));
    centered_y=lv_obj_get_y(rider); settle(); assert(lv_obj_get_y(rider)==centered_y);
    motion_frozen = true; settle(); settle();
    assert(find_text(lv_layer_top(), "IMU unavailable\nExit and retry"));
    click_text(lv_layer_top(), "Exit"); assert(stopped && !busy);
    motion_frozen = false; motion.a[0] = motion.a[1] = 0; motion.a[2] = 1;
    lab_game_open(); settle(); settle(); settle();
    /* Extreme turns, game-over and replay must also avoid offscreen buffers. */
    motion.a[1]=-0.8f; motion.a[0]=-0.35f; motion.a[2]=0.49f;
    for (int i=0; i<14; ++i) settle();
    assert(find_text(lv_layer_top(), "Again"));
    assert(sound_events[GAME_SOUND_LOSE]==1 && sound_quiets>0);
    settle(); assert(sound_events[GAME_SOUND_LOSE]==1);
    assert(!lv_obj_has_flag(lv_obj_get_parent(lv_obj_get_parent(find_text(lv_layer_top(), "Again"))), LV_OBJ_FLAG_HIDDEN));
    assert_no_layers(lv_layer_top());
    click_text(lv_layer_top(), "Again");
    motion.a[1]=0.8f; settle(); settle();
    assert_no_layers(lv_layer_top());
    swipe(8, 160, 170, 0, 200); assert(stopped && !lv_obj_get_child_count(lv_layer_top()));
    motion.a[0]=motion.a[1]=0; motion.a[2]=1;
    lab_game_open(); settle();
    click_text(lv_layer_top(), LV_SYMBOL_PAUSE);
    swipe(195,442,0,-180,200);
    assert(stopped && !lv_obj_get_child_count(lv_layer_top()) && !find_text(lv_screen_active(), "LEARN THE API"));
    puts("PASS: pelican immediate entry, visible X/Y motion and turning, simplified controls, pause/recenter, stale IMU and exit.");
    assert(game_buffer_attempts==0);
    puts("PASS: game entry, motion, turns, pause, game-over/replay, error and reopen allocate ZERO extra pixel buffers; no transform layers.");
    forbid_pixel_buffers=false;
    lv_deinit();
    return 0;
}

void lab_game_sound_emit(game_sound_t cue) { assert(cue>0 && cue<=GAME_SOUND_LOSE); sound_events[cue]++; }
void lab_game_sound_quiet(void) { sound_quiets++; }

static unsigned game_volume=6;
unsigned lab_game_sound_volume(void) { return game_volume; }
void lab_game_sound_set_volume(unsigned v) { if (v<=15) game_volume=v; }
