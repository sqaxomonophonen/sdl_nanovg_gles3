#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <yxml.h>


#define MAX_ARGS 16
struct state {
	int path_level;
	int in_d;
	float cx;
	float cy;
	char d_cmd;

	float arg, arg_multiplier, arg_sign;
	int arg_before_point;
	int in_arg;
	int required_args;
	int arg_counter;
	float args[MAX_ARGS];
	int is_absolute;
} st;

FILE* out;

static void get_absolute(int pair_index, float* x, float* y)
{
	int i0 = pair_index << 1;
	int i1 = (pair_index << 1) + 1;
	if (st.is_absolute) {
		*x = st.args[i0];
		*y = st.args[i1];
	} else {
		*x = st.cx + st.args[i0];
		*y = st.cy + st.args[i1];
	}
}


static void state_endarg()
{
	if (st.in_arg) {
		st.arg *= st.arg_sign;
		assert(st.arg_counter < MAX_ARGS);
		st.args[st.arg_counter++] = st.arg;
		st.in_arg = 0;
	}
	if (st.d_cmd && st.arg_counter == st.required_args) {
		float x,y,x1,y1,x2,y2;
		int set_cursor = 0;
		if (st.d_cmd == 'm') {
			get_absolute(0, &x, &y);
			set_cursor = 1;
			fprintf(out, "\tnvgMoveTo(vg, %.5f, %.5f);\n", x, y);
		} else if (st.d_cmd == 'c') {
			get_absolute(2, &x, &y);
			get_absolute(0, &x1, &y1);
			get_absolute(1, &x2, &y2);
			set_cursor = 1;
			fprintf(out, "\tnvgBezierTo(vg, %.5f, %.5f, %.5f, %.5f, %.5f, %.5f);\n", x1, y1, x2, y2, x, y);
		} else if (st.d_cmd == 'z') {
			fprintf(out, "\tnvgClosePath(vg);\n");
			st.d_cmd = 0;
		} else {
			fprintf(stderr, "unhandled d_cmd: %c\n", st.d_cmd);
			abort();
		}

		if (set_cursor) {
			st.cx = x;
			st.cy = y;
		}
		st.arg_counter = 0;
	}
}

int main(int argc, char** argv)
{
	if (argc != 3) {
		fprintf(stderr, "Usage: %s <in.svg> <out.inc.h>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	FILE* in = fopen(argv[1], "r");
	if (in == NULL) {
		fprintf(stderr, "%s: could not open\n", argv[1]);
		exit(EXIT_FAILURE);
	}

	yxml_t x;
	char stack[1<<13];
	yxml_init(&x, stack, sizeof stack);
	out = fopen(argv[2], "wb");
	assert(out != NULL);

	char name[1<<12];
	strncpy(name, argv[1], sizeof(name));
	for (char* p = name; *p; p++) {
		if (*p == '.') {
			*p = 0;
			break;
		}
	}

	fprintf(out, "static void emit_%s(NVGcontext* vg)\n", name);
	fprintf(out, "{\n");
	fprintf(out, "\tnvgBeginPath(vg);\n");

	int c;

	while ((c = fgetc(in)) != EOF) {
		yxml_ret_t r = yxml_parse(&x, c);
		switch (r) {
		case YXML_ELEMSTART:
			if (strcmp(x.elem, "path") == 0 || st.path_level) {
				st.path_level++;
			}
			break;
		case YXML_ELEMEND:
			if (st.path_level > 0) st.path_level--;
			break;
		case YXML_ATTRSTART:
			if (st.path_level == 1 && strcmp(x.attr, "d") == 0) {
				st.in_d = 1;
			}
			break;
		case YXML_ATTRVAL:
			if (st.in_d) {
				char ch = x.data[0];
				const int lowercase_bit = 0x20;
				int is_cmd = 0;
				if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) {
					st.d_cmd = ch | lowercase_bit;
					st.is_absolute = (ch & lowercase_bit) ? 0 : 1;
					is_cmd = 1;
				}
				int do_reset_arg = 0;
				if (is_cmd) {
					if (st.d_cmd == 'm') {
						st.required_args = 2;
					} else if (st.d_cmd == 'c') {
						st.required_args = 6;
					} else if (st.d_cmd == 'z') {
						st.required_args = 0;
					} else {
						fprintf(stderr, "unhandled svg path d command: %c\n", st.d_cmd);
						abort();
					}
					do_reset_arg = 1;
					st.arg_counter = 0;
				} else if (ch == ' ' || ch == ',') {
					state_endarg();
					do_reset_arg = 1;
				} else if (ch == '-') {
					st.in_arg = 1;
					st.arg_sign = -1.0f;
				} else if (ch == '.') {
					st.in_arg = 1;
					st.arg_before_point = 0;
				} else if (ch >= '0' && ch <= '9') {
					st.in_arg = 1;
					float digit = (float)(ch - '0');
					if (st.arg_before_point) {
						st.arg *= 10.0f;
						st.arg += digit;
					} else {
						st.arg_multiplier *= 0.1f;
						st.arg += digit * st.arg_multiplier;
					}
				} else {
					fprintf(stderr, "unhandled char %c\n", ch);
					abort();
				}

				if (do_reset_arg) {
					st.arg = 0.0f;
					st.arg_before_point = 1;
					st.arg_sign = 1.0f;
					st.arg_multiplier = 1.0f;
				}
			}
			break;
		case YXML_ATTREND:
			state_endarg();
			st.in_d = 0;
			break;
		default:
			break;
		}
	}

	fprintf(out, "}\n");

	fclose(in);
	fclose(out);

	return EXIT_SUCCESS;
}
