#ifndef MINISPHERE__MAP_ENGINE_H__INCLUDED
#define MINISPHERE__MAP_ENGINE_H__INCLUDED

#include "obstruction.h"
#include "spriteset.h"
#include "tileset.h"

typedef struct person person_t;

typedef
enum player_id
{
	PLAYER_1,
	PLAYER_2,
	PLAYER_3,
	PLAYER_4,
	PLAYER_MAX
} player_id_t;

typedef
enum command_op
{
	COMMAND_WAIT,
	COMMAND_ANIMATE,
	COMMAND_FACE_NORTH,
	COMMAND_FACE_NORTHEAST,
	COMMAND_FACE_EAST,
	COMMAND_FACE_SOUTHEAST,
	COMMAND_FACE_SOUTH,
	COMMAND_FACE_SOUTHWEST,
	COMMAND_FACE_WEST,
	COMMAND_FACE_NORTHWEST,
	COMMAND_MOVE_NORTH,
	COMMAND_MOVE_NORTHEAST,
	COMMAND_MOVE_EAST,
	COMMAND_MOVE_SOUTHEAST,
	COMMAND_MOVE_SOUTH,
	COMMAND_MOVE_SOUTHWEST,
	COMMAND_MOVE_WEST,
	COMMAND_MOVE_NORTHWEST,
	COMMAND_RUN_SCRIPT
} command_op_t;

typedef
enum map_op
{
	MAP_SCRIPT_ON_ENTER,
	MAP_SCRIPT_ON_LEAVE,
	MAP_SCRIPT_ON_LEAVE_NORTH,
	MAP_SCRIPT_ON_LEAVE_EAST,
	MAP_SCRIPT_ON_LEAVE_SOUTH,
	MAP_SCRIPT_ON_LEAVE_WEST,
	MAP_SCRIPT_MAX
} map_op_t;

typedef
enum person_op
{
	PERSON_SCRIPT_ON_CREATE,
	PERSON_SCRIPT_ON_DESTROY,
	PERSON_SCRIPT_ON_TOUCH,
	PERSON_SCRIPT_ON_TALK,
	PERSON_SCRIPT_GENERATOR,
	PERSON_SCRIPT_MAX
} person_op_t;

void             map_engine_init          (void);
void             map_engine_uninit        (void);
void             map_engine_on_render     (script_t* script);
void             map_engine_on_update     (script_t* script);
bool             map_engine_running       (void);
int              map_engine_get_framerate (void);
void             map_engine_set_framerate (int framerate);
void             map_engine_attach_camera (person_t* person);
void             map_engine_attach_player (player_id_t player, person_t* person);
bool             map_engine_change_map    (const char* filename);
void             map_engine_exit          (void);
rect_t           map_bounds               (void);
const char*      map_filename             (void);
int              map_layer_by_name        (const char* name);
int              map_num_layers           (void);
int              map_num_persons          (void);
int              map_num_triggers         (void);
int              map_num_zones            (void);
const obsmap_t*  map_obsmap               (int layer);
point3_t         map_origin               (void);
person_t*        map_person_by_name       (const char* name);
int              map_tile_at              (int x, int y, int layer);
tileset_t*       map_tileset              (void);
int              map_trigger_at           (int x, int y, int layer);
int              map_zone_at              (int x, int y, int layer, int which);
bool             map_activate             (map_op_t op, bool use_default);
bool             map_add_trigger          (int x, int y, int layer, script_t* script);
bool             map_add_zone             (rect_t bounds, int layer, script_t* script, int steps);
void             map_detach_person        (const person_t* person);
void             map_normalize_xy         (double* inout_x, double* inout_y, int layer);
void             map_remove_trigger       (int trigger_index);
void             map_remove_zone          (int zone_index);
void             layer_on_render          (int layer, script_t* script);
size2_t          layer_size               (int layer);
int              layer_get_tile           (int layer, int x, int y);
void             layer_set_tile           (int layer, int x, int y, int tile_index);
bool             layer_resize             (int layer, int x_size, int y_size);
person_t*        person_new               (const char* name, spriteset_t* spriteset, bool is_persistent, script_t* create_script);
void             person_free              (person_t* person);
bool             person_busy              (const person_t* person);
bool             person_following         (const person_t* person, const person_t* leader);
bool             person_has_moved         (const person_t* person);
bool             person_ignored_by        (const person_t* person, const person_t* other);
const char*      person_name              (const person_t* person);
bool             person_obstructed_at     (const person_t* person, double x, double y, person_t** out_obstructing_person, int* out_tile_index);
double           person_get_angle         (const person_t* person);
rect_t           person_get_base          (const person_t* person);
color_t          person_get_color         (const person_t* person);
void             person_get_scale         (const person_t*, double* out_scale_x, double* out_scale_y);
void             person_get_speed         (const person_t* person, double* out_x_speed, double* out_y_speed);
spriteset_t*     person_get_spriteset     (person_t* person);
void             person_get_xy            (const person_t* person, double* out_x, double* out_y, bool normalize);
void             person_get_xyz           (const person_t* person, double* out_x, double* out_y, int* out_layer, bool normalize);
void             person_set_angle         (person_t* person, double theta);
void             person_set_color         (person_t* person, color_t mask);
void             person_set_scale         (person_t*, double scale_x, double scale_y);
void             person_set_script        (person_t* person, int type, script_t* script);
void             person_set_speed         (person_t* person, double x_speed, double y_speed);
void             person_set_spriteset     (person_t* person, spriteset_t* spriteset);
void             person_set_xyz           (person_t* person, double x, double y, int layer);
bool             person_activate          (const person_t* person, person_op_t op, const person_t* acting_person, bool use_default);
void             person_clear_queue       (person_t* person);
bool             person_compile_script    (person_t* person, int type, const lstring_t* codestring);
bool             person_follow            (person_t* person, person_t* leader, int distance);
bool             person_queue_command     (person_t* person, int command, bool is_immediate);
bool             person_queue_script      (person_t* person, script_t* script, bool is_immediate);
void             person_talk              (const person_t* person);
void             trigger_get_xyz          (int trigger_index, int* out_x, int* out_y, int* out_layer);
void             trigger_set_layer        (int trigger_index, int layer);
void             trigger_set_script       (int trigger_index, script_t* script);
void             trigger_set_xy           (int trigger_index, int x, int y);
void             trigger_activate         (int trigger_index);
rect_t           zone_get_bounds          (int zone_index);
int              zone_get_layer           (int zone_index);
int              zone_get_steps           (int zone_index);
void             zone_set_bounds          (int zone_index, rect_t bounds);
void             zone_set_layer           (int zone_index, int layer);
void             zone_set_script          (int zone_index, script_t* script);
void             zone_set_steps           (int zone_index, int steps);
void             zone_activate            (int zone_index);

void init_map_engine_api (duk_context* ctx);

#endif // MINISPHERE__MAP_ENGINE_H__INCLUDED
