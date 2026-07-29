#include "NativeSamples.h"

#include <gtk/gtk.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifndef GPU_LINUX_GDK_BACKEND
#  error GPU_LINUX_GDK_BACKEND must select x11 or wayland
#endif

#ifndef GPU_LINUX_GALLERY_TITLE
#  define GPU_LINUX_GALLERY_TITLE "GPU + USL Samples"
#endif

#ifndef GPU_LINUX_APPLICATION_ID
#  define GPU_LINUX_APPLICATION_ID "gpu.samples"
#endif

typedef struct GPUGallery {
  GtkWidget *window;
  GtkWidget *status;
  GPid       child;
} GPUGallery;

static char*
sample_title(const char *id) {
  char   *title;
  size_t  length;

  length = strlen(id);
  title  = g_malloc(length + 1u);
  memcpy(title, id, length + 1u);
  title[0] = g_ascii_toupper(title[0]);
  for (size_t i = 1u; i < length; i++) {
    if (title[i] == '-') {
      title[i] = ' ';
    }
  }
  return title;
}

static GdkPixbuf*
sample_preview(const char *path) {
  GdkPixbuf *source, *cropped, *pixels;
  GError    *error;
  int        width, height;

  error  = NULL;
  source = gdk_pixbuf_new_from_file(path, &error);
  if (!source) {
    g_clear_error(&error);
    return NULL;
  }

  width   = gdk_pixbuf_get_width(source);
  height  = gdk_pixbuf_get_height(source);
  cropped = source;
  if (width > 4 && height > 4) {
    cropped = gdk_pixbuf_new_subpixbuf(source,
                                       2,
                                       2,
                                       width - 4,
                                       height - 4);
  }
  pixels = gdk_pixbuf_scale_simple(cropped,
                                   340,
                                   212,
                                   GDK_INTERP_BILINEAR);
  if (cropped != source) {
    g_object_unref(cropped);
  }
  g_object_unref(source);
  return pixels;
}

static gboolean
draw_preview(GtkWidget *widget, cairo_t *context, gpointer userData) {
  GdkPixbuf     *pixels;
  GtkAllocation allocation;

  pixels = userData;
  gtk_widget_get_allocation(widget, &allocation);
  cairo_set_source_rgb(context, 4.0 / 255.0, 9.0 / 255.0, 22.0 / 255.0);
  cairo_paint(context);
  gdk_cairo_set_source_pixbuf(context,
                              pixels,
                              (allocation.width -
                               gdk_pixbuf_get_width(pixels)) * 0.5,
                              (allocation.height -
                               gdk_pixbuf_get_height(pixels)) * 0.5);
  cairo_paint(context);
  return TRUE;
}

static void
sample_closed(GPid pid, gint status, gpointer userData) {
  GPUGallery *gallery;

  gallery        = userData;
  gallery->child = 0;
  gtk_label_set_text(GTK_LABEL(gallery->status),
                     status == 0 ? "Sample closed" : "Sample failed");
  gtk_widget_set_sensitive(gallery->window, TRUE);
  g_spawn_close_pid(pid);
}

static void
sample_clicked(GtkButton *button, gpointer userData) {
  const GPUNativeSample *sample;
  GPUGallery            *gallery;
  GError                *error;
  gchar                 *arguments[2];

  gallery = userData;
  sample  = g_object_get_data(G_OBJECT(button), "gpu-sample");
  if (!sample || gallery->child) {
    return;
  }

  arguments[0] = (gchar *)sample->executable;
  arguments[1] = NULL;
  error        = NULL;
  if (!g_spawn_async(NULL,
                     arguments,
                     NULL,
                     G_SPAWN_DO_NOT_REAP_CHILD,
                     NULL,
                     NULL,
                     &gallery->child,
                     &error)) {
    gtk_label_set_text(GTK_LABEL(gallery->status),
                       error ? error->message : "Sample launch failed");
    g_clear_error(&error);
    gallery->child = 0;
    return;
  }

  gtk_label_set_text(GTK_LABEL(gallery->status), sample->id);
  gtk_widget_set_sensitive(gallery->window, FALSE);
  g_child_watch_add(gallery->child, sample_closed, gallery);
}

static GtkWidget*
sample_card(const GPUNativeSample *sample, GPUGallery *gallery) {
  GtkWidget *button, *box, *image, *label;
  GdkPixbuf *pixels;
  char      *title;

  button = gtk_button_new();
  box    = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  pixels = sample_preview(sample->preview);
  if (pixels) {
    image = gtk_drawing_area_new();
    gtk_widget_set_size_request(image, 340, 212);
    g_signal_connect(image,
                     "draw",
                     G_CALLBACK(draw_preview),
                     pixels);
    g_object_set_data_full(G_OBJECT(image),
                           "gpu-preview",
                           pixels,
                           g_object_unref);
  } else {
    image = gtk_drawing_area_new();
    gtk_widget_set_size_request(image, 340, 212);
  }

  title = sample_title(sample->id);
  label = gtk_label_new(title);
  g_free(title);
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_style_context_add_class(gtk_widget_get_style_context(label),
                              "sample-title");
  gtk_box_pack_start(GTK_BOX(box), image, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
  gtk_container_add(GTK_CONTAINER(button), box);
  gtk_widget_set_size_request(button, 364, 278);
  gtk_style_context_add_class(gtk_widget_get_style_context(button),
                              "sample-card");
  g_object_set_data(G_OBJECT(button), "gpu-sample", (gpointer)sample);
  g_signal_connect(button,
                   "clicked",
                   G_CALLBACK(sample_clicked),
                   gallery);
  return button;
}

static void
activate(GtkApplication *application, gpointer userData) {
  static const char css[] =
    "window { background: #07090d; color: #f3f1eb; }"
    "headerbar.gallery-header,"
    "headerbar.gallery-header:backdrop {"
    "  background: #0b0d12;"
    "  background-image: none;"
    "  border: 0;"
    "  border-bottom: 1px solid #20242c;"
    "  box-shadow: none;"
    "  color: #f3f1eb;"
    "}"
    "headerbar.gallery-header label,"
    "headerbar.gallery-header:backdrop label {"
    "  color: #f3f1eb;"
    "  text-shadow: none;"
    "}"
    ".gallery-title { font-size: 26px; font-weight: 800; }"
    ".gallery-status { color: #918f8a; }"
    "button.sample-card,"
    "button.sample-card:focus,"
    "button.sample-card:active,"
    "button.sample-card:backdrop,"
    "button.sample-card:disabled {"
    "  background: #0e121a;"
    "  background-image: none;"
    "  border: 1px solid #2f343d;"
    "  border-radius: 18px;"
    "  box-shadow: none;"
    "  padding: 12px;"
    "}"
    "button.sample-card:hover,"
    "button.sample-card:focus { border-color: #ff7014; }"
    "button.sample-card label.sample-title,"
    "button.sample-card:focus label.sample-title,"
    "button.sample-card:backdrop label.sample-title,"
    "button.sample-card:disabled label.sample-title {"
    "  color: #f3f1eb;"
    "  font-size: 18px;"
    "  font-weight: 700;"
    "  text-shadow: none;"
    "}";
  GPUGallery    *gallery;
  GtkCssProvider *provider;
  GtkWidget      *titlebar, *page, *header, *title, *scroll, *flow;

  gallery         = userData;
  gallery->window = gtk_application_window_new(application);
  titlebar        = gtk_header_bar_new();
  gtk_header_bar_set_title(GTK_HEADER_BAR(titlebar),
                           GPU_LINUX_GALLERY_TITLE);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(titlebar), TRUE);
  gtk_style_context_add_class(gtk_widget_get_style_context(titlebar),
                              "gallery-header");
  gtk_window_set_titlebar(GTK_WINDOW(gallery->window), titlebar);
  gtk_window_set_title(GTK_WINDOW(gallery->window),
                       GPU_LINUX_GALLERY_TITLE);
  gtk_window_set_default_size(GTK_WINDOW(gallery->window), 1240, 840);

  page   = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
  header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  title  = gtk_label_new("GPU | Universal Shading (USL)");
  gallery->status = gtk_label_new(GPU_LINUX_GDK_BACKEND " / Vulkan");
  gtk_style_context_add_class(gtk_widget_get_style_context(title),
                              "gallery-title");
  gtk_style_context_add_class(gtk_widget_get_style_context(gallery->status),
                              "gallery-status");
  gtk_widget_set_halign(title, GTK_ALIGN_START);
  gtk_widget_set_halign(gallery->status, GTK_ALIGN_END);
  gtk_box_pack_start(GTK_BOX(header), title, TRUE, TRUE, 0);
  gtk_box_pack_end(GTK_BOX(header), gallery->status, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(page), header, FALSE, FALSE, 0);

  scroll = gtk_scrolled_window_new(NULL, NULL);
  flow   = gtk_flow_box_new();
  gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow),
                                  GTK_SELECTION_NONE);
  gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 18);
  gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 18);
  gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 1);
  gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 3);
  for (size_t i = 0u; i < gpuNativeSampleCount; i++) {
    gtk_container_add(GTK_CONTAINER(flow),
                      sample_card(&gpuNativeSamples[i], gallery));
  }
  gtk_container_add(GTK_CONTAINER(scroll), flow);
  gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);
  gtk_container_set_border_width(GTK_CONTAINER(page), 28);
  gtk_container_add(GTK_CONTAINER(gallery->window), page);

  provider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(provider, css, -1, NULL);
  gtk_style_context_add_provider_for_screen(
    gtk_widget_get_screen(gallery->window),
    GTK_STYLE_PROVIDER(provider),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);
  gtk_widget_show_all(gallery->window);
}

int
main(int argc, char **argv) {
  GtkApplication *application;
  GPUGallery      gallery = {0};
  int             result;

  g_setenv("GDK_BACKEND", GPU_LINUX_GDK_BACKEND, TRUE);
  application = gtk_application_new(GPU_LINUX_APPLICATION_ID,
                                    G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(application,
                   "activate",
                   G_CALLBACK(activate),
                   &gallery);
  result = g_application_run(G_APPLICATION(application), argc, argv);
  g_object_unref(application);
  return result;
}
