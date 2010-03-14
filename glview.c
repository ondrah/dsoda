/*
     _ _       _ _        _                 _       
  __| (_) __ _(_) |_ __ _| |  ___  ___   __| | __ _ 
 / _` | |/ _` | | __/ _` | | / __|/ _ \ / _` |/ _` |
| (_| | | (_| | | || (_| | | \__ \ (_) | (_| | (_| |
 \__,_|_|\__, |_|\__\__,_|_| |___/\___/ \__,_|\__,_|
         |___/                written by Ondra Havel

*/

#include <gtk/gtk.h>
#include <gtk/gtkgl.h>
#include <math.h>
#include <sys/time.h>

#include <GL/gl.h>
#include <GL/glu.h>

#include <gdk/gdkkeysyms.h>

#include "io.h"
#include "local.h"
#include "gui.h"

#define MAX_ZOOM	1.5
#define MIN_ZOOM	0.005

#define DIVS_H	10
#define DIVS_V 8
#define VOLTAGE_SCALE	32.0
#define MARGIN_CANVAS	0.05

#define MAX_XFACTOR	0x100

#define MV	DIVS_V * MAX_ZOOM / 2
#define MH	DIVS_H * MAX_ZOOM / 2

#define GRID_COLOR	0xdd, 0xdd, 0xdd, 0x40
#define AXIS_COLOR	0xff, 0xff, 0xff, 0x80

static GdkCursor *cursor_cross, *cursor_hand;
static GdkGLConfig *glconfig;
static int gl_channels, gl_grid, gl_cursor, gl_trigger;
static int fl_pan = 0, fl_pan_ready = 0, fl_xfactor = 0, fl_osd = 1;
static float zoom_factor = 1, pan_x = 0, pan_y = 0;
static float press_x, press_y;
static int x_factor = 1;
static GtkWidget *my_window;
static int cursor_set[2] = {0,0};
static char *c_msg[2] = {0,0}, *d_msg = 0;
static int c_msg_len[2] = {0,0}, d_msg_len = 0;
static int fl_grid = 1;

static int samples_in_grid = 10000;
static float overlap = 0.5;
static int current_buffer_size = 10240;

enum { I_DOTS = 0, I_LINES };
static int interpolation_type = I_LINES;

struct cursor_coord {
	float x, y;
};

struct cursor_coord cursor[2] = { {.x = 0, .y = 0}, {.x = 0, .y = 0}}; 
static int cursor_source = 0;	// channel source (target)

GLushort channel_rgba[3][4] = {{0,0xffff,0,0x8000}, {0xffff,0xffff,0,0x8000},{0xffff,0,0,0x8000}};

static gchar font_name[] = "courier 10";
static GLuint font_list_base;
static gfloat font_height;

void display_refresh(GtkWidget *da);

void gui_update_buffer_size(unsigned int buffer_size)
{
	current_buffer_size = buffer_size;
	samples_in_grid = 1;
	while(buffer_size >= 10) {
		buffer_size /= 10;
		samples_in_grid *= 10;
	}
	samples_in_grid*=buffer_size;
	overlap = (float)current_buffer_size/samples_in_grid/2;
}

void gui_channel_set_color(unsigned int channel_id, int red, int green, int blue)
{
	channel_rgba[channel_id][0] = red;
	channel_rgba[channel_id][1] = green;
	channel_rgba[channel_id][2] = blue;

	display_refresh(my_window);
}

static
void gl_done()
{
	glDeleteLists(gl_channels, 3);
	glDeleteLists(gl_cursor, 2);
	glDeleteLists(gl_trigger, 1);
	glDeleteLists(gl_grid, 1);
}

static GLuint gl_makegrid();

static
void gl_init()
{
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glEnable(GL_POINT_SMOOTH);
    glPointSize(1);

	gl_grid = gl_makegrid();
    gl_channels = glGenLists(3);
	gl_cursor = glGenLists(2);
	gl_trigger = glGenLists(1);
    glShadeModel(GL_SMOOTH);
	glLineStipple(1, 0xFF);
}

static
void gl_resize(int w, int h)
{
	glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-DIVS_H/2 - MARGIN_CANVAS, DIVS_H/2 + MARGIN_CANVAS, -DIVS_V/2 - MARGIN_CANVAS, DIVS_V/2 + MARGIN_CANVAS, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
}

static
void update_screen()
{
    glPushMatrix();
    glLoadIdentity();
    glLineWidth(2);

	while((int)trigger_point > (int)current_buffer_size)
		trigger_point -= current_buffer_size;

	for (int t = 0 ; t < 2; t++) {
		if(!capture_ch[t])
			continue;

		glNewList(gl_channels + t, GL_COMPILE);
		glBegin(interpolation_type == I_DOTS ? GL_POINTS : GL_LINE_STRIP);
		glColor4usv(channel_rgba[t]);

		int x = trigger_point;
		for(int i = 0; i < current_buffer_size; i++, x++) {
			if(x >= current_buffer_size)
				x = 0;
			glVertex2f(DIVS_H * ((float) i / samples_in_grid - overlap), DIVS_V * my_buffer[2*x + (1-t)] / 256.0 - DIVS_V / 2.0);
		}

		glEnd();
		glEndList();
	}

	glLineWidth(1);

	if(fl_math) {
		glNewList(gl_channels + 2, GL_COMPILE);
		glBegin(interpolation_type == I_DOTS ? GL_POINTS : GL_LINE_STRIP);
		glColor4usv(channel_rgba[2]);

		int o0 = offset_ch[0] * 0xff;
		int o1 = offset_ch[1] * 0xff;

		int x = trigger_point;
		for(int i = 0; i < current_buffer_size; i++, x++) {
			if(x >= current_buffer_size)
				x = 0;

			float c0 = (my_buffer[2 * x + 1] - o0) / VOLTAGE_SCALE * nr_voltages[voltage_ch[0]];
		   	float c1 = (my_buffer[2 * x] - o1) / VOLTAGE_SCALE * nr_voltages[voltage_ch[1]];

			float a, b;
			a = math_source[0] ? c1 : c0;
			b = math_source[1] ? c1 : c0;

			float r;
			switch(math_op) {
				case M_ADD:
					r = a + b;
					break;
				case M_SUB:
					r = a - b;
					break;
				case M_MUL:
					r = a * b;
					break;
			}

			r = r / nr_voltages[voltage_ch[CH_M]];
			glVertex2f(DIVS_H * ((float) i / samples_in_grid - overlap), r + DIVS_V * (offset_ch[CH_M] - 0.5));
		}

		glEnd();
		glEndList();
	}

	if(fl_grid) {
		// draw trigger offset + position
		glNewList(gl_trigger, GL_COMPILE);
		glColor4f(.3, .3, .8, 0.7);

		glLineStipple(1, 0x1c47);
		glEnable(GL_LINE_STIPPLE);

		glBegin(GL_LINES);

		glVertex2f((position_t - 0.5) * DIVS_H, -MV);
		glVertex2f((position_t - 0.5) * DIVS_H, +MV);
		glVertex2f(-DIVS_H/2, (offset_t - 0.5) * DIVS_V);
		glVertex2f(+DIVS_H/2, (offset_t - 0.5) * DIVS_V);

		glLineStipple(1, 0x0101);
		if(capture_ch[0]) {
			glColor4usv(channel_rgba[0]);
			glVertex2f(-MH, (offset_ch[0] - 0.5) * DIVS_V);
			glVertex2f(+MH, (offset_ch[0] - 0.5) * DIVS_V);
		}

		if(capture_ch[1]) {
			glColor4usv(channel_rgba[1]);
			glVertex2f(-MH, (offset_ch[1] - 0.5) * DIVS_V);
			glVertex2f(+MH, (offset_ch[1] - 0.5) * DIVS_V);
		}

		if(fl_math) {
			glColor4usv(channel_rgba[2]);
			glVertex2f(-MH, (offset_ch[2] - 0.5) * DIVS_V);
			glVertex2f(+MH, (offset_ch[2] - 0.5) * DIVS_V);
		}

		glEnd();
		glDisable(GL_LINE_STIPPLE);
		glLineStipple(1, 0x00FF);
		glEndList();
	}

	glClear(GL_COLOR_BUFFER_BIT);
	if(capture_ch[0])
		glCallList(gl_channels);
	if(capture_ch[1])
		glCallList(gl_channels + 1);
	if(fl_math)
		glCallList(gl_channels + 2);
	if(fl_grid) {
		glCallList(gl_grid);
		glCallList(gl_trigger);
	}
	if(cursor_set[0])
		glCallList(gl_cursor);
	if(cursor_set[1])
		glCallList(gl_cursor + 1);

	glPopMatrix();
}

static
GLuint gl_makegrid()
{
    GLuint g = glGenLists(1);
    glNewList(g, GL_COMPILE);
    glLineWidth(1);

	// draw the grid
	glColor4ub(GRID_COLOR);
	glBegin(GL_LINES);
	for(GLfloat i = 1; i <= MV; i++) {
		glVertex2f(-MH, +i);
		glVertex2f(+MH, +i);
		glVertex2f(-MH, -i);
		glVertex2f(+MH, -i);
	}
	for(GLfloat i = 1; i<= MH; i++) {
		glVertex2f(+i, -MV);
		glVertex2f(+i, +MV);
		glVertex2f(-i, -MV);
		glVertex2f(-i, +MV);
	}
	glEnd();

	// x- and y-axis
	glColor4ub(AXIS_COLOR);
	glBegin(GL_LINES);
	glVertex2f(-MH, 0);
	glVertex2f(+MH, 0);
	glVertex2f(0, -MV);
	glVertex2f(0, +MV);
	for(GLfloat i = 0.5; i <= MH; i++) {
		glVertex2f(+i, -0.1);
		glVertex2f(+i, +0.1);
		glVertex2f(-i, -0.1);
		glVertex2f(-i, +0.1);
	}
	for(GLfloat i = 0.5; i <= MV; i++) {
		glVertex2f(-0.1, +i);
		glVertex2f(+0.1, +i);
		glVertex2f(-0.1, -i);
		glVertex2f(+0.1, -i);
	}
	glEnd();
    glEndList();

    return g;
}

static void
realize (GtkWidget *widget, gpointer   data)
{
	gl_init();
	gdk_window_set_cursor(widget->window, cursor_cross);
}

static
gboolean configure_event(GtkWidget *widget, GdkEventConfigure *event, gpointer data)
{
	GdkGLContext *glcontext = gtk_widget_get_gl_context(widget);
	GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable(widget);

	if(!gdk_gl_drawable_gl_begin (gldrawable, glcontext))
		return FALSE;

	gl_resize(widget->allocation.width, widget->allocation.height);

  PangoFontDescription *font_desc;
  PangoFont *font;
  PangoFontMetrics *font_metrics;

  font_list_base = glGenLists (128);

  font_desc = pango_font_description_from_string (font_name);

  font = gdk_gl_font_use_pango_font (font_desc, 0, 128, font_list_base);
  if (font == NULL) {
      g_print ("*** Can't load font '%s'\n", font_name);
  }

	font_metrics = pango_font_get_metrics (font, NULL);
  font_height = pango_font_metrics_get_ascent (font_metrics) +
                pango_font_metrics_get_descent (font_metrics);
  font_height = PANGO_PIXELS (font_height) / 48.0;
  g_print("%f\n", font_height);

  pango_font_description_free (font_desc);
  pango_font_metrics_unref (font_metrics);


	gdk_gl_drawable_gl_end (gldrawable);

	return TRUE;
}

static
gboolean expose_event(GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
	GdkGLContext *glcontext = gtk_widget_get_gl_context(widget);
	GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable(widget);

	if(!gdk_gl_drawable_gl_begin(gldrawable, glcontext))
		return FALSE;

	update_screen();

	if(gdk_gl_drawable_is_double_buffered (gldrawable)) {
		gdk_gl_drawable_swap_buffers (gldrawable);
	} else {
		glFlush ();
	}

	gdk_gl_drawable_gl_end (gldrawable);
	return TRUE;
}

void display_refresh(GtkWidget *da)
{
	static struct timeval otime = { .tv_sec = 0, .tv_usec = 0 };
	struct timeval tv;

   	gettimeofday(&tv, 0);
	unsigned int dmsec = 1000 * (tv.tv_sec - otime.tv_sec) + (tv.tv_usec - otime.tv_usec) / 1000;

	if(dmsec < 1000.0 / 30)		// frame limiter
		return;

	otime = tv;

	gdk_window_invalidate_rect(da->window, &da->allocation, FALSE);
	gdk_window_process_updates(da->window, FALSE);
}

int display_init(int *pargc, char ***pargv)
{
	gint major, minor;

	gtk_gl_init(pargc, pargv);
	gdk_gl_query_version(&major, &minor);
	g_print("OpenGL extension %d.%d\n", major, minor);

	/* Try double-buffered visual */
	glconfig = gdk_gl_config_new_by_mode (GDK_GL_MODE_RGB		|
																				GDK_GL_MODE_DEPTH	|
																				GDK_GL_MODE_DOUBLE);
	if (glconfig == NULL) {
		g_print("*** Cannot find the double-buffered visual.\n");
		g_print("*** Trying single-buffered visual.\n");

		/* Try single-buffered visual */
		glconfig = gdk_gl_config_new_by_mode (GDK_GL_MODE_RGB | GDK_GL_MODE_DEPTH);
		if(glconfig == NULL) {
			g_print("*** No appropriate OpenGL-capable visual found.\n");
			//exit (1);
			return -1;
		}
	}

	/* Get automatically redrawn if any of their children changed allocation. */
 // gtk_container_set_reallocate_redraws (GTK_CONTAINER (window), TRUE);

	return 0;
}

static
void convert_coords(float *dx, float *dy, GtkWidget *w, float cx, float cy)
{
	*dx = DIVS_H * cx / w->allocation.width - DIVS_H / 2;
	*dy = -(DIVS_V * cy / w->allocation.height - DIVS_V / 2);
}

static
void cursor_genreport()
{
	float time_base = (float)samples_in_grid / DIVS_H * (1000000.0 / gui_get_sampling_rate());
	float v = nr_voltages[voltage_ch[cursor_source]];
	float o = (offset_ch[cursor_source] - 0.5) * DIVS_V;
	float dx = (cursor[1].x - cursor[0].x) * time_base * zoom_factor / x_factor;
	float dy = (cursor[1].y - cursor[0].y) * zoom_factor * v;
	char *str_vu = "V";
	char *str_hu = "usec";
	
	if(cursor_set[0])
		c_msg_len[0] = asprintf(&c_msg[0], "c0:  x = %.2f %s y = %.2f %s", cursor[0].x * zoom_factor / x_factor * time_base, str_hu, (cursor[0].y * zoom_factor - o) * v, str_vu);

	if(cursor_set[1])
		c_msg_len[1] = asprintf(&c_msg[1], "c1:  x = %.2f %s y = %.2f %s", cursor[1].x * zoom_factor / x_factor * time_base, str_hu, (cursor[1].y * zoom_factor - o) * v, str_vu);

	if(cursor_set[0] && cursor_set[1])
		d_msg_len = asprintf(&d_msg, "d:   x = %.2f %s y = %.2f %s", dx, str_hu, dy, str_vu);

}

static
void cursor_draw(int i)
{
	struct cursor_coord *ac = &cursor[i];

	glNewList(gl_cursor + i, GL_COMPILE);
	if(i == 1)
		glEnable(GL_LINE_STIPPLE);
	glBegin(GL_LINE_STRIP);

	glColor4us(channel_rgba[cursor_source][0], channel_rgba[cursor_source][1], channel_rgba[cursor_source][2], 0xEEEE);

	glVertex2f(ac->x * zoom_factor / x_factor + pan_x, -MV);
	glVertex2f(ac->x * zoom_factor / x_factor + pan_x, +MV);
	glEnd();

	glBegin(GL_LINES);
	glVertex2f(-MH, ac->y * zoom_factor + pan_y);
	glVertex2f(+MH, ac->y * zoom_factor + pan_y);

	glEnd();
	if(i == 1)
		glDisable(GL_LINE_STIPPLE);

	if(fl_osd) {
		glListBase(font_list_base);
		cursor_genreport();

		for(int i = 0; i < 2; i++) {
			glRasterPos2f(-DIVS_H/2, DIVS_V/2 - 0.2 - i * font_height);
			glCallLists(c_msg_len[i], GL_UNSIGNED_BYTE, c_msg[i]);
		}

		if(cursor_set[0] && cursor_set[1]) {
			glRasterPos2f(-DIVS_H/2, DIVS_V/2 - 0.2 - 2 * font_height);
			glCallLists(d_msg_len, GL_UNSIGNED_BYTE, d_msg);
		}
	}

	glEndList();
}

static
void rezoom()
{
	pan_x = MAX(pan_x, -MH + DIVS_H / 2 * zoom_factor / x_factor);
	pan_x = MIN(pan_x, +MH - DIVS_H / 2 * zoom_factor / x_factor);
	pan_y = MAX(pan_y, -MV + DIVS_V / 2 * zoom_factor);
	pan_y = MIN(pan_y, +MV - DIVS_V / 2 * zoom_factor);

	glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho((-DIVS_H/2 - MARGIN_CANVAS) * zoom_factor / x_factor + pan_x, (DIVS_H/2 + MARGIN_CANVAS) * zoom_factor / x_factor + pan_x, (-DIVS_V/2 - MARGIN_CANVAS) * zoom_factor + pan_y, (DIVS_V/2 + MARGIN_CANVAS) * zoom_factor + pan_y, -1.0, 1.0);
	glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

	display_refresh(my_window);
}

static
gboolean mouse_motion_cb(GtkWidget *w, GdkEventMotion *e, gpointer p)
{
	if(!fl_pan)
		return FALSE;

	if(w->allocation.width < 0 || w->allocation.height < 0)
		return FALSE;

	float mx, my;
	convert_coords(&mx, &my, w, e->x, e->y);
	pan_x += (press_x - mx) * zoom_factor / x_factor;
	pan_y += (press_y - my) * zoom_factor;
	press_x = mx;
	press_y = my;

	rezoom();

	return FALSE;
}

static
void cursor_info()
{
	if(cursor_set[0])
		g_print("%s\n", c_msg[0]);

	if(cursor_set[1])
		g_print("%s\n", c_msg[0]);

	if(cursor_set[0] && cursor_set[1])
		g_print("%s\n", d_msg);
}

static
gboolean mouse_button_press_cb(GtkWidget *w, GdkEventButton *e, gpointer p)
{
	if(e->button == 1 && fl_pan_ready) {
		fl_pan = 1;
		convert_coords(&press_x, &press_y, w, e->x, e->y);
		return FALSE;
	}

	if(e->button == 1 || e->button == 3) {
		int cnr = e->button == 1 ? 0 : 1;

		cursor_set[cnr] = 1;
		convert_coords(&cursor[cnr].x, &cursor[cnr].y, w, e->x, e->y);
		cursor_draw(cnr);
		display_refresh(my_window);
		cursor_info();
	}

	return FALSE;
}

static
gboolean mouse_button_release_cb(GtkWidget *w, GdkEventButton *e, gpointer p)
{
	if(e->button == GDK_BUTTON1_MASK)
		return FALSE;
	
	fl_pan = 0;
	return FALSE;
}

static
gboolean scroll_cb(GtkWidget *w, GdkEventScroll *e, gpointer p)
{
	if(fl_xfactor) {
		if(e->direction > 0) {
			if(x_factor > 1) {
				x_factor--;
				rezoom();
			}
		} else {
			if(x_factor < MAX_XFACTOR) {
				x_factor++;
				rezoom();
			}
		}
		return FALSE;
	}

	float mx, my;
	convert_coords(&mx, &my, w, e->x, e->y);

	if(e->direction > 0) {
		if(zoom_factor == MAX_ZOOM)
			return FALSE;

		pan_x = pan_x - mx * zoom_factor;
		pan_y = pan_y - my * zoom_factor;

		zoom_factor *= 2;
		if(zoom_factor > MAX_ZOOM)
			zoom_factor = MAX_ZOOM;
	} else {
		if(zoom_factor == MIN_ZOOM)
			return FALSE;

		zoom_factor /= 2;
		if(zoom_factor < MIN_ZOOM)
			zoom_factor = MIN_ZOOM;

		pan_x = pan_x + mx * zoom_factor;
		pan_y = pan_y + my * zoom_factor;
	}

	rezoom();
	return FALSE;
}

static
gboolean key_press_cb(GtkWidget *w, GdkEventKey *e, gpointer p)
{
	switch(e->keyval) {
		case GDK_c:
			cursor_source = (cursor_source + 1) % 3;
			g_print("Switching cursor to %d, (%fV)\n", cursor_source + 1, nr_voltages[voltage_ch[cursor_source]]);
			cursor_draw(0);
			cursor_draw(1);
			display_refresh(my_window);
			break;
		case GDK_i:
			interpolation_type ^= 1;
			display_refresh(my_window);
			break;
		case GDK_o:
			fl_osd ^= 1;
			display_refresh(my_window);
			break;
		case GDK_Shift_L:
		case GDK_Shift_R:
			fl_pan_ready = 1;
			gdk_window_set_cursor(w->window, cursor_hand);
			break;
		case GDK_Control_L:
		case GDK_Control_R:
			fl_xfactor = 1;
			break;
		case GDK_g:
			fl_grid ^= 1;
			display_refresh(my_window);
			break;
		case GDK_Escape:
			pan_x = pan_y = 0;
			zoom_factor = 1;
			x_factor = 1;
			cursor_set[0] = cursor_set[1] = 0;
			display_refresh(my_window);
			rezoom();
			break;
	}

	return FALSE;
}

static
gboolean key_release_cb(GtkWidget *w, GdkEventKey *e, gpointer p)
{
	switch(e->keyval) {
		case GDK_Shift_L:
		case GDK_Shift_R:
			fl_pan_ready = 0;
			gdk_window_set_cursor(w->window, cursor_cross);
			break;
		case GDK_Control_L:
		case GDK_Control_R:
			fl_xfactor = 0;
			break;
	}

	return FALSE;
}

GtkWidget *display_create_widget(GtkWidget *parent)
{
	GtkWidget *drawing_area = gtk_drawing_area_new ();
	gtk_widget_set_size_request (drawing_area, 320, 240);

	my_window = drawing_area;

	/* Set OpenGL-capability to the widget. */
	gtk_widget_set_gl_capability (drawing_area, glconfig, NULL, TRUE, GDK_GL_RGBA_TYPE);

	g_signal_connect_after (G_OBJECT (drawing_area), "realize", G_CALLBACK (realize), NULL);
	g_signal_connect (G_OBJECT (drawing_area), "configure_event", G_CALLBACK (configure_event), NULL);
	g_signal_connect (G_OBJECT (drawing_area), "expose_event", G_CALLBACK (expose_event), NULL);

	gtk_widget_add_events(drawing_area,
			GDK_POINTER_MOTION_MASK |
			GDK_BUTTON_PRESS_MASK |
			GDK_BUTTON_RELEASE_MASK | 
			GDK_SCROLL_MASK |
			GDK_KEY_PRESS_MASK | 
			GDK_KEY_RELEASE_MASK

	//		GDK_VISIBILITY_NOTIFY_MASK
			);

	g_signal_connect(G_OBJECT(drawing_area), "motion_notify_event", G_CALLBACK(mouse_motion_cb), 0);
	g_signal_connect(G_OBJECT(drawing_area), "button_press_event", G_CALLBACK(mouse_button_press_cb), 0);
	g_signal_connect(G_OBJECT(drawing_area), "button_release_event", G_CALLBACK(mouse_button_release_cb), 0);
	g_signal_connect(G_OBJECT(drawing_area), "scroll_event", G_CALLBACK(scroll_cb), 0);
	g_signal_connect_swapped(G_OBJECT(parent), "key_press_event", G_CALLBACK(key_press_cb), drawing_area);
	g_signal_connect_swapped(G_OBJECT(parent), "key_release_event", G_CALLBACK(key_release_cb), drawing_area);

	gtk_widget_show (drawing_area);
	//gtk_timeout_add(1000 / 24, update_timer_cb, 0);

	cursor_cross = gdk_cursor_new(GDK_CROSSHAIR);
	cursor_hand = gdk_cursor_new(GDK_HAND1);

	return drawing_area;
}

void display_done()
{
	gl_done();
//	gdk_cursor_unref(cursor_cross);
//	gdk_cursor_unref(cursor_hand);
}
