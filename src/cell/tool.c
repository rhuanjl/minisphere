/**
 *  Cell, the Sphere packaging compiler
 *  Copyright (c) 2015-2018, Fat Cerberus
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

#include "cell.h"
#include "tool.h"

#include "fs.h"
#include "jsal.h"
#include "visor.h"

struct tool
{
	unsigned int refcount;
	void*        callback_ptr;
	char*        verb;
};

tool_t*
tool_new(const char* verb)
{
	js_ref_t* callback_ptr;
	tool_t*   tool;

	callback_ptr = jsal_ref(-1);
	jsal_pop(1);

	tool = calloc(1, sizeof(tool_t));
	tool->verb = strdup(verb);
	tool->callback_ptr = callback_ptr;
	return tool_ref(tool);
}

tool_t*
tool_ref(tool_t* tool)
{
	if (tool == NULL)
		return NULL;
	++tool->refcount;
	return tool;
}

void
tool_unref(tool_t* tool)
{
	if (tool == NULL || --tool->refcount > 0)
		return;

	jsal_unref(tool->callback_ptr);
	free(tool->verb);
	free(tool);
}

bool
tool_run(tool_t* tool, visor_t* visor, const fs_t* fs, const path_t* out_path, vector_t* in_paths)
{
	int           array_index;
	path_t*       dir_path;
	const char*   filename;
	bool          is_outdated = false;
	time_t        last_mtime = 0;
	int           line_number;
	int           num_errors;
	path_t**      path_ptr;
	bool          result_ok = true;
	struct stat   stats;

	iter_t iter;

	if (tool == NULL)
		return true;

	visor_begin_op(visor, "%s '%s'", tool->verb, path_cstr(out_path));

	// ensure the target directory exists
	dir_path = path_strip(path_dup(out_path));
	fs_mkdir(fs, path_cstr(dir_path));
	path_free(dir_path);

	if (fs_stat(fs, path_cstr(out_path), &stats) == 0)
		last_mtime = stats.st_mtime;
	jsal_push_ref_weak(tool->callback_ptr);
	jsal_push_string(path_cstr(out_path));
	jsal_push_new_array();
	iter = vector_enum(in_paths);
	while ((path_ptr = iter_next(&iter))) {
		array_index = jsal_get_length(-1);
		jsal_push_string(path_cstr(*path_ptr));
		jsal_put_prop_index(-2, array_index);
	}
	num_errors = visor_num_errors(visor);
	if (!jsal_try_call(2)) {
		jsal_get_prop_string(-1, "fileName");
		filename = jsal_to_string(-1);
		jsal_get_prop_string(-2, "lineNumber");
		line_number = jsal_get_int(-1);
		jsal_dup(-3);
		jsal_to_string(-1);
		visor_error(visor, "%s", jsal_get_string(-1));
		visor_print(visor, "@ [%s:%d]", filename, line_number);
		jsal_pop(3);
		result_ok = false;
	}
	jsal_pop(1);
	if (visor_num_errors(visor) > num_errors)
		result_ok = false;

	// verify that the tool actually did something.  if the target file doesn't exist,
	// that's definitely an error.  if the target file does exist but the timestamp hasn't changed,
	// only issue a warning because it might have been intentional (unlikely, but possible).
	if (result_ok) {
		if (fs_stat(fs, path_cstr(out_path), &stats) != 0) {
			visor_error(visor, "target file not found after build");
			result_ok = false;
		}
		else if (stats.st_mtime == last_mtime) {
			visor_warn(visor, "target file unchanged after build");
		}
	}
	else {
		// to ensure correctness, delete the target file if there was an error.  if we don't do
		// this, subsequent builds may not work correctly in the case that a tool accidentally
		// writes a target file anyway after producing errors.
		fs_unlink(fs, path_cstr(out_path));
	}

	visor_end_op(visor);
	return result_ok;
}
