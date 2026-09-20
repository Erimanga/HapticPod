/*
 * lab_game_ui.c — 游戏界面：定时读取 IMU 快照、推进模型并绘制场景，触摸菜单负责暂停与设置。
 */
/* Small native LVGL illustration: no bitmap framebuffer or external assets. */
#include "lab.h"
#include "lab_ui.h"
#include "lab_game.h"
#include "lab_game_sound.h"
#include "lab_ride.h"
#include <math.h>
#include <stdio.h>
static lab_ride_t ride;
static lv_obj_t *scene, *sky, *bird, *hud, *travel, *notice, *notice_text, *pause_button, *veil, *message;
static lv_obj_t *volume_slider, *volume_text, *difficulty_slider, *difficulty_text, *supplies[2];
static lv_obj_t *resume_button, *center_button, *again_button;
static lv_obj_t *planks[18], *fish[6], *cones[6];
static lv_timer_t *timer;
static uint32_t last_tick, last_sample, sequence;
static bool started, sensor_failed, tip_seen;
static uint32_t opened_at, sky_tick;
static bool show_tip;
static lv_obj_t *shape(lv_obj_t *p, int x, int y, int w, int h, uint32_t c, int radius)
{
    lv_obj_t *o = lv_obj_create(p); lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(c), 0); lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0); lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}
static lv_obj_t *text(lv_obj_t *p, const char *s, int x, int y, const lv_font_t *font, uint32_t c)
{
    lv_obj_t *o = lv_label_create(p); lv_label_set_text(o, s); lv_obj_set_pos(o, x, y);
    lv_obj_set_style_text_font(o, font, 0); lv_obj_set_style_text_color(o, lv_color_hex(c), 0); return o;
}
static void triangle(lv_event_t *e)
{
    lv_area_t a; lv_obj_get_coords(lv_event_get_target(e), &a);
    lv_draw_triangle_dsc_t d; lv_draw_triangle_dsc_init(&d);
    d.color = lv_color_hex((uint32_t)(uintptr_t)lv_event_get_user_data(e));
    d.p[0].x = (a.x1 + a.x2) / 2; d.p[0].y = a.y1;
    d.p[1].x = a.x1; d.p[1].y = a.y2;
    d.p[2].x = a.x2; d.p[2].y = a.y2;
    lv_draw_triangle(lv_event_get_layer(e), &d);
}
static lv_obj_t *tri(lv_obj_t *p, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *o = shape(p, x, y, w, h, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(o, triangle, LV_EVENT_DRAW_MAIN, (void *)(uintptr_t)color);
    return o;
}
/* Rotate primitive coordinates, never an LVGL object/layer. Object transforms
 * rasterize the subtree into an ARGB buffer (~60 KB for this small sprite).
 * The padded bounds contain the whole rider at the model's maximum turn. */
typedef struct { lv_layer_t *layer; float x, y, c, s; } rider_draw_t;
static lv_point_precise_t rider_point(const rider_draw_t *d, float x, float y)
{
    x -= 54; y -= 105;
    lv_point_precise_t p = { (int32_t)lroundf(d->x + x*d->c - y*d->s),
                             (int32_t)lroundf(d->y + x*d->s + y*d->c) };
    return p;
}
static void rider_line(const rider_draw_t *d, float x1, float y1, float x2, float y2, int width, uint32_t color)
{
    lv_draw_line_dsc_t line; lv_draw_line_dsc_init(&line);
    line.p1 = rider_point(d, x1, y1); line.p2 = rider_point(d, x2, y2);
    line.width = width; line.color = lv_color_hex(color); line.round_start = line.round_end = 1;
    if (line.p1.x == line.p2.x && line.p1.y == line.p2.y) {
        lv_draw_rect_dsc_t circle; lv_draw_rect_dsc_init(&circle);
        circle.bg_color = line.color; circle.bg_opa = LV_OPA_COVER; circle.radius = LV_RADIUS_CIRCLE;
        lv_area_t a = {line.p1.x-width/2, line.p1.y-width/2,
                       line.p1.x-width/2+width-1, line.p1.y-width/2+width-1};
        lv_draw_rect(d->layer, &circle, &a);
    } else lv_draw_line(d->layer, &line);
}
static void rider_capsule(const rider_draw_t *d, int x, int y, int w, int h, uint32_t color)
{
    float r = (w < h ? w : h) * 0.5f;
    if (w >= h) rider_line(d, x+r, y+r, x+w-r, y+r, h, color);
    else rider_line(d, x+r, y+r, x+r, y+h-r, w, color);
}
/* 在当前绘图层直接绘制骑手与自行车，旋转坐标以避免对象旋转产生离屏缓冲。 */
static void draw_rider(lv_event_t *e)
{
    lv_area_t a; lv_obj_get_coords(lv_event_get_target(e), &a);
    float angle = (ride.heading * 0.6f + ride.lean * 4.8f) * 0.017453293f;
    rider_draw_t d = {lv_event_get_layer(e), a.x1+56+54, a.y1+20+105, cosf(angle), sinf(angle)};
    /* Bicycle: two spoked wheels, diamond frame, fork, saddle and handlebars.
       All motion is distance driven: pedals stop at rest/pause and reverse
       while backing up. Coordinates are rotated directly, without layers. */
    float wheel_angle = fmodf(ride.distance * 4.5f, 6.2831853f);
    for (int i=0; i<2; ++i) {
        int x=18+i*74;
        rider_capsule(&d, x-18, 82, 36, 36, 0x244652);
        rider_capsule(&d, x-14, 86, 28, 28, 0xE7E9CC);
        for (int spoke=0; spoke<2; ++spoke) {
            float a=wheel_angle+spoke*1.5707963f;
            float dx=sinf(a)*12, dy=cosf(a)*12;
            rider_line(&d, x-dx, 100-dy, x+dx, 100+dy, 2, 0x739B97);
        }
        rider_capsule(&d, x-3, 97, 6, 6, 0x244652);
    }
    rider_line(&d, 18,100, 54,98, 3,0x345961); /* chain */
    rider_line(&d, 18,100, 42,73, 4,0xE77F59);
    rider_line(&d, 42,73, 54,98, 4,0xE77F59);
    rider_line(&d, 42,73, 79,73, 4,0xFFB477);
    rider_line(&d, 79,73, 54,98, 4,0xE77F59);
    rider_line(&d, 79,73, 92,100, 4,0x315D62);
    rider_line(&d, 79,73, 84,60, 4,0x315D62);
    rider_line(&d, 84,60, 96,60, 4,0x315D62);
    rider_line(&d, 92,60, 97,63, 4,0x244652);
    rider_line(&d, 42,73, 40,68, 4,0x315D62);
    rider_line(&d, 33,68, 48,68, 6,0x244652);
    rider_capsule(&d, 48,92, 12,12, 0x315D62);
    float pedal_angle=fmodf(ride.distance * 1.8f, 6.2831853f);
    for (int leg=0; leg<2; ++leg) {
        float phase=pedal_angle+leg*3.14159265f;
        float foot_x=54+cosf(phase)*12, foot_y=98+sinf(phase)*12;
        float knee_x=58+(foot_x-54)*0.4f, knee_y=80+(foot_y-98)*0.4f;
        uint32_t color=leg ? 0xFFC363 : 0xC68A43;
        rider_line(&d, 54,98, foot_x,foot_y, 3,0x345961);
        rider_line(&d, 46+leg*4,65, knee_x,knee_y, 5,color);
        rider_line(&d, knee_x,knee_y, foot_x,foot_y, 4,color);
        rider_line(&d, foot_x-5,foot_y+2, foot_x+6,foot_y+2, 3,0x244652);
        rider_line(&d, foot_x-3,foot_y, foot_x+6,foot_y, 4,color);
    }
    static const struct { int x,y,w,h; uint32_t color; } parts[] = {
        {19,30,63,45,0xFFF9E8}, {20,40,41,26,0xD9E6D7}, {24,44,27,4,0xF5F4E3},
        {64,13,20,41,0xFFF9E8}, {55,4,34,31,0xFFF9E8},
        {79,19,28,13,0xEBA750}, {78,17,30,5,0xFFCE6E},
        {75,12,5,6,0x244652}, {76,12,2,2,0xFFFFFF},
        {54,1,33,9,0x326F7B}, {50,7,40,4,0x244652},
        {62,32,24,6,0xF37865}, {55,34,12,16,0xF37865}
    };
    for (unsigned i=0; i<sizeof(parts)/sizeof(parts[0]); ++i)
        rider_capsule(&d, parts[i].x, parts[i].y, parts[i].w, parts[i].h, parts[i].color);
    rider_line(&d, 63,49, 75,59, 7,0xD9E6D7);
    rider_line(&d, 75,59, 85,60, 6,0xFFF9E8);
}
/* Ambient birds are direct line primitives, never transformed objects. */
static void draw_sky(lv_event_t *e)
{
    lv_area_t a; lv_obj_get_coords(lv_event_get_target(e), &a);
    float seconds=lv_tick_elaps(opened_at)*0.001f;
    for (int i=0; i<3; ++i) {
        float x=fmodf(seconds*8+i*125,450)-30;
        float y=27+(i%2)*20+sinf(seconds*0.6f+i)*3;
        float flap=3+sinf(seconds*2.8f+i)*2;
        for (int side=-1; side<=1; side+=2) {
            lv_draw_line_dsc_t line; lv_draw_line_dsc_init(&line);
            line.color=lv_color_hex(0x638C91); line.width=2;
            line.round_start=line.round_end=1;
            line.p1.x=a.x1+(int)x; line.p1.y=a.y1+(int)y;
            line.p2.x=a.x1+(int)(x+side*9); line.p2.y=a.y1+(int)(y-flap);
            lv_draw_line(lv_event_get_layer(e), &line);
        }
    }
}
static void toggle(lv_obj_t *o, bool show)
{
    if (show) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}
/* 删除游戏定时器并请求采样、音效停止，后台负责设备收尾。 */
static void close_game(void)
{
    if (timer) { lv_timer_delete(timer); timer = NULL; }
    lab_game_sound_quiet();
    if (started) lab_stop();
    started = false;
}
/* 处理暂停菜单动作，将游戏状态变化与居中、重玩和退出关联。 */
static void action(lv_event_t *e)
{
    unsigned cmd = (unsigned)(uintptr_t)lv_event_get_user_data(e);
    if (cmd == 4) { lab_overlay_close(); return; }
    if (sensor_failed) return;
    if (cmd == 0 && ride.phase == RIDE_PLAYING) { lab_game_sound_quiet(); ride.phase = RIDE_PAUSED; }
    else if (cmd == 1 && ride.phase == RIDE_PAUSED) lab_ride_calibrate(&ride);
    else if (cmd == 2 && ride.phase == RIDE_PAUSED) {
        /* The next fresh frame centers the grip and resumes play. */
        lab_ride_calibrate(&ride);
    } else if (cmd == 3) { lab_game_sound_quiet(); lab_ride_reset(&ride); lab_game_sound_emit(GAME_SOUND_START); }

}
static lv_obj_t *control(lv_obj_t *p, const char *s, int x, int y, int w, unsigned cmd)
{
    lv_obj_t *b = shape(p, x, y, w, 48, 0x203C4C, 12);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(b, action, LV_EVENT_CLICKED, (void *)(uintptr_t)cmd);
    lv_obj_t *l = text(b, s, 0, 0, &lv_font_montserrat_14, 0xE7F6EB); lv_obj_center(l); return l;
}
static void volume_event(lv_event_t *e)
{
    (void)e;
    lab_game_sound_set_volume(lv_slider_get_value(volume_slider));
}
static void difficulty_event(lv_event_t *e)
{
    (void)e;
    static const ride_difficulty_t levels[] = {RIDE_EASY, RIDE_NORMAL, RIDE_HARD};
    lab_ride_difficulty(&ride, levels[lv_slider_get_value(difficulty_slider)]);
}
/* 把模型状态映射为道路、角色、道具位置与 HUD 文本。 */
static void render(void)
{
    char line[100];
    snprintf(line, sizeof(line), "FISH %02u    %02ds    %u HP", ride.fish, (int)ceilf(ride.time), ride.lives);
    lv_label_set_text(hud, line);
    lv_label_set_text(difficulty_text, ride.difficulty == RIDE_EASY ? "Easy / relaxed" : ride.difficulty == RIDE_HARD ? "Hard / lively" : "Normal / balanced");
    unsigned volume=lab_game_sound_volume();
    if (volume) lv_label_set_text_fmt(volume_text,"Sound volume  %u / 15",volume);
    else lv_label_set_text(volume_text,"Sound off / muted");
    toggle(volume_slider, !sensor_failed); toggle(volume_text, !sensor_failed);
    toggle(difficulty_slider, !sensor_failed);
    toggle(difficulty_text, !sensor_failed);
    snprintf(line, sizeof(line), "%s %d.%d m/s   /   %dm", ride.speed < -0.2f ? "REV" : "GO",
             (int)fabsf(ride.speed), (int)(fabsf(ride.speed)*10)%10, (int)ride.distance);
    lv_label_set_text(travel, line);
    if (lv_tick_elaps(sky_tick) >= 120) { sky_tick=lv_tick_get(); lv_obj_invalidate(sky); }
    for (int i = 0; i < 18; ++i) {
        int y = (int)(i * 24 + ride.camera * 7) % 432;
        lv_obj_set_y(planks[i], y < 0 ? y + 432 : y);
    }
    for (int k = 0; k < 6; ++k) { toggle(fish[k], false); toggle(cones[k], false); }
    int nf = 0, nc = 0;
    for (unsigned i = 0; i < RIDE_ROUTE_ITEMS; ++i) {
        float ahead = 10 + i * 12 - ride.camera;
        if (ahead > -18 && ahead < 30 && !ride.collected[i] && nf < 6) {
            lv_obj_set_pos(fish[nf], 180 + (int)(lab_ride_lane(i) * 113), 313 - (int)(ahead * 7)); toggle(fish[nf++], true);
        }
        ahead += 6;
        if (ahead > -18 && ahead < 30 && nc < 6) {
            lv_obj_set_pos(cones[nc], 180 - (int)(lab_ride_lane(i + 1) * 113), 306 - (int)(ahead * 7)); toggle(cones[nc++], true);
        }
    }
    for (unsigned i=0; i<2; ++i) toggle(supplies[i], false);
    unsigned ns=0;
    for (unsigned i=0; i<RIDE_SUPPLIES && ns<2; ++i) {
        float ahead=lab_ride_supply_distance(i)-ride.camera;
        if (!ride.supplies[i] && ahead > -18 && ahead < 30) {
            lv_obj_set_pos(supplies[ns], 180+(int)(lab_ride_supply_lane(i)*113), 311-(int)(ahead*7));
            toggle(supplies[ns++], true);
        }
    }
    lv_obj_set_pos(bird, 139-56 + (int)(ride.x * 112), 230-20 - (int)((ride.distance - ride.camera) * 7));
    lv_obj_invalidate(bird);
    bool blocking = ride.phase == RIDE_PAUSED || ride.phase == RIDE_OVER || sensor_failed;
    toggle(veil, blocking); toggle(pause_button, !blocking);
    toggle(resume_button, !sensor_failed && ride.phase == RIDE_PAUSED);
    toggle(center_button, !sensor_failed && ride.phase == RIDE_PAUSED);
    toggle(again_button, !sensor_failed);
    if (sensor_failed) lv_label_set_text(message, "IMU unavailable\nExit and retry");
    else if (ride.phase == RIDE_PAUSED) lv_label_set_text(message, "Seaside siesta\nTake a breath. Your ride can wait.");
    else if (ride.phase == RIDE_OVER) {
        snprintf(line, sizeof(line), "%s\n%u fish  /  %dm", ride.lives ? "Catch of the day!" : "Splash! Try again", ride.fish, (int)ride.distance);
        lv_label_set_text(message, line);
    }
    const char *feedback = NULL;
    if (!blocking) {
        if (ride.flash > 0) {
            static const char *events[] = {"", "+1 fish!", "Oops! Steady the bike", "+1 HP! Fresh energy", "HP full / 5"};
            feedback=events[ride.event];
        }
        else if (fabsf(ride.lean) > 0.7f) feedback = "Easy on the turns!";
        else if (show_tip && lv_tick_elaps(opened_at) < 4000) feedback = "Swipe from left edge to exit";
    }
    toggle(notice, feedback != NULL);
    if (feedback) lv_label_set_text(notice_text, feedback);
    lv_obj_set_style_bg_color(scene, lv_color_hex(ride.flash > 0 ? (ride.event == RIDE_EVENT_HIT ? 0xF7B894 : 0xCBECC0) : 0x8EDCD4), 0);
}
/* 读取运动快照并推进模型，按事件触发音效后刷新画面。 */
static void tick(lv_timer_t *t)
{
    (void)t;
    uint32_t now = lv_tick_get();
    lab_motion_t m; lab_motion_snapshot(&m);
    lab_result_t result; lab_snapshot(LAB_GAME, &result);
    if (result.status == LAB_FAIL || (!lab_busy() && started)) sensor_failed = true;
    if (m.valid && m.sequence != sequence) {
        float dt = (now - last_tick) / 1000.0f;
        sequence = m.sequence; last_sample = now; last_tick = now;
        if (!sensor_failed) {
            ride_phase_t before=ride.phase;
            unsigned revision=ride.event_revision;
            lab_ride_step(&ride, &m, dt);
            if (before!=RIDE_OVER && ride.phase==RIDE_OVER) {
                lab_game_sound_quiet();
                lab_game_sound_emit(ride.lives ? GAME_SOUND_WIN : GAME_SOUND_LOSE);
            } else if (ride.event_revision!=revision) {
                if (ride.event==RIDE_EVENT_FISH) lab_game_sound_emit(GAME_SOUND_FISH);
                else if (ride.event==RIDE_EVENT_HEAL || ride.event==RIDE_EVENT_FULL) lab_game_sound_emit(GAME_SOUND_HEAL);
                else if (ride.event==RIDE_EVENT_HIT) lab_game_sound_emit(GAME_SOUND_HIT);
            }
        }
    }
    if (now - last_sample > 600 && !sensor_failed) {
        sensor_failed = true; lab_game_sound_quiet(); lab_stop();
    }
    render();
}
/* 启动 IMU 后台动作，创建游戏场景和菜单，注册周期更新。 */
void lab_game_open(void)
{
    if (lab_busy()) return;
    if (!lab_start(LAB_GAME, 0)) return;
    started = true; sensor_failed = false;
    lab_ride_reset(&ride);
    lv_obj_t *root = lab_overlay("", close_game);
    lv_obj_clean(root); /* Keep overlay lifecycle/edge navigation, remove chrome. */
    scene = shape(root, 0, 0, 390, 450, 0x8EDCD4, 0);
    sky = shape(scene, 0, 0, 390, 90, 0xD8F3E6, 0);
    shape(sky, 279, 20, 42, 42, 0xFFD389, 21);
    shape(sky, 35, 52, 76, 13, 0xFFFFFF, 7);
    shape(sky, 50, 37, 32, 28, 0xFFFFFF, 16);
    shape(sky, 76, 45, 23, 20, 0xFFFFFF, 12);
    shape(sky, 220, 71, 60, 8, 0xFFFFFF, 5);
    lv_obj_add_event_cb(sky, draw_sky, LV_EVENT_DRAW_MAIN_END, NULL);
    shape(scene, 46, 90, 298, 360, 0xC5986A, 0);
    lv_obj_t *deck = shape(scene, 50, 90, 290, 360, 0xC5986A, 0);
    for (int i = 0; i < 18; ++i) planks[i] = shape(deck, 0, i * 24, 290, 2, 0xEBC397, 0);
    for (int i = 0; i < 10; ++i) {
        shape(scene, 9, 111 + i * 39, 23, 3, 0xBDEDE0, 2);
        shape(scene, 357, 127 + i * 39, 21, 3, 0xBDEDE0, 2);
    }
    shape(scene, 43, 90, 7, 360, 0xFFF2C9, 2);
    shape(scene, 340, 90, 7, 360, 0xFFF2C9, 2);
    for (int i = 0; i < 6; ++i) {
        fish[i] = shape(scene, 0, 0, 28, 20, 0xFFD25D, 10);
        tri(fish[i], 0, 2, 10, 16, 0xF2A54F); shape(fish[i], 21, 6, 3, 3, 0x244A55, 2);
        cones[i] = tri(scene, 0, 0, 25, 26, 0xF27858);
        shape(cones[i], 8, 11, 9, 4, 0xFFF3DE, 0); shape(cones[i], 0, 23, 25, 3, 0x794E46, 1);
    }
    for (int i=0; i<2; ++i) {
        supplies[i]=shape(scene, 0,0,26,26,0x328A78,8);
        shape(supplies[i], 3,3,20,20,0x9FF2C6,6);
        shape(supplies[i], 11,6,4,14,0xFFFFFF,1);
        shape(supplies[i], 6,11,14,4,0xFFFFFF,1);
    }
    bird = shape(scene, 139-56, 230-20, 220, 180, 0, 0);
    lv_obj_set_style_bg_opa(bird, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(bird, draw_rider, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_t *stats = shape(scene, 24, 377, 278, 62, 0x123949, 18);
    lv_obj_set_style_bg_opa(stats, LV_OPA_80, 0);
    hud = text(stats, "", 0, 0, &lv_font_montserrat_14, 0xFFF1D4);
    lv_obj_align(hud, LV_ALIGN_TOP_MID, 0, 10);
    travel = text(stats, "", 0, 0, &lv_font_montserrat_12, 0xBCE2D8);
    lv_obj_align(travel, LV_ALIGN_BOTTOM_MID, 0, -10);
    pause_button = lv_obj_get_parent(control(scene, LV_SYMBOL_PAUSE, 318, 377, 48, 0));
    lv_obj_set_style_bg_opa(pause_button, LV_OPA_80, 0);
    notice = shape(scene, 47, 99, 296, 30, 0x123949, 15);
    lv_obj_set_style_bg_opa(notice, LV_OPA_80, 0);
    notice_text = text(notice, "", 0, 0, &lv_font_montserrat_12, 0xFFF1D4);
    lv_obj_align(notice_text, LV_ALIGN_CENTER, 0, 0);
    veil = shape(scene, 34, 28, 322, 396, 0x123949, 24);
    lv_obj_set_style_bg_opa(veil, LV_OPA_90, 0);
    message = text(veil, "", 15, 25, &lv_font_montserrat_16, 0xF6F5E4);
    lv_obj_set_width(message, 292); lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(message, 8, 0);
    difficulty_text=text(veil,"",24,88,&lv_font_montserrat_14,0xFFD389);
    difficulty_slider=lv_slider_create(veil);
    lv_obj_set_pos(difficulty_slider,32,124); lv_obj_set_size(difficulty_slider,258,12);
    lv_slider_set_range(difficulty_slider,0,2);
    lv_slider_set_value(difficulty_slider,ride.difficulty==RIDE_EASY ? 0 : ride.difficulty==RIDE_HARD ? 2 : 1,LV_ANIM_OFF);
    lv_obj_set_ext_click_area(difficulty_slider,18);
    lv_obj_set_style_bg_color(difficulty_slider,lv_color_hex(0x91E3BE),LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(difficulty_slider,lv_color_hex(0xFFF1D4),LV_PART_KNOB);
    lv_obj_add_event_cb(difficulty_slider,difficulty_event,LV_EVENT_VALUE_CHANGED,NULL);
    volume_text=text(veil,"",24,158,&lv_font_montserrat_14,0xBCE2D8);
    volume_slider=lv_slider_create(veil);
    lv_obj_set_pos(volume_slider,32,196); lv_obj_set_size(volume_slider,258,10);
    lv_slider_set_range(volume_slider,0,15);
    lv_slider_set_value(volume_slider,lab_game_sound_volume(),LV_ANIM_OFF);
    lv_obj_set_ext_click_area(volume_slider,18);
    lv_obj_set_style_bg_color(volume_slider,lv_color_hex(0x91E3BE),LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(volume_slider,lv_color_hex(0xFFF1D4),LV_PART_KNOB);
    lv_obj_add_event_cb(volume_slider,volume_event,LV_EVENT_VALUE_CHANGED,NULL);
    resume_button = lv_obj_get_parent(control(veil, "Resume", 18, 248, 137, 1));
    center_button = lv_obj_get_parent(control(veil, "Center", 167, 248, 137, 2));
    again_button = lv_obj_get_parent(control(veil, "Again", 18, 320, 137, 3));
    control(veil, "Exit", 167, 320, 137, 4);
    sky_tick = opened_at = lv_tick_get(); show_tip = !tip_seen; tip_seen = true;
    last_tick = last_sample = lv_tick_get(); sequence = 0;
    timer = lv_timer_create(tick, 33, NULL); render();
}
