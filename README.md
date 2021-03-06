# ![Maxwell][logo]Embedding Gtk widgets in WebKit2
A WebKitWebView derived class that lets you embed GtkWidget in it.

### What is Maxwell?
Maxwell is a proof of concept library that extends WebKitWebView and lets you
embed/pack Gtk widgets in it as a regular Gtk container.

### Etymology
The project started inspired by the [Broadway Gdk backend][gdk-broadway] which
lets you run any Gtk application over HTTP using HTML5 and web sockets which
was named after [X Consortium's Broadway release][x11-broadway] where one of
its key features was X-Agent 96.

X Agent 96 => Agent 86 => Maxwell Smart => **Maxwell**

### How does it work?
The introduction of the split process model in WebKit2 made embedding widgets
in WebView rather difficult since part of WebKit operates in one process and
the rest like WebCore and JS engine in another (UI/Web process).

Maxwell takes a similar approach to Broadway, all it needs is a way to render
widgets in the DOM tree and get events from them.

To render widgets we use a CANVAS element the same size of the widget and a
custom URI scheme to get the raw image data from each widget.

```javascript
/* Get image data from widget */
let xhr = new XMLHttpRequest();
xhr.open('GET', 'maxwell:///widget_id', false);
xhr.responseType = 'arraybuffer';
xhr.send();

/* Render image data in canvas */
let data = new Uint8ClampedArray(xhr.response);
let image = new ImageData(data, canvas.width, canvas.height);
canvas.getContext('2d').putImageData(image, 0, 0);
```

On the Gtk side, each child is rendered in an offscreen window which we use as
the source image data to implement the custom URI scheme handler.

| Gtk UI process                       | JavaScript/WebCore |
| :----------------------------------: | :----------------: |
| GtkWidget:draw↴                      |                    |
| Damage event ⟶ JS child_draw()     ⟶ | GET maxwell://     |
| URI handler                        ⟵ | ↲                  |
| ↳[offscreen]⟶[GdkPixbuf]⟶GIOStream ⟶ | [ImageData]↴       |
|                                      | putImageData()     |

For events all we have to do is properly implement GdkWindow::pick-embedded-child
and let Gtk know which child widget should get the event.
In order to do so we need to keep track of the elements position relative to
WebView's viewport which can be calculated with getBoundingClientRect().

## API

The API is pretty straightforward, all you have to do is name the child widget
and add it using regular GtkContainer API.
```c
/* Create a Maxwell Web View */
webview = maxwell_web_view_new ();

/* Create a widget to embed */
entry = gtk_entry_new ();

/* Set a unique name on the widget */
gtk_widget_set_name (entry, "myentry");

/* Add widget to web view container */
gtk_container_add (GTK_CONTAINER (webview), entry);
```
In order for MaxwellWebView to know where to place a child in the DOM tree
you need to add a CANVAS element with a "GtkWidget" class and the unique ID
you used as the widget's name.
```html
<canvas class="GtkWidget" id="myentry"></canvas>
```

Maxwell will also try to honor width and height style properties set on the
canvas element.
So for example if you want your widget to expand horizontally you can do:
```html
<canvas class="GtkWidget" style="width: 100%;" id="myentry"></canvas>
```

## Building
 * Install [meson] and [ninja]
 * `$ sudo apt-get install meson`
 * Create a build directory:
 * `$ mkdir _build && cd _build`
 * Run meson:
 * `$ meson`
 * Run ninja:
 * `$ ninja`
 * `$ sudo ninja install`

## Licensing
Maxwell is released under the terms of the GNU Lesser General Public License,
either version 2.1 or, at your option, any later version.

[logo]: https://github.com/endlessm/maxwell/blob/master/logo.svg
[gdk-broadway]: https://developer.gnome.org/gtk3/stable/gtk-broadway.html
[x11-broadway]: http://sunsite.uakom.sk/sunworldonline/swol-02-1997/swol-02-connectivity.html
[meson]: http://mesonbuild.com/
[ninja]: https://ninja-build.org/
