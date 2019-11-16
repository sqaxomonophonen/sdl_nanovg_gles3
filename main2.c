#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include <SDL.h>

#include "gl.h"
#include "nanovg.h"
#include "stb_sprintf.h"

SDL_Window* window;

NVGcontext* nanovg_create_context();

static void window_size(int* width, int* height, float* pixel_ratio)
{
	int prev_width = *width;
	int prev_height = *height;
	SDL_GL_GetDrawableSize(window, width, height);
	if ((*width != prev_width || *height != prev_height)) {
		printf("%d×%d -> %d×%d\n", prev_width, prev_height, *width, *height);
	}

	int w, h;
	SDL_GetWindowSize(window, &w, &h);
	*pixel_ratio = *width / w;
}

struct guy {
	float eye_r;
	float eye_spacing;

	float x;
	float y;
	float target_x;
	float target_y;
	float eye_left_blink;
	float eye_right_blink;
	int blink_timer;
	float head_tilt;
	float head_up;
	float head_turn;
	float head_bob_phi;
	float lookat_x;
	float lookat_y;
};

static void _guy_set_blink_timer(struct guy* guy)
{
	guy->blink_timer = 100 + (rand() % 300);
}

static void guy_init(struct guy* guy)
{
	memset(guy, 0, sizeof *guy);
	guy->eye_spacing = 0.5;
	guy->eye_r = 1.8;
	guy->head_up = 0;
	guy->x = 100;
	guy->y = 100;
	guy->target_x = guy->x;
	guy->target_y = guy->y;
	_guy_set_blink_timer(guy);
}

static void guy_lookat(struct guy* guy, float x, float y)
{
	guy->lookat_x = x;
	guy->lookat_y = y;
}

static void guy_set_target(struct guy* guy, float x, float y)
{
	guy->target_x = x;
	guy->target_y = y;
}

static void guy_step(struct guy* guy)
{
	int is_walking = 0;
	{
		float dx = guy->target_x - guy->x;
		float dy = guy->target_y - guy->y;
		float dsqr = dx*dx + dy*dy;
		if (dsqr > 0.01f) {
			float d = sqrtf(dsqr);
			float udx = dx / d;
			float udy = dy / d;
			float speed = 2.0f;
			if (d < speed) speed = d;
			guy->x += udx * speed;
			guy->y += udy * speed;
			guy->head_tilt = sinf(guy->head_bob_phi) * 0.15;
			guy->head_turn = guy->head_tilt * 1.0 + udx * 0.5;
			guy->head_bob_phi += 0.3f;
			is_walking = 1;
		} else {
			guy->head_tilt = 0.0f;
			guy->head_turn = 0.0f;
			guy->head_bob_phi = 0.0f;
		}
	}

	{
		float dx = guy->lookat_x - guy->x;
		float dy = guy->lookat_y - guy->y;
		float dsqr = dx*dx + dy*dy;
		const float dmax = 100.0f;
		if (dsqr < dmax*dmax) {
			float d = sqrtf(dsqr);
			float udx = dx / d;
			float udy = dy / d;
			float s = 1.0f;
			const float ease_r = 20.0f;
			if (d < ease_r) s = d / ease_r;
			guy->head_turn = udx * 0.6 * s;
			guy->head_up = udy * -5 * s;
		} else {
			if (!is_walking) guy->head_turn = 0.0f;
			guy->head_up = 0.0f;
		}
	}

	{
		guy->blink_timer--;
		if (guy->blink_timer < 0) {
			const int blink_duration = 10;
			float blink_t = (float)-guy->blink_timer / (float)blink_duration;
			float blink = blink_t < 0.5f ? (blink_t*2.0f) : 1.0f - (blink_t - 0.5f)*2.0f;
			guy->eye_left_blink = blink;
			guy->eye_right_blink = blink;
			if (guy->blink_timer < -blink_duration) {
				_guy_set_blink_timer(guy);
			}
		} else {
			guy->eye_left_blink = 0.0f;
			guy->eye_right_blink = 0.0f;
		}
	}
}

static void guy_draw(struct guy* guy, NVGcontext* vg)
{
	nvgSave(vg);

	nvgTranslate(vg, guy->x, guy->y);

	/* body */
	{
		nvgBeginPath(vg);

		const float x = 0;
		const float y = 10;
		const float x1 = 5;
		const float y1 = -5;
		const float x2 = 14;
		const float y2 = 10;
		const float x3 = 10;
		const float y3 = 20;

		nvgMoveTo(vg, x, y);
		nvgBezierTo(vg, x+x1, y+y1, x+x2, y+y2, x+x3, y+y3);
		nvgLineTo(vg, -10, 30);
		nvgBezierTo(vg, x-x2, y+y2, x-x1, y+y1, x, y);
		nvgClosePath(vg);
		nvgFillColor(vg, nvgRGBA(0,50,150,255));
		nvgFill(vg);
		nvgStrokeColor(vg, nvgRGBA(0,0,0,255));
		nvgStrokeWidth(vg, 2);
		nvgStroke(vg);
	}

	/* head */
	{
		nvgSave(vg);

		const float head_r = 10;

		nvgTranslate(vg, 0, head_r);
		nvgRotate(vg, guy->head_tilt);
		nvgTranslate(vg, 0, -head_r);

		nvgBeginPath(vg);
		nvgCircle(vg, 0, 0, head_r);
		nvgFillColor(vg, nvgRGBA(240,120,100,255));
		nvgFill(vg);
		nvgStrokeColor(vg, nvgRGBA(0,0,0,255));
		nvgStrokeWidth(vg, 2);
		nvgStroke(vg);

		/* eyes */
		const float eye_r = guy->eye_r;

		float eye_left_phi = -guy->eye_spacing + guy->head_turn;
		float eye_right_phi = guy->eye_spacing + guy->head_turn;

		nvgFillColor(vg, nvgRGBA(0,0,0,255));
		if (eye_left_phi > -NVG_PI/2 && eye_left_phi < NVG_PI/2) {
			float eyex = sinf(eye_left_phi) * head_r;
			nvgBeginPath(vg);
			nvgEllipse(vg, eyex, -guy->head_up, eye_r, eye_r * (1.0f - guy->eye_left_blink));
			nvgFill(vg);
		}

		if (eye_right_phi > -NVG_PI/2 && eye_right_phi < NVG_PI/2) {
			float eyex = sinf(eye_right_phi) * head_r;
			nvgBeginPath(vg);
			nvgEllipse(vg, eyex, -guy->head_up, eye_r, eye_r * (1.0f - guy->eye_right_blink));
			nvgFill(vg);
		}

		nvgRestore(vg);
	}


	nvgRestore(vg);
}

int main(int argc, char** argv)
{
	assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) == 0);
	atexit(SDL_Quit);

	SDL_GLContext glctx;
	{
		#ifdef BUILD_LINUX
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
		#elif BUILD_MACOS
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
		#else
		#error "missing BUILD_* define"
		#endif

		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 1);

		window = SDL_CreateWindow(
				"SDL2/NanoVG/GLES3",
				SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
				1920, 1080,
				SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
		if (window == NULL) {
			fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
			abort();
		}
		glctx = SDL_GL_CreateContext(window);
		if (!glctx) {
			fprintf(stderr, "SDL_GL_CreateContextfailed: %s\n", SDL_GetError());
			abort();
		}
	}

	NVGcontext* vg = nanovg_create_context();
	assert(vg != NULL);

	int font = nvgCreateFont(vg, "sans", "./nanovg/example/Roboto-Regular.ttf");
	assert(font != -1);

	nvgFontFace(vg, "sans");

	int screen_width = 0;
	int screen_height = 0;
	float pixel_ratio = 0.0f;
	window_size(&screen_width, &screen_height, &pixel_ratio);

	int swap_interval = 1;
	SDL_GL_SetSwapInterval(1);

	struct guy guy;
	guy_init(&guy);

	int exiting = 0;
	float fps = 0.0f;
	int fps_counter = 0;
	int fullscreen = 0;
	Uint32 last_ticks = 0;

	while (!exiting) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT) {
				exiting = 1;
			} else if (e.type == SDL_KEYDOWN) {
				if (e.key.keysym.sym == SDLK_ESCAPE) {
					exiting = 1;
				} else if (e.key.keysym.sym == SDLK_SPACE) {
					swap_interval ^= 1;
					SDL_GL_SetSwapInterval(swap_interval);
				} else if (e.key.keysym.sym == SDLK_f) {
					fullscreen = !fullscreen;
					//SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
					SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
				}
			} else if (e.type == SDL_MOUSEBUTTONDOWN) {
				guy_set_target(&guy, e.button.x, e.button.y);
			} else if (e.type == SDL_MOUSEMOTION) {
				guy_lookat(&guy, e.motion.x, e.motion.y);
			} else if (e.type == SDL_WINDOWEVENT) {
				if (e.window.event == SDL_WINDOWEVENT_RESIZED) {
					window_size(&screen_width, &screen_height, &pixel_ratio);
				}
			}
		}

		glViewport(0, 0, screen_width, screen_height);
		glClearColor(0, 0.2, 0.1, 0);
		glClear(GL_COLOR_BUFFER_BIT);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_CULL_FACE);
		glDisable(GL_DEPTH_TEST);

		nvgBeginFrame(vg, screen_width / pixel_ratio, screen_height / pixel_ratio, pixel_ratio);

		guy_step(&guy);
		guy_draw(&guy, vg);

		{
			char buf[100];
			stbsp_snprintf(buf, sizeof buf, "%.1f fps", fps);

			nvgSave(vg);
			nvgTranslate(vg, 7, 20);
			nvgFontSize(vg, 20.0f);
			nvgTextAlign(vg, NVG_ALIGN_LEFT);
			nvgFillColor(vg, nvgRGBA(0,0,0,255));
			nvgText(vg, 2, 2, buf, NULL);
			nvgFillColor(vg, nvgRGBA(255,255,255,255));
			nvgText(vg, 0, 0, buf, NULL);
			nvgRestore(vg);
		}

		nvgEndFrame(vg);

		SDL_GL_SwapWindow(window);

		fps_counter++;
		Uint32 ticks = SDL_GetTicks();
		if (ticks > last_ticks + 1000) {
			fps = ((float)ticks - (float)last_ticks)*0.001f * (float)fps_counter;
			last_ticks = ticks;
			fps_counter = 0;
		}
	}

	SDL_GL_DeleteContext(glctx);
	SDL_DestroyWindow(window);

	return EXIT_SUCCESS;
}

