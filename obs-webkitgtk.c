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
	bool destroy_self;

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

	if (data->signal_id == 0)
		return FALSE;

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

static void cleanup(GtkWidget *object, gpointer user_data)
{
	data_t *data = user_data;

	cairo_destroy(data->context);
	data->context = NULL;

	cairo_surface_destroy(data->surface);
	data->surface = NULL;

	if (data->destroy_self)
		g_free(data);
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

	g_signal_connect(data->window, "destroy", G_CALLBACK(cleanup), data);

	return FALSE;
}

static gboolean stop_gtk(gpointer user_data)
{
	data_t *data = user_data;

	gtk_widget_destroy(data->window);
	data->window = NULL;

	return FALSE;
}

static void start(data_t *data)
{
	g_idle_add(start_gtk, data);
}

static void stop(data_t *data)
{
	if (data->signal_id == 0)
		return;

	data->signal_id = 0;

	g_idle_add(stop_gtk, data);
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

	return data;
}

static void destroy(void *p)
{
	data_t *data = p;

	data->destroy_self = TRUE;

	stop(data);
}

static void get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "url", "https://obsproject.com");
	obs_data_set_default_int(settings, "width", 800);
	obs_data_set_default_int(settings, "height", 600);
	obs_data_set_default_bool(settings, "keep_running", true);
}

static obs_properties_t *get_properties(void *p)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_text(props, "url", "URL", OBS_TEXT_DEFAULT);
	obs_properties_add_int(props, "width", "Width", 0, 4096, 1);
	obs_properties_add_int(props, "height", "Height", 0, 4096, 1);
	obs_properties_add_bool(props, "keep_running",
				"Keep running when hidden");

	return props;
}

static void show(void *p)
{
	data_t *data = p;

	if (data->signal_id == 0)
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
