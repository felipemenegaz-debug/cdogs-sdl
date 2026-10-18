/*
    C-Dogs SDL
    A port of the legendary (and fun) action/arcade cdogs.

    Copyright (c) 2013-2015, 2024 Cong Xu
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

#include <SDL_events.h>
#include <SDL_gamecontroller.h>
#include <SDL_haptic.h>

#include "c_array.h"
#include "color.h"
#include "vector.h"


typedef struct
{
	SDL_GameController *gc;
	SDL_Joystick *j;
	SDL_JoystickID id;
	SDL_Haptic *haptic;
	int hapticEffectId;
	int currentCmd;
	int previousCmd;
	int pressedCmd;
	// When switching between states, lock cmds that were already down,
	// and prevent isDown until they are released
	int lockedCmd;
} Joystick;

void JoyInit(CArray *joys);
void JoyReset(CArray *joys);
void JoyLock(CArray *joys);
void JoyTerminate(CArray *joys);

bool JoyIsDown(const SDL_JoystickID id, const int cmd);
bool JoyIsPressed(const SDL_JoystickID id, const int cmd);
int JoyGetPressed(const SDL_JoystickID id);

void JoyPrePoll(CArray *joys);

SDL_JoystickID JoyAdded(const Sint32 which);
void JoyRemoved(const Sint32 which);
void JoyOnButtonDown(const SDL_ControllerButtonEvent e);
void JoyOnButtonUp(const SDL_ControllerButtonEvent e);
void JoyOnAxis(const SDL_ControllerAxisEvent e);

// Analog left-stick vector, x right / y down, length in [0, 1].
// Zero inside the dead zone or when the stick's directions are locked.
// Quantised so tiny stick wobble does not flood the network with events.
struct vec2 JoyGetMove(const SDL_JoystickID id);

void JoyRumble(
	const SDL_JoystickID id, const float strength, const Uint32 length);
void JoyImpact(const SDL_JoystickID id);

const char *JoyName(const SDL_JoystickID id);
void JoyButtonNameColor(
	const SDL_JoystickID id, const int cmd, char *buf, color_t *color);
