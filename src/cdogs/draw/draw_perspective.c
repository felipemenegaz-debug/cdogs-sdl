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
#include "draw/draw_perspective.h"

#include <math.h>

// Pitch of the classic view: TILE_HEIGHT / TILE_WIDTH
#define CLASSIC_COS PERSPECTIVE_CLASSIC_COS

static float ClassicSin(void)
{
	return sqrtf(1.0f - CLASSIC_COS * CLASSIC_COS);
}

void DrawPerspectiveInit(
	DrawPerspective *p, const struct vec2 focus,
	const struct vec2 focusScreen, const float dist, const float cosPitch,
	const float zoom)
{
	p->Focus = focus;
	p->FocusScreen = focusScreen;
	p->CosPitch = cosPitch;
	p->SinPitch = sqrtf(1.0f - cosPitch * cosPitch);
	p->Dist = dist;
	p->Zoom = zoom;
}

bool DrawPerspectiveProject(
	const DrawPerspective *p, const struct vec2 world, const float h,
	const struct vec2 bb, struct vec2 *screen, float *scale)
{
	// Classic coordinates to true 3D: ground y is foreshortened by the classic
	// pitch, and wall heights by its sine. The billboard offset moves along
	// the camera's right and down vectors.
	const float x = world.x - p->Focus.x + bb.x;
	const float y =
		(world.y - p->Focus.y) / CLASSIC_COS + bb.y * p->CosPitch;
	const float z = h / ClassicSin() - bb.y * p->SinPitch;

	const float sx = x;
	const float sy = y * p->CosPitch - z * p->SinPitch;
	const float depth = p->Dist - y * p->SinPitch - z * p->CosPitch;
	if (depth < p->Dist * PERSPECTIVE_NEAR)
	{
		return false;
	}
	const float s = p->Dist / depth * p->Zoom;
	*screen = svec2(p->FocusScreen.x + sx * s, p->FocusScreen.y + sy * s);
	if (scale != NULL)
	{
		*scale = s;
	}
	return true;
}

bool DrawPerspectiveUnproject(
	const DrawPerspective *p, const struct vec2 screen, struct vec2 *world)
{
	// Inverse of DrawPerspectiveProject for h = 0 and no billboard offset
	const float sx = (screen.x - p->FocusScreen.x) / p->Zoom;
	const float sy = (screen.y - p->FocusScreen.y) / p->Zoom;
	const float denom = p->Dist * p->CosPitch + sy * p->SinPitch;
	if (denom <= p->Dist * PERSPECTIVE_NEAR)
	{
		return false;
	}
	const float y = sy * p->Dist / denom;
	const float s = p->Dist / (p->Dist - y * p->SinPitch);
	*world = svec2(p->Focus.x + sx / s, p->Focus.y + y * CLASSIC_COS);
	return true;
}
