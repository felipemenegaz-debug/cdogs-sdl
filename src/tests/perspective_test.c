#define SDL_MAIN_HANDLED
#include <cbehave/cbehave.h>

#include <draw/draw_perspective.h>

#include <math.h>

#define NEAR(a, b, eps) (fabsf((a) - (b)) < (eps))

static DrawPerspective MakeCamera(const float dist)
{
	DrawPerspective p;
	DrawPerspectiveInit(&p, svec2(400, 300), svec2(160, 120), dist);
	return p;
}

FEATURE(PerspectiveFocus, "Camera focus")
	SCENARIO("The focus lands on its screen point at classic size")
		GIVEN("a perspective camera")
			const DrawPerspective p = MakeCamera(PERSPECTIVE_DIST_DEFAULT);

		WHEN("I project the focus point")
			struct vec2 s;
			float scale;
			const bool ok = DrawPerspectiveProject(
				&p, p.Focus, 0, svec2_zero(), &s, &scale);

		THEN("it lands on the focus screen point with scale 1")
			SHOULD_BE_TRUE(ok);
			SHOULD_BE_TRUE(NEAR(s.x, 160, 0.001f));
			SHOULD_BE_TRUE(NEAR(s.y, 120, 0.001f));
			SHOULD_BE_TRUE(NEAR(scale, 1, 0.0001f));
	SCENARIO_END
FEATURE_END

FEATURE(PerspectiveClassic, "Matches the classic view")
	SCENARIO("A far away camera gives the classic oblique projection")
		GIVEN("a camera so far away that perspective vanishes")
			const DrawPerspective p = MakeCamera(1e7f);

		WHEN("I project points on the ground, on wall tops and on billboards")
			struct vec2 ground, wallTop, billboard;
			DrawPerspectiveProject(
				&p, svec2(437, 271), 0, svec2_zero(), &ground, NULL);
			DrawPerspectiveProject(
				&p, svec2(352, 318), 12, svec2_zero(), &wallTop, NULL);
			DrawPerspectiveProject(
				&p, svec2(390, 305), 0, svec2(-5, -20), &billboard, NULL);

		THEN("they land where the classic renderer draws them")
			// Classic: screen = focusScreen + (x - fx, y - fy - h) + offset
			SHOULD_BE_TRUE(NEAR(ground.x, 160 + 37, 0.05f));
			SHOULD_BE_TRUE(NEAR(ground.y, 120 - 29, 0.05f));
			SHOULD_BE_TRUE(NEAR(wallTop.x, 160 - 48, 0.05f));
			SHOULD_BE_TRUE(NEAR(wallTop.y, 120 + 18 - 12, 0.05f));
			SHOULD_BE_TRUE(NEAR(billboard.x, 160 - 10 - 5, 0.05f));
			SHOULD_BE_TRUE(NEAR(billboard.y, 120 + 5 - 20, 0.05f));
	SCENARIO_END
FEATURE_END

FEATURE(PerspectiveDepth, "Depth")
	SCENARIO("Further things shrink, nearer things grow")
		GIVEN("a perspective camera")
			const DrawPerspective p = MakeCamera(PERSPECTIVE_DIST_DEFAULT);

		WHEN("I project points north and south of the focus, and a raised one")
			float north, south, raised;
			struct vec2 s;
			DrawPerspectiveProject(
				&p, svec2(400, 200), 0, svec2_zero(), &s, &north);
			DrawPerspectiveProject(
				&p, svec2(400, 380), 0, svec2_zero(), &s, &south);
			DrawPerspectiveProject(
				&p, svec2(400, 300), 12, svec2_zero(), &s, &raised);

		THEN("north is smaller, south is bigger, raised is closer")
			SHOULD_BE_TRUE(north < 1);
			SHOULD_BE_TRUE(south > 1);
			SHOULD_BE_TRUE(raised > 1);
	SCENARIO_END

	SCENARIO("Billboards face the camera")
		GIVEN("a perspective camera")
			const DrawPerspective p = MakeCamera(PERSPECTIVE_DIST_DEFAULT);

		WHEN("I project the corners of a billboard away from the focus")
			const struct vec2 pos = svec2(330, 250);
			float anchor, topLeft, bottomRight;
			struct vec2 a, tl, br;
			DrawPerspectiveProject(&p, pos, 0, svec2_zero(), &a, &anchor);
			DrawPerspectiveProject(&p, pos, 0, svec2(-32, -44), &tl, &topLeft);
			DrawPerspectiveProject(
				&p, pos, 0, svec2(32, 20), &br, &bottomRight);

		THEN("all corners are at the same depth and keep the sprite's shape")
			SHOULD_BE_TRUE(NEAR(anchor, topLeft, 0.0001f));
			SHOULD_BE_TRUE(NEAR(anchor, bottomRight, 0.0001f));
			SHOULD_BE_TRUE(NEAR(a.x - tl.x, 32 * anchor, 0.01f));
			SHOULD_BE_TRUE(NEAR(a.y - tl.y, 44 * anchor, 0.01f));
			SHOULD_BE_TRUE(NEAR(br.x - a.x, 32 * anchor, 0.01f));
			SHOULD_BE_TRUE(NEAR(br.y - a.y, 20 * anchor, 0.01f));
	SCENARIO_END

	SCENARIO("Points behind the camera are rejected")
		GIVEN("a perspective camera")
			const DrawPerspective p = MakeCamera(PERSPECTIVE_DIST_DEFAULT);

		WHEN("I project a point far south, past the camera")
			struct vec2 s;
			const bool ok = DrawPerspectiveProject(
				&p, svec2(400, 300 + 2000), 0, svec2_zero(), &s, NULL);

		THEN("projection fails")
			SHOULD_BE_FALSE(ok);
	SCENARIO_END
FEATURE_END

FEATURE(PerspectiveUnproject, "Screen to ground")
	SCENARIO("Unprojecting undoes projecting")
		GIVEN("a perspective camera")
			const DrawPerspective p = MakeCamera(PERSPECTIVE_DIST_DEFAULT);

		WHEN("I project ground points and unproject the results")
			const struct vec2 points[3] = {
				svec2(250, 180), svec2(400, 300), svec2(520, 390)};
			bool allOk = true;
			bool allMatch = true;
			for (int i = 0; i < 3; i++)
			{
				struct vec2 s, w;
				allOk = allOk &&
					DrawPerspectiveProject(
						&p, points[i], 0, svec2_zero(), &s, NULL) &&
					DrawPerspectiveUnproject(&p, s, &w);
				allMatch = allMatch && NEAR(w.x, points[i].x, 0.01f) &&
					NEAR(w.y, points[i].y, 0.01f);
			}

		THEN("I get the same ground points back")
			SHOULD_BE_TRUE(allOk);
			SHOULD_BE_TRUE(allMatch);
	SCENARIO_END

	SCENARIO("The sky has no ground point")
		GIVEN("a perspective camera")
			const DrawPerspective p = MakeCamera(PERSPECTIVE_DIST_DEFAULT);

		WHEN("I unproject a screen point far above the horizon")
			struct vec2 w;
			const bool ok =
				DrawPerspectiveUnproject(&p, svec2(160, 120 - 5000), &w);

		THEN("unprojection fails")
			SHOULD_BE_FALSE(ok);
	SCENARIO_END
FEATURE_END

CBEHAVE_RUN(
	"Perspective features are:",
	TEST_FEATURE(PerspectiveFocus),
	TEST_FEATURE(PerspectiveClassic),
	TEST_FEATURE(PerspectiveDepth),
	TEST_FEATURE(PerspectiveUnproject)
)
