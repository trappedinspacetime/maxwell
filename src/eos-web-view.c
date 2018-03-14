/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * eos-web-view.c
 *
 * Copyright (C) 2018 Endless Mobile, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Author: Juan Pablo Ugarte <ugarte@endlessm.com>
 *
 */

#include "eos-web-view.h"
#include "js-utils.h"

struct _EosWebView
{
  GtkOffscreenWindow parent;
};

typedef struct
{
  GtkWidget *child;
  GdkWindow *offscreen;  /* child offscreen window */
  gchar     *id;         /* canvas-id child property */
  gint       x;          /* canvas element X coordinate in webview space */
  gint       y;          /* canvas element Y coordinate in webview space */
} EosWebViewChild;


typedef struct
{
  GList      *children;  /* List of EosWebViewChild */
  GdkPixbuf  *pixbuf;    /* Temporal copy of offscreen window while handling eosdataimage:// */
} EosWebViewPrivate;

enum
{
  CHILD_PROP_0,
  CHILD_PROP_CANVAS_ID,

  N_CHILD_PROPERTIES
};

static GParamSpec *child_properties[N_CHILD_PROPERTIES];

G_DEFINE_TYPE_WITH_PRIVATE (EosWebView, eos_web_view, WEBKIT_TYPE_WEB_VIEW);

#define RESOURCES_PATH "/com/endlessm/eos-web-view"
#define EOS_WEB_VIEW_PRIVATE(d) ((EosWebViewPrivate *) eos_web_view_get_instance_private((EosWebView*)d))
#define EOS_WEB_VIEW_ERROR eos_web_view_error_quark ()

GQuark
eos_web_view_error_quark (void)
{
  return g_quark_from_static_string ("eos-web-view-error-quark");
}

static EosWebViewChild *
eos_web_view_child_new (GtkWidget *child)
{
  EosWebViewChild *data = g_slice_new0 (EosWebViewChild);

  data->child = g_object_ref_sink (child);

  return data;
}

static void
eos_web_view_child_free (EosWebViewChild *data)
{
  if (data == NULL)
    return;

  if (data->offscreen && data->child)
    gtk_widget_unregister_window (gtk_widget_get_parent (data->child),
                                  data->offscreen);

  if (data->offscreen)
    gdk_window_destroy (data->offscreen);

  g_clear_object (&data->child);

  g_free (data->id);

  g_slice_free (EosWebViewChild, data);
}

#define EWV_DEFINE_CHILD_GETTER(prop, type, cmpstmt) \
EosWebViewChild * \
get_child_data_by_##prop (EosWebViewPrivate *priv, type prop) \
{ \
  GList *l;\
  for (l = priv->children; l; l = g_list_next (l)) \
    { \
      EosWebViewChild *data = l->data; \
      if (cmpstmt) \
        return data; \
    } \
  return NULL; \
}

EWV_DEFINE_CHILD_GETTER (id, const gchar *, !g_strcmp0 (data->id, id))
EWV_DEFINE_CHILD_GETTER (child, GtkWidget *, data->child == child)
EWV_DEFINE_CHILD_GETTER (offscreen, GdkWindow *, data->offscreen == offscreen)

static void
eos_web_view_init (EosWebView *self)
{
  EosWebViewPrivate *priv = EOS_WEB_VIEW_PRIVATE (self);

  priv->children = NULL;
  priv->pixbuf   = NULL;
}

static void
eos_web_view_dispose (GObject *object)
{
  EosWebViewPrivate *priv = EOS_WEB_VIEW_PRIVATE (object);

  g_clear_object (&priv->pixbuf);

  /* GtkContainer dispose will free children */
  G_OBJECT_CLASS (eos_web_view_parent_class)->dispose (object);
}

static void
children_update_allocation (EosWebView *webview)
{
  EosWebViewPrivate *priv = EOS_WEB_VIEW_PRIVATE (webview);
  GList *l;

  for (l = priv->children; l; l = g_list_next (l))
    {
      EosWebViewChild *data = l->data;
      GtkRequisition child_requisition;
      GtkAllocation child_allocation;

      gtk_widget_get_preferred_size (data->child, NULL, &child_requisition);

      child_allocation.x = 0;
      child_allocation.y = 0;
      child_allocation.height = child_requisition.height;
      child_allocation.width = child_requisition.width;

      if (data->offscreen)
        gdk_window_resize (data->offscreen,
                           child_allocation.width,
                           child_allocation.height);

      gtk_widget_size_allocate (data->child, &child_allocation);
    }
}

static void
on_image_data_uri_scheme_request (WebKitURISchemeRequest *request,
                                  gpointer                data)
{
  const gchar *path = webkit_uri_scheme_request_get_path (request);
  EosWebViewPrivate *priv = EOS_WEB_VIEW_PRIVATE (data);
  EosWebViewChild *child_data;
  cairo_surface_t *surface;

  if (path && *path == '/' &&
      (child_data = get_child_data_by_id (priv, &path[1])) &&
      child_data->offscreen &&
      (surface = gdk_offscreen_window_get_surface (child_data->offscreen)))
    {
      gint w = gdk_window_get_width (child_data->offscreen);
      gint h = gdk_window_get_height (child_data->offscreen);

      g_clear_object (&priv->pixbuf);
      cairo_surface_flush (surface);
      priv->pixbuf = gdk_pixbuf_get_from_surface (surface, 0, 0, w, h);

      if (gdk_pixbuf_get_colorspace (priv->pixbuf) == GDK_COLORSPACE_RGB &&
          gdk_pixbuf_get_bits_per_sample (priv->pixbuf) == 8 &&
          gdk_pixbuf_get_has_alpha (priv->pixbuf))
        {
          GInputStream *stream = NULL;
          gsize stream_length;

          stream_length = gdk_pixbuf_get_height (priv->pixbuf) *
                          gdk_pixbuf_get_rowstride (priv->pixbuf);

          stream = g_memory_input_stream_new_from_data (gdk_pixbuf_read_pixels (priv->pixbuf),
                                                        stream_length, NULL);
          webkit_uri_scheme_request_finish (request,
                                            stream,
                                            stream_length,
                                            "application/octet-stream");
          g_object_unref (stream);
          return;
        }
    }

  webkit_uri_scheme_request_finish_error (request,
                                          g_error_new_literal (EOS_WEB_VIEW_ERROR, 0,
                                                               "Could not find imagedata uri"));
}

static void
handle_script_message_update_canvas_done (WebKitUserContentManager *manager,
                                          WebKitJavascriptResult   *result,
                                          EosWebView               *webview)
{
  EosWebViewPrivate *priv = EOS_WEB_VIEW_PRIVATE (webview);
  g_clear_object (&priv->pixbuf);
}

static void
handle_script_message_position (WebKitUserContentManager *manager,
                                WebKitJavascriptResult   *result,
                                EosWebView               *webview)
{
  EosWebViewPrivate *priv = EOS_WEB_VIEW_PRIVATE (webview);
  JSGlobalContextRef context = webkit_javascript_result_get_global_context (result);
  JSValueRef value = webkit_javascript_result_get_value (result);

  if (JSValueIsObject (context, value))
    {
      JSObjectRef obj = JSValueToObject (context, value, NULL);
      gchar *child_id = _js_object_get_string (context, obj, "id");
      EosWebViewChild *data = get_child_data_by_id (priv, child_id);

      if (data)
        {
          data->x = _js_object_get_number (context, obj, "x");
          data->y = _js_object_get_number (context, obj, "y");
        }

      g_free (child_id);
    }
  else
    {
      g_warning ("Error running javascript: unexpected return value");
    }
}

static void
handle_script_message_allocate (WebKitUserContentManager *manager,
                                WebKitJavascriptResult   *result,
                                EosWebView               *webview)
{
  JSGlobalContextRef context = webkit_javascript_result_get_global_context (result);
  JSValueRef value = webkit_javascript_result_get_value (result);

  if (JSValueIsObject (context, value))
    {
      children_update_allocation (webview);
    }
  else
    {
      g_warning ("Error running javascript: unexpected return value");
    }
}

static void
eos_web_view_set_child_property (GtkContainer *container,
                                 GtkWidget    *child,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  g_return_if_fail (EOS_IS_WEB_VIEW (container));
  g_return_if_fail (GTK_IS_WIDGET (child));

  switch (property_id)
    {
      case CHILD_PROP_CANVAS_ID:
        {
          EosWebViewPrivate *priv = EOS_WEB_VIEW_PRIVATE (container);
          EosWebViewChild *data = get_child_data_by_child (priv, child);
          if (data)
            {
              g_free (data->id);
              data->id = g_strdup (g_value_get_string (value));
            }
        }
      break;
      default:
        GTK_CONTAINER_WARN_INVALID_CHILD_PROPERTY_ID (container, property_id, pspec);
      break;
    }
}

static void
eos_web_view_get_child_property (GtkContainer *container,
                                 GtkWidget    *child,
                                 guint         property_id,
                                 GValue       *value,
                                 GParamSpec   *pspec)
{
  g_return_if_fail (EOS_IS_WEB_VIEW (container));
  g_return_if_fail (GTK_IS_WIDGET (child));

  switch (property_id)
    {
      case CHILD_PROP_CANVAS_ID:
        {
          EosWebViewPrivate *priv = EOS_WEB_VIEW_PRIVATE (container);
          EosWebViewChild *data = get_child_data_by_child (priv, child);
          g_value_set_string (value, (data) ? data->id : NULL);
        }
      break;
      default:
        GTK_CONTAINER_WARN_INVALID_CHILD_PROPERTY_ID (container, property_id, pspec);
      break;
    }
}

static void
eos_web_view_constructed (GObject *object)
{
  WebKitWebView *webview = WEBKIT_WEB_VIEW (object);
  WebKitUserContentManager *content_manager;
  WebKitSecurityManager *security_manager;
  WebKitWebContext *context;
  WebKitUserScript *script;
  GBytes *script_source;

  G_OBJECT_CLASS (eos_web_view_parent_class)->constructed (object);

  /* Give XMlHttpRequest a chance to work */
  g_object_set (webkit_web_view_get_settings (webview),
                "allow-universal-access-from-file-urls", TRUE,
                "allow-file-access-from-file-urls", TRUE,
                "enable-write-console-messages-to-stdout", TRUE,
                NULL);

  /* Install custom URI scheme to inject image buffers */
  context = webkit_web_view_get_context (webview);
  security_manager = webkit_web_context_get_security_manager (context);
  webkit_security_manager_register_uri_scheme_as_cors_enabled (security_manager,
                                                               "eosimagedata");
  webkit_web_context_register_uri_scheme (context, "eosimagedata",
                                          on_image_data_uri_scheme_request,
                                          object, NULL);

  /* Add script */
  content_manager = webkit_web_view_get_user_content_manager (webview);
  script_source = g_resources_lookup_data (RESOURCES_PATH"/eos-web-view.js",
                                           G_RESOURCE_LOOKUP_FLAGS_NONE,
                                           NULL);
  script = webkit_user_script_new (g_bytes_get_data (script_source, NULL),
                                   WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
                                   WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
                                   NULL, NULL);
  webkit_user_content_manager_add_script (content_manager, script);

  /* Handler to free pixbuf imediately after updating canvas */
  g_signal_connect_object (content_manager, "script-message-received::update_canvas_done",
                           G_CALLBACK (handle_script_message_update_canvas_done),
                           webview, 0);
  webkit_user_content_manager_register_script_message_handler (content_manager,
                                                               "update_canvas_done");

  /* handle child position changes */
  g_signal_connect_object (content_manager, "script-message-received::position",
                           G_CALLBACK (handle_script_message_position),
                           webview, 0);
  webkit_user_content_manager_register_script_message_handler (content_manager,
                                                               "position");

  g_signal_connect_object (content_manager, "script-message-received::allocate",
                           G_CALLBACK (handle_script_message_allocate),
                           webview, 0);
  webkit_user_content_manager_register_script_message_handler (content_manager,
                                                               "allocate");
}

static void
on_child_visible_notify (GObject    *object,
                         GParamSpec *pspec,
                         EosWebView *webview)
{
  EosWebViewPrivate *priv = EOS_WEB_VIEW_PRIVATE (webview);
  GtkWidget *widget = GTK_WIDGET (object);
  EosWebViewChild *data = get_child_data_by_child (priv, widget);

  if (!data)
    return;

  _js_run (WEBKIT_WEB_VIEW (webview),
           "if (window.hasOwnProperty ('eos_web_view'))"
           "  eos_web_view.children.%s.style.visibility = '%s';",
           data->id,
           gtk_widget_get_visible (widget) ? "visible" : "hidden");
}

static void
eos_web_view_add (GtkContainer *container, GtkWidget *child)
{
  EosWebViewPrivate *priv;
  EosWebViewChild *data;

  g_return_if_fail (EOS_IS_WEB_VIEW (container));
  priv = EOS_WEB_VIEW_PRIVATE (container);
  g_return_if_fail (GTK_IS_WIDGET (child));
  g_return_if_fail (gtk_widget_get_parent (child) == NULL);

  data = eos_web_view_child_new (child);

  priv->children = g_list_prepend (priv->children, data);

  g_signal_connect_object (child, "notify::visible",
                           G_CALLBACK (on_child_visible_notify),
                           container, 0);

  gtk_widget_set_parent (child, GTK_WIDGET (container));
}

static void
eos_web_view_remove (GtkContainer *container, GtkWidget *child)
{
  EosWebViewPrivate *priv;
  EosWebViewChild *data;

  g_return_if_fail (EOS_IS_WEB_VIEW (container));
  priv = EOS_WEB_VIEW_PRIVATE (container);
  g_return_if_fail (GTK_IS_WIDGET (child));
  g_return_if_fail (gtk_widget_get_parent (child) == GTK_WIDGET (container));

  if ((data = get_child_data_by_child (priv, child)))
    {
      priv->children = g_list_remove (priv->children, data);
      eos_web_view_child_free (data);
    }

  gtk_widget_unparent (child);
}

static void
offscreen_to_parent (GdkWindow  *offscreen_window,
                     double      offscreen_x,
                     double      offscreen_y,
                     double     *parent_x,
                     double     *parent_y,
                     EosWebView *webview)
{
  EosWebViewPrivate *priv = EOS_WEB_VIEW_PRIVATE (webview);
  EosWebViewChild *data = get_child_data_by_offscreen (priv, offscreen_window);

  if (data)
    {
      *parent_x = offscreen_x + data->x;
      *parent_y = offscreen_y + data->y;
    }
  else
    {
      *parent_x = offscreen_x;
      *parent_y = offscreen_y;
    }
}

static void
offscreen_from_parent (GdkWindow  *window,
                       double      parent_x,
                       double      parent_y,
                       double     *offscreen_x,
                       double     *offscreen_y,
                       EosWebView *webview)
{
  EosWebViewPrivate *priv = EOS_WEB_VIEW_PRIVATE (webview);
  EosWebViewChild *data = get_child_data_by_offscreen (priv, window);

  if (data)
    {
      *offscreen_x = parent_x - data->x;
      *offscreen_y = parent_y - data->y;
    }
  else
    {
      *offscreen_x = parent_x;
      *offscreen_y = parent_y;
    }
}

static GdkWindow *
pick_offscreen_child (GdkWindow  *offscreen_window,
                      double      widget_x,
                      double      widget_y,
                      EosWebView *webview)
{
  EosWebViewPrivate *priv = EOS_WEB_VIEW_PRIVATE (webview);
  GList *l;

  for (l = priv->children; l; l = g_list_next (l))
    {
      EosWebViewChild *data = l->data;

      gint w = gdk_window_get_width (data->offscreen);
      gint h = gdk_window_get_height (data->offscreen);

      if (widget_x >= data->x && widget_x <= data->x+w &&
          widget_y >= data->y && widget_y <= data->y+h)
        return data->offscreen;
    }

  return NULL;
}

static void
ensure_offscreen (GtkWidget *webview, EosWebViewChild *data)
{
  GdkWindowAttr attributes;
  gint attributes_mask;

  attributes.x = 0;
  attributes.y = 0;
  attributes.width = 1;
  attributes.height = 1;
  attributes.window_type = GDK_WINDOW_OFFSCREEN;
  attributes.event_mask = gtk_widget_get_events (webview)
                        | GDK_EXPOSURE_MASK
                        | GDK_POINTER_MOTION_MASK
                        | GDK_BUTTON_PRESS_MASK
                        | GDK_BUTTON_RELEASE_MASK
                        | GDK_SCROLL_MASK
                        | GDK_ENTER_NOTIFY_MASK
                        | GDK_LEAVE_NOTIFY_MASK;

  attributes.visual = gtk_widget_get_visual (webview);
  attributes.wclass = GDK_INPUT_OUTPUT;

  attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL;

  if (gtk_widget_get_visible (data->child))
    {
      GtkAllocation child_allocation;

      gtk_widget_get_allocation (data->child, &child_allocation);
      attributes.width = child_allocation.width;
      attributes.height = child_allocation.height;
    }
  data->offscreen = gdk_window_new (gdk_screen_get_root_window (gtk_widget_get_screen (webview)),
                                    &attributes, attributes_mask);
  gtk_widget_register_window (webview, data->offscreen);
  gtk_widget_set_parent_window (data->child, data->offscreen);
  gdk_offscreen_window_set_embedder (data->offscreen,
                                     gtk_widget_get_window (webview));
  g_signal_connect (data->offscreen, "to-embedder",
                    G_CALLBACK (offscreen_to_parent), webview);
  g_signal_connect (data->offscreen, "from-embedder",
                    G_CALLBACK (offscreen_from_parent), webview);

  gdk_window_show (data->offscreen);
}

static void
eos_web_view_realize (GtkWidget *widget)
{
  EosWebViewPrivate *priv = EOS_WEB_VIEW_PRIVATE (widget);
  GList *l;

  GTK_WIDGET_CLASS (eos_web_view_parent_class)->realize (widget);

  g_signal_connect_object (gtk_widget_get_window (widget),
                           "pick-embedded-child",
                           G_CALLBACK (pick_offscreen_child),
                           widget,
                           0);

  for (l = priv->children; l; l = g_list_next (l))
    ensure_offscreen (widget, l->data);
}

static void
eos_web_view_unrealize (GtkWidget *widget)
{
  /* TODO: implement */
  GTK_WIDGET_CLASS (eos_web_view_parent_class)->unrealize (widget);
}

static void
eos_web_view_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
  children_update_allocation (EOS_WEB_VIEW (widget));
  GTK_WIDGET_CLASS (eos_web_view_parent_class)->size_allocate (widget, allocation);
}

static gboolean
eos_web_view_draw (GtkWidget *widget, cairo_t *cr)
{
  EosWebViewPrivate *priv = EOS_WEB_VIEW_PRIVATE (widget);
  gboolean retval;
  GList *l;

  retval = GTK_WIDGET_CLASS (eos_web_view_parent_class)->draw (widget, cr);

  for (l = priv->children; l; l = g_list_next (l))
    {
      EosWebViewChild *data = l->data;

      if (data->offscreen && gtk_cairo_should_draw_window (cr, data->offscreen))
        {
          gint w = gdk_window_get_width (data->offscreen);
          gint h = gdk_window_get_height (data->offscreen);

          gtk_render_background (gtk_widget_get_style_context (widget),
                                 cr, 0, 0, w, h);

          gtk_container_propagate_draw (GTK_CONTAINER (widget), data->child, cr);

          if (data->id && gtk_widget_get_visible (data->child))
            _js_run (WEBKIT_WEB_VIEW (widget),
                     "if (window.hasOwnProperty ('eos_web_view'))"
                     "  eos_web_view.update_canvas('%s', %d, %d);",
                     data->id, w, h);
        }
    }

  return retval;
}

static void
eos_web_view_forall (GtkContainer *container,
                     gboolean      include_internals,
                     GtkCallback   callback,
                     gpointer      callback_data)
{
  EosWebViewPrivate *priv = EOS_WEB_VIEW_PRIVATE (container);
  GList *l;

  g_return_if_fail (callback != NULL);

  for (l = priv->children; l; l = g_list_next (l))
    {
      EosWebViewChild *data = l->data;
      (*callback) (data->child, callback_data);
    }
}

static void
eos_web_view_class_init (EosWebViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);

  object_class->dispose = eos_web_view_dispose;
  object_class->constructed = eos_web_view_constructed;

  widget_class->realize = eos_web_view_realize;
  widget_class->unrealize = eos_web_view_unrealize;
  widget_class->size_allocate = eos_web_view_size_allocate;
  widget_class->draw = eos_web_view_draw;

  container_class->add = eos_web_view_add;
  container_class->remove = eos_web_view_remove;
  container_class->forall = eos_web_view_forall;
  container_class->set_child_property = eos_web_view_set_child_property;
  container_class->get_child_property = eos_web_view_get_child_property;

  child_properties[CHILD_PROP_CANVAS_ID] =
    g_param_spec_string ("canvas-id",
                         "Canvas Id",
                         "The HTML canvas element id where to embedded child",
                         NULL,
                         G_PARAM_READWRITE);

  gtk_container_class_install_child_properties (container_class,
                                                N_CHILD_PROPERTIES,
                                                child_properties);
}

/* Public API */

GtkWidget *
eos_web_view_new ()
{
  return (GtkWidget *) g_object_new (EOS_TYPE_WEB_VIEW, NULL);
}

void
eos_web_view_pack_child (EosWebView  *webview,
                         GtkWidget   *child,
                         const gchar *id)
{
  g_return_if_fail (EOS_IS_WEB_VIEW (webview));
  g_return_if_fail (GTK_IS_WIDGET (child));
  g_return_if_fail (id != NULL);
  g_return_if_fail (*id != '\0');

  gtk_container_add (GTK_CONTAINER (webview), child);
  gtk_container_child_set (GTK_CONTAINER (webview), child,
                           "canvas-id", id,
                           NULL);
}