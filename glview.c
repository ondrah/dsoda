#include <gtk/gtk.h>
#include <gtk/gtkgl.h>
#include <math.h>
#include <sys/time.h>

#include <GL/gl.h>
#include <GL/glu.h>

#include <gdk/gdkkeysyms.h>

#include "io.h"
#include "local.h"

static GdkCursor *cursor_cross, *cursor_hand;
static GdkGLConfig *glconfig;
static int gl_channels, gl_grid, gl_math, gl_cursor;
static int fl_pan = 0, fl_pan_ready = 0;
static float zoom_factor = 1, pan_x = 0, pan_y = 0;
static float press_x, press_y;

struct cursor_coord {
	float x, y;
};

struct cursor_coord cursor[2] = { {.x = 0, .y = 0}, {.x = 0, .y = 0}}; 
int cursor_active = 0;

#define DP_DEPTH	16
#define MAX_CHANNELS 2

#define INTERPOLATION_OFF 0

#define DIVS_TIME	10
#define DIVS_VOLTAGE 8
#define VOLTAGE_SCALE	32

#define MARGIN_CANVAS	0.05

#define CHANNEL1_RGB	0.0f, 1.0f, 0.0f
#define CHANNEL2_RGB	1.0f, 1.0f, 0.0f

int dpIndex = 0;
int interpolationMode = 1;

static
void gl_done()
{
    if (gl_channels)
        glDeleteLists(gl_channels, DP_DEPTH*(MAX_CHANNELS+1));

	glDeleteLists(gl_grid, gl_grid);
}

GLuint gl_makegrid();

static
void gl_init()
{
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glEnable(GL_POINT_SMOOTH);
    glPointSize(5);

	gl_grid = gl_makegrid();
    gl_channels = glGenLists(MAX_CHANNELS);
    gl_math = glGenLists(1);
	gl_cursor = glGenLists(2);
    glShadeModel(GL_SMOOTH/*GL_FLAT*/);
    glLineStipple (1, 0x00FF);
}

void gl_resize(int w, int h)
{
   // glViewport(0, 0, (GLint)w, (GLint)h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-DIVS_TIME/2 - MARGIN_CANVAS, DIVS_TIME/2 + MARGIN_CANVAS, -DIVS_VOLTAGE/2 - MARGIN_CANVAS, DIVS_VOLTAGE/2 + MARGIN_CANVAS, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
}

struct timeval otime = { .tv_sec = 0, .tv_usec = 0 };

void update_screen()
{
	struct timeval tv;

   	gettimeofday(&tv, 0);
	unsigned int dmsec = 1000 * (tv.tv_sec - otime.tv_sec) + (tv.tv_usec - otime.tv_usec) / 1000;

	if(dmsec < 1000.0 / 30)		// frame limiter
		return;

	otime = tv;

    glPushMatrix();
   // glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
	//glTranslatef(-1.5f,0.0f,-6.0f);
    //glScalef(0.10, 0.10, 10);

    glLineWidth(2);

//	if(!fl_gui_running) {
//		glClear(GL_COLOR_BUFFER_BIT);
//		glCallList(gl_grid);
//		glPopMatrix();
//		return;
//	}

	while((int)trigger_point > (int)my_buffer_size) {
		trigger_point -= my_buffer_size;
	//	DMSG("tp=%d\n", trigger_point);
	}

	//printf("mbs=%d\n", my_buffer_size);
	for (int t = 0 ; t < MAX_CHANNELS; t++) {
		if(!capture_ch[t])
			continue;

		glNewList(gl_channels + t, GL_COMPILE);
		glBegin((interpolationMode == INTERPOLATION_OFF)?GL_POINTS:GL_LINE_STRIP);
		if(!t) {
			glColor4f(CHANNEL2_RGB, 0.5);
		} else {
			glColor4f(CHANNEL1_RGB, 0.5);
		}

//		for (int i = trigger_point; i < my_buffer_size; i++) {
//			glVertex2f(DIVS_TIME * ((i - trigger_point) / 10240.0 - 0.5) /* * SCALE_FACTOR */, DIVS_VOLTAGE * my_buffer[2*i + t] / 256.0 - DIVS_VOLTAGE / 2.0);
//		}
//		for (int i = 0; i < trigger_point; i++) {
//			glVertex2f(DIVS_TIME * ((i + my_buffer_size - trigger_point) / 10240.0 - 0.5) /* * SCALE_FACTOR */, DIVS_VOLTAGE * my_buffer[2*i + t] / 256.0 - DIVS_VOLTAGE / 2.0);
//		}

		int x = trigger_point;
		for (int i = 0; i < my_buffer_size; i++, x++) {
			if(x >= my_buffer_size)
				x = 0;
			glVertex2f(DIVS_TIME * ((float) i / my_buffer_size - 0.5) /* * SCALE_FACTOR */, DIVS_VOLTAGE * my_buffer[2*x + t] / 256.0 - DIVS_VOLTAGE / 2.0);
		}

		glEnd();
		glEndList();
	}

	if(fl_math) {
		glNewList(gl_math, GL_COMPILE);
		glBegin((interpolationMode == INTERPOLATION_OFF)?GL_POINTS:GL_LINE_STRIP);
		glColor4f(1.0f, 0.0f, 0.0f, 0.5);

		int x = trigger_point;
		for (int i = 0; i < my_buffer_size; i++, x++) {
			if(x >= my_buffer_size)
				x = 0;
			int v = (my_buffer[2*x] + my_buffer[2*x + 1] ) >> 1;
			glVertex2f(DIVS_TIME * ((float)i / my_buffer_size - 0.5) /* * SCALE_FACTOR */, DIVS_VOLTAGE * v / 256.0 - DIVS_VOLTAGE / 2.0);
		}
//		for (int i = 0; i < trigger_point; i++) {
//			int v = (my_buffer[2*i] + my_buffer[2*i + 1] ) >> 1;
//			glVertex2f(DIVS_TIME * ((i + my_buffer_size - trigger_point) / 10240.0 - 0.5) /* * SCALE_FACTOR */, DIVS_VOLTAGE * v / 256.0 - DIVS_VOLTAGE / 2.0);
//		}

		glEnd();
		glEndList();
	}

              //  glDisable(GL_LINE_SMOOTH);
				glClear(GL_COLOR_BUFFER_BIT);
				if(capture_ch[0])
					glCallList(gl_channels);
				if(capture_ch[1])
					glCallList(gl_channels + 1);
				if(fl_math)
					glCallList(gl_math);
                glCallList(gl_grid);
				glCallList(gl_cursor);
				glCallList(gl_cursor + 1);
                glPopMatrix();
            /*    break;

			
            case VIEWMODE_XY:
                glDisable(GL_LINE_SMOOTH);
                glCallList(gl_grid);
                break;

            case VIEWMODE_SPECTRUM:
                aThread->bufferMutex.lock();
                unsigned p = aThread->triggerPoint + samplesUntransformed/2;
                for (unsigned i = 0; i < samplesToTransform; i++, p++)
                {
                    if (p >= samplesInBuffer)
                    {
                        p -= samplesInBuffer;
                    }
                    for (int t = 0; t < MAX_CHANNELS; t++)
                    {
                        aThread->fhtBuffer[t][i] = (double)aThread->buffer[p][t];
                    }
                }
                aThread->transform();
                aThread->bufferMutex.unlock();

                glPushMatrix();
                glTranslatef(-DIVS_TIME/2, -DIVS_VOLTAGE/2, 0);
                glScalef(DIVS_TIME*timeDiv/transformedSamples, DIVS_VOLTAGE, 1.0);
                glLineWidth(1);
                for (int t = 0; t < MAX_CHANNELS; t++)
                {
                    glColor4f(chColor[t][0], chColor[t][1], chColor[t][2], chColor[t][3]);
                    glBegin((interpolationMode == INTERPOLATION_OFF)?GL_LINES:GL_QUADS);
                    GLfloat viewLen = transformViewLen;
                    if (interpolationMode != INTERPOLATION_OFF)
                    {
                        viewLen--;
                    }
                    for (unsigned i = 0; i < viewLen; i++)
                    {
                        glVertex2f(i, 0);
                        glVertex2f(i, aThread->fhtBuffer[t][i+transformViewPos]);
                        if (interpolationMode != INTERPOLATION_OFF)
                        {
                            glVertex2f(i+1, aThread->fhtBuffer[t][i+transformViewPos]);
                            glVertex2f(i+1, 0);
                        }
                    }
                    glEnd();
                }
                glPopMatrix();
                glDisable(GL_LINE_SMOOTH);
                glCallList(gl_grid);    // Draw grid
                break;
				*/

}

GLuint gl_makegrid()
{
    GLuint list = glGenLists(1);
    glNewList(list, GL_COMPILE);
    glLineWidth(1);

	glColor4f(0.7, 0.7, 0.7, 0.5);  // Grid Color
	glEnable(GL_LINE_STIPPLE);
	glBegin(GL_LINES);
	for(GLfloat i=1; i<=DIVS_TIME/2; i++) {
		glVertex2f(i, -DIVS_VOLTAGE/2);
		glVertex2f(i, DIVS_VOLTAGE/2);
		glVertex2f(-i, -DIVS_VOLTAGE/2);
		glVertex2f(-i, DIVS_VOLTAGE/2);
	}
	for(GLfloat i=1; i<=DIVS_VOLTAGE/2; i++) {
		glVertex2f(-DIVS_TIME/2, i);
		glVertex2f(DIVS_TIME/2, i);
		glVertex2f(-DIVS_TIME/2, -i);
		glVertex2f(DIVS_TIME/2, -i);
	}
	glEnd();

	// x- and y-axis
	glColor4f(1.0, 1.0, 1.0, 0.3);
	glDisable(GL_LINE_STIPPLE);
	glBegin(GL_LINES);
	glVertex2f(-DIVS_TIME/2, 0);
	glVertex2f(DIVS_TIME/2, 0);
	glVertex2f(0, -DIVS_VOLTAGE/2);
	glVertex2f(0, DIVS_VOLTAGE/2);
	for(GLfloat i=0; i<=DIVS_TIME/2; i+=0.5) {
		glVertex2f(i, -0.1);
		glVertex2f(i, 0.1);
		glVertex2f(-i, -0.1);
		glVertex2f(-i, 0.1);
	}
	for(GLfloat i=0; i<=DIVS_VOLTAGE/2; i+=0.5) {
		glVertex2f(-0.1, i);
		glVertex2f(0.1, i);
		glVertex2f(-0.1, -i);
		glVertex2f(0.1, -i);
	}
	glEnd();
    glEndList();

    return list;
}

static void
realize (GtkWidget *widget, gpointer   data)
{
	//DMSG("realize\n");
	gl_init();
	gdk_window_set_cursor(widget->window, cursor_cross);
}

static gboolean
configure_event (GtkWidget         *widget, GdkEventConfigure *event, gpointer           data)
{
  GdkGLContext *glcontext = gtk_widget_get_gl_context (widget);
  GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable (widget);

  g_print("configure\n");
  /*** OpenGL BEGIN ***/
  if (!gdk_gl_drawable_gl_begin (gldrawable, glcontext))
    return FALSE;
  int w = widget->allocation.width;
  int h = widget->allocation.height;
  DMSG("w %d h %d\n", w, h);
  glViewport (0, 0, w, h);
  gl_resize(w, h);

  gdk_gl_drawable_gl_end (gldrawable);
  /*** OpenGL END ***/

  return TRUE;
}

static gboolean
expose_event (GtkWidget      *widget,
	      GdkEventExpose *event,
	      gpointer        data)
{
	GdkGLContext *glcontext = gtk_widget_get_gl_context(widget);
  GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable(widget);

  /*** OpenGL BEGIN ***/
  if (!gdk_gl_drawable_gl_begin(gldrawable, glcontext))
    return FALSE;

	update_screen();

	if (gdk_gl_drawable_is_double_buffered (gldrawable))
		gdk_gl_drawable_swap_buffers (gldrawable);
	else
		glFlush ();

	gdk_gl_drawable_gl_end (gldrawable);
	return TRUE;
}

void display_refresh(GtkWidget *da)
{
	gdk_window_invalidate_rect(da->window, &da->allocation, FALSE);
	gdk_window_process_updates(da->window, FALSE);
}

//static
//gint update_timer_cb()
//{
//	display_refresh();
//	return TRUE;
//}

int display_init(int *pargc, char ***pargv)
{
  gint major, minor;

  gtk_gl_init(pargc, pargv);
  gdk_gl_query_version(&major, &minor);
  g_print("OpenGL extension %d.%d\n", major, minor);

  /* Try double-buffered visual */
  glconfig = gdk_gl_config_new_by_mode (GDK_GL_MODE_RGB    |
                                        GDK_GL_MODE_DEPTH  |
                                        GDK_GL_MODE_DOUBLE);
  if (glconfig == NULL) {
      g_print ("*** Cannot find the double-buffered visual.\n");
      g_print ("*** Trying single-buffered visual.\n");

      /* Try single-buffered visual */
      glconfig = gdk_gl_config_new_by_mode (GDK_GL_MODE_RGB   |
                                            GDK_GL_MODE_DEPTH);
      if (glconfig == NULL) {
          g_print ("*** No appropriate OpenGL-capable visual found.\n");
          //exit (1);
		  return -1;
        }
    }

  //examine_gl_config_attrib (glconfig);

  /* Get automatically redrawn if any of their children changed allocation. */
 // gtk_container_set_reallocate_redraws (GTK_CONTAINER (window), TRUE);

  return 0;
}

static
void convert_coords(float *dx, float *dy, GtkWidget *w, float cx, float cy)
{
	*dx = DIVS_TIME * cx / w->allocation.width - DIVS_TIME / 2;
	*dy = -(DIVS_VOLTAGE * cy / w->allocation.height - DIVS_VOLTAGE / 2);
}

static
void cursor_draw(int i)
{
	struct cursor_coord *ac = &cursor[i];

	glNewList(gl_cursor + i, GL_COMPILE);
	if(i == 1)
		glEnable(GL_LINE_STIPPLE);
	glBegin(GL_LINE_STRIP);

	glColor4f(CHANNEL1_RGB, 0.8);

	glVertex2f(ac->x * zoom_factor, -DIVS_VOLTAGE / 2);
	glVertex2f(ac->x * zoom_factor, DIVS_VOLTAGE / 2);
	glEnd();

	glBegin(GL_LINES);
	glVertex2f(-DIVS_TIME / 2, ac->y * zoom_factor);
	glVertex2f(DIVS_TIME / 2, ac->y * zoom_factor);

	glEnd();
	if(i == 1)
		glDisable(GL_LINE_STIPPLE);
	glEndList();

}

static
void rezoom()
{
	glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho((-DIVS_TIME/2 - MARGIN_CANVAS + pan_x) * zoom_factor, (DIVS_TIME/2 + MARGIN_CANVAS + pan_x) * zoom_factor, (-DIVS_VOLTAGE/2 - MARGIN_CANVAS + pan_y) * zoom_factor, (DIVS_VOLTAGE/2 + MARGIN_CANVAS + pan_y) * zoom_factor, -1.0, 1.0);
	glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static
gboolean mouse_motion_cb(GtkWidget *w, GdkEventMotion *e, gpointer p)
{
	//DMSG("x = %d, y=%d!\n", (int)e->x, (int)e->y);

	if(w->allocation.width < 0 || w->allocation.height < 0)
		return FALSE;

	if(fl_pan) {
		float mx;
		float my;
		convert_coords(&mx, &my, w, e->x, e->y);
		pan_x += (press_x - mx); // zoom_factor;
		pan_y += (press_y - my); // zoom_factor;
		press_x = mx;
		press_y = my;

		pan_x = MAX(pan_x, -DIVS_TIME / 2 / zoom_factor);
		pan_x = MIN(pan_x, +DIVS_TIME / 2 / zoom_factor);
		pan_y = MAX(pan_y, -DIVS_VOLTAGE / 2 / zoom_factor);
		pan_y = MIN(pan_y, +DIVS_VOLTAGE / 2 / zoom_factor);

		rezoom();
	}

	if(cursor_active) {
		convert_coords(&cursor[1].x, &cursor[1].y, w, e->x, e->y);
		cursor_draw(1);
	}

	return FALSE;
}

static
gboolean mouse_button_press_cb(GtkWidget *w, GdkEventButton *e, gpointer p)
{
	//DMSG("type = %d\n", e->type);

	if(fl_pan_ready) {
		fl_pan = 1;
		convert_coords(&press_x, &press_y, w, e->x, e->y);
		return FALSE;
	}

	cursor_active = 1;
	convert_coords(&cursor[0].x, &cursor[0].y, w, e->x, e->y);
	cursor_draw(0);

	return FALSE;
}

static
gboolean mouse_button_release_cb(GtkWidget *w, GdkEventButton *e, gpointer p)
{
	if(cursor_active) {
		cursor_active = 0;
		return FALSE;
	}
	
	fl_pan = 0;
	return FALSE;
}

static
gboolean scroll_cb(GtkWidget *w, GdkEventScroll *e, gpointer p)
{
	if(e->direction > 0) {
		DMSG("-\n");
		zoom_factor *= 2;
		if(zoom_factor > 2)
			zoom_factor = 2;
	} else {
		DMSG("+\n");
		zoom_factor /= 2;
		if(zoom_factor < 0.005)
			zoom_factor = 0.005;
	}

	rezoom();
	return FALSE;
}

static
gboolean key_press_cb(GtkWidget *w, GdkEventKey *e, gpointer p)
{
	if(e->keyval == GDK_Shift_L) {
		fl_pan_ready = 1;
		gdk_window_set_cursor(w->window, cursor_hand);
	}

	return FALSE;
}

static
gboolean key_release_cb(GtkWidget *w, GdkEventKey *e, gpointer p)
{
	if(e->keyval == GDK_Shift_L) {
		fl_pan_ready = 0;
		gdk_window_set_cursor(w->window, cursor_cross);
	}

	return FALSE;
}

GtkWidget *display_create_widget(GtkWidget *parent)
{
	GtkWidget *drawing_area = gtk_drawing_area_new ();
	gtk_widget_set_size_request (drawing_area, 320, 240);

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
