/*
	C-Dogs SDL
	A port of the legendary (and fun) action/arcade cdogs.

	Copyright (c) 2026 Felipe Menegaz
	All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	Redistributions of source code must retain the above copyright notice, this
	list of conditions and the following disclaimer.
	Redistributions in binary form must reproduce the above copyright notice,
	this list of conditions and the following disclaimer in the documentation
	and/or other materials provided with the distribution.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
	ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
	LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
	CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
	SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
	CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
	POSSIBILITY OF SUCH DAMAGE.
*/
#include "draw/draw_25d.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "actors.h"
#include "aheasing/easing.h"
#include "config.h"
#include "draw/draw.h"
#include "draw/draw_perspective.h"
#include "grafx.h"
#include "log.h"
#include "los.h"
#include "map.h"
#include "objs.h"
#include "particle.h"
#include "thing.h"

#if SDL_VERSION_ATLEAST(2, 0, 18)
#define HAS_GEOMETRY 1
#else
#define HAS_GEOMETRY 0
#endif

// Things are first drawn with their classic code into cells of an atlas
// texture, then each cell is drawn as a billboard (or a ground decal).
// Sizes are logical pixels; the texture has render scale times as many.
#define ATLAS_SIZE 1024
#define CELL_W 64
#define CELL_H 64
// Where the thing's position sits inside its cell
#define CELL_ANCHOR_X 32
#define CELL_ANCHOR_Y 44
#define CELLS_PER_ROW (ATLAS_SIZE / CELL_W)
#define MAX_CELLS (CELLS_PER_ROW * (ATLAS_SIZE / CELL_H))

// Wall height in classic screen pixels, same as the classic wall offset
#define WALL_H ((float)-WALL_OFFSET_Y)
// Shading per face, so walls read as solid boxes
#define SHADE_FRONT 0.8f
#define SHADE_SIDE 0.62f
// Quads on the ground and on walls are split so that the affine texture
// mapping of each triangle stays close to true perspective
#define QUAD_SUBDIV 2
// Never draw more tiles than this in either direction
#define MAX_RANGE_TILES 96

typedef struct
{
	const Thing *t;
	int row;
	int cell; // -1: no room left in the atlas, drawn flat instead
} ThingEntry;
typedef struct
{
	const Tile *tile;
	struct vec2i pos;
} DoorEntry;

static SDL_Texture *sAtlas = NULL;
static int sAtlasScale = 0;
static bool sAtlasFailed = false;
static CArray sBelow;  // of ThingEntry
static CArray sNormal; // of ThingEntry
static CArray sAbove;  // of ThingEntry
static CArray sDoors;  // of DoorEntry
static bool sArraysInit = false;

// Lighting (Graphics.Lights): a cool, dim ambient and warm point lights from
// the players, shots, muzzle flashes, explosions and fire, applied as vertex
// colours. Vertex colours can only darken the art, so the ambient is below 1
// and lights bring the colour back up to the original. Lights with Glow also
// add a soft splash of light on the floor (additive), which is what makes a
// shot or an explosion read on a dark floor.
typedef struct
{
	struct vec2 Pos;
	float Radius;
	float R, G, B;
	float Glow;
} Light;
#define MAX_LIGHTS 128
static Light sLights[MAX_LIGHTS];
static int sNumLights = 0;
static bool sLighting = false;
static const float sAmbient[3] = {0.62f, 0.64f, 0.78f};
// Under lighting, fogged (seen but out of sight) tiles keep this much light
#define FOG_LIT 0.5f
#define GLOW_SIZE 64
static SDL_Texture *sGlow = NULL;
static bool sGlowFailed = false;
// Set while drawing things that must keep their own colour
static bool sUnlit = false;

bool Draw25DIsSupported(void)
{
	return HAS_GEOMETRY;
}

void Draw25DResetTextures(void)
{
	// Textures are freed together with their renderer
	sAtlas = NULL;
	sAtlasFailed = false;
	sGlow = NULL;
	sGlowFailed = false;
}

// A round, soft white spot for the light splashes on the floor
static bool EnsureGlow(SDL_Renderer *r)
{
	if (sGlow != NULL)
	{
		return true;
	}
	if (sGlowFailed)
	{
		return false;
	}
	sGlow = SDL_CreateTexture(
		r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, GLOW_SIZE,
		GLOW_SIZE);
	if (sGlow == NULL)
	{
		LOG(LM_GFX, LL_ERROR, "cannot create 2.5D glow: %s", SDL_GetError());
		sGlowFailed = true;
		return false;
	}
	Uint32 px[GLOW_SIZE * GLOW_SIZE];
	for (int y = 0; y < GLOW_SIZE; y++)
	{
		for (int x = 0; x < GLOW_SIZE; x++)
		{
			const float dx = (x + 0.5f) / GLOW_SIZE * 2 - 1;
			const float dy = (y + 0.5f) / GLOW_SIZE * 2 - 1;
			float f = 1.0f - (dx * dx + dy * dy);
			f = f > 0 ? f * f : 0;
			const Uint32 v = (Uint32)(f * 255);
			px[y * GLOW_SIZE + x] = 0xFF000000u | v << 16 | v << 8 | v;
		}
	}
	SDL_UpdateTexture(sGlow, NULL, px, GLOW_SIZE * sizeof(Uint32));
	SDL_SetTextureBlendMode(sGlow, SDL_BLENDMODE_ADD);
#if SDL_VERSION_ATLEAST(2, 0, 12)
	SDL_SetTextureScaleMode(sGlow, SDL_ScaleModeLinear);
#endif
	return true;
}

static bool EnsureAtlas(SDL_Renderer *r)
{
	const int scale = gGraphicsDevice.gameWindow.renderScale;
	if (sAtlas != NULL && sAtlasScale != scale)
	{
		SDL_DestroyTexture(sAtlas);
		sAtlas = NULL;
		sAtlasFailed = false;
	}
	if (sAtlas != NULL)
	{
		return true;
	}
	if (sAtlasFailed)
	{
		return false;
	}
	sAtlas = SDL_CreateTexture(
		r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
		ATLAS_SIZE * scale, ATLAS_SIZE * scale);
	if (sAtlas == NULL)
	{
		LOG(LM_GFX, LL_ERROR, "cannot create 2.5D atlas: %s", SDL_GetError());
		sAtlasFailed = true;
		return false;
	}
	// Drawing with alpha blending into a transparent target leaves
	// premultiplied colours, so draw the atlas back out the same way
	const SDL_BlendMode premultiplied = SDL_ComposeCustomBlendMode(
		SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
		SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ONE,
		SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
	if (SDL_SetTextureBlendMode(sAtlas, premultiplied) != 0)
	{
		SDL_SetTextureBlendMode(sAtlas, SDL_BLENDMODE_BLEND);
	}
#if SDL_VERSION_ATLEAST(2, 0, 12)
	if (scale > 1)
	{
		SDL_SetTextureScaleMode(sAtlas, SDL_ScaleModeLinear);
	}
#endif
	sAtlasScale = scale;
	return true;
}

static void InitArrays(void)
{
	if (sArraysInit)
	{
		return;
	}
	CArrayInit(&sBelow, sizeof(ThingEntry));
	CArrayInit(&sNormal, sizeof(ThingEntry));
	CArrayInit(&sAbove, sizeof(ThingEntry));
	CArrayInit(&sDoors, sizeof(DoorEntry));
	sArraysInit = true;
}

static struct vec2i CellOrigin(const int cell)
{
	return svec2i(
		(cell % CELLS_PER_ROW) * CELL_W, (cell / CELLS_PER_ROW) * CELL_H);
}

static SDL_Color ToSDLColor(const color_t c, const float shade)
{
	SDL_Color sc = {
		(Uint8)(c.r * shade), (Uint8)(c.g * shade), (Uint8)(c.b * shade),
		c.a};
	return sc;
}

typedef struct
{
	struct vec2 w;
	float h;
} Point3;

static Point3 Lerp3(const Point3 a, const Point3 b, const float t)
{
	Point3 p = {
		svec2(a.w.x + (b.w.x - a.w.x) * t, a.w.y + (b.w.y - a.w.y) * t),
		a.h + (b.h - a.h) * t};
	return p;
}

static void AddLight(
	const struct vec2 pos, const float radius, const float r, const float g,
	const float b, const float glow)
{
	if (sNumLights >= MAX_LIGHTS)
	{
		return;
	}
	Light *l = &sLights[sNumLights++];
	l->Pos = pos;
	l->Radius = radius;
	l->R = r;
	l->G = g;
	l->B = b;
	l->Glow = glow;
}

static bool NameStarts(const char *name, const char *prefix)
{
	return name != NULL && strncmp(name, prefix, strlen(prefix)) == 0;
}

// Lights given off by a thing, by kind and particle class name
static void AddThingLight(const Thing *t)
{
	const struct vec2 pos = t->Pos;
	switch (t->kind)
	{
	case KIND_CHARACTER: {
		const TActor *a = CArrayGet(&gActors, t->id);
		if (a->PlayerUID >= 0 && !a->dead)
		{
			AddLight(pos, 7 * TILE_WIDTH, 0.5f, 0.46f, 0.38f, 0.1f);
		}
		break;
	}
	case KIND_MOBILEOBJECT:
		AddLight(pos, 2 * TILE_WIDTH, 0.35f, 0.3f, 0.22f, 0.18f);
		break;
	case KIND_PARTICLE: {
		const Particle *pa = CArrayGet(&gParticles, t->id);
		const char *name = pa->Class != NULL ? pa->Class->Name : NULL;
		if (NameStarts(name, "muzzle_flash"))
		{
			AddLight(pos, 3 * TILE_WIDTH, 0.9f, 0.75f, 0.45f, 0.6f);
		}
		else if (
			NameStarts(name, "boom") || NameStarts(name, "explosion") ||
			NameStarts(name, "fireball"))
		{
			AddLight(pos, 5 * TILE_WIDTH, 1.0f, 0.6f, 0.3f, 0.7f);
		}
		else if (NameStarts(name, "flame"))
		{
			AddLight(pos, 3 * TILE_WIDTH, 0.7f, 0.4f, 0.15f, 0.45f);
		}
		else if (NameStarts(name, "spark"))
		{
			AddLight(pos, TILE_WIDTH, 0.4f, 0.35f, 0.25f, 0.3f);
		}
		break;
	}
	default:
		break;
	}
}

// Light reaching a world point, as a colour multiplier (0..1 per channel)
static void LightAt(const struct vec2 w, float out[3])
{
	if (!sLighting || sUnlit)
	{
		out[0] = out[1] = out[2] = 1.0f;
		return;
	}
	out[0] = sAmbient[0];
	out[1] = sAmbient[1];
	out[2] = sAmbient[2];
	for (int i = 0; i < sNumLights; i++)
	{
		const Light *l = &sLights[i];
		const float dx = w.x - l->Pos.x;
		// Map y is foreshortened (12px tiles for 16px of ground)
		const float dy = (w.y - l->Pos.y) * TILE_WIDTH / TILE_HEIGHT;
		const float d2 = dx * dx + dy * dy;
		const float r2 = l->Radius * l->Radius;
		if (d2 >= r2)
		{
			continue;
		}
		// Smooth falloff: full at the centre, zero at the radius
		const float f = 1.0f - d2 / r2;
		out[0] += l->R * f * f;
		out[1] += l->G * f * f;
		out[2] += l->B * f * f;
	}
	for (int i = 0; i < 3; i++)
	{
		out[i] = MIN(out[i], 1.0f);
	}
}

static SDL_Color LitColor(
	const SDL_Color base, const struct vec2 w, const float shade)
{
	float l[3];
	LightAt(w, l);
	SDL_Color c = {
		(Uint8)(base.r * l[0] * shade), (Uint8)(base.g * l[1] * shade),
		(Uint8)(base.b * l[2] * shade), base.a};
	return c;
}

// Draw a planar quad in the world, corners in order top-left, top-right,
// bottom-right, bottom-left of the texture region uv0..uv1. corner, if not
// NULL, is a shade per corner, blended across the quad; every vertex is lit.
static void DrawQuad3D(
	SDL_Renderer *r, SDL_Texture *tex, const DrawPerspective *p,
	const Point3 c[4], const struct vec2 uv0, const struct vec2 uv1,
	const SDL_Color color, const float *corner)
{
#if HAS_GEOMETRY
	SDL_Vertex v[(QUAD_SUBDIV + 1) * (QUAD_SUBDIV + 1)];
	for (int j = 0; j <= QUAD_SUBDIV; j++)
	{
		const float fy = (float)j / QUAD_SUBDIV;
		const Point3 left = Lerp3(c[0], c[3], fy);
		const Point3 right = Lerp3(c[1], c[2], fy);
		for (int i = 0; i <= QUAD_SUBDIV; i++)
		{
			const float fx = (float)i / QUAD_SUBDIV;
			const Point3 pt = Lerp3(left, right, fx);
			struct vec2 s;
			if (!DrawPerspectiveProject(p, pt.w, pt.h, svec2_zero(), &s, NULL))
			{
				return;
			}
			SDL_Vertex *vx = &v[j * (QUAD_SUBDIV + 1) + i];
			vx->position.x = s.x;
			vx->position.y = s.y;
			float shade = 1.0f;
			if (corner != NULL)
			{
				const float top = corner[0] + (corner[1] - corner[0]) * fx;
				const float bottom = corner[3] + (corner[2] - corner[3]) * fx;
				shade = top + (bottom - top) * fy;
			}
			vx->color = LitColor(color, pt.w, shade);
			vx->tex_coord.x = uv0.x + (uv1.x - uv0.x) * fx;
			vx->tex_coord.y = uv0.y + (uv1.y - uv0.y) * fy;
		}
	}
	int idx[QUAD_SUBDIV * QUAD_SUBDIV * 6];
	int n = 0;
	for (int j = 0; j < QUAD_SUBDIV; j++)
	{
		for (int i = 0; i < QUAD_SUBDIV; i++)
		{
			const int a = j * (QUAD_SUBDIV + 1) + i;
			const int b = a + 1;
			const int d = a + QUAD_SUBDIV + 1;
			const int e = d + 1;
			idx[n++] = a;
			idx[n++] = b;
			idx[n++] = e;
			idx[n++] = a;
			idx[n++] = e;
			idx[n++] = d;
		}
	}
	SDL_RenderGeometry(r, tex, v, (QUAD_SUBDIV + 1) * (QUAD_SUBDIV + 1), idx, n);
#else
	UNUSED(r);
	UNUSED(tex);
	UNUSED(p);
	UNUSED(c);
	UNUSED(uv0);
	UNUSED(uv1);
	UNUSED(color);
	UNUSED(corner);
#endif
}

static void PrepareTexture(SDL_Texture *tex)
{
	// Vertex colours do the tinting; clear what classic drawing left behind
	SDL_SetTextureColorMod(tex, 255, 255, 255);
	SDL_SetTextureAlphaMod(tex, 255);
}

static struct vec2 TileWorldPos(const int x, const int y)
{
	return svec2((float)(x * TILE_WIDTH), (float)(y * TILE_HEIGHT));
}

static bool IsSolid(const Map *map, const struct vec2i pos)
{
	if (!MapIsTileIn(map, pos))
	{
		return true;
	}
	const Tile *t = MapGetTile(map, pos);
	return t->Class != NULL && t->Class->Type == TILE_CLASS_WALL;
}

// How much a tile is seen: 1 in sight, the fog level, 0 never seen
static float TileSeen(const struct vec2i pos, const bool useFog)
{
	if (!MapIsTileIn(&gMap, pos))
	{
		return 0;
	}
	const color_t mask = DrawTileLOSMask(MapGetTile(&gMap, pos), useFog);
	if (ColorEquals(mask, colorTransparent))
	{
		return 0;
	}
	if (ColorEquals(mask, colorWhite))
	{
		return 1;
	}
	return sLighting ? FOG_LIT : mask.r / 255.0f;
}

static void DrawFloorTile(
	SDL_Renderer *r, const DrawPerspective *p, const Tile *t,
	const struct vec2i pos, const bool useFog)
{
	if (t->Class == NULL || t->Class->Pic == NULL ||
		t->Class->Pic->Tex == NULL || t->Class->Type == TILE_CLASS_WALL)
	{
		return;
	}
	const color_t mask = DrawTileLOSMask(t, useFog);
	if (ColorEquals(mask, colorTransparent))
	{
		return;
	}
	const struct vec2 w0 = TileWorldPos(pos.x, pos.y);
	const struct vec2 w1 = TileWorldPos(pos.x + 1, pos.y + 1);
	const Point3 c[4] = {
		{w0, 0}, {svec2(w1.x, w0.y), 0}, {w1, 0}, {svec2(w0.x, w1.y), 0}};
	PrepareTexture(t->Class->Pic->Tex);
	// Each corner takes the average of the floor tiles around it, so the
	// edge of what is in sight fades over a tile instead of stepping. Walls
	// are left out of the average, or rooms would get dark rims along them.
	float corner[4];
	const struct vec2i cornerTile[4] = {
		pos, svec2i(pos.x + 1, pos.y), svec2i(pos.x + 1, pos.y + 1),
		svec2i(pos.x, pos.y + 1)};
	for (int i = 0; i < 4; i++)
	{
		const struct vec2i k = cornerTile[i];
		const struct vec2i around[4] = {
			svec2i(k.x - 1, k.y - 1), svec2i(k.x, k.y - 1),
			svec2i(k.x - 1, k.y), k};
		float sum = 0;
		int n = 0;
		for (int j = 0; j < 4; j++)
		{
			if (!IsSolid(&gMap, around[j]))
			{
				sum += TileSeen(around[j], useFog);
				n++;
			}
		}
		corner[i] = n > 0 ? sum / n : TileSeen(pos, useFog);
	}
	DrawQuad3D(
		r, t->Class->Pic->Tex, p, c, svec2_zero(), svec2_one(),
		ToSDLColor(colorWhite, 1.0f), corner);
}

// Draw a box occupying a whole tile: the texture holds the top face in its
// upper half and the front face in its lower half, like classic wall pics
static void DrawBox(
	SDL_Renderer *r, const DrawPerspective *p, SDL_Texture *tex,
	const struct vec2 uv0, const struct vec2 uv1, const bool hasFront,
	const struct vec2i pos, const color_t mask, const bool sides)
{
	const struct vec2 w0 = TileWorldPos(pos.x, pos.y);
	const struct vec2 w1 = TileWorldPos(pos.x + 1, pos.y + 1);
	const float vMid = hasFront ? (uv0.y + uv1.y) / 2 : uv1.y;
	const struct vec2 frontUV0 = svec2(uv0.x, vMid);
	PrepareTexture(tex);

	if (hasFront)
	{
		// The camera is always south of the walls it can see, so only the
		// south face and the face towards the focus column can show
		if (sides && !IsSolid(&gMap, svec2i(pos.x + 1, pos.y)) &&
			w1.x < p->Focus.x)
		{
			const Point3 c[4] = {
				{svec2(w1.x, w1.y), WALL_H},
				{svec2(w1.x, w0.y), WALL_H},
				{svec2(w1.x, w0.y), 0},
				{svec2(w1.x, w1.y), 0}};
			DrawQuad3D(
				r, tex, p, c, frontUV0, uv1, ToSDLColor(mask, SHADE_SIDE), NULL);
		}
		if (sides && !IsSolid(&gMap, svec2i(pos.x - 1, pos.y)) &&
			w0.x > p->Focus.x)
		{
			const Point3 c[4] = {
				{svec2(w0.x, w0.y), WALL_H},
				{svec2(w0.x, w1.y), WALL_H},
				{svec2(w0.x, w1.y), 0},
				{svec2(w0.x, w0.y), 0}};
			DrawQuad3D(
				r, tex, p, c, frontUV0, uv1, ToSDLColor(mask, SHADE_SIDE), NULL);
		}
		if (!sides || !IsSolid(&gMap, svec2i(pos.x, pos.y + 1)))
		{
			const Point3 c[4] = {
				{svec2(w0.x, w1.y), WALL_H},
				{svec2(w1.x, w1.y), WALL_H},
				{svec2(w1.x, w1.y), 0},
				{svec2(w0.x, w1.y), 0}};
			DrawQuad3D(
				r, tex, p, c, frontUV0, uv1, ToSDLColor(mask, SHADE_FRONT), NULL);
		}
	}

	const Point3 top[4] = {
		{w0, WALL_H},
		{svec2(w1.x, w0.y), WALL_H},
		{w1, WALL_H},
		{svec2(w0.x, w1.y), WALL_H}};
	DrawQuad3D(
		r, tex, p, top, uv0, svec2(uv1.x, vMid), ToSDLColor(mask, 1.0f), NULL);
}

static void DrawWallTile(
	SDL_Renderer *r, const DrawPerspective *p, const Tile *t,
	const struct vec2i pos, const bool useFog)
{
	const Pic *pic = t->Class->Pic;
	if (pic == NULL || pic->Tex == NULL)
	{
		return;
	}
	const color_t mask = DrawTileLOSMask(t, useFog);
	if (ColorEquals(mask, colorTransparent))
	{
		return;
	}
	DrawBox(
		r, p, pic->Tex, svec2_zero(), svec2_one(),
		pic->size.y > TILE_HEIGHT, pos, mask, true);
}

// Doors are thin panels standing across the middle of their tile, between
// the walls on either side, and they sink into the floor as they open. The
// tile itself is the open doorway (drawn as floor). Door art follows the
// classic layout: horizontal doors are the top over a TILE_HEIGHT face,
// vertical doors are their top as seen from above.
#define DOOR_THICK 6.0f

// How closed a door is: 1 closed, 0 open, in between while it moves (with
// the same easing as the classic door animation)
static float DoorClosed(const DoorState *d)
{
	if (d->Count <= 0)
	{
		return d->IsOpen ? 0.0f : 1.0f;
	}
	const double opened =
		d->IsOpen
			? CircularEaseOut(
				  (DOORSTATE_COUNT - d->Count) / (double)DOORSTATE_COUNT)
			: CircularEaseIn(d->Count / (double)DOORSTATE_COUNT);
	return 1.0f - (float)opened;
}

static void DrawDoorTile(
	SDL_Renderer *r, const DrawPerspective *p, const DoorEntry *d,
	const bool useFog)
{
	const DoorState *door = &d->tile->Door;
	if (door->Class == NULL || door->Class->Pic == NULL ||
		door->Class->Pic->Tex == NULL)
	{
		return;
	}
	const color_t mask = DrawTileLOSMask(d->tile, useFog);
	if (ColorEquals(mask, colorTransparent))
	{
		return;
	}
	const float closed = DoorClosed(door);
	if (closed <= 0)
	{
		return;
	}
	const Pic *pic = door->Class->Pic;
	SDL_Texture *tex = pic->Tex;
	PrepareTexture(tex);
	const float h = WALL_H * closed;
	const struct vec2 w0 = TileWorldPos(d->pos.x, d->pos.y);
	const struct vec2 w1 = TileWorldPos(d->pos.x + 1, d->pos.y + 1);
	if (door->IsHorizontal)
	{
		// Across a north-south passage: a slab along x, thin in y. Map y is
		// foreshortened, so the thickness shrinks the same way
		const float yc = (w0.y + w1.y) / 2;
		const float t = DOOR_THICK * TILE_HEIGHT / TILE_WIDTH / 2;
		const float topRows = (float)MAX(pic->size.y - TILE_HEIGHT, 1);
		const float vTop = topRows / pic->size.y;
		// Front: the top part of the face is what is left above the floor
		const Point3 front[4] = {
			{svec2(w0.x, yc + t), h},
			{svec2(w1.x, yc + t), h},
			{svec2(w1.x, yc + t), 0},
			{svec2(w0.x, yc + t), 0}};
		DrawQuad3D(
			r, tex, p, front, svec2(0, vTop),
			svec2(1, vTop + (1 - vTop) * closed),
			ToSDLColor(mask, SHADE_FRONT), NULL);
		const Point3 top[4] = {
			{svec2(w0.x, yc - t), h},
			{svec2(w1.x, yc - t), h},
			{svec2(w1.x, yc + t), h},
			{svec2(w0.x, yc + t), h}};
		DrawQuad3D(
			r, tex, p, top, svec2_zero(), svec2(1, vTop),
			ToSDLColor(mask, 1.0f), NULL);
	}
	else
	{
		// Across an east-west passage: a slab along y, thin in x; its long
		// sides show when the camera is off to that side
		const float xc = (w0.x + w1.x) / 2;
		const float xa = xc - DOOR_THICK / 2;
		const float xb = xc + DOOR_THICK / 2;
		if (xb < p->Focus.x)
		{
			const Point3 c[4] = {
				{svec2(xb, w1.y), h},
				{svec2(xb, w0.y), h},
				{svec2(xb, w0.y), 0},
				{svec2(xb, w1.y), 0}};
			DrawQuad3D(
				r, tex, p, c, svec2_zero(), svec2(1, closed),
				ToSDLColor(mask, SHADE_SIDE), NULL);
		}
		if (xa > p->Focus.x)
		{
			const Point3 c[4] = {
				{svec2(xa, w0.y), h},
				{svec2(xa, w1.y), h},
				{svec2(xa, w1.y), 0},
				{svec2(xa, w0.y), 0}};
			DrawQuad3D(
				r, tex, p, c, svec2_zero(), svec2(1, closed),
				ToSDLColor(mask, SHADE_SIDE), NULL);
		}
		const Point3 front[4] = {
			{svec2(xa, w1.y), h},
			{svec2(xb, w1.y), h},
			{svec2(xb, w1.y), 0},
			{svec2(xa, w1.y), 0}};
		DrawQuad3D(
			r, tex, p, front, svec2_zero(), svec2(1, closed),
			ToSDLColor(mask, SHADE_FRONT), NULL);
		const Point3 top[4] = {
			{svec2(xa, w0.y), h},
			{svec2(xb, w0.y), h},
			{svec2(xb, w1.y), h},
			{svec2(xa, w1.y), h}};
		DrawQuad3D(
			r, tex, p, top, svec2_zero(), svec2_one(), ToSDLColor(mask, 1.0f),
			NULL);
	}
}

static struct vec2 ThingDrawPos(const Thing *t)
{
	return svec2_add(t->Pos, t->drawShake);
}

static void CellUV(const int cell, struct vec2 *uv0, struct vec2 *uv1)
{
	const struct vec2i o = CellOrigin(cell);
	*uv0 = svec2((float)o.x / ATLAS_SIZE, (float)o.y / ATLAS_SIZE);
	*uv1 = svec2(
		(float)(o.x + CELL_W) / ATLAS_SIZE, (float)(o.y + CELL_H) / ATLAS_SIZE);
}

static void DrawBillboard(
	SDL_Renderer *r, const DrawPerspective *p, const ThingEntry *e,
	const bool lit)
{
	const struct vec2 pos = ThingDrawPos(e->t);
	if (e->cell < 0)
	{
		// Out of atlas space: draw the classic sprite unscaled where it
		// would stand
		struct vec2 s;
		if (DrawPerspectiveProject(p, pos, 0, svec2_zero(), &s, NULL))
		{
			DrawThingAt(e->t, svec2i_floor(s), true);
		}
		return;
	}
#if HAS_GEOMETRY
	struct vec2 uv0, uv1;
	CellUV(e->cell, &uv0, &uv1);
	const struct vec2 bb[4] = {
		svec2(-CELL_ANCHOR_X, -CELL_ANCHOR_Y),
		svec2(CELL_W - CELL_ANCHOR_X, -CELL_ANCHOR_Y),
		svec2(CELL_W - CELL_ANCHOR_X, CELL_H - CELL_ANCHOR_Y),
		svec2(-CELL_ANCHOR_X, CELL_H - CELL_ANCHOR_Y)};
	const struct vec2 uv[4] = {
		uv0, svec2(uv1.x, uv0.y), uv1, svec2(uv0.x, uv1.y)};
	// Lit where it stands; things that give off light are drawn unlit
	const int lightsBefore = sNumLights;
	AddThingLight(e->t);
	const bool emits = sNumLights > lightsBefore;
	sNumLights = lightsBefore;
	const SDL_Color white = ToSDLColor(colorWhite, 1.0f);
	const SDL_Color color =
		emits || !lit ? white : LitColor(white, pos, 1.0f);
	SDL_Vertex v[4];
	for (int i = 0; i < 4; i++)
	{
		struct vec2 s;
		if (!DrawPerspectiveProject(p, pos, 0, bb[i], &s, NULL))
		{
			return;
		}
		v[i].position.x = s.x;
		v[i].position.y = s.y;
		v[i].color = color;
		v[i].tex_coord.x = uv[i].x;
		v[i].tex_coord.y = uv[i].y;
	}
	const int idx[6] = {0, 1, 2, 0, 2, 3};
	SDL_RenderGeometry(r, sAtlas, v, 4, idx, 6);

	// Laser sights are lines in screen space; start them from the feet
	struct vec2 feet;
	if (DrawPerspectiveProject(p, pos, 0, svec2_zero(), &feet, NULL))
	{
		DrawThingLaserSight(e->t, svec2i_floor(feet));
	}
#else
	UNUSED(r);
#endif
}

static void DrawDecal(
	SDL_Renderer *r, const DrawPerspective *p, const ThingEntry *e)
{
	if (e->cell < 0)
	{
		DrawBillboard(r, p, e, true);
		return;
	}
	// Things lying on the floor (wrecks, debris) become ground quads
	const struct vec2 pos = ThingDrawPos(e->t);
	struct vec2 uv0, uv1;
	CellUV(e->cell, &uv0, &uv1);
	const float x0 = pos.x - CELL_ANCHOR_X;
	const float x1 = pos.x + CELL_W - CELL_ANCHOR_X;
	const float y0 = pos.y - CELL_ANCHOR_Y;
	const float y1 = pos.y + CELL_H - CELL_ANCHOR_Y;
	const Point3 c[4] = {
		{svec2(x0, y0), 0}, {svec2(x1, y0), 0}, {svec2(x1, y1), 0},
		{svec2(x0, y1), 0}};
	DrawQuad3D(r, sAtlas, p, c, uv0, uv1, ToSDLColor(colorWhite, 1.0f), NULL);
}

// Soft splashes of light on the floor, added on top of it; walls drawn
// afterwards cover the ones behind them
static void DrawGlows(SDL_Renderer *r, const DrawPerspective *p)
{
	if (!sLighting || sNumLights == 0 || !EnsureGlow(r))
	{
		return;
	}
	sUnlit = true;
	for (int i = 0; i < sNumLights; i++)
	{
		const Light *l = &sLights[i];
		if (l->Glow <= 0)
		{
			continue;
		}
		const float rx = l->Radius;
		const float ry = l->Radius * TILE_HEIGHT / TILE_WIDTH;
		const Point3 c[4] = {
			{svec2(l->Pos.x - rx, l->Pos.y - ry), 0},
			{svec2(l->Pos.x + rx, l->Pos.y - ry), 0},
			{svec2(l->Pos.x + rx, l->Pos.y + ry), 0},
			{svec2(l->Pos.x - rx, l->Pos.y + ry), 0}};
		const SDL_Color color = {
			(Uint8)(MIN(l->R * l->Glow, 1.0f) * 255),
			(Uint8)(MIN(l->G * l->Glow, 1.0f) * 255),
			(Uint8)(MIN(l->B * l->Glow, 1.0f) * 255), 255};
		DrawQuad3D(r, sGlow, p, c, svec2_zero(), svec2_one(), color, NULL);
	}
	sUnlit = false;
}

static int CompareThingEntries(const void *v1, const void *v2)
{
	const ThingEntry *e1 = v1;
	const ThingEntry *e2 = v2;
	if (e1->row != e2->row)
	{
		return e1->row < e2->row ? -1 : 1;
	}
	if (e1->t->Pos.y != e2->t->Pos.y)
	{
		return e1->t->Pos.y < e2->t->Pos.y ? -1 : 1;
	}
	return 0;
}

// Find the map tiles that can show up in the current view
static void GetTileRange(
	const DrawPerspective *p, const SDL_Rect *view, struct vec2i *t0,
	struct vec2i *t1)
{
	const struct vec2 corners[4] = {
		svec2((float)view->x, (float)view->y),
		svec2((float)(view->x + view->w), (float)view->y),
		svec2((float)view->x, (float)(view->y + view->h)),
		svec2((float)(view->x + view->w), (float)(view->y + view->h))};
	struct vec2 lo = p->Focus;
	struct vec2 hi = p->Focus;
	for (int i = 0; i < 4; i++)
	{
		struct vec2 w;
		if (!DrawPerspectiveUnproject(p, corners[i], &w))
		{
			// Looking past the horizon; the range cap below limits it
			w = svec2(
				p->Focus.x + (corners[i].x - p->FocusScreen.x) * 4,
				p->Focus.y - MAX_RANGE_TILES * TILE_HEIGHT);
		}
		lo = svec2(MIN(lo.x, w.x), MIN(lo.y, w.y));
		hi = svec2(MAX(hi.x, w.x), MAX(hi.y, w.y));
	}
	const struct vec2i focusTile = svec2i(
		(int)floorf(p->Focus.x / TILE_WIDTH),
		(int)floorf(p->Focus.y / TILE_HEIGHT));
	// Margins: walls and sprites below the view rise into it, and sprites
	// are wider than a tile
	*t0 = svec2i(
		MAX((int)floorf(lo.x / TILE_WIDTH) - 2,
			focusTile.x - MAX_RANGE_TILES / 2),
		MAX((int)floorf(lo.y / TILE_HEIGHT) - 1,
			focusTile.y - MAX_RANGE_TILES / 2));
	*t1 = svec2i(
		MIN((int)floorf(hi.x / TILE_WIDTH) + 2,
			focusTile.x + MAX_RANGE_TILES / 2),
		MIN((int)floorf(hi.y / TILE_HEIGHT) + 4,
			focusTile.y + MAX_RANGE_TILES / 2));
	*t0 = svec2i_max(*t0, svec2i_zero());
	*t1 = svec2i_min(*t1, svec2i_subtract(gMap.Size, svec2i_one()));
}

static void CollectThings(
	const struct vec2i t0, const struct vec2i t1, int *numCells)
{
	CArrayClear(&sBelow);
	CArrayClear(&sNormal);
	CArrayClear(&sAbove);
	CArrayClear(&sDoors);
	struct vec2i pos;
	for (pos.y = t0.y; pos.y <= t1.y; pos.y++)
	{
		for (pos.x = t0.x; pos.x <= t1.x; pos.x++)
		{
			Tile *tile = MapGetTile(&gMap, pos);
			tile->outOfSight = !LOSTileIsVisible(&gMap, pos);
			if (tile->Class != NULL && tile->Class->Type == TILE_CLASS_DOOR)
			{
				DoorEntry d = {tile, pos};
				CArrayPushBack(&sDoors, &d);
			}
			if (tile->outOfSight)
			{
				continue;
			}
			CA_FOREACH(ThingId, tid, tile->things)
			ThingEntry e = {ThingIdGetThing(tid), pos.y, -1};
			AddThingLight(e.t);
			if (*numCells < MAX_CELLS)
			{
				e.cell = (*numCells)++;
			}
			if (ThingDrawBelow(e.t))
			{
				CArrayPushBack(&sBelow, &e);
			}
			else if (ThingDrawAbove(e.t))
			{
				CArrayPushBack(&sAbove, &e);
			}
			else
			{
				CArrayPushBack(&sNormal, &e);
			}
			CA_FOREACH_END()
		}
	}
	qsort(sBelow.data, sBelow.size, sBelow.elemSize, CompareThingEntries);
	qsort(sNormal.data, sNormal.size, sNormal.elemSize, CompareThingEntries);
	qsort(sAbove.data, sAbove.size, sAbove.elemSize, CompareThingEntries);
}

static void RenderCell(SDL_Renderer *r, const int cell)
{
	const struct vec2i o = CellOrigin(cell);
	const SDL_Rect rect = {o.x, o.y, CELL_W, CELL_H};
	SDL_RenderSetClipRect(r, &rect);
}

// Draw every thing and door with its classic code into the atlas
static void RenderAtlas(SDL_Renderer *r)
{
	SDL_Texture *prevTarget = SDL_GetRenderTarget(r);
	const SDL_bool hadClip = SDL_RenderIsClipEnabled(r);
	SDL_Rect prevClip;
	SDL_RenderGetClipRect(r, &prevClip);
	int logicalW, logicalH;
	SDL_RenderGetLogicalSize(r, &logicalW, &logicalH);

	if (SDL_SetRenderTarget(r, sAtlas) != 0)
	{
		LOG(LM_GFX, LL_ERROR, "cannot draw to 2.5D atlas: %s", SDL_GetError());
		return;
	}
	// Draw in logical pixels, at the atlas' (high) resolution
	SDL_RenderSetScale(r, (float)sAtlasScale, (float)sAtlasScale);
	SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
	SDL_RenderClear(r);

	CArray *lists[3] = {&sBelow, &sNormal, &sAbove};
	for (int i = 0; i < 3; i++)
	{
		CA_FOREACH(const ThingEntry, e, *lists[i])
		if (e->cell < 0)
		{
			continue;
		}
		RenderCell(r, e->cell);
		DrawThingAt(
			e->t,
			svec2i_add(CellOrigin(e->cell), svec2i(CELL_ANCHOR_X, CELL_ANCHOR_Y)),
			false);
		CA_FOREACH_END()
	}

	SDL_RenderSetClipRect(r, NULL);
	SDL_SetRenderTarget(r, prevTarget);
	// Changing targets resets the scale; the logical size brings back the
	// render scale of the final texture
	if (logicalW > 0 && logicalH > 0)
	{
		SDL_RenderSetLogicalSize(r, logicalW, logicalH);
	}
	SDL_RenderSetClipRect(r, hadClip ? &prevClip : NULL);
}

void Draw25DWorld(
	DrawBuffer *b, const struct vec2i offset, const DrawBufferArgs *args)
{
	UNUSED(args);
	SDL_Renderer *r = gGraphicsDevice.gameWindow.renderer;
	InitArrays();

	// The focus lands exactly where the classic view would put it, so split
	// screens and the HUD stay where they were
	const struct vec2 focusScreen = svec2(
		b->Origin.x - (float)b->xTop + (float)offset.x,
		b->Origin.y - (float)b->yTop + (float)offset.y);
	const float dist = PERSPECTIVE_DIST_DEFAULT *
					   MAX(1.0f, (float)gGraphicsDevice.cachedConfig.Res.y /
									 PERSPECTIVE_REF_ROWS);
	DrawPerspectiveInit(&b->Perspective, b->Origin, focusScreen, dist);
	b->UsePerspective = true;
	const DrawPerspective *p = &b->Perspective;

	SDL_Rect view;
	if (SDL_RenderIsClipEnabled(r))
	{
		SDL_RenderGetClipRect(r, &view);
	}
	else
	{
		view.x = 0;
		view.y = 0;
		view.w = gGraphicsDevice.cachedConfig.Res.x;
		view.h = gGraphicsDevice.cachedConfig.Res.y;
	}
	struct vec2i t0, t1;
	GetTileRange(p, &view, &t0, &t1);
	if (t0.x > t1.x || t0.y > t1.y)
	{
		return;
	}

	sLighting = ConfigGetBool(&gConfig, "Graphics.Lights");
	sNumLights = 0;
	int numCells = 0;
	CollectThings(t0, t1, &numCells);
	if (!EnsureAtlas(r))
	{
		return;
	}
	RenderAtlas(r);

	const bool useFog = ConfigGetBool(&gConfig, "Game.Fog");
	struct vec2i pos;

	// Floor first: nothing lies beneath it
	for (pos.y = t0.y; pos.y <= t1.y; pos.y++)
	{
		for (pos.x = t0.x; pos.x <= t1.x; pos.x++)
		{
			DrawFloorTile(r, p, MapGetTile(&gMap, pos), pos, useFog);
		}
	}
	PrepareTexture(sAtlas);
	CA_FOREACH(const ThingEntry, e, sBelow)
	DrawDecal(r, p, e);
	CA_FOREACH_END()
	DrawGlows(r, p);

	// Then back to front, row by row. Within a row, walls go from the sides
	// in towards the focus column so their side faces overlap correctly,
	// then the things standing in that row.
	const int focusX = (int)floorf(p->Focus.x / TILE_WIDTH);
	int nextThing = 0;
	int nextDoor = 0;
	for (pos.y = t0.y; pos.y <= t1.y; pos.y++)
	{
		for (int side = 0; side < 2; side++)
		{
			const int start = side == 0 ? t0.x : t1.x;
			const int end = side == 0 ? MIN(focusX - 1, t1.x) : MAX(focusX, t0.x);
			const int step = side == 0 ? 1 : -1;
			for (pos.x = start; step > 0 ? pos.x <= end : pos.x >= end;
				 pos.x += step)
			{
				const Tile *t = MapGetTile(&gMap, pos);
				if (t->Class != NULL && t->Class->Type == TILE_CLASS_WALL)
				{
					DrawWallTile(r, p, t, pos, useFog);
				}
			}
		}
		while (nextDoor < (int)sDoors.size)
		{
			const DoorEntry *d = CArrayGet(&sDoors, nextDoor);
			if (d->pos.y != pos.y)
			{
				break;
			}
			DrawDoorTile(r, p, d, useFog);
			nextDoor++;
		}
		PrepareTexture(sAtlas);
		while (nextThing < (int)sNormal.size)
		{
			const ThingEntry *e = CArrayGet(&sNormal, nextThing);
			if (e->row != pos.y)
			{
				break;
			}
			DrawBillboard(r, p, e, true);
			nextThing++;
		}
	}

	PrepareTexture(sAtlas);
	// Texts and markers drawn above everything keep their colours
	CA_FOREACH(const ThingEntry, e, sAbove)
	DrawBillboard(r, p, e, false);
	CA_FOREACH_END()
}
