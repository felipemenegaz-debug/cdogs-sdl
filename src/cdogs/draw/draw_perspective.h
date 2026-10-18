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
#pragma once

#include <stdbool.h>

#include "vector.h"

// The classic view is an oblique 3/4 projection: a 16x12 tile is a square
// on the ground seen at a pitch where cos(pitch) = 12/16, and walls are 12px
// tall on screen. DrawPerspective treats that as a real 3D scene and looks at
// it with a perspective camera at the same pitch, so that at the camera focus
// everything lines up 1:1 with the classic view, while things further away
// (towards the top of the screen) shrink and nearer ones grow.
//
// World points are given in classic coordinates: x/y in map pixels and a
// height h in classic screen pixels (a wall top is h = 12).

// Camera distance in pixels for a screen PERSPECTIVE_REF_ROWS tall; smaller
// means stronger perspective. Taller screens scale it, so the perspective
// looks the same at any internal resolution.
#define PERSPECTIVE_DIST_DEFAULT 480.0f
#define PERSPECTIVE_REF_ROWS 240.0f
// Points closer to the camera than this fraction of the distance are culled
#define PERSPECTIVE_NEAR 0.1f

typedef struct
{
	struct vec2 Focus;		 // map point the camera looks at
	struct vec2 FocusScreen; // where the focus lands on screen
	float CosPitch;
	float SinPitch;
	float Dist;
} DrawPerspective;

void DrawPerspectiveInit(
	DrawPerspective *p, const struct vec2 focus,
	const struct vec2 focusScreen, const float dist);

// Project a world point. bb is an extra offset in pixels within the billboard
// plane, which always faces the camera (x right, y down), so sprites anchored
// at a world point keep their classic shape and only change in size.
// Returns false if the point is too close to (or behind) the camera.
bool DrawPerspectiveProject(
	const DrawPerspective *p, const struct vec2 world, const float h,
	const struct vec2 bb, struct vec2 *screen, float *scale);

// The ground (h = 0) point that projects onto a screen point.
// Returns false if the screen point is at or above the horizon.
bool DrawPerspectiveUnproject(
	const DrawPerspective *p, const struct vec2 screen, struct vec2 *world);
