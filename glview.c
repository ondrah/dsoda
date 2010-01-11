#include <gtk/gtk.h>
#include <gtk/gtkgl.h>
#include <math.h>

#include <GL/gl.h>
#include <GL/glu.h>

#include "hantek_io.h"

static GdkGLConfig *glconfig;
int gl_channels, gl_grid;

#define DP_DEPTH	16
#define MAX_CHANNELS 2

#define INTERPOLATION_OFF 0

#define DIVS_TIME	10
#define DIVS_VOLTAGE 8
#define VOLTAGE_SCALE	32

int dpIndex = 0;
int interpolationMode = 0;

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
	printf("gl_grid = %d\n", gl_grid);
    gl_channels = glGenLists(MAX_CHANNELS);
	printf("gl_channels = %d\n", gl_channels);
    glShadeModel(GL_SMOOTH/*GL_FLAT*/);
    glLineStipple (1, 0x000F);
}

void gl_resize(int w, int h)
{
   // glViewport(0, 0, (GLint)w, (GLint)h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-DIVS_TIME/2 - 0.1, DIVS_TIME/2 + 0.1, -DIVS_VOLTAGE/2 - 0.1, DIVS_VOLTAGE/2 + 0.1, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
}

int timeDiv = 10;
int timeShift = 10;

void update_screen()
{
    glPushMatrix();
   // glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
	//glTranslatef(-1.5f,0.0f,-6.0f);
    //glScalef(0.10, 0.10, 10);

    glLineWidth(2);
	unsigned samplesInBuffer = 1000;
	unsigned samplesInScale = 1000;
	unsigned samplesInvisible = samplesInBuffer - samplesInScale;
	unsigned viewLen = (unsigned)(samplesInScale/timeDiv);
	unsigned restLen = samplesInScale - viewLen;

	//unsigned samplesToTransform = aThread->transformSize;
	unsigned samplesToTransform = 1000;
	unsigned transformedSamples = samplesToTransform/2;
	unsigned transformViewLen = (unsigned)(transformedSamples/timeDiv);
	unsigned transformRestLen = transformedSamples - transformViewLen;

//	if(!fl_gui_running) {
//		glClear(GL_COLOR_BUFFER_BIT);
//		glCallList(gl_grid);
//		glPopMatrix();
//		return;
//	}

	while((int)trigger_point > (int)my_buffer_size) {
		trigger_point -= my_buffer_size;
		g_printf("tp=%d\n", trigger_point);
	}

	//printf("mbs=%d\n", my_buffer_size);
	for (int t = 0 ; t < MAX_CHANNELS; t++) {
		//if (chActive[t])
		{
			glNewList(gl_channels + t, GL_COMPILE);
			glBegin((interpolationMode == INTERPOLATION_OFF)?GL_POINTS:GL_LINE_STRIP);
			if(!t) {
				glColor4f(0.0f, 1.0f, 0.0f, 0.5);
			} else {
				glColor4f(1.0f, 1.0f, 0.0f, 0.5);
			}

			//for (int i = 0; i < my_buffer_size; i++) {
			for (int i = trigger_point; i < my_buffer_size; i++) {
				glVertex2f(DIVS_TIME * ((i - trigger_point) / 10240.0 - 0.5) /* * SCALE_FACTOR */, DIVS_VOLTAGE * my_buffer[2*i+t] / 256.0 - DIVS_VOLTAGE / 2.0);
			}
			for (int i = 0; i < trigger_point; i++) {
				glVertex2f(DIVS_TIME * ((i + my_buffer_size - trigger_point) / 10240.0 - 0.5) /* * SCALE_FACTOR */, DIVS_VOLTAGE * my_buffer[2*i+t] / 256.0 - DIVS_VOLTAGE / 2.0);
			}
			glEnd();
			glEndList();
		}
	}

				/*
                if (mathType != MATHTYPE_OFF)
                {
                    glNewList(gl_channels + dpIndex*MAX_CHANNELS + MAX_CHANNELS, GL_COMPILE);
                    glBegin((interpolationMode == INTERPOLATION_OFF)?GL_POINTS:GL_LINE_STRIP);
                    unsigned p = aThread->triggerPoint + viewPos;
                    for (unsigned i = 0; i < viewLen; i++)
                    {
                        if (p >= samplesInBuffer)
                        {
                            p -= samplesInBuffer;
                        }
                        int ch2Val = aThread->buffer[p][0] - VOLTAGE_SCALE/2;
                        int ch1Val = aThread->buffer[p++][1] - VOLTAGE_SCALE/2;
                        switch(mathType)
                        {
                            case MATHTYPE_1ADD2:
                                glVertex2f(i, ch1Val + ch2Val + chMOffset);
                                break;
                            case MATHTYPE_1SUB2:
                                glVertex2f(i, ch1Val - ch2Val + chMOffset);
                                break;
                            case MATHTYPE_2SUB1:
                                glVertex2f(i, ch2Val - ch1Val + chMOffset);
                                break;
                        }
                    }
                    glEnd();
                    glEndList();
                }
				*/

//                glPushMatrix();
//                //glTranslatef(-DIVS_TIME/2, -DIVS_VOLTAGE/2, 0);
//                //glScalef(DIVS_TIME*timeDiv/samplesInScale, DIVS_VOLTAGE/VOLTAGE_SCALE, 1.0);
//                glScalef(DIVS_TIME*timeDiv/samplesInScale, DIVS_VOLTAGE/VOLTAGE_SCALE, 1.0);
//
//                glEnable(GL_LINE_SMOOTH);
//                glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
//
//                for (int t = 0 ; t < MAX_CHANNELS; t++)
//                {
//                   // if (chActive[t])
//                    {
//                        for (int i = (digitalPhosphor?DP_DEPTH:0); i >= 0; i--)
//                        {
//                            glColor4f(chColor[t][0], chColor[t][1], chColor[t][2],
//                                chColor[t][3] - 0.7*log(i + 1));
//                            int index = (dpIndex + i) % DP_DEPTH;
//						  glCallList(gl_channels + index*MAX_CHANNELS + t);
//                        }
//                    }
//                }
				/*
                if (mathType != MATHTYPE_OFF)
                {
                    // TODO: find error in math channel digital phosphor code
                    for (int i = (digitalPhosphor?DP_DEPTH:0); i >= 0; i--)
                    {
                        glColor4f(chColor[MAX_CHANNELS][0], chColor[MAX_CHANNELS][1],
                            chColor[MAX_CHANNELS][2], chColor[MAX_CHANNELS][3] - 0.7*log(i + 1));
                        int index = (dpIndex + i) % DP_DEPTH;
                        glCallList(gl_channels + index*MAX_CHANNELS + MAX_CHANNELS);
                    }
                }
				*/
//                if (digitalPhosphor)
//                {
//                    if (++dpIndex >= DP_DEPTH)
//                    {
//                        dpIndex = 0;
//                    }
//                }

/*
                glPushMatrix();
                glColor4f(0.0, 1.0, 0.0, 1.0);
                glTranslatef(-DIVS_TIME/2, -DIVS_VOLTAGE/2, 0.0);
                str=QString("%1").arg("400ms");
                font.glString(str, 0.3);
                glPopMatrix();
*/
              //  glDisable(GL_LINE_SMOOTH);
				glClear(GL_COLOR_BUFFER_BIT);
                glCallList(gl_channels);
                glCallList(gl_channels + 1);
                glCallList(gl_grid);
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

//    glPopMatrix();

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

/*
void GLBox::setTimeDiv(double div)
{
    timeDiv = div;
    updateGL();
}

void GLBox::setTimeShift(double shift)
{
    timeShift = shift;
    updateGL();
}
*/

static void
realize (GtkWidget *widget, gpointer   data)
{
	g_printf("realize\n");
	gl_init();
	//gl_resize(640,480);

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

  glViewport (0, 0, widget->allocation.width, widget->allocation.height);
  gl_resize(widget->allocation.width, widget->allocation.height);

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

int display_init(int argv, char **argc)
{
  gint major, minor;

  GtkWidget *vbox;
  GtkWidget *button;
  GtkWidget *window;

  gtk_gl_init (&argc, &argv);
  gdk_gl_query_version (&major, &minor);
  g_print ("\nOpenGL extension version - %d.%d\n", major, minor);

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

GtkWidget *display_create_widget()
{
	GtkWidget *drawing_area = gtk_drawing_area_new ();
	gtk_widget_set_size_request (drawing_area, 320, 240);

	/* Set OpenGL-capability to the widget. */
	gtk_widget_set_gl_capability (drawing_area, glconfig, NULL, TRUE, GDK_GL_RGBA_TYPE);

	g_signal_connect_after (G_OBJECT (drawing_area), "realize", G_CALLBACK (realize), NULL);
	g_signal_connect (G_OBJECT (drawing_area), "configure_event", G_CALLBACK (configure_event), NULL);
	g_signal_connect (G_OBJECT (drawing_area), "expose_event", G_CALLBACK (expose_event), NULL);

	gtk_widget_show (drawing_area);
	//gtk_timeout_add(1000 / 24, update_timer_cb, 0);

	return drawing_area;
}

void display_done()
{
}
