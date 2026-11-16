/*
 * Zelda's Errands - village decorations.
 *
 * One actor draws every decoration as plain vertex-coloured geometry: no
 * textures to load and nothing to clash with the scene. It sits at the world
 * origin with scale 1, so the vertices below are world coordinates.
 *
 *   - candy-striped festival poles, each topped with a paper lantern whose
 *     glow is a real point light
 *   - bunting strung pole to pole, triangular flags gently fluttering
 *   - a maypole in the plaza with ribbons to the ground
 *
 * Every pole and the maypole is snapped to the floor at spawn, so heights
 * never need to be guessed.
 */

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/ActorDB.h"
#include "ZEParty.h"

#include <cmath>
#include <vector>
#include <algorithm>
#include <cstring>

extern "C" {
#include "macros.h"
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace ZEParty {

struct Pole {
    f32 x, z;
    u8 lantern; // index into kLanternColors
};

// Festival poles. Positions are checked against screenshots of the village.
static const Pole kPoles[] = {
    // the meadow inside the gate, either side of the path to Zelda
    { -2080.0f, 880.0f, 0 },  { -2080.0f, 1230.0f, 1 }, { -1880.0f, 760.0f, 2 }, { -1880.0f, 1130.0f, 3 },
    // the plaza
    { -1000.0f, 250.0f, 4 },  { -1000.0f, 650.0f, 5 },  { -150.0f, 200.0f, 0 },  { -150.0f, 650.0f, 1 },
};
// Bunting runs between pairs of poles (indices into kPoles).
static const u8 kBunting[][2] = {
    { 0, 1 }, { 2, 3 }, { 0, 2 }, { 1, 3 }, { 4, 5 }, { 6, 7 }, { 4, 6 }, { 5, 7 },
};

static const Vec3f kMaypole = { -580.0f, 0.0f, 430.0f };

static const Color_RGB8 kFlagColors[] = {
    { 235, 60, 70 }, { 255, 200, 40 }, { 60, 140, 240 }, { 255, 120, 190 }, { 80, 200, 90 }, { 160, 90, 230 },
};
static const Color_RGB8 kLanternColors[] = {
    { 255, 120, 160 }, { 255, 210, 90 }, { 140, 200, 255 }, { 190, 140, 255 }, { 255, 170, 80 }, { 140, 255, 160 },
};

static const f32 kPoleHeight = 210.0f;
static const f32 kPoleHalf = 3.5f;

typedef struct {
    Actor actor;
    f32 poleY[ARRAY_COUNT(kPoles)];
    f32 maypoleY;
    LightNode* lights[ARRAY_COUNT(kPoles)];
    LightInfo lightInfo[ARRAY_COUNT(kPoles)];
    ColliderCylinder collider;
    s16 timer;
} ZEDecor;

s16 sDecorId = -1;

// --- a tiny triangle batcher ------------------------------------------------
struct Mesh {
    std::vector<Vtx> v;
    void Tri(const Vec3f& a, const Vec3f& b, const Vec3f& c, Color_RGB8 ca, Color_RGB8 cb, Color_RGB8 cc) {
        Push(a, ca);
        Push(b, cb);
        Push(c, cc);
    }
    void Quad(const Vec3f& a, const Vec3f& b, const Vec3f& c, const Vec3f& d, Color_RGB8 col) {
        Tri(a, b, c, col, col, col);
        Tri(a, c, d, col, col, col);
    }
    void Push(const Vec3f& p, Color_RGB8 col) {
        Vtx x;
        x.v.ob[0] = (s16)p.x;
        x.v.ob[1] = (s16)p.y;
        x.v.ob[2] = (s16)p.z;
        x.v.flag = 0;
        x.v.tc[0] = x.v.tc[1] = 0;
        x.v.cn[0] = col.r;
        x.v.cn[1] = col.g;
        x.v.cn[2] = col.b;
        x.v.cn[3] = 255;
        v.push_back(x);
    }
    // Copies the vertices into self frame's graphics memory and emits them in
    // 30-vertex batches (10 triangles), the RSP vertex cache's safe size.
    Gfx* Emit(GraphicsContext* gfxCtx, Gfx* gfx) {
        if (v.empty()) {
            return gfx;
        }
        Vtx* buf = (Vtx*)Graph_Alloc(gfxCtx, v.size() * sizeof(Vtx));
        memcpy(buf, v.data(), v.size() * sizeof(Vtx));
        for (size_t base = 0; base < v.size(); base += 30) {
            size_t n = std::min<size_t>(30, v.size() - base);
            gSPVertex(gfx++, &buf[base], n, 0);
            for (size_t t = 0; t + 2 < n; t += 6) {
                if (t + 5 < n) {
                    gSP2Triangles(gfx++, t, t + 1, t + 2, 0, t + 3, t + 4, t + 5, 0);
                } else {
                    gSP1Triangle(gfx++, t, t + 1, t + 2, 0);
                }
            }
        }
        return gfx;
    }
};

static Color_RGB8 Shade(Color_RGB8 c, f32 k) {
    return { (u8)std::min(255.0f, c.r * k), (u8)std::min(255.0f, c.g * k), (u8)std::min(255.0f, c.b * k) };
}

// A square prism from y0 to y1 with alternating stripes.
static void StripedPost(Mesh& m, f32 x, f32 z, f32 y0, f32 y1, f32 half, Color_RGB8 a, Color_RGB8 b, int stripes) {
    static const f32 sx[4] = { -1, 1, 1, -1 };
    static const f32 sz[4] = { -1, -1, 1, 1 };
    static const f32 light[4] = { 0.75f, 1.0f, 0.85f, 0.6f };
    for (int s = 0; s < stripes; s++) {
        f32 ya = y0 + (y1 - y0) * s / stripes, yb = y0 + (y1 - y0) * (s + 1) / stripes;
        for (int f = 0; f < 4; f++) {
            int g = (f + 1) & 3;
            Color_RGB8 c = Shade(((s + f) & 1) ? a : b, light[f]);
            m.Quad({ x + sx[f] * half, ya, z + sz[f] * half }, { x + sx[g] * half, ya, z + sz[g] * half },
                   { x + sx[g] * half, yb, z + sz[g] * half }, { x + sx[f] * half, yb, z + sz[f] * half }, c);
        }
    }
}

// A paper lantern: a box with darker caps.
static void Lantern(Mesh& m, f32 x, f32 y, f32 z, Color_RGB8 c) {
    const f32 w = 9.0f, h = 13.0f;
    StripedPost(m, x, z, y - h, y + h, w, c, Shade(c, 0.9f), 1);
    Color_RGB8 cap = Shade(c, 0.45f);
    m.Quad({ x - w, y + h, z - w }, { x + w, y + h, z - w }, { x + w, y + h, z + w }, { x - w, y + h, z + w }, cap);
    m.Quad({ x - w, y - h, z - w }, { x + w, y - h, z - w }, { x + w, y - h, z + w }, { x - w, y - h, z + w }, cap);
}

// Bunting between two points: a sagging string with hanging flags.
static void Bunting(Mesh& m, Vec3f a, Vec3f b, s16 time) {
    f32 dx = b.x - a.x, dz = b.z - a.z;
    f32 len = sqrtf(dx * dx + dz * dz);
    if (len < 1.0f) {
        return;
    }
    // horizontal unit vectors along and across the string
    f32 ux = dx / len, uz = dz / len;
    f32 nx = -uz, nz = ux;
    f32 sag = 0.10f * len;
    int flags = std::max(3, (int)(len / 26.0f));
    auto at = [&](f32 t) -> Vec3f {
        return { a.x + dx * t, a.y + (b.y - a.y) * t - sag * 4.0f * t * (1.0f - t), a.z + dz * t };
    };
    Color_RGB8 cord = { 245, 240, 225 };
    const int cordSegs = 12;
    for (int i = 0; i < cordSegs; i++) {
        Vec3f p = at((f32)i / cordSegs), q = at((f32)(i + 1) / cordSegs);
        m.Quad(p, q, { q.x, q.y - 1.6f, q.z }, { p.x, p.y - 1.6f, p.z }, cord);
    }
    for (int i = 0; i < flags; i++) {
        f32 t0 = (i + 0.15f) / flags, t1 = (i + 0.85f) / flags;
        Vec3f p = at(t0), q = at(t1), mid = at((t0 + t1) * 0.5f);
        f32 flutter = 4.0f * Math_SinS(time * 0x500 + i * 0x2400);
        Vec3f tip = { mid.x + nx * flutter, mid.y - 22.0f, mid.z + nz * flutter };
        Color_RGB8 c = kFlagColors[i % ARRAY_COUNT(kFlagColors)];
        m.Tri(p, q, tip, c, c, Shade(c, 0.8f));
    }
}

static void Maypole(Mesh& m, f32 x, f32 y, f32 z, s16 time) {
    const f32 h = 360.0f;
    const int sides = 8, levels = 18;
    const f32 r = 6.0f;
    Color_RGB8 white = { 250, 245, 240 }, pink = { 255, 105, 170 };
    // a spiral-striped octagonal pole
    for (int l = 0; l < levels; l++) {
        f32 ya = y + h * l / levels, yb = y + h * (l + 1) / levels;
        for (int s = 0; s < sides; s++) {
            s16 a0 = (s16)(s * 0x10000 / sides), a1 = (s16)((s + 1) * 0x10000 / sides);
            f32 lightK = 0.65f + 0.35f * (0.5f + 0.5f * Math_SinS(a0));
            Color_RGB8 c = Shade(((l + s) / 2) & 1 ? pink : white, lightK);
            m.Quad({ x + r * Math_SinS(a0), ya, z + r * Math_CosS(a0) }, { x + r * Math_SinS(a1), ya, z + r * Math_CosS(a1) },
                   { x + r * Math_SinS(a1), yb, z + r * Math_CosS(a1) }, { x + r * Math_SinS(a0), yb, z + r * Math_CosS(a0) },
                   c);
        }
    }
    // a golden crown
    Color_RGB8 gold = { 255, 205, 60 };
    for (int s = 0; s < sides; s++) {
        s16 a0 = (s16)(s * 0x10000 / sides), a1 = (s16)((s + 1) * 0x10000 / sides);
        m.Tri({ x + 18.0f * Math_SinS(a0), y + h, z + 18.0f * Math_CosS(a0) },
              { x + 18.0f * Math_SinS(a1), y + h, z + 18.0f * Math_CosS(a1) }, { x, y + h + 28.0f, z }, gold, gold,
              Shade(gold, 1.15f));
    }
    // ribbons from the crown to the ground, swaying a little
    const int ribbons = 12;
    for (int i = 0; i < ribbons; i++) {
        s16 ang = (s16)(i * 0x10000 / ribbons + time * 0x40);
        f32 sway = 8.0f * Math_SinS(time * 0x300 + i * 0x1555);
        f32 bx = x + 175.0f * Math_SinS(ang), bz = z + 175.0f * Math_CosS(ang);
        f32 px = -Math_CosS(ang), pz = Math_SinS(ang); // across the ribbon
        Vec3f top = { x + 6.0f * Math_SinS(ang), y + h - 6.0f, z + 6.0f * Math_CosS(ang) };
        Vec3f b0 = { bx - px * (3.5f + sway * 0.15f), y + 4.0f, bz - pz * 3.5f };
        Vec3f b1 = { bx + px * 3.5f, y + 4.0f, bz + pz * (3.5f + sway * 0.15f) };
        Color_RGB8 c = kFlagColors[i % ARRAY_COUNT(kFlagColors)];
        m.Tri(top, b0, b1, Shade(c, 1.1f), c, c);
    }
}

static ColliderCylinderInit sMaypoleCyl = {
    { COLTYPE_NONE, AT_NONE, AC_NONE, OC1_ON | OC1_TYPE_ALL, OC2_TYPE_2, COLSHAPE_CYLINDER },
    { ELEMTYPE_UNK0, { 0, 0, 0 }, { 0, 0, 0 }, TOUCH_NONE, BUMP_NONE, OCELEM_ON },
    { 12, 300, 0, { 0, 0, 0 } },
};

static f32 FloorAt(PlayState* play, f32 x, f32 yGuess, f32 z) {
    Vec3f probe = { x, yGuess + 150.0f, z };
    CollisionPoly* poly;
    f32 y = BgCheck_EntityRaycastFloor1(&play->colCtx, &poly, &probe);
    return y > BGCHECK_Y_MIN ? y : yGuess;
}

static void Decor_Init(Actor* thisx, PlayState* play) {
    ZEDecor* self = (ZEDecor*)thisx;
    thisx->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
    Actor_SetScale(thisx, 1.0f);
    thisx->world.pos = thisx->home.pos = { 0, 0, 0 };
    thisx->shape.rot = { 0, 0, 0 };
    for (size_t i = 0; i < ARRAY_COUNT(kPoles); i++) {
        // the meadow is around y=100-140, the plaza y=0; probe from well above both
        self->poleY[i] = FloorAt(play, kPoles[i].x, 300.0f, kPoles[i].z);
        const Color_RGB8& c = kLanternColors[kPoles[i].lantern];
        Lights_PointGlowSetInfo(&self->lightInfo[i], (s16)kPoles[i].x, (s16)(self->poleY[i] + kPoleHeight + 16.0f),
                                (s16)kPoles[i].z, c.r, c.g, c.b, 45);
        self->lights[i] = CVarGetInteger(CVAR_ENHANCEMENT("ZE_TestNoLights"), 0)
                              ? NULL
                              : LightContext_InsertLight(play, &play->lightCtx, &self->lightInfo[i]);
    }
    self->maypoleY = FloorAt(play, kMaypole.x, 100.0f, kMaypole.z);
    Collider_InitCylinder(play, &self->collider);
    Collider_SetCylinder(play, &self->collider, thisx, &sMaypoleCyl);
    thisx->colChkInfo.mass = MASS_IMMOVABLE;
    self->timer = 0;
}

static void Decor_Destroy(Actor* thisx, PlayState* play) {
    ZEDecor* self = (ZEDecor*)thisx;
    for (size_t i = 0; i < ARRAY_COUNT(kPoles); i++) {
        if (self->lights[i] != NULL) {
            LightContext_RemoveLight(play, &play->lightCtx, self->lights[i]);
        }
    }
    Collider_DestroyCylinder(play, &self->collider);
}

static void Decor_Update(Actor* thisx, PlayState* play) {
    ZEDecor* self = (ZEDecor*)thisx;
    self->timer++;
    // the maypole is solid; the collider lives at the pole, not at the actor
    self->collider.dim.pos.x = (s16)kMaypole.x;
    self->collider.dim.pos.y = (s16)self->maypoleY;
    self->collider.dim.pos.z = (s16)kMaypole.z;
    CollisionCheck_SetOC(play, &play->colChkCtx, &self->collider.base);
}

// Cheap visibility test: skip anything behind the camera or far away. The
// meadow and the plaza are never on screen together, so this roughly halves
// the vertices sent each frame - which matters, because the renderer
// transforms every vertex on the handheld's CPU.
static bool Visible(PlayState* play, f32 x, f32 y, f32 z, f32 radius) {
    const Vec3f& eye = play->view.eye;
    const Vec3f& at = play->view.lookAt;
    f32 fx = at.x - eye.x, fy = at.y - eye.y, fz = at.z - eye.z;
    f32 flen = sqrtf(fx * fx + fy * fy + fz * fz);
    f32 dx = x - eye.x, dy = y - eye.y, dz = z - eye.z;
    f32 dist = sqrtf(dx * dx + dy * dy + dz * dz);
    if (dist - radius > 2400.0f) {
        return false;
    }
    if (flen < 1.0f) {
        return true;
    }
    return (dx * fx + dy * fy + dz * fz) / flen > -radius;
}

static void Decor_Draw(Actor* thisx, PlayState* play) {
    ZEDecor* self = (ZEDecor*)thisx;
    static Mesh mesh; // reused every frame to avoid reallocating
    mesh.v.clear();

    Color_RGB8 red = { 230, 50, 60 }, white = { 250, 248, 240 };
    for (size_t i = 0; i < ARRAY_COUNT(kPoles); i++) {
        const Pole& p = kPoles[i];
        f32 y = self->poleY[i];
        if (!Visible(play, p.x, y + kPoleHeight * 0.5f, p.z, kPoleHeight * 0.6f)) {
            continue;
        }
        StripedPost(mesh, p.x, p.z, y, y + kPoleHeight, kPoleHalf, red, white, 9);
        Lantern(mesh, p.x, y + kPoleHeight + 16.0f, p.z, kLanternColors[p.lantern]);
    }
    for (size_t i = 0; i < ARRAY_COUNT(kBunting); i++) {
        const Pole& a = kPoles[kBunting[i][0]];
        const Pole& b = kPoles[kBunting[i][1]];
        f32 mx = (a.x + b.x) * 0.5f, mz = (a.z + b.z) * 0.5f;
        f32 half = 0.5f * sqrtf((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z));
        if (!Visible(play, mx, self->poleY[kBunting[i][0]] + kPoleHeight, mz, half + 30.0f)) {
            continue;
        }
        Bunting(mesh, { a.x, self->poleY[kBunting[i][0]] + kPoleHeight - 6.0f, a.z },
                { b.x, self->poleY[kBunting[i][1]] + kPoleHeight - 6.0f, b.z }, self->timer + i * 7);
    }
    if (Visible(play, kMaypole.x, self->maypoleY + 180.0f, kMaypole.z, 260.0f)) {
        Maypole(mesh, kMaypole.x, self->maypoleY, kMaypole.z, self->timer);
    }

    if (self->timer == 30) {
        s16 lo[3] = { 32767, 32767, 32767 }, hi[3] = { -32768, -32768, -32768 };
        for (const Vtx& v : mesh.v) {
            for (int k = 0; k < 3; k++) {
                lo[k] = std::min(lo[k], v.v.ob[k]);
                hi[k] = std::max(hi[k], v.v.ob[k]);
            }
        }
        LUSLOG_INFO("ZEDECOR %d verts, x %d..%d y %d..%d z %d..%d", (int)mesh.v.size(), lo[0], hi[0], lo[1], hi[1],
                    lo[2], hi[2]);
        for (size_t i = 0; i < ARRAY_COUNT(kPoles); i++) {
            LUSLOG_INFO("ZEDECOR pole %d at %.0f,%.0f,%.0f", (int)i, kPoles[i].x, self->poleY[i], kPoles[i].z);
        }
    }

    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPTexture(POLY_OPA_DISP++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    POLY_OPA_DISP = mesh.Emit(play->state.gfxCtx, POLY_OPA_DISP);
    gSPSetGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
    CLOSE_DISPS(play->state.gfxCtx);
}

void Decor_Register() {
    ActorDBInit decor = {
        "ZE_Decor",
        "Zelda's Errands decorations",
        -1,
        ACTORCAT_ITEMACTION,
        ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED,
        OBJECT_GAMEPLAY_KEEP,
        sizeof(ZEDecor),
        (ActorFunc)Decor_Init,
        (ActorFunc)Decor_Destroy,
        (ActorFunc)Decor_Update,
        (ActorFunc)Decor_Draw,
        nullptr,
    };
    ActorDB::Instance->AddEntry(decor);
    sDecorId = (s16)ActorDB::Instance->RetrieveId("ZE_Decor");
}

s16 DecorActorId() {
    return sDecorId;
}

} // namespace ZEParty
