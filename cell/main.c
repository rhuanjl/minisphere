#ifdef _MSC_VER
#define _CRT_NONSTDC_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#include "duktape.h"

#define CELL_VERSION "2.0.0"

static duk_ret_t js_game(duk_context* ctx);

static duk_context* s_duk = NULL;
static bool         s_want_spk = false;

int
main(int argc, char* argv[])
{
	// YES, these are entirely necessary. :o)
	static const char* const MESSAGES[] =
	{
		"Cell seems to be going through some sort of transformation...!",
		"He's pumping himself up like a balloon!",
		"This is the end for you!",
		"Very soon, I am going to explode. And when I do...",
		"Careful now! I wouldn't attack me if I were you...",
		"I'm quite volatile, and the slightest jolt could set me off!",
		"One minute left! There's nothing you can do now... ha ha ha ha!",
		"If only you'd finished me off a little bit sooner...",
		"Ten more seconds, and the Earth will be gone!",
		"Let's just call our little match a draw, shall we?",
	};
	
	const char* js_error_msg;
	int         num_messages;
	int         retval = EXIT_SUCCESS;
	const char* target_name;

	srand((unsigned int)time(NULL));
	num_messages = sizeof MESSAGES / sizeof(const char*);

	printf("Cell %s  (c) 2015 Fat Cerberus\n", CELL_VERSION);
	printf("%s\n\n", MESSAGES[rand() % num_messages]);
	
	// initialize JavaScript environment
	s_duk = duk_create_heap_default();
	duk_push_c_function(s_duk, js_game, DUK_VARARGS);
	duk_put_global_string(s_duk, "game");
	
	// evaluate the build script
	if (duk_pcompile_file(s_duk, 0x0, "cell.js") != DUK_EXEC_SUCCESS
		|| duk_pcall(s_duk, 0) != DUK_EXEC_SUCCESS)
	{
		js_error_msg = duk_safe_to_string(s_duk, -1);
		if (strstr(js_error_msg, "no sourcecode"))
			fprintf(stderr, "ERROR: cell.js was not found.\n");
		else
			fprintf(stderr, "ERROR: JS error (%s)", js_error_msg);
		
		retval = EXIT_FAILURE;
		goto shutdown;
	}

	target_name = argc > 1 ? argv[1] : "make";
	if (duk_get_global_string(s_duk, target_name) && duk_is_callable(s_duk, -1)) {
		printf("Processing target '%s'\n", target_name);
		if (duk_pcall(s_duk, 0) != DUK_EXEC_SUCCESS) {
			fprintf(stderr, "ERROR: JS error (%s)\n", duk_safe_to_string(s_duk, -1));
			retval = EXIT_FAILURE;
			goto shutdown;
		}
		printf("Success!\n");
	}
	else {
		fprintf(stderr, "ERROR: Nothing to do for target '%s'\n", target_name);
		retval = EXIT_FAILURE;
		goto shutdown;
	}

shutdown:
	duk_destroy_heap(s_duk);
	return retval;
}

static duk_ret_t
js_game(duk_context* ctx)
{
	FILE*       file;
	const char* name;
	const char* author;
	const char* description;
	char*       parse;
	const char* resolution;
	int         res_x, res_y;
	const char* script;

	duk_require_object_coercible(ctx, 0);
	name = (duk_get_prop_string(ctx, 0, "name"), duk_require_string(ctx, -1));
	author = (duk_get_prop_string(ctx, 0, "author"), duk_require_string(ctx, -1));
	description = (duk_get_prop_string(ctx, 0, "description"), duk_require_string(ctx, -1));
	resolution = (duk_get_prop_string(ctx, 0, "resolution"), duk_require_string(ctx, -1));
	script = (duk_get_prop_string(ctx, 0, "script"), duk_require_string(ctx, -1));
	
	// parse screen resolution
	parse = strdup(resolution);
	res_x = atoi(strtok(parse, "x"));
	res_y = atoi(strtok(NULL, "x"));

	// write game.sgm
	if (!(file = fopen("game.sgm", "wb")))
		return -1;
	fprintf(file, "name=%s", name);
	fprintf(file, "author=%s\n", author);
	fprintf(file, "description=%s\n", description);
	fprintf(file, "resolution=%s\n", author);
	fclose(file);
	return 0;
}
