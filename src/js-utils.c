/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * js-utils.c
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

#include "js-utils.h"

static void
run_javascrip_finish_handler (GObject      *object,
                              GAsyncResult *result,
                              gpointer      script)
{
    WebKitJavascriptResult *js_result;
    GError *error = NULL;

    js_result = webkit_web_view_run_javascript_finish (WEBKIT_WEB_VIEW (object),
                                                       result,
                                                       &error);
    if (!js_result)
      {
        g_warning ("Error running javascript: %s\n%s", error->message, (gchar*)script);
        g_error_free (error);
      }

  g_free (script);
}

void
_js_run (WebKitWebView *webview, const gchar *format, ...)
{
  va_list args;
  gchar *script;

  va_start (args, format);
  script = g_strdup_vprintf (format, args);
  va_end (args);

  webkit_web_view_run_javascript (WEBKIT_WEB_VIEW (webview), script, NULL,
                                  run_javascrip_finish_handler,
                                  script);
}

static gchar *
_js_value_get_string (JSGlobalContextRef context, JSValueRef value)
{
  if (JSValueIsString (context, value))
    {
      JSStringRef js_str = JSValueToStringCopy (context, value, NULL);
      gsize length = JSStringGetMaximumUTF8CStringSize (js_str);
      gchar *str = g_malloc (length);

      JSStringGetUTF8CString (js_str, str, length);
      JSStringRelease (js_str);

      return str;
    }
  return NULL;
}

static JSValueRef
get_prop (JSGlobalContextRef context, JSObjectRef object, gchar *property)
{
  JSStringRef pname = JSStringCreateWithUTF8CString (property);
  JSValueRef retval;

  retval = JSObjectGetProperty (context, object, pname, NULL);

  JSStringRelease (pname);

  return retval;
}

gchar *
_js_object_get_string (JSGlobalContextRef context,
                       JSObjectRef        object,
                       gchar             *property)
{
  return _js_value_get_string (context, get_prop (context, object, property));
}

gdouble
_js_object_get_number (JSGlobalContextRef context,
                       JSObjectRef        object,
                       gchar             *property)
{
  return JSValueToNumber (context, get_prop (context, object, property), NULL);
}