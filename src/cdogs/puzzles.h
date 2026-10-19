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
#pragma once

#include "map_build.h"
#include "objs.h"

// Puzzles that run during the game (Game.Puzzles):
// - a seal on a room floor ("arena_seal"): stepping near it locks the doors
//   around and calls a wave inside; the doors open when the wave is dead
//   (or after 45 s, so nobody gets stuck);
// - two levers ("lever"): knock both down within a few seconds (3 with two
//   or more players, 6 alone) for a cache of health, ammo and a gun; if
//   the time runs out the first one stands up again.

// Place the seal and the pair of levers in a generated map
void MapAddPuzzleObjects(MapBuilder *mb);

void PuzzlesInit(void);
// Server side, every frame
void PuzzlesUpdate(const int ticks);
// Doors locked by an active seal don't open
bool PuzzlesIsDoorLocked(const struct vec2i tile);
// A puzzle object was destroyed (levers)
void PuzzlesOnObjectDestroyed(const TObject *o);
