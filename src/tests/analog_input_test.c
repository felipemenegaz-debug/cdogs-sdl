#define SDL_MAIN_HANDLED
#include <math.h>
#include <string.h>

#include <SDL.h>
#include <cbehave/cbehave.h>

#include <actors.h>
#include <character.h>
#include <events.h>
#include <game_events.h>
#include <handle_game_events.h>
#include <joystick.h>

// Angles in this test use the stick convention: 0 deg = right, 90 = down

static SDL_Joystick *sVirtual;
static SDL_JoystickID sId;

static float Deg(const float radians)
{
	float d = radians * 180.0f / (float)MPI;
	while (d < 0)
		d += 360;
	while (d >= 360)
		d -= 360;
	return d;
}
static float VecDeg(const struct vec2 v)
{
	return Deg(atan2f(v.y, v.x));
}
// Actor aim radians are 0 = up, clockwise; convert to the stick convention
static float AimDeg(const float aimRadians)
{
	return Deg(aimRadians - (float)MPI / 2);
}
static float AngleDiff(const float a, const float b)
{
	const float d = fabsf(a - b);
	return d > 180 ? 360 - d : d;
}
static struct vec2 StickVec(const float deg, const float push)
{
	const float r = deg * (float)MPI / 180.0f;
	return svec2(cosf(r) * push, sinf(r) * push);
}

// Move the virtual controller's left stick and deliver the axis event
static void SetStick(const float deg, const float push)
{
	const struct vec2 v = StickVec(deg, push);
	SDL_JoystickSetVirtualAxis(
		sVirtual, SDL_CONTROLLER_AXIS_LEFTX, (Sint16)(v.x * 32767));
	SDL_JoystickSetVirtualAxis(
		sVirtual, SDL_CONTROLLER_AXIS_LEFTY, (Sint16)(v.y * 32767));
	SDL_JoystickUpdate();
	SDL_ControllerAxisEvent e;
	memset(&e, 0, sizeof e);
	e.which = sId;
	e.axis = SDL_CONTROLLER_AXIS_LEFTX;
	e.value = (Sint16)(v.x * 32767);
	JoyOnAxis(e);
	e.axis = SDL_CONTROLLER_AXIS_LEFTY;
	e.value = (Sint16)(v.y * 32767);
	JoyOnAxis(e);
}

FEATURE(stick_reading, "Reading the analog stick")

	SDL_SetMainReady();
	SDL_Init(SDL_INIT_GAMECONTROLLER);
	CArrayInit(&gEventHandlers.joysticks, sizeof(Joystick));
	gConfig = ConfigDefault();
	const int index = SDL_JoystickAttachVirtual(
		SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX,
		SDL_CONTROLLER_BUTTON_MAX, 0);
	sId = JoyAdded(index);
	sVirtual = SDL_JoystickFromInstanceID(sId);
	struct vec2 v;

	SCENARIO("A virtual controller is connected")
		THEN("it should be recognised as a game controller")
			SHOULD_BE_TRUE(sId >= 0 && sVirtual != NULL);

	SCENARIO_END

	SCENARIO("Pushing the stick to 30 degrees")
		GIVEN("the stick fully pushed 30 degrees below right")
			SetStick(30, 1);
		WHEN("I read the movement vector")
			v = JoyGetMove(sId);
		THEN("it should point at 30 degrees, not snap to 0 or 45")
			SHOULD_BE_TRUE(AngleDiff(VecDeg(v), 30) < 2);
		AND("it should be full speed")
			SHOULD_BE_TRUE(fabsf(svec2_length(v) - 1) < 0.01f);

	SCENARIO_END

	SCENARIO("Sweeping the stick around a full circle")
		int distinct = 0;
		float last = -1;
		GIVEN("the stick swept from 0 to 359 degrees")
			for (int deg = 0; deg < 360; deg++)
			{
				SetStick((float)deg, 1);
				const float a = VecDeg(JoyGetMove(sId));
				if (last < 0 || AngleDiff(a, last) > 0.5f)
				{
					distinct++;
					last = a;
				}
			}
		THEN("far more than the classic 8 directions should be reachable")
			SHOULD_INT_GE(distinct, 100);

	SCENARIO_END

	SCENARIO("Small stick drift")
		GIVEN("the stick pushed only 20%")
			SetStick(45, 0.2f);
		THEN("there should be no movement")
			SHOULD_BE_TRUE(svec2_is_zero(JoyGetMove(sId)));
		AND("no direction should be pressed")
			SHOULD_BE_FALSE(JoyIsDown(sId, CMD_DIRECTIONS));

	SCENARIO_END

	SCENARIO("Pushing the stick part of the way")
		GIVEN("the stick pushed 55%")
			SetStick(0, 0.55f);
		WHEN("I read the movement vector")
			v = JoyGetMove(sId);
		THEN("speed should be partial")
			SHOULD_BE_TRUE(
				svec2_length(v) > 0.2f && svec2_length(v) < 0.6f);

	SCENARIO_END

	SCENARIO("Digital directions follow the stick's sector")
		GIVEN("the stick at 10 degrees")
			SetStick(10, 1);
		THEN("only right should be pressed")
			SHOULD_BE_TRUE(JoyIsDown(sId, CMD_RIGHT));
			SHOULD_BE_FALSE(JoyIsDown(sId, CMD_DOWN));
		GIVEN("the stick at 40 degrees")
			SetStick(40, 1);
		THEN("right and down should be pressed")
			SHOULD_BE_TRUE(JoyIsDown(sId, CMD_RIGHT));
			SHOULD_BE_TRUE(JoyIsDown(sId, CMD_DOWN));
		GIVEN("the stick at 80 degrees")
			SetStick(80, 1);
		THEN("only down should be pressed")
			SHOULD_BE_FALSE(JoyIsDown(sId, CMD_RIGHT));
			SHOULD_BE_TRUE(JoyIsDown(sId, CMD_DOWN));

	SCENARIO_END

	SCENARIO("Leaving a menu while holding the stick")
		GIVEN("the stick held right")
			SetStick(0, 1);
		WHEN("the inputs are locked, as after a pickup menu")
			JoyLock(&gEventHandlers.joysticks);
		THEN("the held stick should not move the player")
			SHOULD_BE_TRUE(svec2_is_zero(JoyGetMove(sId)));
		WHEN("the stick is released and pushed again")
			SetStick(0, 0);
			SetStick(0, 1);
		THEN("the player should move again")
			SHOULD_BE_FALSE(svec2_is_zero(JoyGetMove(sId)));

	SCENARIO_END

	SCENARIO("With the analog stick option off")
		GIVEN("Interface.AnalogStick off and the stick at 30 degrees")
			ConfigTrySetFromString(&gConfig, "Interface.AnalogStick", "false");
			SetStick(0, 0);
			SetStick(30, 1);
		THEN("there should be no analog movement")
			SHOULD_BE_TRUE(svec2_is_zero(JoyGetMove(sId)));
		AND("right should be pressed, as in the original")
			SHOULD_BE_TRUE(JoyIsDown(sId, CMD_RIGHT));
		GIVEN("the stick pushed only 40% to the right")
			SetStick(0, 0.4f);
		THEN("nothing should be pressed: the original dead zone is half way")
			SHOULD_BE_FALSE(JoyIsDown(sId, CMD_RIGHT));
		ConfigTrySetFromString(&gConfig, "Interface.AnalogStick", "true");

	SCENARIO_END

	CArrayTerminate(&gEventHandlers.joysticks);
	SDL_JoystickDetachVirtual(index);
	SDL_Quit();
FEATURE_END

FEATURE(actor_analog, "Actors driven by the analog stick")

	CharacterStoreInit(&gCampaign.Setting.characters);
	Character *c = CharacterStoreAddOther(&gCampaign.Setting.characters);
	CharacterClass cc;
	memset(&cc, 0, sizeof cc);
	c->Class = &cc;
	c->speed = 1.0f;
	c->maxHealth = 100;
	WeaponClassesInitialize(&gWeaponClasses);
	WeaponClass wc;
	memset(&wc, 0, sizeof wc);
	CSTRDUP(wc.name, "thegun");
	CArrayPushBack(&gWeaponClasses.Guns, &wc);
	GameEventsInit(&gGameEvents);
	ActorsInit();
	gConfig = ConfigDefault();

	GameEvent e = GameEventNewActorAdd(svec2(1, 1), c, NULL);
	e.u.ActorAdd.CharId = 0;
	e.u.ActorAdd.Health = 100;
	TActor *a = ActorAdd(e.u.ActorAdd);
	ActorPickupGun(a, &wc);
	HandleGameEvents(&gGameEvents, NULL, NULL, NULL, NULL);

	SCENARIO("Moving with the stick at 30 degrees")
		GIVEN("the stick fully pushed at 30 degrees")
			a->AnalogMove = StickVec(30, 1);
		WHEN("the actor is commanded")
			CommandActor(a, CMD_RIGHT | CMD_DOWN, 1);
		THEN("it should walk at 30 degrees")
			SHOULD_BE_TRUE(AngleDiff(VecDeg(a->MoveVel), 30) < 0.5f);
		AND("it should aim and shoot at 30 degrees")
			SHOULD_BE_TRUE(AngleDiff(AimDeg(ActorGetAimRadians(a)), 30) < 0.5f);
		AND("its sprite should use the nearest of the 8 directions")
			SHOULD_INT_EQUAL(a->direction, DIRECTION_DOWNRIGHT);

	SCENARIO_END

	SCENARIO("Pushing the stick half way")
		GIVEN("the stick half pushed")
			a->AnalogMove = StickVec(0, 0.5f);
		WHEN("the actor is commanded")
			CommandActor(a, CMD_RIGHT, 1);
		THEN("it should walk at half speed")
			SHOULD_BE_TRUE(fabsf(svec2_length(a->MoveVel) - 0.5f) < 0.01f);

	SCENARIO_END

	SCENARIO("Playing with the keyboard")
		GIVEN("no analog input")
			a->AnalogMove = svec2_zero();
		WHEN("right and down are pressed")
			CommandActor(a, CMD_RIGHT | CMD_DOWN, 1);
		THEN("it should walk diagonally, exactly as before")
			SHOULD_BE_TRUE(AngleDiff(VecDeg(a->MoveVel), 45) < 0.01f);
		AND("it should shoot at the classic diagonal angle")
			SHOULD_BE_TRUE(
				ActorGetAimRadians(a) == (float)dir2radians[DIRECTION_DOWNRIGHT]);

	SCENARIO_END

	SCENARIO("Being confused")
		GIVEN("a confused actor")
			a->confused = 10;
		AND("the stick at 30 degrees")
			a->AnalogMove = StickVec(30, 1);
		WHEN("the actor is commanded")
			CommandActor(a, CMD_RIGHT | CMD_DOWN, 1);
		THEN("it should walk the opposite way, at 210 degrees")
			SHOULD_BE_TRUE(AngleDiff(VecDeg(a->MoveVel), 210) < 0.5f);
		a->confused = 0;

	SCENARIO_END

	SCENARIO("Something else turns the actor")
		GIVEN("the actor aimed with the stick at 30 degrees")
			a->AnalogMove = StickVec(30, 1);
			CommandActor(a, CMD_RIGHT | CMD_DOWN, 1);
		WHEN("the game turns it to face left")
			a->direction = DIRECTION_LEFT;
		THEN("the old stick angle should be ignored")
			SHOULD_BE_TRUE(
				ActorGetAimRadians(a) == (float)dir2radians[DIRECTION_LEFT]);

	SCENARIO_END

	WeaponClassesTerminate(&gWeaponClasses);
	ActorsTerminate();
	GameEventsTerminate(&gGameEvents);
	CharacterStoreTerminate(&gCampaign.Setting.characters);
FEATURE_END

CBEHAVE_RUN(
	"Analog input features are:",
	TEST_FEATURE(stick_reading),
	TEST_FEATURE(actor_analog)
)
