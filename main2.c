#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>

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

void v3_dump(union v3 v)
{
	printf("[%.3f %.3f %.3f]\n", v.x, v.y, v.z);
}

union v3 v3_axis_x()
{
	return (union v3){.x=1};
}

union v3 v3_axis_y()
{
	return (union v3){.y=1};
}

union v3 v3_axis_z()
{
	return (union v3){.z=1};
}

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

float v3_length(union v3 v)
{
	return sqrtf(v3_dot(v, v));
}

union v3 v3_scale(union v3 v, float scalar)
{
	for (int i = 0; i < 3; i++) v.s[i] *= scalar;
	return v;
}

union v3 v3_normalize(union v3 v)
{
	return v3_scale(v, 1.0f / v3_length(v));
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
	struct {
		union v3 basis_x;
		union v3 basis_y;
		union v3 basis_z;
	};
};

union v3 m33_row(union m33* m, int row)
{
	return m->b[row];
}

union v3 m33_col(union m33* m, int col)
{
	union v3 r;
	for (int i = 0; i < 3; i++) {
		r.s[i] = m->b[i].s[col];
	}
	return r;
}

union v3 m33_get_view_v3(union m33* m)
{
	/* XXX or do I need the inverse of m? */
	return m->basis_z;
}

union v3 m33_apply(union m33* m, union v3 v)
{
	union v3 r;
	for (int i = 0; i < 3; i++) r.s[i] = v3_dot(v, m33_row(m, i));
	return r;
}

void m33_set_identity(union m33* m)
{
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			m->b[i].s[j] = (i == j) ? 1.0f : 0.0f;
		}
	}
}

void m33_set_rotate(union m33* m, float radians, union v3 axis)
{
	axis = v3_normalize(axis);
	float c = cosf(radians);
	float s = sinf(radians);
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			float x = axis.s[i] * axis.s[j] * (1-c);
			if (i == j) {
				x += c;
			} else {
				int k = 3-i-j;
				assert(k >= 0 && k < 3);
				float sgn0 = (i-j)&1 ? 1 : -1;
				float sgn1 = (i-j)>0 ? 1 : -1;
				x += axis.s[k] * s * sgn0 * sgn1;
			}
			m->b[i].s[j] = x;
		}
	}
}

void m33_multiply(union m33* dst, union m33* a, union m33* b)
{
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			dst->b[i].s[j] = v3_dot(m33_row(a, i), m33_col(b, j));
		}
	}
}

void m33_multiply_inplace(union m33* dst, union m33* m)
{
	union m33 result;
	m33_multiply(&result, dst, m);
	memcpy(dst, &result, sizeof result);
}


union ipair {
	struct { int a,b; };
	struct { int left, right; };
	struct { int offset, length; };
	int i[2];
};

static inline void ipair_lookup(const int** out_begin, const int** out_end, union ipair* ipairs, int index, int* xs)
{
	const union ipair lu = ipairs[index];
	int* begin = &xs[lu.offset];
	int* end = begin + lu.length;
	if (out_begin) *out_begin = begin;
	if (out_end) *out_end = end;
}

struct outline {
	// specified
	int n_vertices;
	union v3* vertices;

	int n_polygons;
	int* polygon_materials;
	union ipair* polygon_lookup;
	int* polygon_vertex_indices;

	// derived
	union v3* polygon_normals;
	int n_edges;
	union ipair* edge_vertex_pairs;
	union ipair* edge_polygon_pairs;
	union ipair* vertex_edge_lookup;
	int* vertex_edges;

	// temporary
	int* polygon_flags;
	int* edge_flags;
};

static int edge_vertex_pair_compar(const void* vx, const void* vy)
{
	const union ipair* x = vx;
	const union ipair* y = vy;
	if (x->a != y->a) {
		return x->a - y->a;
	} else {
		return x->b - y->b;
	}
}

/* calculates everything in the "derived" section of struct outline, from the
 * "specified" section, and also initializes "temporary" stuff */
static void outline_prep(struct outline* o)
{
	/* prep normals */
	assert((o->polygon_normals = calloc(o->n_polygons, sizeof *o->polygon_normals)) != NULL);
	const int n_polygons = o->n_polygons;
	int n_max_edges = 0;
	for (int i = 0; i < n_polygons; i++) {
		int offset = o->polygon_lookup[i].offset;
		int n_polygon_vertices = o->polygon_lookup[i].length;
		assert(n_polygon_vertices >= 3);
		union v3 v0 = o->vertices[o->polygon_vertex_indices[offset]];
		union v3 v1 = o->vertices[o->polygon_vertex_indices[offset+1]];
		union v3 v2 = o->vertices[o->polygon_vertex_indices[offset+n_polygon_vertices-1]];
		o->polygon_normals[i] = v3_normalize(v3_cross_product(v3_sub(v1, v0), v3_sub(v2, v0)));
		n_max_edges += n_polygon_vertices;
	}

	/* find all edge pairs; sort to eliminate duplicates; remaining are
	 * unique edges */
	assert((o->edge_vertex_pairs = calloc(n_max_edges, sizeof(*o->edge_vertex_pairs))) != NULL);
	int edge_vertex_pair_index = 0;
	for (int polygon_index = 0; polygon_index < n_polygons; polygon_index++) {
		int offset = o->polygon_lookup[polygon_index].offset;
		int n_polygon_vertices = o->polygon_lookup[polygon_index].length;

		int prev = offset + n_polygon_vertices - 1;
		for (int j = offset; j < (offset+n_polygon_vertices); j++) {
			int va = o->polygon_vertex_indices[prev];
			int vb = o->polygon_vertex_indices[j];
			prev = j;

			/* ensure va < vb (because edge (va,vb) === (vb,va)) */
			if (va > vb) {
				int tmp = va;
				va = vb;
				vb = tmp;
			}

			assert(va < vb);
			assert(edge_vertex_pair_index < n_max_edges);
			o->edge_vertex_pairs[edge_vertex_pair_index].a = va;
			o->edge_vertex_pairs[edge_vertex_pair_index].b = vb;
			edge_vertex_pair_index++;

		}
	}
	assert(edge_vertex_pair_index == n_max_edges);
	qsort(o->edge_vertex_pairs, n_max_edges, sizeof(*o->edge_vertex_pairs), edge_vertex_pair_compar);
	int n_edges = 0;
	union ipair* prev = NULL;
	union ipair* cur = o->edge_vertex_pairs;
	union ipair* wr = o->edge_vertex_pairs;
	for (int i = 0; i < n_max_edges; i++) {
		if (prev == NULL || memcmp(prev, cur, sizeof *cur) != 0) {
			if (wr != cur) {
				memcpy(wr, cur, sizeof *cur);
			}
			n_edges++;
			wr++;
		}
		prev = cur;
		cur++;
	}
	assert(cur == (o->edge_vertex_pairs + n_max_edges));
	assert(wr == (o->edge_vertex_pairs + n_edges));
	o->n_edges = n_edges;

	/* trim edge_vertex_pairs to actual size now we know it */
	assert((o->edge_vertex_pairs = realloc(o->edge_vertex_pairs, n_edges*sizeof(*o->edge_vertex_pairs))) != NULL);

	/* calculate vertex->edge lookup */
	assert((o->vertex_edge_lookup = calloc(o->n_vertices, sizeof *o->vertex_edge_lookup)) != NULL);
	const int n_vertex_edges = 2*n_edges;
	assert((o->vertex_edges = calloc(n_vertex_edges, sizeof *o->vertex_edges)) != NULL);
	int vei = 0;
	/* XXX O(n^2).. or O(n_vertices * n_edges) */
	for (int i = 0; i < o->n_vertices; i++) {
		const int vei_start = vei;
		for (int j = 0; j < n_edges; j++) {
			for (int k = 0; k < 2; k++) {
				if (o->edge_vertex_pairs[j].i[k] != i) continue;
				assert(vei < n_vertex_edges);
				o->vertex_edges[vei++] = j;
			}
		}
		o->vertex_edge_lookup[i].offset = vei_start;
		o->vertex_edge_lookup[i].length = vei - vei_start;
	}
	assert(vei == n_vertex_edges);

	/* calculate edge->polygon lookup */
	assert((o->edge_polygon_pairs = calloc(n_edges, sizeof *o->edge_polygon_pairs)) != NULL);
	for (int i = 0; i < n_edges; i++) {
		o->edge_polygon_pairs[i].left = -1;
		o->edge_polygon_pairs[i].right = -1;
	}
	/* for each polygon... */
	for (int polygon_index = 0; polygon_index < n_polygons; polygon_index++) {
		const int *pvi_begin, *pvi_end;
		ipair_lookup(&pvi_begin, &pvi_end, o->polygon_lookup, polygon_index, o->polygon_vertex_indices);

		const int* pvi1 = pvi_end-1;
		/* for each polygon edge / vertex pair... */
		for (const int* pvi2 = pvi_begin; pvi2 < pvi_end; pvi2++) {
			/* find matching edge... */
			const int *ve_begin, *ve_end;
			ipair_lookup(&ve_begin, &ve_end, o->vertex_edge_lookup, *pvi1, o->vertex_edges);
			for (const int* vei = ve_begin; vei < ve_end; vei++) {
				union ipair* evps = &o->edge_vertex_pairs[*vei];
				int pvi1_found_at = -1;
				int pvi2_found_at = -1;
				for (int j = 0; j < 2; j++) {
					int edge_vertex_index = evps->i[j];
					if (edge_vertex_index == *pvi1) {
						pvi1_found_at = j;
					} else if (edge_vertex_index == *pvi2) {
						pvi2_found_at = j;
					}
				}
				assert(pvi1_found_at != -1 || pvi2_found_at != 1);
				if (pvi1_found_at == -1 || pvi2_found_at == -1) continue;

				/* edge matches; write polygon index on proper
				 * side of edge */
				union ipair* epp = &o->edge_polygon_pairs[*vei];
				if (pvi2_found_at > pvi1_found_at) {
					assert(epp->right == -1);
					epp->right = polygon_index;
				} else {
					assert(epp->left == -1);
					epp->left = polygon_index;
				}
			}

			pvi1 = pvi2;
		}
	}

	/* temporary stuff */
	assert((o->polygon_flags = calloc(o->n_polygons, sizeof *o->polygon_flags)) != NULL);
	assert((o->edge_flags = calloc(o->n_edges, sizeof *o->edge_flags)) != NULL);
}

#define DRAW (1<<0)
#define REVERSE (1<<1)
#define VISITED (1<<2)


static inline int outline__get_edge_vertex_index(struct outline* o, int edge_index, int vertex_index)
{
	return o->edge_vertex_pairs[edge_index].i[vertex_index ^ (o->edge_flags[edge_index] & REVERSE ? 1 : 0)];
}

static inline union v3 outline__get_edge_vertex(struct outline* o, union m33* tx, int edge_index, int vertex_index)
{
	union v3 v = o->vertices[outline__get_edge_vertex_index(o, edge_index, vertex_index)];
	v = m33_apply(tx, v);
	return v;
}

int XXX_TODO_RESOLVE_MULTIPLE_EXITS = 0;
static inline int outline__follow(struct outline* o, int edge_index)
{
	int vi = outline__get_edge_vertex_index(o, edge_index, 1);

	const int *ve_begin, *ve_end;
	ipair_lookup(&ve_begin, &ve_end, o->vertex_edge_lookup, vi, o->vertex_edges);
	int n_exits_found = 0;
	int found_edge;
	for (const int* vei = ve_begin; vei < ve_end; vei++) {
		if (*vei == edge_index) continue;
		if ((o->edge_flags[*vei] & DRAW) == 0) continue;
		n_exits_found++;
		found_edge = *vei;
	}

	assert(n_exits_found > 0);

	if (n_exits_found == 1) {
		return found_edge;
	} else {
		XXX_TODO_RESOLVE_MULTIPLE_EXITS = 1;
		assert(0);
		// TODO
		return -1;
	}
}

static int outline_draw(struct outline* o, NVGcontext* vg, union m33* tx, int draw_material)
{
	union v3 view = m33_get_view_v3(tx);

	/* tag visible polygons; they both have to be facing the "camera"
	 * (according to tx), and the polygon material has to match
	 * draw_material */
	const int n_polygons = o->n_polygons;
	int n_tags = 0;
	for (int i = 0; i < n_polygons; i++) {
		int flags = 0;
		if (o->polygon_materials[i] == draw_material) {
			if (v3_dot(view, o->polygon_normals[i]) > 0.0f) {
				flags |= DRAW;
				n_tags++;
			}
		}
		o->polygon_flags[i] = flags;
	}

	if (n_tags == 0) return 0; /* nothing to draw */

	const int n_edges = o->n_edges;
	n_tags = 0;
	for (int i = 0; i < n_edges; i++) {
		union ipair epp = o->edge_polygon_pairs[i];

		int draw_left = epp.left != -1 && (o->polygon_flags[epp.left] & DRAW);
		int draw_right = epp.right != -1 && (o->polygon_flags[epp.right] & DRAW);
		int flags = 0;
		if (draw_left != draw_right) {
			flags |= DRAW;
			if (draw_left) flags |= REVERSE;
			n_tags++;
		}
		o->edge_flags[i] = flags;
	}
	assert(n_tags > 0);

	nvgBeginPath(vg);

	int n_islands = 0;
	for (int i = 0; i < n_edges; i++) {
		{
			int f = o->edge_flags[i];
			if ((f & DRAW) == 0) continue;
			if (f & VISITED) continue;
		}

		float area = 0.0f;
		const int first_edge_index = i;
		int edge_index = first_edge_index;
		union v3 prev_vertex = outline__get_edge_vertex(o, tx, edge_index, 0);
		nvgMoveTo(vg, prev_vertex.x, prev_vertex.y);
		do {
			int* edge_flags = &o->edge_flags[edge_index];
			assert((*edge_flags & VISITED) == 0);
			*edge_flags |= VISITED;

			union v3 vertex = outline__get_edge_vertex(o, tx, edge_index, 1);
			nvgLineTo(vg, vertex.x, vertex.y);

			area += (vertex.x - prev_vertex.x) * (vertex.y + prev_vertex.y);
			prev_vertex = vertex;

			edge_index = outline__follow(o, edge_index);
		} while (edge_index != first_edge_index);

		nvgClosePath(vg);
		nvgPathWinding(vg, area > 0 ? NVG_CW : NVG_CCW);

		n_islands++;
	}

	return 1;
}

static void outline_init_hat(struct outline* o)
{
	const float radius = 100.0f;
	const int n_segments = 12;
	const int n_strips = 32;

	memset(o, 0, sizeof *o);

	const int n_vertices = n_segments * n_strips + 1;
	o->n_vertices = n_vertices;
	assert((o->vertices = calloc(n_vertices, sizeof *o->vertices)) != NULL);

	const int n_polygons = n_segments * n_strips;
	o->n_polygons = n_polygons;
	assert((o->polygon_materials = calloc(n_polygons, sizeof *o->polygon_materials)) != NULL);
	assert((o->polygon_lookup = calloc(n_polygons, sizeof *o->polygon_lookup)) != NULL);

	const int n_polygon_vertex_indices = (4*(n_segments-1)*n_strips) + 3*n_strips;
	assert((o->polygon_vertex_indices = calloc(n_polygon_vertex_indices, sizeof *o->polygon_vertex_indices)) != NULL);

	int vi = 0;
	int pi = 0;
	int pvi = 0;
	int segment_offset = 0;
	for (int i = 0; i < n_segments; i++) {
		union v3 v;
		float it = ((float)-i / (float)n_segments) * NVG_PI * 0.5f;
		float r = cosf(it);
		v.y = -sinf(it) * radius;

		const int polygon_material = (i == 1 || i == 4 || i > 6) ? 1 : 0;
		const int is_top = (i == n_segments-1);
		const int n_polygon_sides = is_top ? 3 : 4;

		int jprev = n_strips-1;
		for (int j = 0; j < n_strips; j++) {
			float jphi = ((float)j / (float)n_strips) * NVG_PI * 2.0f;

			// XXX choose different curve maybe?
			v.x = sinf(jphi) * r * radius;
			v.z = cosf(jphi) * r * radius;

			assert(vi < n_vertices);
			o->vertices[vi] = v;
			vi++;

			assert(pi < n_polygons);
			o->polygon_materials[pi] = polygon_material;
			o->polygon_lookup[pi].offset = pvi;
			o->polygon_lookup[pi].length = n_polygon_sides;
			pi++;

			int pvi_before = pvi;
			o->polygon_vertex_indices[pvi++] = jprev + segment_offset;
			o->polygon_vertex_indices[pvi++] = j + segment_offset;
			if (is_top) {
				o->polygon_vertex_indices[pvi++] = n_vertices-1;
			} else {
				o->polygon_vertex_indices[pvi++] = j + segment_offset + n_strips;
				o->polygon_vertex_indices[pvi++] = jprev + segment_offset + n_strips;
			}
			assert((pvi - pvi_before) == n_polygon_sides);
			jprev = j;
		}
		segment_offset += n_strips;
	}

	o->vertices[vi++] = (union v3){.y = radius};

	outline_prep(o);
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


	struct outline outline;
	outline_init_hat(&outline);

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
			union m33 tx;
			m33_set_rotate(&tx, x, v3_axis_x());
			nvgSave(vg);
			nvgTranslate(vg, 1000, 150);

			XXX_TODO_RESOLVE_MULTIPLE_EXITS = 0;
			if (outline_draw(&outline, vg, &tx, 0)) {
				nvgFillColor(vg, nvgRGBA(100,100,100,255));
				nvgFill(vg);
				nvgStrokeColor(vg, nvgRGBA(0,0,0,255));
				nvgStrokeWidth(vg, 2);
				nvgStroke(vg);
			}

			if (outline_draw(&outline, vg, &tx, 1)) {
				nvgFillColor(vg, nvgRGBA(255,0,0,255));
				nvgFill(vg);
				nvgStrokeColor(vg, nvgRGBA(0,0,0,255));
				nvgStrokeWidth(vg, 2);
				nvgStroke(vg);
			}

			if (XXX_TODO_RESOLVE_MULTIPLE_EXITS) {
				nvgBeginPath(vg);
				nvgRect(vg,200,200,50,50);
				nvgFillColor(vg, nvgRGBA(255,0,0,255));
				nvgFill(vg);
			}

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

