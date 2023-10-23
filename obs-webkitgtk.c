/*
 * obs-webkitgtk. OBS Studio source plugin.
 * Copyright (C) 2020 Florian Zwoch <fzwoch@gmail.com>
 *
 * This file is part of obs-webkitgtk.
 *
 * obs-webkitgtk is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * obs-webkitgtk is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with obs-webkitgtk. If not, see <http://www.gnu.org/licenses/>.
 */

#include <obs/obs-module.h>
#include <unistd.h>
#include <glib.h>
#include <signal.h>

OBS_DECLARE_MODULE()

typedef struct {
	GThread *thread;
	GPid pid;
	gint pipe;
	int count;
	obs_source_t *source;
	obs_data_t *settings;
} data_t;

static const char *get_name(void *type_data)
{
	return "WebKitGTK";
}

static gpointer thread(gpointer user_data)
{
	data_t *data = user_data;

	int width = obs_data_get_int(data->settings, "width");
	int height = obs_data_get_int(data->settings, "height");

	guint8 *buffer = g_new(guint8, width * height * 4);

	while (1) {
		guint8 *ptr = buffer;
		gint buffer_size = width * height * 4;

		while (buffer_size > 0) {
			ssize_t size = read(data->pipe, ptr, buffer_size);
			if (size <= 0) {
				goto done;
			}

			ptr += size;
			buffer_size -= size;

			if (buffer_size == 0) {
				break;
			}
		}

		struct obs_source_frame frame = {};

		frame.width = width;
		frame.height = height;
		frame.format = obs_data_get_bool(data->settings, "swap_colors") ? VIDEO_FORMAT_RGBA : VIDEO_FORMAT_BGRA;
		frame.linesize[0] = width * 4;
		frame.data[0] = buffer;

		frame.timestamp = data->count++;

		obs_source_output_video(data->source, &frame);
	}

done:
	g_free(buffer);

	return NULL;
}

static void start(data_t *data)
{
	gchar *path = g_file_read_link("/proc/self/exe", NULL);

	gchar *app =
		g_strdup_printf("%s/../libexec/obs-plugins/obs-webkitgtk-helper",
				g_path_get_dirname(path));

	if (g_file_test(app, G_FILE_TEST_IS_EXECUTABLE) == FALSE) {
		g_free(app);

		app = g_strdup_printf("%s/../lib64/obs-plugins/obs-webkitgtk-helper",
				      g_path_get_dirname(path));
	}

	if (g_file_test(app, G_FILE_TEST_IS_EXECUTABLE) == FALSE) {
		g_free(app);

		app = g_strdup_printf("%s/../lib/obs-plugins/obs-webkitgtk-helper",
				      g_path_get_dirname(path));
	}

	if (g_file_test(app, G_FILE_TEST_IS_EXECUTABLE) == FALSE) {
		g_free(app);

		app = g_strdup_printf("%s/obs-webkitgtk-helper",
				      g_path_get_dirname(path));
	}

	if (g_file_test(app, G_FILE_TEST_IS_EXECUTABLE) == FALSE) {
		g_free(app);

		app = g_strdup_printf("%s/obs-plugins/obs-webkitgtk-helper",
				      g_path_get_dirname(path));
	}

	if (g_file_test(app, G_FILE_TEST_IS_EXECUTABLE) == FALSE) {
		blog(LOG_ERROR,
		     "Could not find obs-webkitgtk-helper application");
		g_free(app);

		return;
	}

	char width[16], height[16];

	g_snprintf(width, sizeof(width), "%lld",
		   obs_data_get_int(data->settings, "width"));
	g_snprintf(height, sizeof(height), "%lld",
		   obs_data_get_int(data->settings, "height"));

	char *helper[] = {app, width, height,
			  (char *)obs_data_get_string(data->settings, "url"),
			  NULL};

	gboolean res = g_spawn_async_with_pipes(NULL, helper, NULL,
						G_SPAWN_DEFAULT, NULL, NULL,
						&data->pid, NULL, &data->pipe,
						NULL, NULL);

	g_free(path);
	g_free(app);

	if (res == FALSE) {
		blog(LOG_ERROR, "Could not spawn obs-webkitgtk-helper process");

		return;
	}

	data->count = 0;

	data->thread = g_thread_new("obs-webkitgtk-helper", thread, data);
}

static void stop(data_t *data)
{
	if (data->pid == 0)
		return;

	kill(data->pid, SIGINT);
	data->pid = 0;

	g_thread_join(data->thread);

	if (obs_data_get_bool(data->settings, "clear_after_stop"))
		obs_source_output_video(data->source, NULL);
}

static void update(void *p, obs_data_t *settings)
{
	data_t *data = p;

	stop(data);

	if (!obs_data_get_bool(settings, "keep_running") &&
	    !obs_source_showing(data->source))
		return;

	start(data);
}

static void *create(obs_data_t *settings, obs_source_t *source)
{
	data_t *data = g_new0(data_t, 1);

	data->source = source;
	data->settings = settings;

	if (obs_data_get_bool(data->settings, "keep_running"))
		start(data);

	return data;
}

static void destroy(void *p)
{
	data_t *data = p;

	stop(data);
}

static void get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "url", "https://obsproject.com/browser-source");
	obs_data_set_default_int(settings, "width", 800);
	obs_data_set_default_int(settings, "height", 600);
	obs_data_set_default_bool(settings, "keep_running", true);
	obs_data_set_default_bool(settings, "clear_after_stop", true);
	obs_data_set_default_bool(settings, "swap_colors", false);
}

static obs_properties_t *get_properties(void *p)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_text(props, "url", "URL", OBS_TEXT_DEFAULT);
	obs_properties_add_int(props, "width", "Width", 0, 4096, 1);
	obs_properties_add_int(props, "height", "Height", 0, 4096, 1);
	obs_properties_add_bool(props, "keep_running",
				"Keep running when hidden");
	obs_properties_add_bool(props, "clear_after_stop",
				"Clear data after stop");
	obs_properties_add_bool(props, "swap_colors",
				"Swap Red/Blue channels");

	return props;
}

static void show(void *p)
{
	data_t *data = p;

	if (data->pid == 0)
		start(data);
}

static void hide(void *p)
{
	data_t *data = p;

	if (!obs_data_get_bool(data->settings, "keep_running"))
		stop(data);
}

bool obs_module_load(void)
{
	struct obs_source_info info = {
		.id = "webkitgtk",
		.type = OBS_SOURCE_TYPE_INPUT,
		.output_flags = OBS_SOURCE_ASYNC_VIDEO |
				OBS_SOURCE_DO_NOT_DUPLICATE,

		.get_name = get_name,
		.create = create,
		.destroy = destroy,

		.get_defaults = get_defaults,
		.get_properties = get_properties,
		.update = update,

		.show = show,
		.hide = hide,
	};

	obs_register_source(&info);

	return true;
}
