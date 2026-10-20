/*
	C-Dogs SDL
	A port of the legendary (and fun) action/arcade cdogs.

	Copyright (c) 2026, Felipe Menegaz
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
#include "puzzles.h"

#include "actors.h"
#include "config.h"
#include "game_events.h"
#include "gamedata.h"
#include "log.h"
#include "net_util.h"
#include "player.h"

// Seal: doors this far (tiles) from it lock, the wave comes this far
#define SEAL_TRIGGER_TILES 2.5f
#define SEAL_LOCK_TILES 8
#define SEAL_TIMEOUT_S 45
// Levers: how far apart (tiles) and how long between the two
#define LEVER_MIN_TILES 4
#define LEVER_MAX_TILES 6
#define LEVER_WINDOW_COOP_S 3
#define LEVER_WINDOW_SOLO_S 6

static struct
{
	bool Active;
	bool Done;
	int SealUID;
	struct vec2 Pos;
	CArray LockedTiles; // of struct vec2i
	CArray WaveUIDs;	// of int
	int Ticks;
} sSeal;

static struct
{
	bool Pending;
	bool Done;
	struct vec2 Pos;
	int Ticks;
} sLever;

static void Text(const struct vec2 pos, const char *text)
{
	GameEvent s = GameEventNew(GAME_EVENT_ADD_PARTICLE);
	s.u.AddParticle.Class = StrParticleClass(&gParticleClasses, "locked_text");
	s.u.AddParticle.Pos = pos;
	s.u.AddParticle.Z = (BULLET_Z * 2) * Z_FACTOR;
	strcpy(s.u.AddParticle.Text, text);
	GameEventsEnqueue(&gGameEvents, s);
}

void PuzzlesInit(void)
{
	CArrayTerminate(&sSeal.LockedTiles);
	CArrayTerminate(&sSeal.WaveUIDs);
	memset(&sSeal, 0, sizeof sSeal);
	CArrayInit(&sSeal.LockedTiles, sizeof(struct vec2i));
	CArrayInit(&sSeal.WaveUIDs, sizeof(int));
	memset(&sLever, 0, sizeof sLever);
}

bool PuzzlesIsDoorLocked(const struct vec2i tile)
{
	if (!sSeal.Active)
	{
		return false;
	}
	CA_FOREACH(const struct vec2i, v, sSeal.LockedTiles)
	if (svec2i_is_equal(*v, tile))
	{
		return true;
	}
	CA_FOREACH_END()
	return false;
}

static const TObject *FindObject(const char *className)
{
	CA_FOREACH(const TObject, o, gObjs)
	if (o->isInUse && strcmp(o->Class->Name, className) == 0)
	{
		return o;
	}
	CA_FOREACH_END()
	return NULL;
}

static bool APlayerIsNear(const struct vec2 pos, const float tiles)
{
	const float d2 = (tiles * TILE_WIDTH) * (tiles * TILE_WIDTH);
	CA_FOREACH(const PlayerData, p, gPlayerDatas)
	if (!IsPlayerAlive(p))
	{
		continue;
	}
	const TActor *a = ActorGetByUID(p->ActorUID);
	if (a != NULL && svec2_distance_squared(a->Pos, pos) < d2)
	{
		return true;
	}
	CA_FOREACH_END()
	return false;
}

static void SealLock(const TObject *seal)
{
	sSeal.Active = true;
	sSeal.SealUID = seal->uid;
	sSeal.Pos = seal->thing.Pos;
	sSeal.Ticks = 0;
	CArrayClear(&sSeal.LockedTiles);
	CArrayClear(&sSeal.WaveUIDs);
	// Lock the doors around
	const struct vec2i c = Vec2ToTile(sSeal.Pos);
	for (int dy = -SEAL_LOCK_TILES; dy <= SEAL_LOCK_TILES; dy++)
	{
		for (int dx = -SEAL_LOCK_TILES; dx <= SEAL_LOCK_TILES; dx++)
		{
			const struct vec2i v = svec2i(c.x + dx, c.y + dy);
			const Tile *t = MapGetTile(&gMap, v);
			if (t != NULL && t->Class->Type == TILE_CLASS_DOOR)
			{
				CArrayPushBack(&sSeal.LockedTiles, &v);
			}
		}
	}
	// The wave, inside the room, a few tiles from the seal
	const int n = 3 + GetNumPlayers(PLAYER_ALIVE_OR_DYING, false, false);
	if (gCampaign.Setting.characters.baddieIds.size > 0)
	{
		for (int i = 0, tries = 0; i < n && tries < 200; tries++)
		{
			const struct vec2i v = svec2i(
				c.x + rand() % 9 - 4, c.y + rand() % 9 - 4);
			const Tile *t = MapGetTile(&gMap, v);
			if (t == NULL || !TileCanWalk(t) || !TileIsClear(t) ||
				!t->Class->IsRoom ||
				APlayerIsNear(Vec2CenterOfTile(v), 2.0f))
			{
				continue;
			}
			const int charId =
				CharacterStoreGetRandomBaddieId(&gCampaign.Setting.characters);
			const Character *ch =
				CArrayGet(&gCampaign.Setting.characters.OtherChars, charId);
			GameEvent e = GameEventNewActorAdd(Vec2CenterOfTile(v), ch, NULL);
			e.u.ActorAdd.CharId = charId;
			GameEventsEnqueue(&gGameEvents, e);
			const int uid = (int)e.u.ActorAdd.UID;
			CArrayPushBack(&sSeal.WaveUIDs, &uid);
			i++;
		}
	}
	Text(sSeal.Pos, "Sealed!");
	LOG(LM_MAP, LL_INFO, "seal: %d doors locked, wave of %d",
		(int)sSeal.LockedTiles.size, (int)sSeal.WaveUIDs.size);
}

static void SealOpen(const bool cleared)
{
	sSeal.Active = false;
	sSeal.Done = true;
	CArrayClear(&sSeal.LockedTiles);
	// The seal goes out (its wreck is the spent seal)
	GameEvent e = GameEventNew(GAME_EVENT_MAP_OBJECT_REMOVE);
	e.u.MapObjectRemove.UID = sSeal.SealUID;
	e.u.MapObjectRemove.ActorUID = -1;
	GameEventsEnqueue(&gGameEvents, e);
	Text(sSeal.Pos, cleared ? "Cleared!" : "Unsealed");
	LOG(LM_MAP, LL_INFO, "seal open (%s)", cleared ? "cleared" : "time out");
}

static bool WaveIsDead(void)
{
	CA_FOREACH(const int, uid, sSeal.WaveUIDs)
	const TActor *a = ActorGetByUID(*uid);
	if (a != NULL && !a->dead)
	{
		return false;
	}
	CA_FOREACH_END()
	return true;
}

static int Seconds(const int s)
{
	return s * ConfigGetInt(&gConfig, "Game.FPS");
}

static void AddPickup(const struct vec2 pos, const char *pickupClass)
{
	GameEvent e = GameEventNew(GAME_EVENT_ADD_PICKUP);
	strcpy(e.u.AddPickup.PickupClass, pickupClass);
	e.u.AddPickup.Pos = Vec2ToNet(pos);
	e.u.AddPickup.IsRandomSpawned = true;
	GameEventsEnqueue(&gGameEvents, e);
}

static void LeverReward(const struct vec2 pos)
{
	if (ConfigGetBool(&gConfig, "Game.HealthPickups"))
	{
		AddPickup(svec2_add(pos, svec2(-8, 0)), "health");
	}
	if (gCampaign.Setting.Ammo && AmmoGetNumClasses(&gAmmo) > 0)
	{
		const Ammo *a = AmmoGetById(&gAmmo, rand() % AmmoGetNumClasses(&gAmmo));
		char buf[256];
		sprintf(buf, "ammo_%s", a->Name);
		AddPickup(svec2_add(pos, svec2(8, 0)), buf);
	}
	if (gMission.Weapons.size > 0)
	{
		const WeaponClass **wc =
			CArrayGet(&gMission.Weapons, rand() % gMission.Weapons.size);
		char buf[256];
		sprintf(buf, "gun_%s", (*wc)->name);
		AddPickup(svec2_add(pos, svec2(0, 8)), buf);
	}
	Text(pos, "Bonus!");
}

bool PuzzlesGetLeverToFinish(struct vec2 *pos)
{
	if (!sLever.Pending || sLever.Done)
	{
		return false;
	}
	const TObject *o = FindObject("lever");
	if (o == NULL)
	{
		return false;
	}
	*pos = o->thing.Pos;
	return true;
}

void PuzzlesOnObjectDestroyed(const TObject *o)
{
	if (strcmp(o->Class->Name, "lever") != 0 || sLever.Done)
	{
		return;
	}
	if (!sLever.Pending)
	{
		sLever.Pending = true;
		sLever.Pos = o->thing.Pos;
		sLever.Ticks = 0;
		return;
	}
	// Both down in time
	sLever.Pending = false;
	sLever.Done = true;
	LeverReward(svec2_scale(svec2_add(sLever.Pos, o->thing.Pos), 0.5f));
	LOG(LM_MAP, LL_INFO, "levers: both down in time");
}

static void LeverReset(void)
{
	sLever.Pending = false;
	// Remove the knocked-down lever there and stand a new one up
	CA_FOREACH(const TObject, o, gObjs)
	if (o->isInUse && strcmp(o->Class->Name, "lever_down") == 0 &&
		svec2_distance_squared(o->thing.Pos, sLever.Pos) < 4 * 4)
	{
		GameEvent r = GameEventNew(GAME_EVENT_MAP_OBJECT_REMOVE);
		r.u.MapObjectRemove.UID = o->uid;
		r.u.MapObjectRemove.ActorUID = -1;
		GameEventsEnqueue(&gGameEvents, r);
		break;
	}
	CA_FOREACH_END()
	const MapObject *lever = StrMapObject("lever");
	if (lever != NULL)
	{
		GameEvent e = GameEventNew(GAME_EVENT_MAP_OBJECT_ADD);
		e.u.MapObjectAdd.UID = ObjsGetNextUID();
		strcpy(e.u.MapObjectAdd.MapObjectClass, lever->Name);
		e.u.MapObjectAdd.has_Pos = true;
		e.u.MapObjectAdd.Pos = Vec2ToNet(sLever.Pos);
		e.u.MapObjectAdd.ThingFlags = MapObjectGetFlags(lever);
		e.u.MapObjectAdd.Health = lever->Health;
		GameEventsEnqueue(&gGameEvents, e);
	}
	Text(sLever.Pos, "Too slow!");
	LOG(LM_MAP, LL_INFO, "levers: too slow, the first stands up again");
}

void PuzzlesUpdate(const int ticks)
{
	if (!ConfigGetBool(&gConfig, "Game.Puzzles"))
	{
		return;
	}
	// Seal
	if (!sSeal.Active && !sSeal.Done)
	{
		// Missions without random enemies (only hand-placed ones) have
		// nothing to send: the seal stays a decoration there
		if (gCampaign.Setting.characters.baddieIds.size == 0)
		{
			sSeal.Done = true;
		}
		else
		{
			const TObject *seal = FindObject("arena_seal");
			if (seal != NULL &&
				APlayerIsNear(seal->thing.Pos, SEAL_TRIGGER_TILES))
			{
				SealLock(seal);
			}
		}
	}
	else if (sSeal.Active)
	{
		sSeal.Ticks += ticks;
		// Wait a moment for the wave to be added before checking it
		if (sSeal.Ticks > Seconds(1) && WaveIsDead())
		{
			SealOpen(true);
		}
		else if (sSeal.Ticks > Seconds(SEAL_TIMEOUT_S))
		{
			SealOpen(false);
		}
	}
	// Levers
	if (sLever.Pending)
	{
		sLever.Ticks += ticks;
		const int players = GetNumPlayers(PLAYER_ALIVE_OR_DYING, false, false);
		const int window =
			Seconds(players >= 2 ? LEVER_WINDOW_COOP_S : LEVER_WINDOW_SOLO_S);
		if (sLever.Ticks > window)
		{
			LeverReset();
		}
	}
}

static bool RoomTileOK(MapBuilder *mb, const struct vec2i v)
{
	const Tile *t = MapGetTile(mb->Map, v);
	return t != NULL && t->Class->IsRoom && TileIsClear(t) && TileCanWalk(t);
}

static int CountRoomTilesAround(MapBuilder *mb, const struct vec2i v, const int r)
{
	int n = 0;
	for (int dy = -r; dy <= r; dy++)
	{
		for (int dx = -r; dx <= r; dx++)
		{
			const struct vec2i w = svec2i(v.x + dx, v.y + dy);
			if (MapIsTileIn(mb->Map, w) && RoomTileOK(mb, w))
			{
				n++;
			}
		}
	}
	return n;
}

void MapAddPuzzleObjects(MapBuilder *mb)
{
	if (!ConfigGetBool(&gConfig, "Game.Puzzles"))
	{
		return;
	}
	// The seal, in the middle of a roomy room; not in missions without random
	// enemies (only hand-placed ones), which have nothing to send
	const MapObject *seal = mb->characters->baddieIds.size > 0
								? StrMapObject("arena_seal")
								: NULL;
	for (int i = 0; seal != NULL && i < 3000; i++)
	{
		const struct vec2i v = MapGetRandomTile(mb->Map);
		if (RoomTileOK(mb, v) && CountRoomTilesAround(mb, v, 2) >= 20 &&
			MapTryPlaceOneObject(mb, v, seal, 0, false))
		{
			LOG(LM_MAP, LL_INFO, "arena_seal at (%d, %d)", v.x, v.y);
			break;
		}
	}
	// Two levers a few tiles apart, in sight of each other
	const MapObject *lever = StrMapObject("lever");
	for (int i = 0; lever != NULL && i < 3000; i++)
	{
		const struct vec2i a = MapGetRandomTile(mb->Map);
		if (!RoomTileOK(mb, a))
		{
			continue;
		}
		const int d = LEVER_MIN_TILES + rand() % (LEVER_MAX_TILES - LEVER_MIN_TILES + 1);
		const struct vec2i dirs[] = {
			svec2i(d, 0), svec2i(-d, 0), svec2i(0, d), svec2i(0, -d)};
		const struct vec2i b = svec2i_add(a, dirs[rand() % 4]);
		if (!MapIsTileIn(mb->Map, b) || !RoomTileOK(mb, b))
		{
			continue;
		}
		// Every tile between them must be open room floor
		bool clear = true;
		const struct vec2i step = svec2i(
			b.x > a.x ? 1 : (b.x < a.x ? -1 : 0),
			b.y > a.y ? 1 : (b.y < a.y ? -1 : 0));
		for (struct vec2i w = svec2i_add(a, step); !svec2i_is_equal(w, b);
			 w = svec2i_add(w, step))
		{
			if (!RoomTileOK(mb, w))
			{
				clear = false;
				break;
			}
		}
		if (clear && MapTryPlaceOneObject(mb, a, lever, 0, false) &&
			MapTryPlaceOneObject(mb, b, lever, 0, false))
		{
			LOG(LM_MAP, LL_INFO, "levers at (%d, %d) and (%d, %d)", a.x, a.y,
				b.x, b.y);
			break;
		}
	}
}
