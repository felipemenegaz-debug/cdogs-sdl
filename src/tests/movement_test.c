#define SDL_MAIN_HANDLED
#include <math.h>

#include <cbehave/cbehave.h>

#include <actors.h>
#include <animation.h>
#include <camera.h>

// Timings below assume the default 70 game ticks per second
#define TICKS_PER_S 70.0f
#define SPEED 1.0f

#define NEAR(a, b, eps) (fabsf((a) - (b)) < (eps))

FEATURE(walk_inertia, "Walking with a short inertia")

	SCENARIO("Starting to walk")
		struct vec2 v = svec2_zero();
		int t90 = -1;
		GIVEN("an actor at rest")
		WHEN("right is held for one second")
			for (int t = 1; t <= 70; t++)
			{
				v = ActorStepWalkVel(v, svec2(1, 0), SPEED, 1);
				if (t90 < 0 && v.x >= 0.9f * SPEED)
				{
					t90 = t;
				}
			}
		THEN("it should reach 90% speed in about 0.08 s, not instantly")
			SHOULD_INT_GE(t90, 4);
			SHOULD_BE_TRUE(t90 / TICKS_PER_S <= 0.1f);
		AND("it should end at exactly full speed")
			SHOULD_BE_TRUE(NEAR(v.x, SPEED, 1e-6f) && NEAR(v.y, 0, 1e-6f));
	SCENARIO_END

	SCENARIO("Letting go")
		struct vec2 v = svec2(SPEED, 0);
		int ticks = 0;
		float slide = 0;
		GIVEN("an actor walking at full speed")
		WHEN("the input is released")
			while (!svec2_is_zero(v) && ticks < 70)
			{
				v = ActorStepWalkVel(v, svec2_zero(), SPEED, 1);
				slide += svec2_length(v);
				ticks++;
			}
		THEN("it should stop in about 0.08 s")
			SHOULD_BE_TRUE(ticks / TICKS_PER_S <= 0.1f);
		AND("slide a little, but well under a quarter tile")
			SHOULD_BE_TRUE(slide > 1.0f && slide < TILE_WIDTH / 4.0f);
	SCENARIO_END

	SCENARIO("Turning around")
		struct vec2 v = svec2(SPEED, 0);
		int ticks = 0;
		GIVEN("an actor walking right at full speed")
		WHEN("left is held until it walks left at 90% speed")
			while (v.x > -0.9f * SPEED && ticks < 70)
			{
				v = ActorStepWalkVel(v, svec2(-1, 0), SPEED, 1);
				ticks++;
			}
		THEN("it should brake harder than when letting go, ~0.13 s in all")
			SHOULD_BE_TRUE(ticks / TICKS_PER_S <= 0.15f);
	SCENARIO_END

	SCENARIO("Walking diagonally")
		struct vec2 v = svec2_zero();
		GIVEN("an unnormalised diagonal input")
		WHEN("it is held for a second")
			for (int t = 0; t < 70; t++)
			{
				v = ActorStepWalkVel(v, svec2(1, 1), SPEED, 1);
			}
		THEN("it should not be faster than straight walking")
			SHOULD_BE_TRUE(NEAR(svec2_length(v), SPEED, 1e-5f));
	SCENARIO_END

	SCENARIO("Pushing the stick half way")
		struct vec2 v = svec2_zero();
		GIVEN("the analog stick half pushed")
		WHEN("it is held for a second")
			for (int t = 0; t < 70; t++)
			{
				v = ActorStepWalkVel(v, svec2(0, 0.5f), SPEED, 1);
			}
		THEN("it should settle at half speed")
			SHOULD_BE_TRUE(NEAR(v.y, 0.5f * SPEED, 1e-5f));
	SCENARIO_END

FEATURE_END

FEATURE(walk_cycle, "Walk cycle follows the distance walked")

	SCENARIO("Walking one stride")
		Animation a = AnimationGetActorAnimation(ACTORANIMATION_WALKING);
		int steps = 0;
		GIVEN("a walk cycle")
		WHEN("an actor walks 32 px in 1 px steps")
			for (int i = 0; i < 32; i++)
			{
				AnimationUpdate(&a, ActorWalkAnimTicks(1));
				const int f = AnimationGetFrame(&a);
				if (a.newFrame && (f == 2 || f == 6))
				{
					steps++;
				}
			}
		THEN("it should take two steps")
			SHOULD_INT_EQUAL(steps, 2);
	SCENARIO_END

	SCENARIO("Standing against a wall")
		Animation a = AnimationGetActorAnimation(ACTORANIMATION_WALKING);
		GIVEN("a walk cycle")
		WHEN("the actor does not get anywhere")
			for (int i = 0; i < 70; i++)
			{
				AnimationUpdate(&a, ActorWalkAnimTicks(0));
			}
		THEN("the legs should not move")
			SHOULD_INT_EQUAL(AnimationGetFrame(&a), 0);
	SCENARIO_END

	SCENARIO("Walking backwards")
		Animation a = AnimationGetActorAnimation(ACTORANIMATION_WALKING);
		GIVEN("a walk cycle")
		WHEN("the actor walks 6 px away from its aim")
			for (int i = 0; i < 6; i++)
			{
				AnimationUpdate(&a, ActorWalkAnimTicks(-1));
			}
		THEN("the cycle should play backwards, from frame 0 to 6")
			SHOULD_INT_EQUAL(AnimationGetFrame(&a), 6);
	SCENARIO_END

FEATURE_END

FEATURE(camera_look, "Camera looks ahead")

	SCENARIO("Aiming at full speed")
		struct vec2 look;
		GIVEN("a 1080p view")
		WHEN("the actor aims right while standing still")
			look = CameraLookTarget(svec2(1, 0), svec2_zero(), svec2i(1920, 1080));
		THEN("the camera should lean 150 px towards the aim")
			SHOULD_BE_TRUE(NEAR(look.x, 150, 0.01f) && NEAR(look.y, 0, 0.01f));
		WHEN("it also runs fast towards the aim")
			look = CameraLookTarget(svec2(1, 0), svec2(10, 0), svec2i(1920, 1080));
		THEN("the lean should be capped at 170 px")
			SHOULD_BE_TRUE(NEAR(svec2_length(look), 170, 0.01f));
	SCENARIO_END

	SCENARIO("Split screen")
		struct vec2 full, half;
		GIVEN("a full view and half of it")
			full = CameraLookTarget(svec2(1, 0), svec2_zero(), svec2i(1280, 720));
			half = CameraLookTarget(svec2(1, 0), svec2_zero(), svec2i(640, 720));
		THEN("the half view should lean half as far")
			SHOULD_BE_TRUE(NEAR(half.x * 2, full.x, 0.01f));
	SCENARIO_END

	SCENARIO("Following at different frame rates")
		struct vec2 a = svec2_zero(), b = svec2_zero();
		GIVEN("a target 200 px away")
		WHEN("half a second passes at 30 and at 144 frames per second")
			for (int i = 0; i < 15; i++)
				a = CameraLookStep(a, svec2(200, 0), 1.0f / 30.0f);
			for (int i = 0; i < 72; i++)
				b = CameraLookStep(b, svec2(200, 0), 1.0f / 144.0f);
		THEN("the camera should be at the same place")
			SHOULD_BE_TRUE(NEAR(a.x, b.x, 0.01f));
		AND("most of the way there, without overshooting")
			SHOULD_BE_TRUE(a.x > 0.9f * 200 && a.x < 200);
	SCENARIO_END

FEATURE_END

CBEHAVE_RUN(
	"Movement and camera features are:",
	TEST_FEATURE(walk_inertia),
	TEST_FEATURE(walk_cycle),
	TEST_FEATURE(camera_look)
)
