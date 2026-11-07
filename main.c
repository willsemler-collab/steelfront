/*
 * STEEL FRONT - top-down tank battle for the TI-84 Plus CE
 * Written in C for the CE C/C++ toolchain (CEdev). Uses graphx, keypadc, fileioc.
 *
 * Controls:
 *   Up / Down      drive forward / reverse
 *   Left / Right   turn hull
 *   [alpha]        turn turret left
 *   [X,T,0,n]      turn turret right
 *   [2nd]          fire
 *   [clear]        quit
 *
 * Armor matters: front hits do little, side hits more, rear hits the most.
 * Flank the heavy tanks - their front plate bounces your shells.
 */

#include <graphx.h>
#include <keypadc.h>
#include <fileioc.h>
#include <tice.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#define W   320
#define H   240
#define TOP 16                 /* HUD height */
#define FP  8                  /* fixed point shift */
#define PX(v) ((v) >> FP)

/* ---------- palette ---------- */
enum {
    C_GROUND, C_SPECK, C_BLACK, C_WHITE, C_PHULL, C_PTREAD, C_PTUR,
    C_EHULL, C_ETREAD, C_ETUR, C_HHULL, C_ROCK, C_ROCKL, C_YEL, C_ORG,
    C_RED, C_HUD, C_GREEN, C_CRATE, C_SHADOW, C_SMOKE, C_SCORCH, C_PLANE,
    C_GREY, C_COUNT
};
#define C_TRANS 250

static const uint8_t pal_rgb[C_COUNT][3] = {
    {150, 140,  90}, {125, 118,  72}, {  0,   0,   0}, {255, 255, 255},
    { 70, 110,  60}, { 40,  55,  35}, { 95, 145,  80}, {125, 125, 130},
    { 60,  60,  65}, {160, 160, 165}, {120,  60,  55}, {105, 100,  95},
    {150, 145, 140}, {255, 230,  60}, {255, 140,  30}, {215,  40,  30},
    { 30,  35,  30}, { 60, 200,  60}, {150, 100,  50}, {115, 105,  68},
    { 90,  90,  90}, { 85,  78,  52}, { 70,  80,  65}, {175, 175, 175},
};

/* sin * 256 for 64 directions; angle 0 = right, increasing clockwise */
static const int16_t sinT[64] = {
    0,25,50,74,98,121,142,162,181,198,213,226,237,245,251,255,256,255,251,245,
    237,226,213,198,181,162,142,121,98,74,50,25,0,-25,-50,-74,-98,-121,-142,
    -162,-181,-198,-213,-226,-237,-245,-251,-255,-256,-255,-251,-245,-237,-226,
    -213,-198,-181,-162,-142,-121,-98,-74,-50,-25
};
#define SIN(a) sinT[(a) & 63]
#define COS(a) sinT[((a) + 16) & 63]

/* ---------- entities ---------- */
enum { K_PLAYER, K_LIGHT, K_MEDIUM, K_HEAVY };

typedef struct {
    int x, y;               /* fixed point */
    uint8_t hull, turret;   /* 0..63 */
    int8_t hp, maxhp;
    uint8_t reload, reload_max;
    uint8_t kind, flash;
    uint8_t ai_timer, ai_mode;
    int8_t ai_dir;
    int speed;              /* fixed point px/frame */
    bool alive;
} tank_t;

typedef struct { int x, y, dx, dy; uint8_t life, ang, owner; bool on; } shell_t;
typedef struct { int x, y, dx, dy; uint8_t life, color; } part_t;
typedef struct { int16_t x, y; uint8_t r; } rock_t;
typedef struct { int16_t x, y; uint8_t timer; bool on; } spot_t;   /* crates, bombs */
typedef struct { int x, dx; int16_t y; uint8_t dropped; bool on; } plane_t;

#define MAX_EN 6
#define MAX_SH 20
#define MAX_PT 48
#define MAX_RK 7
#define MAX_CR 3
#define MAX_BM 6
#define MAX_SC 16
#define MAX_SP 40

static tank_t player, en[MAX_EN];
static shell_t shells[MAX_SH];
static part_t parts[MAX_PT];
static rock_t rocks[MAX_RK];
static spot_t crates[MAX_CR], bombs[MAX_BM];
static int16_t scorch[MAX_SC][2];
static uint8_t n_scorch;
static int16_t specks[MAX_SP][2];
static plane_t plane;

static unsigned int score, hiscore, kills;
static uint8_t wave, banner_t, next_wave_t, death_t, msg_t, msg_col;
static int plane_t_left;
static const char *msg;
static unsigned int frame;

static const int8_t  k_hp[]    = { 12, 3, 5, 6 };
static const uint8_t k_reload[] = { 40, 90, 110, 140 };
static const int     k_speed[] = { 300, 260, 190, 140 };
static const uint8_t k_armor[] = { 1, 0, 1, 2 };
static const uint8_t k_len[]   = { 10, 9, 10, 12 };
static const uint8_t k_wid[]   = { 7, 6, 7, 8 };

static int rnd(int n) { return rand() % n; }

static int8_t adiff(uint8_t target, uint8_t cur) {
    int d = ((int)target - (int)cur) & 63;
    if (d >= 32) d -= 64;
    return (int8_t)d;
}

/* nearest of the 64 directions to vector (dx, dy) */
static uint8_t angle_to(int dx, int dy) {
    uint8_t a, best = 0;
    int bd = dx * COS(0) + dy * SIN(0);
    for (a = 1; a < 64; a++) {
        int d = dx * COS(a) + dy * SIN(a);
        if (d > bd) { bd = d; best = a; }
    }
    return best;
}

static void say(const char *m, uint8_t col) { msg = m; msg_col = col; msg_t = 45; }

/* ---------- particles ---------- */
static void boom(int x, int y, uint8_t n, bool big) {
    uint8_t i;
    for (i = 0; i < MAX_PT && n; i++) {
        part_t *p = &parts[i];
        uint8_t a;
        int sp;
        if (p->life) continue;
        a = (uint8_t)rnd(64);
        sp = rnd(big ? 420 : 220) + 60;
        p->x = x; p->y = y;
        p->dx = (COS(a) * sp) >> 8;
        p->dy = (SIN(a) * sp) >> 8;
        p->life = (uint8_t)(8 + rnd(big ? 22 : 10));
        p->color = rnd(3) == 0 ? C_YEL : (rnd(2) ? C_ORG : C_RED);
        n--;
    }
}

static void update_parts(void) {
    uint8_t i;
    for (i = 0; i < MAX_PT; i++) {
        part_t *p = &parts[i];
        if (!p->life) continue;
        p->x += p->dx; p->y += p->dy;
        p->dx -= p->dx >> 3; p->dy -= p->dy >> 3;
        if (--p->life < 6) p->color = C_SMOKE;
    }
}

/* ---------- collision ---------- */
static bool blocked(int px, int py, const tank_t *self) {
    uint8_t i;
    if (px < 9 || px > W - 9 || py < TOP + 9 || py > H - 9) return true;
    for (i = 0; i < MAX_RK; i++) {
        int dx = px - rocks[i].x, dy = py - rocks[i].y, rr = rocks[i].r + 8;
        if (dx * dx + dy * dy < rr * rr) return true;
    }
    if (self != &player && player.alive) {
        int dx = px - PX(player.x), dy = py - PX(player.y);
        if (dx * dx + dy * dy < 18 * 18) return true;
    }
    for (i = 0; i < MAX_EN; i++) {
        const tank_t *t = &en[i];
        int dx, dy;
        if (t == self || !t->alive) continue;
        dx = px - PX(t->x); dy = py - PX(t->y);
        if (dx * dx + dy * dy < 18 * 18) return true;
    }
    return false;
}

/* returns false if the tank could not move freely */
static bool move_tank(tank_t *t, int dir) {
    int sp = dir > 0 ? t->speed : -(t->speed >> 1);
    int nx = t->x + ((sp * COS(t->hull)) >> 8);
    int ny = t->y + ((sp * SIN(t->hull)) >> 8);
    if (!blocked(PX(nx), PX(ny), t)) { t->x = nx; t->y = ny; return true; }
    if (!blocked(PX(nx), PX(t->y), t)) t->x = nx;
    else if (!blocked(PX(t->x), PX(ny), t)) t->y = ny;
    return false;
}

/* ---------- combat ---------- */
static void setup_tank(tank_t *t, uint8_t kind) {
    t->kind = kind;
    t->hp = t->maxhp = k_hp[kind];
    t->reload_max = k_reload[kind];
    if (kind != K_PLAYER) {
        int r = k_reload[kind] - (wave > 7 ? 28 : wave * 4);
        t->reload_max = (uint8_t)r;
    }
    t->speed = k_speed[kind];
    t->flash = 0;
    t->ai_timer = 0;
    t->ai_dir = 1;
    t->ai_mode = 0;
    t->alive = true;
}

static void fire(tank_t *t, uint8_t owner, int8_t spread) {
    uint8_t i, a = (uint8_t)((t->turret + spread) & 63);
    for (i = 0; i < MAX_SH; i++) {
        shell_t *s = &shells[i];
        if (s->on) continue;
        s->x = t->x + COS(a) * 14;
        s->y = t->y + SIN(a) * 14;
        s->dx = COS(a) * 5;
        s->dy = SIN(a) * 5;
        s->life = 55;
        s->ang = a;
        s->owner = owner;
        s->on = true;
        boom(s->x, s->y, 3, false);
        break;
    }
    t->reload = t->reload_max;
}

static void add_scorch(int x, int y) {
    uint8_t i = n_scorch % MAX_SC;
    scorch[i][0] = (int16_t)PX(x);
    scorch[i][1] = (int16_t)PX(y);
    n_scorch++;
}

static void damage(tank_t *t, int dmg) {
    if (!t->alive || dmg <= 0) return;
    t->hp -= (int8_t)dmg;
    t->flash = 5;
    if (t->hp > 0) return;

    t->alive = false;
    boom(t->x, t->y, 24, true);
    add_scorch(t->x, t->y);
    if (t == &player) {
        death_t = 75;
        say("TANK DESTROYED", C_RED);
        return;
    }
    kills++;
    score += t->kind * 100 + wave * 10;
    say("TARGET DESTROYED", C_YEL);
    if (rnd(4) == 0) {
        uint8_t i;
        for (i = 0; i < MAX_CR; i++) {
            if (crates[i].on) continue;
            crates[i].x = (int16_t)PX(t->x);
            crates[i].y = (int16_t)PX(t->y);
            crates[i].timer = 255;
            crates[i].on = true;
            break;
        }
    }
}

/* shell travelling in direction sang hits tank t: armor depends on impact side */
static void shell_hit(tank_t *t, uint8_t sang, bool by_player) {
    int rel = ((int)sang - (int)t->hull) & 63;
    int d = rel - 32;          /* 0 = head-on into the front plate */
    int zone, dmg;
    if (d < 0) d = -d;
    zone = d <= 9 ? 0 : (d >= 22 ? 2 : 1);   /* 0 front, 1 side, 2 rear */
    dmg = 2 + zone - k_armor[t->kind];
    boom(t->x - (COS(sang) * 6), t->y - (SIN(sang) * 6), 6, false);

    if (by_player) {
        if (dmg <= 0) say("BOUNCED!", C_GREY);
        else if (zone == 0) say("HIT", C_WHITE);
        else if (zone == 1) say("SIDE HIT", C_YEL);
        else say("REAR HIT!", C_ORG);
    } else if (t == &player) {
        say(zone == 0 ? "FRONT ARMOR HIT" : zone == 1 ? "SIDE ARMOR HIT" : "REAR ARMOR HIT", C_RED);
    }
    damage(t, dmg);
}

static void update_shells(void) {
    uint8_t i, j;
    for (i = 0; i < MAX_SH; i++) {
        shell_t *s = &shells[i];
        int px, py;
        if (!s->on) continue;
        s->x += s->dx; s->y += s->dy;
        px = PX(s->x); py = PX(s->y);
        if (--s->life == 0 || px < 0 || px >= W || py < TOP || py >= H) { s->on = false; continue; }
        for (j = 0; j < MAX_RK; j++) {
            int dx = px - rocks[j].x, dy = py - rocks[j].y;
            if (dx * dx + dy * dy < rocks[j].r * rocks[j].r) {
                s->on = false;
                boom(s->x, s->y, 4, false);
                break;
            }
        }
        if (!s->on) continue;
        if (s->owner == 0) {
            for (j = 0; j < MAX_EN; j++) {
                tank_t *t = &en[j];
                int dx, dy, r;
                if (!t->alive) continue;
                dx = px - PX(t->x); dy = py - PX(t->y); r = k_wid[t->kind] + 3;
                if (dx * dx + dy * dy < r * r) {
                    s->on = false;
                    shell_hit(t, s->ang, true);
                    break;
                }
            }
        } else if (player.alive) {
            int dx = px - PX(player.x), dy = py - PX(player.y);
            if (dx * dx + dy * dy < 10 * 10) {
                s->on = false;
                shell_hit(&player, s->ang, false);
            }
        }
    }
}

/* ---------- enemy AI ---------- */
static void update_enemy(tank_t *e) {
    int dx = PX(player.x) - PX(e->x), dy = PX(player.y) - PX(e->y);
    int dist2 = dx * dx + dy * dy;
    uint8_t want = angle_to(dx, dy);
    uint8_t target;
    int8_t d;
    bool slow = e->kind == K_HEAVY && (frame & 1);

    if (e->flash) e->flash--;
    if (e->reload) e->reload--;

    /* turret tracks the player */
    if (player.alive && !slow) {
        d = adiff(want, e->turret);
        if (d > 0) e->turret = (e->turret + 1) & 63;
        else if (d < 0) e->turret = (e->turret - 1) & 63;
    }

    /* pick a behaviour every so often */
    if (e->ai_timer) e->ai_timer--;
    else {
        e->ai_timer = (uint8_t)(30 + rnd(60));
        e->ai_dir = rnd(2) ? 1 : -1;
        if (dist2 > 120 * 120) e->ai_mode = 0;          /* close in */
        else e->ai_mode = (uint8_t)(1 + rnd(2));        /* 1 strafe, 2 hold */
        if (e->kind == K_HEAVY && e->ai_mode == 1) e->ai_mode = 2; /* heavies keep their front to you */
    }
    if (e->ai_mode == 0 && dist2 < 80 * 80) e->ai_mode = 1;

    target = e->ai_mode == 1 ? (uint8_t)((want + 16 * e->ai_dir) & 63) : want;
    if (!slow) {
        d = adiff(target, e->hull);
        if (d > 0) e->hull = (e->hull + 1) & 63;
        else if (d < 0) e->hull = (e->hull - 1) & 63;
    }
    d = adiff(target, e->hull);
    if (e->ai_mode != 2 && d > -4 && d < 4) {
        if (!move_tank(e, 1)) {
            e->ai_mode = 1;
            e->ai_dir = -e->ai_dir;
            e->ai_timer = 25;
        }
    }

    /* fire when lined up */
    if (player.alive && !e->reload && dist2 < 230 * 230) {
        d = adiff(want, e->turret);
        if (d >= -1 && d <= 1) fire(e, 1, (int8_t)(rnd(3) - 1));
    }
}

/* ---------- waves & world ---------- */
static void start_wave(void) {
    uint8_t i, n, tries;
    wave++;
    n = wave + 1 > MAX_EN ? MAX_EN : wave + 1;
    for (i = 0; i < n; i++) {
        tank_t *e = &en[i];
        int r = rnd(10);
        uint8_t kind = K_LIGHT;
        if (wave >= 4 && r < 3) kind = K_HEAVY;
        else if (wave >= 2 && r < 6) kind = K_MEDIUM;
        e->alive = false;
        for (tries = 0; tries < 40; tries++) {
            int x, y, dx, dy;
            switch (rnd(4)) {
                case 0:  x = 14;     y = TOP + 14 + rnd(H - TOP - 28); break;
                case 1:  x = W - 14; y = TOP + 14 + rnd(H - TOP - 28); break;
                case 2:  x = 14 + rnd(W - 28); y = TOP + 14; break;
                default: x = 14 + rnd(W - 28); y = H - 14; break;
            }
            dx = x - PX(player.x); dy = y - PX(player.y);
            if (dx * dx + dy * dy < 110 * 110 || blocked(x, y, e)) continue;
            setup_tank(e, kind);
            e->x = x << FP; e->y = y << FP;
            e->hull = e->turret = angle_to(-dx, -dy);
            e->reload = (uint8_t)(40 + rnd(60));
            break;
        }
    }
    banner_t = 60;
}

static void new_game(void) {
    uint8_t i;
    for (i = 0; i < MAX_EN; i++) en[i].alive = false;
    for (i = 0; i < MAX_SH; i++) shells[i].on = false;
    for (i = 0; i < MAX_PT; i++) parts[i].life = 0;
    for (i = 0; i < MAX_CR; i++) crates[i].on = false;
    for (i = 0; i < MAX_BM; i++) bombs[i].on = false;
    plane.on = false;
    n_scorch = 0;
    score = kills = 0;
    wave = 0;
    msg_t = banner_t = next_wave_t = death_t = 0;
    plane_t_left = 500;
    frame = 0;

    setup_tank(&player, K_PLAYER);
    player.x = (W / 2) << FP;
    player.y = ((H + TOP) / 2) << FP;
    player.hull = player.turret = 48;   /* facing up */
    player.reload = 0;

    for (i = 0; i < MAX_RK; i++) {
        int x, y, dx, dy;
        do {
            x = 30 + rnd(W - 60);
            y = TOP + 30 + rnd(H - TOP - 60);
            dx = x - W / 2; dy = y - (H + TOP) / 2;
        } while (dx * dx + dy * dy < 55 * 55);
        rocks[i].x = (int16_t)x;
        rocks[i].y = (int16_t)y;
        rocks[i].r = (uint8_t)(9 + rnd(9));
    }
    for (i = 0; i < MAX_SP; i++) {
        specks[i][0] = (int16_t)rnd(W - 3);
        specks[i][1] = (int16_t)(TOP + rnd(H - TOP - 2));
    }
    start_wave();
}

static void update_plane(void) {
    uint8_t i;
    if (!plane.on) {
        if (wave >= 2 && player.alive && --plane_t_left <= 0) {
            int dir = rnd(2) ? 1 : -1;
            int y = PX(player.y) + rnd(41) - 20;
            if (y < TOP + 20) y = TOP + 20;
            if (y > H - 20) y = H - 20;
            plane.on = true;
            plane.dx = dir * (3 << FP);
            plane.x = dir > 0 ? -(30 << FP) : (W + 30) << FP;
            plane.y = (int16_t)y;
            plane.dropped = 0;
            plane_t_left = 450 + rnd(300) - (wave > 10 ? 200 : wave * 20);
            say("AIR RAID!", C_RED);
        }
        return;
    }
    plane.x += plane.dx;
    {
        int px = PX(plane.x), dx = px - PX(player.x);
        if (dx < 0) dx = -dx;
        if (dx < 40 && plane.dropped < 3 && frame % 6 == 0) {
            for (i = 0; i < MAX_BM; i++) {
                if (bombs[i].on) continue;
                bombs[i].x = (int16_t)px;
                bombs[i].y = plane.y;
                bombs[i].timer = 35;
                bombs[i].on = true;
                plane.dropped++;
                break;
            }
        }
        if (px < -40 || px > W + 40) plane.on = false;
    }
}

static void bomb_damage(tank_t *t, int bx, int by) {
    int dx, dy;
    if (!t->alive) return;
    dx = PX(t->x) - bx; dy = PX(t->y) - by;
    if (dx * dx + dy * dy < 20 * 20) {
        if (t == &player) say("BOMB HIT", C_RED);
        damage(t, 3);
    }
}

static void update_bombs(void) {
    uint8_t i, j;
    for (i = 0; i < MAX_BM; i++) {
        spot_t *b = &bombs[i];
        if (!b->on || --b->timer) continue;
        b->on = false;
        boom(b->x << FP, b->y << FP, 16, true);
        bomb_damage(&player, b->x, b->y);
        for (j = 0; j < MAX_EN; j++) bomb_damage(&en[j], b->x, b->y);
    }
}

static void update_crates(void) {
    uint8_t i;
    for (i = 0; i < MAX_CR; i++) {
        spot_t *c = &crates[i];
        int dx, dy;
        if (!c->on) continue;
        if (--c->timer == 0) { c->on = false; continue; }
        dx = c->x - PX(player.x); dy = c->y - PX(player.y);
        if (player.alive && dx * dx + dy * dy < 14 * 14) {
            player.hp += 4;
            if (player.hp > player.maxhp) player.hp = player.maxhp;
            c->on = false;
            say("REPAIRED +4", C_GREEN);
        }
    }
}

/* returns true to keep playing */
static bool update_player(void) {
    uint8_t k1, k2, k3, k6, k7;
    kb_Scan();
    k1 = (uint8_t)kb_Data[1]; k2 = (uint8_t)kb_Data[2]; k3 = (uint8_t)kb_Data[3];
    k6 = (uint8_t)kb_Data[6]; k7 = (uint8_t)kb_Data[7];
    if (k6 & kb_Clear) return false;
    if (!player.alive) return true;

    if (player.flash) player.flash--;
    if (player.reload) player.reload--;
    if (k7 & kb_Left)  player.hull = (player.hull - 1) & 63;
    if (k7 & kb_Right) player.hull = (player.hull + 1) & 63;
    if (k7 & kb_Up)    move_tank(&player, 1);
    else if (k7 & kb_Down) move_tank(&player, -1);
    if (k2 & kb_Alpha)    player.turret = (player.turret - 1) & 63;
    if (k3 & kb_GraphVar) player.turret = (player.turret + 1) & 63;
    if ((k1 & kb_2nd) && !player.reload) fire(&player, 0, 0);
    return true;
}

/* ---------- drawing ---------- */
static void quad(int cx, int cy, int fx, int fy, int len, int wid) {
    int rx = -fy, ry = fx;
    int ax = cx + ((fx * len + rx * wid) >> 8), ay = cy + ((fy * len + ry * wid) >> 8);
    int bx = cx + ((fx * len - rx * wid) >> 8), by = cy + ((fy * len - ry * wid) >> 8);
    int qx = cx + ((-fx * len - rx * wid) >> 8), qy = cy + ((-fy * len - ry * wid) >> 8);
    int dx = cx + ((-fx * len + rx * wid) >> 8), dy = cy + ((-fy * len + ry * wid) >> 8);
    gfx_FillTriangle(ax, ay, bx, by, qx, qy);
    gfx_FillTriangle(ax, ay, qx, qy, dx, dy);
}

static void draw_tank(const tank_t *t) {
    int cx = PX(t->x), cy = PX(t->y);
    int len = k_len[t->kind], wid = k_wid[t->kind];
    int fx = COS(t->hull), fy = SIN(t->hull);
    int tx = cx + ((COS(t->turret) * (len + 4)) >> 8);
    int ty = cy + ((SIN(t->turret) * (len + 4)) >> 8);
    uint8_t tread, body, tur;

    if (t->kind == K_PLAYER) { tread = C_PTREAD; body = C_PHULL; tur = C_PTUR; }
    else if (t->kind == K_HEAVY) { tread = C_ETREAD; body = C_HHULL; tur = C_EHULL; }
    else if (t->kind == K_LIGHT) { tread = C_ETREAD; body = C_ETUR; tur = C_GREY; }
    else { tread = C_ETREAD; body = C_EHULL; tur = C_ETUR; }
    if (t->flash & 2) tread = body = tur = C_WHITE;

    gfx_SetColor(tread);
    quad(cx, cy, fx, fy, len, wid);
    gfx_SetColor(body);
    quad(cx, cy, fx, fy, len - 1, wid - 3);
    gfx_SetColor(tur);
    gfx_FillCircle(cx, cy, wid - 3);
    gfx_SetColor(tread);
    gfx_Line(cx, cy, tx, ty);
    gfx_Line(cx + 1, cy, tx + 1, ty);
    gfx_Line(cx, cy + 1, tx, ty + 1);
}

static void draw_plane(void) {
    int px = PX(plane.x), py = plane.y, s = plane.dx > 0 ? 1 : -1, pass;
    for (pass = 0; pass < 2; pass++) {
        int ox = pass ? 0 : 10, oy = pass ? 0 : 12;
        gfx_SetColor(pass ? C_PLANE : C_SHADOW);
        gfx_FillRectangle(px - 12 + ox, py - 2 + oy, 24, 5);          /* fuselage */
        gfx_FillRectangle(px - 3 + ox, py - 15 + oy, 7, 31);          /* wings */
        gfx_FillRectangle(px - 12 * s - 1 + ox, py - 6 + oy, 3, 13);  /* tail */
    }
    gfx_SetColor(C_BLACK);
    gfx_FillRectangle(px + 11 * s - 1, py - 1, 3, 3);                  /* nose */
}

static void bar(int x, int y, int w, int val, int max, uint8_t col) {
    gfx_SetColor(C_BLACK);
    gfx_FillRectangle(x, y, w, 7);
    if (val > 0) {
        gfx_SetColor(col);
        gfx_FillRectangle(x + 1, y + 1, (w - 2) * val / max, 5);
    }
}

static void center_text(const char *s, int y) {
    gfx_PrintStringXY(s, (W - (int)gfx_GetStringWidth(s)) / 2, y);
}

static void draw_hud(void) {
    gfx_SetColor(C_HUD);
    gfx_FillRectangle(0, 0, W, TOP);
    gfx_SetTextFGColor(C_WHITE);
    gfx_PrintStringXY("HP", 4, 4);
    bar(22, 4, 60, player.hp, player.maxhp, player.hp > 4 ? C_GREEN : C_RED);
    gfx_PrintStringXY("GUN", 92, 4);
    if (player.reload) bar(120, 4, 40, player.reload_max - player.reload, player.reload_max, C_ORG);
    else { gfx_SetTextFGColor(C_GREEN); gfx_PrintStringXY("READY", 120, 4); gfx_SetTextFGColor(C_WHITE); }
    gfx_PrintStringXY("WAVE", 172, 4);
    gfx_PrintInt(wave, 1);
    gfx_PrintStringXY("SCORE", 230, 4);
    gfx_PrintUInt(score, 1);
}

static void draw_world(void) {
    uint8_t i;
    gfx_FillScreen(C_GROUND);
    gfx_SetColor(C_SPECK);
    for (i = 0; i < MAX_SP; i++) gfx_FillRectangle(specks[i][0], specks[i][1], 3, 2);
    gfx_SetColor(C_SCORCH);
    for (i = 0; i < MAX_SC && i < n_scorch; i++) gfx_FillCircle(scorch[i][0], scorch[i][1], 9);

    for (i = 0; i < MAX_RK; i++) {
        gfx_SetColor(C_SHADOW);
        gfx_FillCircle(rocks[i].x + 3, rocks[i].y + 3, rocks[i].r);
        gfx_SetColor(C_ROCK);
        gfx_FillCircle(rocks[i].x, rocks[i].y, rocks[i].r);
        gfx_SetColor(C_ROCKL);
        gfx_FillCircle(rocks[i].x - rocks[i].r / 3, rocks[i].y - rocks[i].r / 3, rocks[i].r / 3);
    }
    for (i = 0; i < MAX_CR; i++) {
        spot_t *c = &crates[i];
        if (!c->on || (c->timer < 60 && (frame & 4))) continue;
        gfx_SetColor(C_CRATE);
        gfx_FillRectangle(c->x - 5, c->y - 5, 11, 11);
        gfx_SetColor(C_GREEN);
        gfx_FillRectangle(c->x - 1, c->y - 4, 3, 9);
        gfx_FillRectangle(c->x - 4, c->y - 1, 9, 3);
    }
    gfx_SetColor(C_RED);
    for (i = 0; i < MAX_BM; i++)
        if (bombs[i].on) gfx_Circle(bombs[i].x, bombs[i].y, 4 + bombs[i].timer / 2);

    for (i = 0; i < MAX_EN; i++) if (en[i].alive) draw_tank(&en[i]);
    if (player.alive) draw_tank(&player);

    for (i = 0; i < MAX_SH; i++) {
        shell_t *s = &shells[i];
        if (!s->on) continue;
        gfx_SetColor(s->owner ? C_ORG : C_YEL);
        gfx_FillRectangle(PX(s->x) - 1, PX(s->y) - 1, 3, 3);
    }
    for (i = 0; i < MAX_PT; i++) {
        part_t *p = &parts[i];
        if (!p->life) continue;
        gfx_SetColor(p->color);
        gfx_FillRectangle(PX(p->x), PX(p->y), p->life > 12 ? 3 : 2, p->life > 12 ? 3 : 2);
    }
    if (plane.on) draw_plane();

    draw_hud();
    if (msg_t) {
        gfx_SetTextFGColor(msg_col);
        center_text(msg, TOP + 6);
    }
    if (banner_t) {
        gfx_SetTextFGColor(C_WHITE);
        gfx_SetTextScale(2, 2);
        gfx_PrintStringXY("WAVE ", 120, 104);
        gfx_PrintInt(wave, 1);
        gfx_SetTextScale(1, 1);
    }
}

/* ---------- screens ---------- */
static void wait_release(void) {
    do { kb_Scan(); } while (kb_Data[1] || kb_Data[6]);
}

/* waits for [2nd] (true) or [clear] (false) */
static bool wait_choice(void) {
    wait_release();
    for (;;) {
        kb_Scan();
        if (kb_Data[1] & kb_2nd) return true;
        if (kb_Data[6] & kb_Clear) return false;
    }
}

static void save_hiscore(void) {
    ti_var_t f = ti_Open("STLFRTHS", "w");
    if (!f) return;
    ti_Write(&hiscore, sizeof hiscore, 1, f);
    ti_SetArchiveStatus(true, f);   /* survive RAM resets */
    ti_Close(f);
}

static void load_hiscore(void) {
    ti_var_t f = ti_Open("STLFRTHS", "r");
    hiscore = 0;
    if (!f) return;
    if (ti_Read(&hiscore, sizeof hiscore, 1, f) != 1) hiscore = 0;
    ti_Close(f);
}

static bool title_screen(void) {
    tank_t demo;
    gfx_FillScreen(C_GROUND);
    setup_tank(&demo, K_PLAYER);
    demo.x = 160 << FP; demo.y = 86 << FP; demo.hull = 0; demo.turret = 60;
    gfx_SetColor(C_SHADOW);
    gfx_FillCircle(163, 89, 14);
    draw_tank(&demo);
    gfx_SetTextFGColor(C_BLACK);
    gfx_SetTextScale(3, 3);
    center_text("STEEL FRONT", 30);
    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(C_HUD);
    center_text("ARROWS: DRIVE AND TURN HULL", 118);
    center_text("ALPHA / XT0N: TURN TURRET", 130);
    center_text("2ND: FIRE    CLEAR: QUIT", 142);
    center_text("FRONT ARMOR IS THICK - FLANK THEM!", 160);
    center_text("GRAB GREEN CRATES TO REPAIR", 172);
    gfx_SetTextFGColor(C_BLACK);
    center_text("PRESS 2ND TO START", 196);
    gfx_SetTextXY(120, 214);
    gfx_PrintString("HIGH SCORE ");
    gfx_PrintUInt(hiscore, 1);
    gfx_SwapDraw();
    return wait_choice();
}

static bool game_over_screen(void) {
    bool best = false;
    if (score > hiscore) { hiscore = score; best = true; save_hiscore(); }
    draw_world();
    gfx_SetColor(C_HUD);
    gfx_FillRectangle(60, 70, 200, 110);
    gfx_SetTextFGColor(C_RED);
    gfx_SetTextScale(2, 2);
    center_text("GAME OVER", 80);
    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(C_WHITE);
    gfx_SetTextXY(90, 104); gfx_PrintString("SCORE  "); gfx_PrintUInt(score, 1);
    gfx_SetTextXY(90, 116); gfx_PrintString("WAVE   "); gfx_PrintInt(wave, 1);
    gfx_SetTextXY(90, 128); gfx_PrintString("KILLS  "); gfx_PrintUInt(kills, 1);
    gfx_SetTextFGColor(best ? C_YEL : C_WHITE);
    gfx_SetTextXY(90, 140); gfx_PrintString(best ? "NEW HIGH SCORE!" : "BEST   ");
    if (!best) gfx_PrintUInt(hiscore, 1);
    gfx_SetTextFGColor(C_GREY);
    center_text("2ND: AGAIN   CLEAR: QUIT", 162);
    gfx_SwapDraw();
    return wait_choice();
}

static void play(void) {
    new_game();
    wait_release();
    for (;;) {
        clock_t start = clock();
        uint8_t i, alive = 0;

        frame++;
        if (!update_player()) break;
        for (i = 0; i < MAX_EN; i++) if (en[i].alive) { update_enemy(&en[i]); alive++; }
        update_shells();
        update_plane();
        update_bombs();
        update_crates();
        update_parts();
        if (msg_t) msg_t--;
        if (banner_t) banner_t--;

        if (death_t && --death_t == 0) break;
        if (!alive && player.alive) {
            if (!next_wave_t) {
                next_wave_t = 75;
                if (wave) { score += wave * 50; say("WAVE CLEAR", C_GREEN); }
            } else if (--next_wave_t == 0) start_wave();
        }

        draw_world();
        gfx_SwapDraw();
        while (clock() - start < CLOCKS_PER_SEC / 30) ;
    }
}

int main(void) {
    uint8_t i;
    srand((unsigned int)rtc_Time());
    load_hiscore();
    gfx_Begin();
    for (i = 0; i < C_COUNT; i++)
        gfx_palette[i] = gfx_RGBTo1555(pal_rgb[i][0], pal_rgb[i][1], pal_rgb[i][2]);
    gfx_SetDrawBuffer();
    gfx_SetTextTransparentColor(C_TRANS);
    gfx_SetTextBGColor(C_TRANS);

    if (title_screen()) {
        do play(); while (game_over_screen());
    }
    gfx_End();
    return 0;
}
