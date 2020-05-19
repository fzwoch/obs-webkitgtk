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
#include <webkit2/webkit2.h>

OBS_DECLARE_MODULE()

typedef struct {
	GtkWidget *window;
	WebKitWebView *webview;
	GtkAllocation allocation;
	cairo_surface_t *surface;
	cairo_t *context;
	int count;
	gulong signal_id;
	GMutex mutex;
	GCond cond;

	obs_source_t *source;
	obs_data_t *settings;
} data_t;

static const char *get_name(void *type_data)
{
	return "WebkitGtk";
}

static gboolean capture(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	data_t *data = user_data;

	if (data->context == NULL) {
		gtk_widget_get_allocation(GTK_WIDGET(data->webview),
					  &data->allocation);

		data->surface = cairo_image_surface_create(
			CAIRO_FORMAT_ARGB32, data->allocation.width,
			data->allocation.height);

		data->context = cairo_create(data->surface);
	}

	gtk_widget_draw(GTK_WIDGET(data->webview), data->context);

	struct obs_source_frame frame = {};

	frame.width = data->allocation.width;
	frame.height = data->allocation.height;
	frame.format = VIDEO_FORMAT_BGRA;
	frame.linesize[0] = data->allocation.width * 4;
	frame.data[0] = cairo_image_surface_get_data(data->surface);

	frame.timestamp = data->count++;

	obs_source_output_video(data->source, &frame);

	return TRUE;
}

static gboolean start_gtk(gpointer user_data)
{
	data_t *data = user_data;

	data->count = 0;

	data->window = gtk_offscreen_window_new();
	gtk_window_set_default_size(GTK_WINDOW(data->window),
				    obs_data_get_int(data->settings, "width"),
				    obs_data_get_int(data->settings, "height"));

	data->webview = WEBKIT_WEB_VIEW(webkit_web_view_new());

	gtk_container_add(GTK_CONTAINER(data->window),
			  GTK_WIDGET(data->webview));

	webkit_web_view_load_uri(data->webview,
				 obs_data_get_string(data->settings, "url"));

	gtk_widget_show_all(data->window);

	data->signal_id = g_signal_connect(data->window, "damage-event",
					   G_CALLBACK(capture), data);

	g_mutex_lock(&data->mutex);
	g_cond_signal(&data->cond);
	g_mutex_unlock(&data->mutex);

	return FALSE;
}

static gboolean stop_gtk(gpointer user_data)
{
	data_t *data = user_data;

	g_signal_handler_disconnect(data->window, data->signal_id);
	data->signal_id = 0;

	gtk_widget_destroy(data->window);

	cairo_destroy(data->context);
	data->context = NULL;

	cairo_surface_destroy(data->surface);
	data->surface = NULL;

	g_mutex_lock(&data->mutex);
	g_cond_signal(&data->cond);
	g_mutex_unlock(&data->mutex);

	return FALSE;
}

static void start(data_t *data)
{
	g_mutex_lock(&data->mutex);

	g_idle_add(start_gtk, data);

	g_cond_wait(&data->cond, &data->mutex);
	g_mutex_unlock(&data->mutex);
}

static void stop(data_t *data)
{
	if (data->signal_id == 0)
		return;

	// urks, we do not get told when the GTK main thread goes down
	// if it does.. the lock further down will deadlock.
	// so check whether we are still alive. and if we are not
	// just leave. not that will will get some errors printed
	// in the log regardless..
	if (g_main_context_acquire(g_main_context_default())) {
		g_main_context_release(g_main_context_default());
		return;
	}

	g_mutex_lock(&data->mutex);

	g_idle_add(stop_gtk, data);

	g_cond_wait(&data->cond, &data->mutex);
	g_mutex_unlock(&data->mutex);
}

static void update(void *data, obs_data_t *settings)
{
	if (((data_t *)data)->signal_id == 0)
		return;

	stop(data);
	start(data);
}

static void *create(obs_data_t *settings, obs_source_t *source)
{
	data_t *data = g_new0(data_t, 1);

	g_mutex_init(&data->mutex);
	g_cond_init(&data->cond);

	data->source = source;
	data->settings = settings;

	return data;
}

static void destroy(void *p)
{
	data_t *data = p;

	stop(data);

	g_mutex_clear(&data->mutex);
	g_cond_clear(&data->cond);

	g_free(data);
}

static void get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "url", "https://obsproject.com");
	obs_data_set_default_int(settings, "width", 800);
	obs_data_set_default_int(settings, "height", 600);
}

static obs_properties_t *get_properties(void *p)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_text(props, "url", "URL", OBS_TEXT_DEFAULT);
	obs_properties_add_int(props, "width", "Width", 0, 4096, 1);
	obs_properties_add_int(props, "height", "Height", 0, 4096, 1);

	return props;
}

static void show(void *data)
{
	start(data);
}

static void hide(void *data)
{
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
