#include <stdio.h>
#include <stdlib.h>

#include <yxml.h>

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
	char stack[8912];
	yxml_init(&x, stack, sizeof stack);

	int c;
	while ((c = fgetc(in)) != EOF) {
		yxml_ret_t r = yxml_parse(&x, c);
		switch (r) {
		case YXML_ELEMSTART:
			printf("<%s>\n", x.elem);
			break;
		case YXML_ELEMEND:
			break;
		case YXML_ATTRSTART:
			printf(" attr %s\n", x.attr);
		case YXML_ATTRVAL:
			printf("  val %s\n", x.data);
		}
	}

	return EXIT_SUCCESS;
}
