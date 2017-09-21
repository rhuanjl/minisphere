/**
 *  miniSphere JavaScript game engine
 *  Copyright (c) 2015-2017, Fat Cerberus
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  * Neither the name of miniSphere nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
**/

#include "minisphere.h"

#include <libmng.h>
#include <zlib.h>
#include "api.h"
#include "async.h"
#include "audio.h"
#include "debugger.h"
#include "galileo.h"
#include "input.h"
#include "jsal.h"
#include "legacy.h"
#include "map_engine.h"
#include "pegasus.h"
#include "sockets.h"
#include "spriteset.h"
#include "vanilla.h"

// enable Windows visual styles (MSVC)
#ifdef _MSC_VER
#pragma comment(linker, \
    "\"/manifestdependency:type='Win32' "\
    "name='Microsoft.Windows.Common-Controls' "\
    "version='6.0.0.0' "\
    "processorArchitecture='*' "\
    "publicKeyToken='6595b64144ccf1df' "\
    "language='*'\"")
#endif

static void on_enqueue_js_job   (void);
static void on_socket_idle      (void);
static bool initialize_engine   (void);
static void shutdown_engine     (void);
static bool find_startup_game   (path_t* *out_path);
static bool parse_command_line  (int argc, char* argv[], path_t* *out_game_path, bool *out_want_fullscreen, int *out_fullscreen, int *out_verbosity, bool *out_want_throttle, bool *out_want_debug);
static void print_banner        (bool want_copyright, bool want_deps);
static void print_usage         (void);
static void report_error        (const char* fmt, ...);
static void show_error_screen   (const char* message);

ALLEGRO_EVENT_QUEUE* g_events = NULL;
game_t*              g_game = NULL;
path_t*              g_game_path = NULL;
path_t*              g_last_game_path = NULL;
screen_t*            g_screen = NULL;
font_t*              g_system_font = NULL;

static jmp_buf s_jmp_exit;
static jmp_buf s_jmp_restart;

static const char* const ERROR_TEXT[][2] =
{
	{ ":pig_nose: *MUNCH*", "a hunger-pig just devourized your game!" },
	{ "*CRASH!*", "it's an 812-car pileup!" },
	{ "so, um... a funny thing happened...", "...on the way to the boss..." },
	{ "here's the deal.", "the game encountered an error." },
	{ "this game sucks!", "or maybe it's just the programmer..." },
	{ "cows eat kitties. pigs don't eat cows.", "they just get \"replaced\" by them." },
	{ "hey look, a squirrel!", "I wonder if IT'S responsible for this." },
	{ "sorry. it's just...", "...well, this is a trainwreck of a game." },
	{ "you better run, and you better hide...", "...'cause a big fat hawk just ate that guy!" },
	{ "an exception was thrown.", "miniSphere takes exception to sucky games." },
	{ "honk. HONK. honk. HONK. :o)", "there's a clown behind you." },
	{ "this game has OVER NINE THOUSAND errors.", "WHAT?! 9000?! no way that can be right!" },
};

int
main(int argc, char* argv[])
{
	// HERE BE DRAGONS!
	// as the oldest function in the miniSphere codebase by definition, this has become
	// something of a hairball over time, and likely quite fragile.  don't be surprised if
	// attempting to edit it causes something to break. :o)

	lstring_t*           dialog_name;
	const char*          error_stack = NULL;
	const char*          error_text;
	ALLEGRO_FILECHOOSER* file_dlg;
	path_t*              games_path;
	image_t*             icon;
	size2_t              resolution;
	const path_t*        script_path;
	bool                 use_conserve_cpu;
	int                  use_frameskip;
	bool                 use_fullscreen;
	int                  use_verbosity;
	bool                 want_debug;

	// parse the command line
	if (parse_command_line(argc, argv, &g_game_path,
		&use_fullscreen, &use_frameskip, &use_verbosity, &use_conserve_cpu, &want_debug))
	{
		console_init(use_verbosity);
	}
	else {
		return EXIT_FAILURE;
	}

	print_banner(true, false);
	printf("\n");

	// print out options
	console_log(1, "parsing command line");
	console_log(1, "    game path: %s", g_game_path != NULL ? path_cstr(g_game_path) : "<none provided>");
	console_log(1, "    fullscreen: %s", use_fullscreen ? "on" : "off");
	console_log(1, "    frameskip limit: %d frames", use_frameskip);
	console_log(1, "    sleep when idle: %s", use_conserve_cpu ? "yes" : "no");
	console_log(1, "    console verbosity: V%d", use_verbosity);
#if defined(MINISPHERE_SPHERUN)
	console_log(1, "    debugger mode: %s", want_debug ? "active" : "passive");
#endif
	console_log(1, "");

	if (!initialize_engine())
		return EXIT_FAILURE;

	// set up jump points for script bailout
	console_log(1, "setting up jump points for longjmp");
	if (setjmp(s_jmp_exit)) {
		// user closed window, script called Exit(), etc.
		if (g_screen != NULL)
			use_fullscreen = screen_get_fullscreen(g_screen);
		shutdown_engine();
		if (g_last_game_path != NULL) {  // returning from ExecuteGame()?
			if (!initialize_engine()) {
				path_free(g_last_game_path);
				return EXIT_FAILURE;
			}
			g_game_path = g_last_game_path;
			g_last_game_path = NULL;
		}
		else {
			return EXIT_SUCCESS;
		}
	}
	if (setjmp(s_jmp_restart)) {
		// script called RestartGame() or ExecuteGame()
		use_fullscreen = screen_get_fullscreen(g_screen);
		shutdown_engine();
		console_log(1, "\nrestarting to launch new game");
		console_log(1, "    path: %s", path_cstr(g_game_path));
		if (!initialize_engine())
			return EXIT_FAILURE;
	}

	// locate the game manifest
	console_log(1, "searching for a game to launch");
	games_path = path_rebase(path_new("miniSphere/Games/"), home_path());
	path_mkdir(games_path);
	if (g_game_path == NULL) {
		// no game specified on command line, see if we have a startup game
		find_startup_game(&g_game_path);
	}
	if (g_game_path != NULL) {
		// user provided a path or startup game was found, attempt to load it
		g_game = game_open(path_cstr(g_game_path));
	}
	else {
		// no game path provided and no startup game, let user find one
		dialog_name = lstr_newf("%s - Select a Sphere game to launch", SPHERE_ENGINE_NAME);
		file_dlg = al_create_native_file_dialog(path_cstr(games_path),
			lstr_cstr(dialog_name),
			"game.sgm;game.s2gm;*.spk", ALLEGRO_FILECHOOSER_FILE_MUST_EXIST);
		al_show_native_file_dialog(NULL, file_dlg);
		lstr_free(dialog_name);
		if (al_get_native_file_dialog_count(file_dlg) > 0) {
			path_free(g_game_path);
			g_game_path = path_new(al_get_native_file_dialog_path(file_dlg, 0));
			g_game = game_open(path_cstr(g_game_path));
			al_destroy_native_file_dialog(file_dlg);
		}
		else {
			// user cancelled the dialog box
			al_destroy_native_file_dialog(file_dlg);
			path_free(games_path);
			return EXIT_SUCCESS;
		}
	}
	path_free(games_path);

	if (g_game == NULL) {
		// if after all that, we still don't have a valid game pointer, bail out;
		// there's not much else we can do.
#if !defined(MINISPHERE_SPHERUN)
		al_show_native_message_box(NULL, "Unable to Load Game", path_cstr(g_game_path),
			"miniSphere either couldn't read the game manifest or a manifest file was not found.  Check that the directory listed above contains a valid Sphere game manifest file.\n\n"
			"For Sphere developers:\nUsing SpheRun to start the game from the command line may yield more insight.",
			NULL, ALLEGRO_MESSAGEBOX_ERROR);
#else
		fprintf(stderr, "ERROR: couldn't start game '%s'\n", path_cstr(g_game_path));
#endif
		sphere_exit(false);
	}

	// set up the render context ("screen") so we can draw stuff
	resolution = game_resolution(g_game);
	if (!(icon = image_load("@/icon.png")))
		icon = image_load("#/icon.png");
	g_screen = screen_new(game_name(g_game), icon, resolution, use_frameskip, !use_conserve_cpu);
	if (g_screen == NULL) {
		al_show_native_message_box(NULL, "Unable to Create Render Context", "miniSphere couldn't create a render context.",
			"Your hardware may be too old to run miniSphere, or there could be a problem with the drivers on this system.  Check that your graphics drivers in particular are fully installed and up-to-date.",
			NULL, ALLEGRO_MESSAGEBOX_ERROR);
		return EXIT_FAILURE;
	}

	al_set_new_bitmap_flags(ALLEGRO_NO_PREMULTIPLIED_ALPHA);
	al_set_blender(ALLEGRO_ADD, ALLEGRO_ALPHA, ALLEGRO_INVERSE_ALPHA);
	g_events = al_create_event_queue();
	al_register_event_source(g_events,
		al_get_display_event_source(screen_display(g_screen)));
	attach_input_display();
	kb_load_keymap();

	api_init();
	initialize_vanilla_api();
	initialize_pegasus_api();

	// attempt to locate and load system font
	console_log(1, "loading system default font");
	if (!(g_system_font = legacy_default_font())) {
		al_show_native_message_box(screen_display(g_screen), "No System Font Available", "The system font couldn't be loaded.",
			"miniSphere was unable to find the system font or an error occurred while loading it.  As a default font is required for normal operation, the engine will now close.",
			NULL, ALLEGRO_MESSAGEBOX_ERROR);
		return EXIT_FAILURE;
	}

	// switch to fullscreen if necessary and initialize clipping
	if (use_fullscreen)
		screen_toggle_fullscreen(g_screen);
	screen_show_mouse(g_screen, false);

	// enable the SSj debug server, wait for a connection if requested.
#if defined(MINISPHERE_SPHERUN)
	if (want_debug) {
		al_clear_to_color(al_map_rgba(0, 0, 0, 255));
		screen_draw_status(g_screen, "waiting for debugger", color_new(255, 255, 255, 255));
		al_flip_display();
		al_clear_to_color(al_map_rgba(0, 0, 0, 255));
	}
	debugger_init(want_debug, false);
#endif

	// execute the main script
	script_path = game_script_path(g_game);
	if (!script_eval(path_cstr(script_path), game_version(g_game) >= 2))
		goto on_js_error;

	if (game_version(g_game) >= 2 && jsal_is_object_coercible(-1)) {
		// modular mode (Sv2).  check for an exported Game class and instantiate it,
		// then call game.start().
		jsal_get_prop_string(-1, "default");
		if (jsal_is_function(-1)) {
			if (!jsal_try_construct(0))
				goto on_js_error;
			jsal_get_prop_string(-1, "start");
			jsal_pull(-2);
			if (jsal_is_function(-2) && !jsal_try_call_method(0))
				goto on_js_error;
		}
		jsal_pop(2);
	}

	// in Sv1 mode only, call game() function (if it exists)
	if (game_version(g_game) <= 1) {
		jsal_get_global_string("game");
		if (jsal_is_function(-1) && !jsal_try_call(0))
			goto on_js_error;
		jsal_pop(2);
	}

	// start the Sphere v2 frame loop.  note that this isn't contingent on the
	// game's API version: the loop terminates when there are no Dispatch API jobs,
	// so Sphere 1.x compatibility is not compromised.
	if (!pegasus_run())
		goto on_js_error;

	sphere_exit(false);

on_js_error:
	jsal_dup(-1);
	error_text = jsal_to_string(-1);
	screen_show_mouse(g_screen, true);
	if (jsal_is_object_coercible(-2)) {
		jsal_get_prop_string(-2, "stack");
		error_stack = jsal_get_string(-1);
	}
	fprintf(stderr, "GAME CRASH: uncaught JavaScript exception.\n");
	if (error_stack != NULL) {
		fprintf(stderr, "%s\n", error_stack);
		if (error_text[strlen(error_text) - 1] != '\n')
			jsal_push_sprintf("JavaScript exception!\n\n%s\n", error_stack);
		else
			jsal_push_sprintf("%s\n", error_text);
	}
	else {
		fprintf(stderr, "%s\n", error_text);
		jsal_push_sprintf("JavaScript exception\n\n%s\n", error_text);
	}
	show_error_screen(jsal_get_string(-1));
	sphere_exit(false);
}

no_return
sphere_abort(const char* message)
{
	show_error_screen(message);
	sphere_exit(false);
}

no_return
sphere_exit(bool shutting_down)
{
	if (shutting_down) {
		path_free(g_last_game_path);
		g_last_game_path = NULL;
	}
	longjmp(s_jmp_exit, 1);
}

void
sphere_run(bool allow_dispatch)
{
	ALLEGRO_EVENT event;

	sockets_update();

#if defined(MINISPHERE_SPHERUN)
	if (allow_dispatch)
		debugger_update();
#endif

	if (allow_dispatch)
		async_run_jobs(ASYNC_TICK);
	update_input();
	audio_update();

	// process Allegro events
	while (al_get_next_event(g_events, &event)) {
		switch (event.type) {
		case ALLEGRO_EVENT_DISPLAY_CLOSE:
			sphere_exit(true);
		}
	}
}

no_return
sphere_restart(void)
{
	longjmp(s_jmp_restart, 1);
}

void
sphere_sleep(double time)
{
	double end_time;
	double time_left;

	end_time = al_get_time() + time;
	do {
		time_left = end_time - al_get_time();
		if (time_left > 0.001)  // engine may stall with < 1ms timeout
			al_wait_for_event_timed(g_events, NULL, time_left);
		sphere_run(false);
	} while (al_get_time() < end_time);
}

static void
on_enqueue_js_job(void)
{
	script_t* script;

	script = script_new_func(0);
	async_defer(script, 0, ASYNC_TICK);
}

static void
on_socket_idle(void)
{
	sphere_sleep(0.05);
}

static bool
initialize_engine(void)
{
	uint32_t al_version;

	srand(time(NULL));

	// initialize Allegro
	al_version = al_get_allegro_version();
	console_log(1, "initializing Allegro %u.%u.%u.%u",
		al_version >> 24, (al_version >> 16) & 0xFF, (al_version >> 8) & 0xFF,
		(al_version & 0xFF) - 1);
	al_set_org_name("Fat Cerberus");
	al_set_app_name("miniSphere");
	if (!al_init())
		goto on_error;
	if (!al_init_native_dialog_addon())
		goto on_error;
	if (!al_init_primitives_addon())
		goto on_error;
	if (!al_init_image_addon())
		goto on_error;

	// initialize networking
	console_log(1, "initializing Dyad %s", dyad_getVersion());
	dyad_init();
	dyad_setUpdateTimeout(0.0);

	// initialize JavaScript
	console_log(1, "initializing ChakraCore");
	if (!jsal_init())
		goto on_error;
	jsal_on_enqueue_job(on_enqueue_js_job);

	// initialize engine components
	async_init();
	galileo_init();
	audio_init();
	initialize_input();
	sockets_init(on_socket_idle);
	spritesets_init();
	map_engine_init();
	scripts_init();

	legacy_init();

	return true;

on_error:
	al_show_native_message_box(NULL, "Unable to Start", "Does your car turn over in the morning?",
		"miniSphere was unable to initialize one or more engine components.  The engine cannot continue in this state and will now close.",
		NULL, ALLEGRO_MESSAGEBOX_ERROR);
	return false;
}

static void
shutdown_engine(void)
{
	kb_save_keymap();

#if defined(MINISPHERE_SPHERUN)
	debugger_uninit();
#endif

	map_engine_uninit();
	shutdown_input();
	scripts_uninit();
	sockets_uninit();

	console_log(1, "shutting down JavaScript");
	jsal_uninit();

	console_log(1, "shutting down Dyad");
	dyad_shutdown();

	spritesets_uninit();
	audio_uninit();
	galileo_uninit();
	async_uninit();

	console_log(1, "shutting down Allegro");
	screen_free(g_screen);
	g_screen = NULL;
	if (g_events != NULL)
		al_destroy_event_queue(g_events);
	g_events = NULL;
	game_unref(g_game);
	g_game = NULL;
	al_uninstall_system();
}

static bool
find_startup_game(path_t* *out_path)
{
	ALLEGRO_FS_ENTRY* engine_dir;
	const char*       file_ext;
	const char*       filename;
	ALLEGRO_FS_ENTRY* fse;
	int               n_spk_files = 0;

	// prefer a bundled startup game over an SPK if one exists
	*out_path = path_rebase(path_new("startup/game.sgm"), assets_path());
	if (al_filename_exists(path_cstr(*out_path)))
		return true;  // found a startup game
	path_free(*out_path);
	*out_path = path_rebase(path_new("startup/game.json"), assets_path());
	if (al_filename_exists(path_cstr(*out_path)))
		return true;  // found a startup game
	path_free(*out_path);

	// check for a single bundled SPK package
	*out_path = path_dup(assets_path());
	engine_dir = al_create_fs_entry(path_cstr(*out_path));
	al_open_directory(engine_dir);
	while (fse = al_read_directory(engine_dir)) {
		filename = al_get_fs_entry_name(fse);
		file_ext = strrchr(filename, '.');
		if (file_ext != NULL && strcmp(file_ext, ".spk") == 0) {
			if (++n_spk_files == 1)
				*out_path = path_new(filename);
		}
		al_destroy_fs_entry(fse);
	}
	al_close_directory(engine_dir);
	if (n_spk_files == 1)
		return true;  // found an SPK

	// as a last resort, use the default startup game
	*out_path = path_rebase(path_new("system/startup.spk"), assets_path());
	if (al_filename_exists(path_cstr(*out_path)))
		return true;
	path_free(*out_path);
	*out_path = path_rebase(path_new("../share/minisphere/system/startup.spk"), assets_path());
	if (al_filename_exists(path_cstr(*out_path)))
		return true;
	path_free(*out_path);
	*out_path = NULL;

	// if we reached this point, no suitable startup game was found.
	path_free(*out_path);
	*out_path = NULL;
	return false;
}

static bool
parse_command_line(
	int argc, char* argv[],
	path_t* *out_game_path, bool *out_want_fullscreen, int *out_frameskip,
	int *out_verbosity, bool *out_want_throttle, bool *out_want_debug)
{
	bool parse_options = true;

	int i, j;

	// establish default settings
#if defined(MINISPHERE_SPHERUN)
	*out_want_fullscreen = false;
#else
	*out_want_fullscreen = true;
	#endif

	*out_game_path = NULL;
	*out_frameskip = 20;
	*out_verbosity = 0;
	*out_want_throttle = true;
	*out_want_debug = false;

	// process command line arguments
	for (i = 1; i < argc; ++i) {
		if (strstr(argv[i], "--") == argv[i] && parse_options) {
			if (strcmp(argv[i], "--") == 0)
				parse_options = false;
			else if (strcmp(argv[i], "--frameskip") == 0) {
				if (++i >= argc) goto missing_argument;
				*out_frameskip = atoi(argv[i]);
			}
			else if (strcmp(argv[i], "--no-sleep") == 0) {
				*out_want_throttle = false;
			}
			else if (strcmp(argv[i], "--fullscreen") == 0) {
				*out_want_fullscreen = true;
			}
			else if (strcmp(argv[i], "--windowed") == 0) {
				*out_want_fullscreen = false;
			}
#if defined(MINISPHERE_SPHERUN)
			else if (strcmp(argv[i], "--version") == 0) {
				print_banner(true, true);
				return false;
			}
			else if (strcmp(argv[i], "--help") == 0) {
				print_usage();
				return false;
			}
			else if (strcmp(argv[i], "--debug") == 0) {
				*out_want_debug = true;
			}
			else if (strcmp(argv[i], "--verbose") == 0) {
				if (++i >= argc) goto missing_argument;
				*out_verbosity = atoi(argv[i]);
			}
			else {
				report_error("unrecognized option '%s'\n", argv[i]);
				return false;
			}
#else
			else if (strcmp(argv[i], "--verbose") == 0)
				++i;
#endif
		}
		else if (argv[i][0] == '-' && parse_options) {
			for (j = 1; j < (int)strlen(argv[i]); ++j) {
				switch (argv[i][j]) {
				case '0': case '1': case '2': case '3': case '4':
					*out_verbosity = argv[i][j] - '0';
					break;
				case 'd':
					*out_want_debug = true;
					break;
				default:
					report_error("unrecognized option '-%c'\n", argv[i][j]);
					return false;
				}
			}
		}
		else {
			if (*out_game_path == NULL) {
				*out_game_path = path_new(argv[i]);
				if (!path_resolve(*out_game_path, NULL)) {
					report_error("pathname not found '%s'\n", path_cstr(*out_game_path));
					path_free(*out_game_path);
					*out_game_path = NULL;
					return false;
				}
			}
			else {
				report_error("more than one game specified on command line\n");
				return false;
			}
		}
	}

#if defined(MINISPHERE_SPHERUN)
	if (*out_game_path == NULL) {
		print_usage();
		return false;
	}
#endif

	return true;

missing_argument:
	report_error("missing argument for option '%s'\n", argv[i - 1]);
	return false;
}

static void
print_banner(bool want_copyright, bool want_deps)
{
	char*    al_version;
	uint32_t al_version_id;

	printf("%s %s JS game engine (%s)\n", SPHERE_ENGINE_NAME, SPHERE_VERSION, sizeof(void*) == 4 ? "x86" : "x64");
	if (want_copyright) {
		printf("a lightweight JavaScript-powered game engine\n");
		printf("(c) 2015-2017 Fat Cerberus\n");
	}
	if (want_deps) {
		al_version_id = al_get_allegro_version();
		al_version = strnewf("%d.%d.%d.%d", al_version_id >> 24,
			(al_version_id >> 16) & 0xFF, (al_version_id >> 8) & 0xFF,
			(al_version_id & 0xFF) - 1);
		printf("\n");
		printf("    Allegro: v%-8s   libmng: v%s\n", al_version, mng_version_text());
		printf("     Dyad.c: v%-8s     zlib: v%s\n", dyad_getVersion(), zlibVersion());
		free(al_version);
	}
}

static void
print_usage(void)
{
	print_banner(true, false);
	printf("\n");
	printf("USAGE:\n");
	printf("   spherun [--fullscreen | --windowed] [--frameskip <n>] [--no-sleep]         \n");
	printf("           [--debug] [--verbose <n>] <game_path>                              \n");
	printf("\n");
	printf("OPTIONS:\n");
	printf("       --fullscreen   Start miniSphere in fullscreen mode.                    \n");
	printf("       --windowed     Start miniSphere in windowed mode.  This is the default.\n");
	printf("       --frameskip    Set the maximum number of consecutive frames to skip.   \n");
	printf("       --no-sleep     Prevent the engine from sleeping between frames.        \n");
	printf("   -d, --debug        Wait up to 30 seconds for the debugger to attach.       \n");
	printf("       --verbose      Set the engine's verbosity level from 0 to 4.  This can \n");
	printf("                      be abbreviated as '-n', where n is [0-4].               \n");
	printf("       --version      Show which version of miniSphere is installed.          \n");
	printf("       --help         Show this help text.                                    \n");
	printf("\n");
	printf("NOTE:\n");
	printf("   spherun(1) is used to execute Sphere games in a development environment. If\n");
	printf("   your intent is simply to play a game, use minisphere(1) instead.           \n");
}

static void
report_error(const char* fmt, ...)
{
	va_list ap;

	lstring_t* error_text;

	va_start(ap, fmt);
	error_text = lstr_vnewf(fmt, ap);
	va_end(ap);
#if defined(MINISPHERE_SPHERUN)
	fprintf(stderr, "spherun: ERROR: %s", lstr_cstr(error_text));
#else
	al_show_native_message_box(NULL,
		"miniSphere", "An error occurred starting the engine.", lstr_cstr(error_text),
		NULL, ALLEGRO_MESSAGEBOX_ERROR);
#endif
	lstr_free(error_text);
}

static void
show_error_screen(const char* message)
{
	wraptext_t*            error_info;
	bool                   is_copied = false;
	bool                   is_finished;
	int                    frames_till_close;
	ALLEGRO_KEYBOARD_STATE keyboard;
	const char*            line_text;
	int                    num_lines;
	transform_t*           projection;
	size2_t                resolution;
	const char*            subtitle;
	const char*            title;
	int                    title_index;

	int i;

	// cancel all jobs, including recurring.  we need to run a frame loop
	// and we don't want any JS code to execute after this point.
	async_cancel_all(true);

	title_index = rand() % (sizeof ERROR_TEXT / sizeof(const char*) / 2);
	title = ERROR_TEXT[title_index][0];
	subtitle = ERROR_TEXT[title_index][1];
	if (g_system_font == NULL)
		goto show_error_box;

	// word-wrap the error message to fit inside the error box
	resolution = screen_size(g_screen);
	if (!(error_info = wraptext_new(message, g_system_font, resolution.width - 84)))
		goto show_error_box;
	num_lines = wraptext_len(error_info);

	// reset the projection to pixel-perfect orthographic and disable clipping.
	// it's assumed that JavaScript execution won't continue after this point, so we shouldn't
	// step on any toes by doing this.
	screen_unskip_frame(g_screen);
	image_render_to(screen_backbuffer(g_screen), NULL);
	image_set_scissor(screen_backbuffer(g_screen), rect(0, 0, resolution.width, resolution.height));
	projection = transform_new();
	transform_orthographic(projection, 0, 0, resolution.width, resolution.height, -1.0f, 1.0f);
	image_set_transform(screen_backbuffer(g_screen), projection);
	transform_unref(projection);
	
	is_finished = false;
	frames_till_close = 30;
	while (!is_finished) {
		al_draw_filled_rounded_rectangle(32, 48, resolution.width - 32, resolution.height - 32, 5, 5, al_map_rgba(16, 16, 16, 255));
		font_draw_text(g_system_font, color_new(0, 0, 0, 255), resolution.width / 2 + 1, 11, TEXT_ALIGN_CENTER, title);
		font_draw_text(g_system_font, color_new(255, 255, 255, 255), resolution.width / 2, 10, TEXT_ALIGN_CENTER, title);
		font_draw_text(g_system_font, color_new(0, 0, 0, 255), resolution.width / 2 + 1, 23, TEXT_ALIGN_CENTER, subtitle);
		font_draw_text(g_system_font, color_new(255, 255, 255, 255), resolution.width / 2, 22, TEXT_ALIGN_CENTER, subtitle);
		for (i = 0; i < num_lines; ++i) {
			line_text = wraptext_line(error_info, i);
			font_draw_text(g_system_font, color_new(0, 0, 0, 255),
				resolution.width / 2 + 1, 59 + i * font_height(g_system_font),
				TEXT_ALIGN_CENTER, line_text);
			font_draw_text(g_system_font, color_new(192, 192, 192, 255),
				resolution.width / 2, 58 + i * font_height(g_system_font),
				TEXT_ALIGN_CENTER, line_text);
		}
		if (frames_till_close <= 0) {
			font_draw_text(g_system_font, color_new(255, 255, 255, 255),
				resolution.width / 2, resolution.height - 10 - font_height(g_system_font),
				TEXT_ALIGN_CENTER,
				is_copied ? "[Space]/[Esc] to close" : "[Ctrl+C] to copy, [Space]/[Esc] to close");
		}
		screen_flip(g_screen, 30, true);
		if (frames_till_close <= 0) {
			al_get_keyboard_state(&keyboard);
			is_finished = al_key_down(&keyboard, ALLEGRO_KEY_ESCAPE)
				|| al_key_down(&keyboard, ALLEGRO_KEY_SPACE);

			// if Ctrl+C is pressed, copy the error message and location to clipboard
			if ((al_key_down(&keyboard, ALLEGRO_KEY_LCTRL) || al_key_down(&keyboard, ALLEGRO_KEY_RCTRL))
				&& al_key_down(&keyboard, ALLEGRO_KEY_C))
			{
				is_copied = true;
				al_set_clipboard_text(screen_display(g_screen), message);
			}
		}
		else {
			--frames_till_close;
		}
	}
	wraptext_free(error_info);
	return;

show_error_box:
	// use a native message box only as a last resort
	al_show_native_message_box(NULL, "Script Error",
		"miniSphere encountered an error during game execution.",
		message, NULL, ALLEGRO_MESSAGEBOX_ERROR);
}
