/*
	C-Dogs SDL
	A port of the legendary (and fun) action/arcade cdogs.
	Copyright (c) 2013-2019, 2026 Cong Xu

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
#include "camera.h"

#include "actors.h"
#include "draw/draw.h"
#include "draw/drawtools.h"
#include "events.h"
#include "font.h"
#include "log.h"
#include "los.h"
#include "player.h"

#define PAN_SPEED 4
// Look-ahead, as fractions of the view's reference height (the height of a
// 16:9 area that fits in the view; 1080 px at 1080p): lean 150 px towards
// the aim, plus 0.18 s (12.6 ticks at 70 FPS) of the velocity, up to 170 px.
#define LOOK_AIM (150.0f / 1080.0f)
#define LOOK_VEL_TICKS 12.6f
#define LOOK_MAX (170.0f / 1080.0f)
// 1/s: how fast the camera catches up with the look-ahead
#define LOOK_FOLLOW 7.0f

void CameraInit(Camera *camera)
{
	memset(camera, 0, sizeof *camera);
	CameraReset(camera);
	camera->lastPosition = svec2_zero();
	for (int i = 0; i < MAX_LOCAL_PLAYERS; i++)
	{
		camera->LookActorUID[i] = -1;
	}
	HUDInit(&camera->HUD, &gGraphicsDevice, &gMission);
	camera->shake = ScreenShakeZero();
}

void CameraReset(Camera *camera)
{
	DrawBufferTerminate(&camera->Buffer);
	DrawBufferInit(
		&camera->Buffer, svec2i(X_TILES, Y_TILES), &gGraphicsDevice);
}

void CameraTerminate(Camera *camera)
{
	DrawBufferTerminate(&camera->Buffer);
	HUDTerminate(&camera->HUD);
}

void CameraInput(Camera *camera, const int cmd, const int lastCmd)
{
	// Control the camera
	if (camera->spectateMode == SPECTATE_NONE)
	{
		return;
	}
	// Arrows: pan camera
	// CMD1/2: choose next player to follow
	if (CMD_HAS_DIRECTION(cmd))
	{
		camera->spectateMode = SPECTATE_FREE;
		const int pan = PAN_SPEED;
		if (Left(cmd))
			camera->lastPosition.x -= pan;
		else if (Right(cmd))
			camera->lastPosition.x += pan;
		if (Up(cmd))
			camera->lastPosition.y -= pan;
		else if (Down(cmd))
			camera->lastPosition.y += pan;
	}
	else if (
		(AnyButton(cmd) && !AnyButton(lastCmd)) || camera->FollowNextPlayer)
	{
		// Can't follow if there are no players
		if (GetNumPlayers(PLAYER_ALIVE_OR_DYING, false, false) == 0)
		{
			return;
		}
		camera->spectateMode = SPECTATE_FOLLOW;
		// Find index of player
		int playerIndex = -1;
		CA_FOREACH(const PlayerData, p, gPlayerDatas)
		if (p->ActorUID == camera->FollowActorUID)
		{
			playerIndex = _ca_index;
			break;
		}
		CA_FOREACH_END()
		// Get the next player by index that has an actor in the game
		const int d = Button1(cmd) ? 1 : -1;
		for (int i = playerIndex + d;; i += d)
		{
			i = CLAMP_OPPOSITE(i, 0, (int)gPlayerDatas.size - 1);
			// Check if clamping made us hit the termination condition
			if (i == playerIndex)
				break;
			const PlayerData *p = CArrayGet(&gPlayerDatas, i);
			if (IsPlayerAliveOrDying(p))
			{
				// Follow this player
				camera->FollowActorUID = p->ActorUID;
				camera->FollowNextPlayer = false;
				break;
			}
		}
	}
}

static struct vec2 GetFollowPlayerPos(
	const struct vec2 lastPos, const PlayerData *p);
static struct vec2 UpdateLook(
	Camera *camera, const int i, const TActor *a, const struct vec2i viewSize,
	const int ms);
static void ResetLooks(Camera *camera);
void CameraUpdate(Camera *camera, const int ticks, const int ms)
{
	camera->HUD.DrawData = HUDGetDrawData();
	camera->FocusActorUID = -1;
	if (camera->HUD.DrawData.NumScreens == 0)
	{
		// Try to spectate if there are other players alive
		if (camera->spectateMode == SPECTATE_NONE)
		{
			// Enter spectator mode
			// Free-look mode
			camera->spectateMode = SPECTATE_FREE;
			// If there are other players alive, follow them
			CA_FOREACH(const PlayerData, p, gPlayerDatas)
			if (IsPlayerAliveOrDying(p))
			{
				camera->spectateMode = SPECTATE_FOLLOW;
				camera->FollowActorUID = p->ActorUID;
				break;
			}
			CA_FOREACH_END()
		}
	}
	else
	{
		// Don't spectate
		camera->spectateMode = SPECTATE_NONE;
	}
	if (camera->spectateMode == SPECTATE_FOLLOW)
	{
		const TActor *a = ActorGetByUID(camera->FollowActorUID);
		if (a != NULL)
		{
			camera->HUD.DrawData.Players[0] = PlayerDataGetByUID(a->PlayerUID);
			if (camera->HUD.DrawData.Players[0] != NULL)
			{
				camera->HUD.DrawData.NumScreens = 1;
				camera->lastPosition = GetFollowPlayerPos(
					camera->lastPosition, camera->HUD.DrawData.Players[0]);
			}
		}
	}

	HUDUpdate(&camera->HUD, ms);
	camera->shake = ScreenShakeUpdate(camera->shake, ticks);

	// Determine how many camera views to use, and set camera position
	const struct vec2i res = gGraphicsDevice.cachedConfig.Res;
	if (camera->HUD.DrawData.NumScreens == 0)
	{
		ResetLooks(camera);
		camera->NumViews = 1;
		SoundSetEars(camera->lastPosition);
	}
	else
	{
		const bool onePlayer = camera->HUD.DrawData.NumScreens == 1;
		const bool singleScreen = CameraIsSingleScreen();
		if (onePlayer || singleScreen)
		{
			// Single camera screen
			// Keep the ear positions on the player(s), not on the camera
			// look-ahead or map centre, so that sounds don't get muffled
			// just because there's a wall between player and camera centre
			struct vec2 earPos;
			if (onePlayer)
			{
				const PlayerData *firstPlayer =
					camera->HUD.DrawData.Players[0];
				const TActor *p = ActorGetByUID(firstPlayer->ActorUID);
				earPos = p->thing.Pos;
				camera->lastPosition = svec2_add(
					p->thing.Pos, UpdateLook(camera, 0, p, res, ms));
				camera->FocusActorUID = p->uid;
			}
			else
			{
				// One screen shared by all players: no look-ahead, it
				// could push the other players off the screen
				ResetLooks(camera);
				camera->lastPosition = PlayersGetMidpoint();
				earPos = camera->lastPosition;
			}

			// Special case: if map is smaller than screen, center the camera
			if (gMap.Size.x * TILE_WIDTH < gGraphicsDevice.cachedConfig.Res.x)
			{
				camera->lastPosition.x = (float)gMap.Size.x * TILE_WIDTH / 2;
				camera->FocusActorUID = -1;
			}
			if (gMap.Size.y * TILE_HEIGHT < gGraphicsDevice.cachedConfig.Res.y)
			{
				camera->lastPosition.y = (float)gMap.Size.y * TILE_HEIGHT / 2;
				camera->FocusActorUID = -1;
			}

			SoundSetEars(earPos);

			camera->NumViews = 1;
		}
		else if (camera->HUD.DrawData.NumScreens == 2)
		{
			// side-by-side split
			for (int i = 0; i < camera->HUD.DrawData.NumScreens; i++)
			{
				const PlayerData *p = camera->HUD.DrawData.Players[i];
				const TActor *a = ActorGetByUID(p->ActorUID);
				camera->lastPosition = a->thing.Pos;
				SoundSetEarsSide(i == 0, camera->lastPosition);
				UpdateLook(camera, i, a, svec2i(res.x / 2, res.y), ms);
			}

			camera->NumViews = 2;
		}
		else if (
			camera->HUD.DrawData.NumScreens >= 3 &&
			camera->HUD.DrawData.NumScreens <= 4)
		{
			// 4 player split screen
			bool isLocalPlayerAlive[MAX_LOCAL_PLAYERS];
			memset(isLocalPlayerAlive, 0, sizeof isLocalPlayerAlive);
			for (int i = 0; i < camera->HUD.DrawData.NumScreens; i++)
			{
				const PlayerData *p = camera->HUD.DrawData.Players[i];
				isLocalPlayerAlive[i] = IsPlayerAliveOrDying(p);
				if (!isLocalPlayerAlive[i])
				{
					continue;
				}
				const TActor *a = ActorGetByUID(p->ActorUID);
				camera->lastPosition = a->thing.Pos;
				UpdateLook(camera, i, a, svec2i(res.x / 2, res.y / 2), ms);

				// Set the sound "ears"
				const bool isLeft = i == 0 || i == 2;
				const bool isUpper = i <= 2;
				SoundSetEar(isLeft, isUpper ? 0 : 1, camera->lastPosition);
				// If any player is dead, that ear reverts to the other ear
				// of the same side of the remaining player
				const int otherIdxOnSameSide = i ^ 2;
				if (!isLocalPlayerAlive[otherIdxOnSameSide])
				{
					SoundSetEar(
						isLeft, !isUpper ? 0 : 1, camera->lastPosition);
				}
				else if (
					!isLocalPlayerAlive[3 - i] &&
					!isLocalPlayerAlive[3 - otherIdxOnSameSide])
				{
					// If both players of one side are dead,
					// those ears revert to any of the other remaining
					// players
					SoundSetEarsSide(!isLeft, camera->lastPosition);
				}
			}

			camera->NumViews = 4;
		}
		else
		{
			CASSERT(false, "not implemented yet");
		}
	}
}
// Try to follow a player
static struct vec2 GetFollowPlayerPos(
	const struct vec2 lastPos, const PlayerData *p)
{
	const TActor *a = ActorGetByUID(p->ActorUID);
	if (a == NULL)
		return lastPos;
	return a->Pos;
}

struct vec2 CameraLookTarget(
	const struct vec2 aimDir, const struct vec2 vel,
	const struct vec2i viewSize)
{
	const float refH = MIN(viewSize.x * 9.0f / 16.0f, (float)viewSize.y);
	const struct vec2 look = svec2_add(
		svec2_scale(aimDir, LOOK_AIM * refH),
		svec2_scale(vel, LOOK_VEL_TICKS));
	const float len = svec2_length(look);
	const float maxLen = LOOK_MAX * refH;
	if (len > maxLen)
	{
		return svec2_scale(look, maxLen / len);
	}
	return look;
}

struct vec2 CameraLookStep(
	const struct vec2 look, const struct vec2 target, const float seconds)
{
	const float t = 1.0f - expf(-LOOK_FOLLOW * seconds);
	return svec2_add(look, svec2_scale(svec2_subtract(target, look), t));
}

static struct vec2 UpdateLook(
	Camera *camera, const int i, const TActor *a, const struct vec2i viewSize,
	const int ms)
{
	if (a == NULL)
	{
		camera->LookActorUID[i] = -1;
		camera->Look[i] = svec2_zero();
		return camera->Look[i];
	}
	if (camera->LookActorUID[i] != a->uid)
	{
		// New actor for this view (spawned, or another player): start on
		// it and ease into the look-ahead
		camera->LookActorUID[i] = a->uid;
		camera->Look[i] = svec2_zero();
	}
	struct vec2 target = svec2_zero();
	if (!a->dead)
	{
		// Aim radians: 0 = up, clockwise
		const float aim = ActorGetAimRadians(a);
		target = CameraLookTarget(
			svec2(sinf(aim), -cosf(aim)), a->MoveVel, viewSize);
	}
	camera->Look[i] =
		CameraLookStep(camera->Look[i], target, (float)ms / 1000.0f);
	return camera->Look[i];
}

static void ResetLooks(Camera *camera)
{
	for (int i = 0; i < MAX_LOCAL_PLAYERS; i++)
	{
		camera->LookActorUID[i] = -1;
		camera->Look[i] = svec2_zero();
	}
}

static void DoBuffer(
	Camera *camera, const int w, const struct vec2 noise,
	const struct vec2i offset);
void CameraDraw(Camera *camera, const HUDDrawData drawData)
{
	const struct vec2i centerOffset = svec2i(-4, -8);
	const int w = gGraphicsDevice.cachedConfig.Res.x;
	const int h = gGraphicsDevice.cachedConfig.Res.y;

	const struct vec2 noise = camera->shake.Delta;

	GraphicsResetClip(gGraphicsDevice.gameWindow.renderer);
	if (drawData.NumScreens == 0)
	{
		DoBuffer(camera, X_TILES, noise, centerOffset);
	}
	else
	{
		// Redo LOS if PVP, so that each split screen has its own LOS
		if (IsPVP(gCampaign.Entry.Mode) && drawData.NumScreens > 0)
		{
			LOSReset(&gMap.LOS);
		}
		if (camera->NumViews == 1)
		{
			// Single camera screen

			// Redo LOS for every local human player
			if (IsPVP(gCampaign.Entry.Mode))
			{
				CA_FOREACH(const PlayerData, p, gPlayerDatas)
				if (!p->IsLocal || !IsPlayerAliveOrDying(p) ||
					!IsPlayerHuman(p))
				{
					continue;
				}
				const TActor *a = ActorGetByUID(p->ActorUID);
				LOSCalcFrom(&gMap, Vec2ToTile(a->thing.Pos), false);
				CA_FOREACH_END()
			}

			DoBuffer(camera, X_TILES, noise, centerOffset);
		}
		else if (drawData.NumScreens == 2)
		{
			// side-by-side split
			for (int i = 0; i < drawData.NumScreens; i++)
			{
				const PlayerData *p = drawData.Players[i];
				if (!IsPlayerAliveOrDying(p))
				{
					continue;
				}
				const TActor *a = ActorGetByUID(p->ActorUID);
				if (a == NULL)
				{
					continue;
				}
				camera->lastPosition = svec2_add(a->thing.Pos, camera->Look[i]);
				camera->FocusActorUID = a->uid;
				struct vec2i centerOffsetPlayer = centerOffset;
				const Rect2i clip = Rect2iNew(
					svec2i((i & 1) ? w / 2 : 0, 0), svec2i(w / 2, h));
				GraphicsSetClip(gGraphicsDevice.gameWindow.renderer, clip);
				if (i == 1)
				{
					centerOffsetPlayer.x += w / 2 - centerOffset.x;
				}

				LOSCalcFrom(&gMap, Vec2ToTile(a->thing.Pos), false);
				DoBuffer(camera, X_TILES_HALF, noise, centerOffsetPlayer);
			}
			Draw_Line(w / 2 - 1, 0, w / 2 - 1, h - 1, colorBlack);
			Draw_Line(w / 2, 0, w / 2, h - 1, colorBlack);
		}
		else if (drawData.NumScreens >= 3 && drawData.NumScreens <= 4)
		{
			// 4 player split screen
			for (int i = 0; i < drawData.NumScreens; i++)
			{
				const PlayerData *p = drawData.Players[i];
				if (!IsPlayerAliveOrDying(p))
				{
					continue;
				}
				const TActor *a = ActorGetByUID(p->ActorUID);
				if (a == NULL)
				{
					continue;
				}
				camera->lastPosition = svec2_add(a->thing.Pos, camera->Look[i]);
				camera->FocusActorUID = a->uid;
				struct vec2i centerOffsetPlayer = centerOffset;
				const Rect2i clip = Rect2iNew(
					svec2i((i & 1) ? w / 2 : 0, (i < 2) ? 0 : h / 2 - 1),
					svec2i(w / 2, h / 2));
				GraphicsSetClip(gGraphicsDevice.gameWindow.renderer, clip);
				if (i & 1)
				{
					centerOffsetPlayer.x += w / 2 - centerOffset.x;
				}
				if (i < 2)
				{
					centerOffsetPlayer.y -= h / 4 + centerOffset.y;
				}
				else
				{
					centerOffsetPlayer.y += h / 4 - centerOffset.y;
				}
				LOSCalcFrom(&gMap, Vec2ToTile(a->thing.Pos), false);
				DoBuffer(camera, X_TILES_HALF, noise, centerOffsetPlayer);
			}
			Draw_Line(w / 2 - 1, 0, w / 2 - 1, h - 1, colorBlack);
			Draw_Line(w / 2, 0, w / 2, h - 1, colorBlack);
			Draw_Line(0, h / 2 - 1, w - 1, h / 2 - 1, colorBlack);
			Draw_Line(0, h / 2, w - 1, h / 2, colorBlack);
		}
		else
		{
			assert(0 && "not implemented yet");
		}
	}
	GraphicsResetClip(gGraphicsDevice.gameWindow.renderer);
}
static void DoBuffer(
	Camera *camera, const int w, const struct vec2 noise,
	const struct vec2i offset)
{
	DrawBufferSetFromMap(
		&camera->Buffer, &gMap, svec2_add(camera->lastPosition, noise), w);
	if (gPlayerDatas.size > 0)
	{
		DrawBufferFix(&camera->Buffer);
	}
	DrawBufferArgs args;
	memset(&args, 0, sizeof args);
	args.HUD = ConfigGetBool(&gConfig, "Graphics.ShowHUD");
	args.FocusActorUID = camera->FocusActorUID;
	args.Perspective = ConfigGetBool(&gConfig, "Graphics.Perspective");
	DrawBufferDraw(&camera->Buffer, offset, &args);
}

void CameraDrawMode(const Camera *camera)
{
	// Draw camera mode
	char cameraNameBuf[256];
	bool drawCameraMode = false;
	switch (camera->spectateMode)
	{
	case SPECTATE_NONE:
		// do nothing
		break;
	case SPECTATE_FOLLOW: {
		const TActor *a = ActorGetByUID(camera->FollowActorUID);
		if (a == NULL || !a->isInUse)
			break;
		const PlayerData *p = PlayerDataGetByUID(a->PlayerUID);
		if (p == NULL)
			break;
		sprintf(cameraNameBuf, "Following %s", p->name);
		drawCameraMode = true;
	}
	break;
	case SPECTATE_FREE:
		strcpy(cameraNameBuf, "Free-look Mode");
		drawCameraMode = true;
		break;
	default:
		CASSERT(false, "Unknown spectate mode");
		break;
	}
	if (!drawCameraMode)
	{
		return;
	}

	const int w = gGraphicsDevice.cachedConfig.Res.x;
	const int h = gGraphicsDevice.cachedConfig.Res.y;

	// Draw the message centered at the bottom
	FontStrMask(
		cameraNameBuf,
		svec2i((w - FontStrW(cameraNameBuf)) / 2, h - FontH() * 2),
		colorYellow);

	// Show camera controls
	const PlayerData *p = GetFirstPlayer(false, true, true);
	// Use default keyboard controls
	input_device_e inputDevice = INPUT_DEVICE_KEYBOARD;
	int deviceIndex = 0;
	if (p != NULL)
	{
		inputDevice = p->inputDevice;
		deviceIndex = p->deviceIndex;
	}
	struct vec2i pos = svec2i(
		(w - FontStrW("foo/bar to follow player, baz to free-look")) / 2,
		h - FontH());
	pos = DrawButton(inputDevice, deviceIndex, CMD_BUTTON1, pos);
	pos = FontStrMask("/", pos, colorYellow);
	pos = DrawButton(inputDevice, deviceIndex, CMD_BUTTON2, pos);
	pos = FontStrMask(" to follow player, ", pos, colorYellow);
	pos = DrawDirectionButtons(inputDevice, deviceIndex, pos);
	FontStrMask(" to free-look", pos, colorYellow);
}

bool CameraIsSingleScreen(void)
{
	if (ConfigGetEnum(&gConfig, "Interface.Splitscreen") == SPLITSCREEN_ALWAYS)
	{
		return false;
	}
	// Do split screen for PVP, unless whole map fits on camera, or there
	// are no human players alive or dying, then just do single screen
	const bool mapFitsInScreen =
		gMap.Size.x * TILE_WIDTH < gGraphicsDevice.cachedConfig.Res.x &&
		gMap.Size.y * TILE_HEIGHT < gGraphicsDevice.cachedConfig.Res.y;
	if (IsPVP(gCampaign.Entry.Mode) && !mapFitsInScreen &&
		GetNumPlayers(PLAYER_ALIVE_OR_DYING, true, true) > 0)
	{
		return false;
	}
	// Otherwise, if we are forcing never splitscreen, use single screen
	// regardless of whether the players are within camera range
	if (ConfigGetEnum(&gConfig, "Interface.Splitscreen") == SPLITSCREEN_NEVER)
	{
		return true;
	}
	// Finally, use split screen if players don't fit on camera
	struct vec2 min, max;
	PlayersGetBoundingRectangle(&min, &max);
	return max.x - min.x <
			   gGraphicsDevice.cachedConfig.Res.x - CAMERA_SPLIT_PADDING &&
		   max.y - min.y <
			   gGraphicsDevice.cachedConfig.Res.y - CAMERA_SPLIT_PADDING;
}
