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

union v3 {
	struct { float x,y,z; };
	float s[3];
};

union v3 v3_sub(union v3 a, union v3 b)
{
	union v3 r;
	for (int i = 0; i < 3; i++) r.s[i] = a.s[i] - b.s[i];
	return r;
}

float v3_dot(union v3 a, union v3 b)
{
	float sum = 0.0f;
	for (int i = 0; i < 3; i++) sum += a.s[i]*b.s[i];
	return sum;
}

union v3 v3_cross_product(union v3 b, union v3 c)
{
	union v3 a;
	a.x = b.y*c.z - b.z*c.y;
	a.y = b.z*c.x - b.x*c.z;
	a.z = b.x*c.y - b.y*c.x;
	return a;
}

union m33 {
	float s[9];
	union v3 b[3];
};

union v3 m33_get_view_v3(union m33* m)
{
	/* XXX or do I need the inverse of m? */
	return m->b[0];
}

union v3 m33_apply(union m33* m, union v3 v)
{
	// XXX TODO
	return v;
}

struct outline {
	int n_vertices;
	union v3* vertices;

	int n_polygons;
	int* polygon_materials;
	int* polygon_lookup; // (polygon_vertex_indices offset, n) pairs
	int* polygon_vertex_indices;

	// derived
	union v3* polygon_normals;
	int n_edges;
	int* edge_vertex_pairs;
	int* edge_polygon_pairs;
	int* vertex_edge_lookup; // (vertex_edges offset, n) pairs
	int* vertex_edges;

	// temporary
	int* polygon_tags;

};

static int edge_vertex_pair_compar(const void* va, const void* vb)
{
	const int* a = va;
	const int* b = vb;
	if (a[0] != b[0]) {
		return a[0] - b[0];
	} else {
		return a[1] - b[1];
	}
}

static void outline_prep(struct outline* o)
{
	/* prep normals */
	assert((o->polygon_normals = calloc(o->n_polygons, sizeof *o->polygon_normals)) != NULL);
	const int n_polygons = o->n_polygons;
	int n_max_edges = 0;
	for (int i = 0; i < n_polygons; i++) {
		int offset = o->polygon_lookup[(i<<1)];
		int n_vertices = o->polygon_lookup[(i<<1)+1];
		assert(n_vertices >= 3);
		union v3 v0 = o->vertices[o->polygon_vertex_indices[offset]];
		union v3 v1 = o->vertices[o->polygon_vertex_indices[offset+1]];
		union v3 v2 = o->vertices[o->polygon_vertex_indices[offset+2]];
		o->polygon_normals[i] = v3_cross_product(v3_sub(v1, v0), v3_sub(v2, v0));
		n_max_edges += n_vertices;
	}

	/* temporary stuff */
	assert((o->polygon_tags = calloc(o->n_polygons, sizeof *o->polygon_tags)) != NULL);


	/* find all edge pairs; sort to eliminate duplicates; remaining are
	 * unique edges */
	assert((o->edge_vertex_pairs = calloc(n_max_edges, 2*sizeof(*o->edge_vertex_pairs))) != NULL);
	int evpi = 0;
	for (int i = 0; i < n_polygons; i++) {
		int offset = o->polygon_lookup[(i<<1)];
		int n_vertices = o->polygon_lookup[(i<<1)+1];

		int prev = n_vertices - 1;
		for (int j = 0; j < n_vertices; j++) {
			int va = o->polygon_vertex_indices[offset + prev];
			int vb = o->polygon_vertex_indices[offset + j];
			prev = j;

			o->edge_vertex_pairs[evpi++] = va;
			o->edge_vertex_pairs[evpi++] = vb;
		}
	}
	qsort(o->edge_vertex_pairs, n_max_edges, 2*sizeof(*o->edge_vertex_pairs), edge_vertex_pair_compar);
	int n_edges = 0;
	int* prev = NULL;
	int* cur = o->edge_vertex_pairs;
	int* wr = o->edge_vertex_pairs;
	for (int i = 0; i < n_max_edges; i++) {
		const size_t sz = 2*sizeof(int);
		if (prev == NULL || memcmp(prev, cur, sz) != 0) {
			if (wr != cur) memcpy(wr, cur, sz);
			n_edges++;
			wr += 2;
		}
		prev = cur;
		cur += 2;
	}
	o->n_edges = n_edges;

	// TODO int* edge_polygon_pairs;
	// TODO int* vertex_edge_lookup; // (vertex_edges offset, n) pairs
	// TODO int* vertex_edges;
}

static inline int outline__must_draw_edge(struct outline* o, int edge_index)
{
	int tag_a = 0;
	int tag_b = 0;

	int polygon_a = o->edge_polygon_pairs[edge_index<<1];
	int polygon_b = o->edge_polygon_pairs[(edge_index<<1)+1];
	if (polygon_a >= 0) {
		tag_a = o->polygon_tags[polygon_a];
	}
	if (polygon_b >= 0) {
		tag_b = o->polygon_tags[polygon_b];
	}

	return tag_a != tag_b;
}

static inline int outline__next_edge(struct outline* o, int edge_index, int direction)
{
	int v = o->edge_vertex_pairs[(edge_index<<1) + direction];

	int* lookup = &o->vertex_edge_lookup[v<<1];
	int offset = lookup[0];
	int n = lookup[1];

	for (int index = offset; index < offset+n; index++) {
		int next_edge_index = o->vertex_edges[index];
		if (next_edge_index == edge_index) continue;
		if (outline__must_draw_edge(o, next_edge_index)) return next_edge_index;
	}
	return -1;
}

static int outline_draw(struct outline* o, NVGcontext* vg, union m33* tx, int draw_material)
{
	union v3 view = m33_get_view_v3(tx);

	const int n_polygons = o->n_polygons;
	int n_tags = 0;
	for (int i = 0; i < n_polygons; i++) {
		int tag = 0;
		if (o->polygon_materials[i] == draw_material) {
			tag = v3_dot(view, o->polygon_normals[i]) > 0.0f;
			if (tag) n_tags++;
		}
		o->polygon_tags[i] = tag;
	}

	if (n_tags == 0) return 0; /* nothing to draw */

	const int n_edges = o->n_edges;
	int first_edge_index = -1;
	int direction = 0; // XXX set? CW/CCW?
	for (int i = 0; i < n_edges; i++) {
		if (outline__must_draw_edge(o, i)) {
			first_edge_index = i;
			break;
		}
	}

	assert(first_edge_index > -1);

	int edge_index = first_edge_index;
	int moveto = 1;
	nvgBeginPath(vg);
	do {
		union v3 v;
		if (moveto) {
			v = o->vertices[o->edge_vertex_pairs[(edge_index<<1) + (direction^1)]];
			v = m33_apply(tx, v);
			nvgMoveTo(vg, v.x, v.y);
			moveto = 0;
		}

		v = o->vertices[o->edge_vertex_pairs[(edge_index<<1) + direction]];
		v = m33_apply(tx, v);
		nvgLineTo(vg, v.x, v.y);

		edge_index = outline__next_edge(o, first_edge_index, direction);
	} while (edge_index != first_edge_index);
	nvgClosePath(vg);

	return 1;
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
		if (dsqr > 0 && dsqr < dmax*dmax) {
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

	float x = 0.0f;
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
			nvgSave(vg);
			nvgTranslate(vg, 150, 150);
			nvgRotate(vg, x);
			nvgTranslate(vg, -150, -150);
			nvgBeginPath(vg);
			nvgMoveTo(vg, 100, 100);
			nvgLineTo(vg, 100, 200);
			nvgLineTo(vg, 200, 200);
			nvgLineTo(vg, 80, 120);
			nvgLineTo(vg, 160, 190);
			nvgLineTo(vg, 110, 190);
			nvgClosePath(vg);
			nvgFillColor(vg, nvgRGBA(255,255,255,100));
			nvgFill(vg);
			//nvgStrokeColor(vg, nvgRGBA(0,0,0,255));
			//nvgStrokeWidth(vg, 1);
			//nvgStroke(vg);
			nvgRestore(vg);
		}

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

		x += 0.01f;
	}

	SDL_GL_DeleteContext(glctx);
	SDL_DestroyWindow(window);

	return EXIT_SUCCESS;
}

