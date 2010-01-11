#define _XOPEN_SOURCE
#define _POSIX_SOURCE

#include <unistd.h>
#include <gtk/gtk.h>
#include <gtk/gtkitemfactory.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <gtkdatabox.h>
#include <gtkdatabox_lines.h>
#include <gtkdatabox_grid.h>
#include <gtkdatabox_cross_simple.h>

#include <stdio.h>

#include "hantek_io.h"
#include "hantek_thread.h"

#include "local.h"

#define APPNAME	"HantekGTK"

static int fl_running = 0;

static int voltage_ch[2] = { VOLTAGE_5V, VOLTAGE_5V };
static int trigger_slope = SLOPE_PLUS;
static int trigger_source = TRIGGER_CH1;
static int trigger_position = 0xebfe;
static int selected_channels = SELECT_CH1CH2;
static int buffer_size = 10240;
static int reject_hf = 0;		//< reject high frequencies
static int coupling_ch[2] = { COUPLING_AC, COUPLING_AC };
static int offset_ch1 = 0xb9, offset_ch2 = 0x89, offset_t = 0x99;
static gfloat OFFSET_CH1, OFFSET_CH2, OFFSET_T;
static struct offset_ranges offset_ranges;
static int attenuation_ch[2] = { 1, 1};

unsigned int trigger_point = 0;

static int p[2];	// dso_thread => gui update mechanism pipe

enum
{
   SHOW_ACTUAL_X,
   SHOW_ACTUAL_Y,
   SHOW_MARKED_X,
   SHOW_MARKED_Y,
   SHOW_DELTA_X,
   SHOW_DELTA_Y,
   SHOW_NUM_ENTRIES
};

#define SCALAR(a)	(sizeof(a) / sizeof(a[0]))

static const char *str_buffer_sizes[] = { "10KS/ch", "512KS/ch", "1MS/ch"};
static const unsigned int nr_buffer_sizes[] = { 10240, 524228, 1048576 };
static const char *str_sampling_rates[] = { 
	"8ns 125MS/s", "10ns 100MS/s", "20ns 50MS/s", "40ns 25MS/s", "100ns 10MS/s", "200ns 5MS/s", "400ns 2.5MS/s", "1us 1MS/s", "2us 500KS/s", "4us 250KS/s", "10us 100KS/s", "20us 50KS/s", "40us 25KS/s", "100us 10KS/s", "200us 5KS/s", "400us 2.5KS/s", "20ms 50S/s"
};
static const float nr_sampling_rates[] = { 125000000, 100000000,50000000,25000000,10000000,5000000,2500000,1000000,500000,250000,100000,50000,25000,10000,5000,2500,50
};

//static const float nr_time_steps[] = {8, 10, 20, 40, 100, 200, 400, 1, 2, 4,
//	10, 20, 40, 100, 200, 400, 20, 40, 80, 200, 400, 800, 1.20482, 2.38095,
//	4.7619, 12.0482 , 23.8095, 47.619, 71.4286 };

static const char *str_voltages[] = { "10mV", "20mV", "50mV", "100mV", "200mV", "500mV", "1V", "2V", "5V", };
static const char *str_attenuations[] = {"x1", "x10"};
static int nr_attenuations[] = { 1, 10 };

static int buffer_size_idx = 0, sampling_rate_idx = 0;
unsigned int period_usec = 10000;

static GtkWidget *window = NULL;	//!< Main window.
static GtkWidget **entries;
//static GtkDatabox *box;
static GtkWidget *box;
static GtkWidget *time_per_window, *set_srate, *set_bsize, *run_button;

unsigned int nr_points = 0;		//!< number of samples in the graph
gfloat *X, *Y, *I;
GtkDataboxGraph *graph_ch1, *graph_ch2;
//gfloat MX = 0, MY = 0;

static gint
show_motion_notify_cb (GtkWidget ** entries, GdkEventMotion * event
		       /*, GtkWidget *widget */ )
{
   gfloat x, y;
   gchar *text;

   x = gtk_databox_pixel_to_value_x (GTK_DATABOX(box), event->x);
   y = gtk_databox_pixel_to_value_y (GTK_DATABOX(box), event->y);

   text = g_strdup_printf ("%g", x);
   gtk_entry_set_text (GTK_ENTRY (entries[SHOW_ACTUAL_X]), text);
   g_free ((gpointer) text);
   text = g_strdup_printf ("%g", y);
   gtk_entry_set_text (GTK_ENTRY (entries[SHOW_ACTUAL_Y]), text);
   g_free ((gpointer) text);

   return FALSE;
}

static gint
show_button_press_cb (GtkDatabox * box, GdkEventButton * event,
		      GtkWidget ** entries)
{
   gfloat x, y;
   gchar *text;

   if (!(event->button == 1 || event->button == 2))
      return FALSE;

   x = gtk_databox_pixel_to_value_x (box, event->x);
   y = gtk_databox_pixel_to_value_y (box, event->y);

   text = g_strdup_printf ("%g", x);
   gtk_entry_set_text (GTK_ENTRY (entries[SHOW_MARKED_X]), text);
   g_free ((gpointer) text);
   text = g_strdup_printf ("%g", y);
   gtk_entry_set_text (GTK_ENTRY (entries[SHOW_MARKED_Y]), text);
   g_free ((gpointer) text);

   return FALSE;
}

static void
show_changed_cb (GtkDatabox * box,
		 GtkDataboxValueRectangle * selectionValues,
		 GtkWidget ** entries)
{
   gchar *text;

   text = g_strdup_printf ("%g", selectionValues->x2 - selectionValues->x1);
   gtk_entry_set_text (GTK_ENTRY (entries[SHOW_DELTA_X]), text);
   g_free ((gpointer) text);
   text = g_strdup_printf ("%g", selectionValues->y2 - selectionValues->y1);
   gtk_entry_set_text (GTK_ENTRY (entries[SHOW_DELTA_Y]), text);
   g_free ((gpointer) text);

   text = g_strdup_printf ("%g", selectionValues->x2);
   gtk_entry_set_text (GTK_ENTRY (entries[SHOW_ACTUAL_X]), text);
   g_free ((gpointer) text);
   text = g_strdup_printf ("%g", selectionValues->y2);
   gtk_entry_set_text (GTK_ENTRY (entries[SHOW_ACTUAL_Y]), text);
   g_free ((gpointer) text);
}

static GtkWidget *
show_entry (GtkWidget * hbox, gchar * text)
{
   GtkWidget *frame;
   GtkWidget *entry;

   frame = gtk_frame_new (text);
   gtk_container_add (GTK_CONTAINER (hbox), frame);
   entry = gtk_entry_new ();
   gtk_widget_set_size_request (entry, 20, -1);
   gtk_editable_set_editable (GTK_EDITABLE (entry), FALSE);
   gtk_container_add (GTK_CONTAINER (frame), entry);

   return entry;
}

static void
run_clicked()
{
	//DMSG("run clicked\n");

	int state = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(run_button));
	if(state) {
		DMSG("running capture\n");
		gtk_button_set_label(GTK_BUTTON(run_button), "Stop");
		dso_capture_start();

		dso_set_trigger_sample_rate(sampling_rate_idx, selected_channels, trigger_source, trigger_slope, trigger_position, buffer_size);
		//dso_set_filter(reject_hf);

		dso_thread_resume();
		gtk_databox_set_enable_zoom(GTK_DATABOX(box), 0);
		gtk_databox_set_enable_selection(GTK_DATABOX(box), 0);
		fl_running = 1;	// start updating the screen
	} else {
		DMSG("stopping capture\n");
		dso_thread_pause();
		gtk_button_set_label(GTK_BUTTON(run_button), "Run");
		gtk_databox_set_enable_zoom(GTK_DATABOX(box), 1);
		gtk_databox_set_enable_selection(GTK_DATABOX(box), 1);
		fl_running = 0;
	}
}

static void
log_clicked()
{
	printf("log clicked\n");
}

static void
update_offset()
{
	/*
	OFFSET_CH1 = offset_ch1 / 32.0;
	OFFSET_CH2 = offset_ch2 / 32.0;
	OFFSET_T = offset_t / 32.0;*/
	
	int ro_ch1, ro_ch2, ro_t;	// real offsets
	ro_ch1 = (offset_ranges.channel[0][voltage_ch[0]][1] - offset_ranges.channel[0][voltage_ch[0]][0]) * offset_ch1/255.0 + offset_ranges.channel[0][voltage_ch[0]][0];
	ro_ch2 = (offset_ranges.channel[1][voltage_ch[1]][1] - offset_ranges.channel[1][voltage_ch[1]][0]) * offset_ch2/255.0 + offset_ranges.channel[1][voltage_ch[1]][0];
	ro_t = (offset_ranges.trigger[1] - offset_ranges.trigger[0]) * offset_t / 255.0 + offset_ranges.trigger[0];

	OFFSET_T = ro_t;

	dso_set_offset(ro_ch1, ro_ch2, ro_t);
	//FIXME repaint
}

static void
attenuation_cb(GtkWidget *w, int ch)
{
	DMSG("ch %d\n", ch);

	int nval = gtk_combo_box_get_active(GTK_COMBO_BOX(w));
	attenuation_ch[ch] = nr_attenuations[nval];
}

static void
coupling_cb(GtkWidget *w, int ch)
{
	DMSG("ch %d\n", ch);

	int nval = gtk_combo_box_get_active(GTK_COMBO_BOX(w));
	coupling_ch[ch] = nval;
	dso_set_voltage_and_coupling(voltage_ch[0], voltage_ch[1], coupling_ch[0], coupling_ch[1], trigger_source);
}

static void
voltage_changed_cb(GtkWidget *v, int ch)
{
	DMSG("ch %d\n", ch);

	int nval = gtk_combo_box_get_active(GTK_COMBO_BOX(v));

	voltage_ch[ch] = nval;
	dso_set_voltage_and_coupling(voltage_ch[0],voltage_ch[1], coupling_ch[0], coupling_ch[1], trigger_source);
	update_offset();
}

static void
trigger_slope_cb(GtkWidget *v, int ch)
{
	trigger_slope = gtk_combo_box_get_active(GTK_COMBO_BOX(v));
	if(fl_running) {
		dso_set_trigger_sample_rate(sampling_rate_idx, selected_channels, trigger_source, trigger_slope, trigger_position, buffer_size);
	}
}

static void
trigger_source_cb(GtkWidget *v, int ch)
{
	trigger_source = gtk_combo_box_get_active(GTK_COMBO_BOX(v));
	if(fl_running) {
		dso_set_trigger_sample_rate(sampling_rate_idx, selected_channels, trigger_source, trigger_slope, trigger_position, buffer_size);
	}
}

static GtkWidget *
create_channel_box(const char *name, int channel_id, GtkWidget *parent)
{
	GtkWidget *frame;
	frame = gtk_frame_new (name);

	GtkWidget *gw = gtk_vbox_new (FALSE, 10);
	gtk_container_add (GTK_CONTAINER (frame), gw);

	GtkWidget *c_voltage = gtk_combo_box_new_text();
	for(int i=0;i<SCALAR(str_voltages);i++)
		gtk_combo_box_insert_text(GTK_COMBO_BOX(c_voltage), i, str_voltages[i]);
	gtk_combo_box_set_active(GTK_COMBO_BOX(c_voltage), voltage_ch[channel_id]);

	g_signal_connect (G_OBJECT(c_voltage), "changed", G_CALLBACK(voltage_changed_cb), channel_id);

	GtkWidget *c_attenuation = gtk_combo_box_new_text();
	for(int i=0;i<SCALAR(str_attenuations);i++)
		gtk_combo_box_insert_text(GTK_COMBO_BOX(c_attenuation), i, str_attenuations[i]);
	gtk_combo_box_set_active(GTK_COMBO_BOX(c_attenuation), 0);
	g_signal_connect(GTK_OBJECT(c_attenuation), "changed", G_CALLBACK (attenuation_cb), channel_id);

	GtkWidget *enabled = gtk_check_button_new_with_label("capture");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enabled), 1);
	g_signal_connect_swapped (GTK_OBJECT (enabled), "toggled",
		//		 G_CALLBACK (run_clicked),
		0, GTK_OBJECT (parent));

	gtk_box_pack_start (GTK_BOX (gw), enabled, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (gw), c_voltage, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (gw), c_attenuation, FALSE, FALSE, 0);
	
	GtkWidget *c_coupling= gtk_combo_box_new_text();
	gtk_combo_box_insert_text(GTK_COMBO_BOX(c_coupling), 0, "AC");
	gtk_combo_box_insert_text(GTK_COMBO_BOX(c_coupling), 1, "DC");
	gtk_combo_box_set_active(GTK_COMBO_BOX(c_coupling), coupling_ch[channel_id]);
	gtk_box_pack_start (GTK_BOX (gw), c_coupling, TRUE, TRUE, 0);
	g_signal_connect(GTK_OBJECT (c_coupling), "changed", G_CALLBACK (coupling_cb), channel_id);

	return frame;
}

static void
update_time_per_window()
{
	char buf[64];
	float t = nr_buffer_sizes[buffer_size_idx] / nr_sampling_rates[sampling_rate_idx];
	float r = t;
	char *unit;

	if(t < 0.001) {
		r *= 1000000;
		unit = "us";
	} else if(t < 1) {
		r *= 1000;
		unit = "ms";
	} else {
		unit = "s";
	}
	snprintf(buf,sizeof(buf),"%g %s", r, unit);
	gtk_label_set_text(GTK_LABEL(time_per_window), buf);
}

static
void buffer_size_cb()
{
	buffer_size_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(set_bsize));
	//dso_adjust_buffer(nr_buffer_sizes[buffer_size_idx]);
	DMSG("buffer sizes other than 10240 not supported\n");
	update_time_per_window();
}

static
void sampling_rate_cb()
{
	sampling_rate_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(set_srate));
	update_time_per_window();

	period_usec = 1000000 / nr_sampling_rates[sampling_rate_idx];

	dso_set_trigger_sample_rate(sampling_rate_idx, selected_channels, trigger_source, trigger_slope, trigger_position, buffer_size);
	//dso_set_filter(reject_hf);
}

static
void gui_about()
{
	GtkWidget *dialog = gtk_about_dialog_new();
	gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dialog), "HantekGTK");
	gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), "0.1"); 
	gtk_about_dialog_set_copyright(GTK_ABOUT_DIALOG(dialog), "(c) ond");
	gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dialog), "HantekGTK is a simple frontend for Hantek oscilloscopes with data logging capability.");
	gtk_about_dialog_set_website(GTK_ABOUT_DIALOG(dialog), "http://hantekgtk.sf.net");
	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file("hantek-icon.png", NULL);
	gtk_about_dialog_set_logo(GTK_ABOUT_DIALOG(dialog), pixbuf);
	g_object_unref(pixbuf);
	gtk_dialog_run(GTK_DIALOG (dialog));
	gtk_widget_destroy(dialog);
}

static void clear_graph_cb();

// get currently selected time step of suitable time unit
//static
//float get_time_step()
//{
//	return nr_time_steps[sampling_rate_idx];
//}
//
static
void load_file(char *fname)
{
	float time_step = //get_time_step ();
	1000000/nr_sampling_rates[sampling_rate_idx];
	DMSG("loading file with time step %g\n", time_step);

	if(nr_points)
		clear_graph_cb();

   FILE *f;
   f=fopen(fname,"r");

   struct stat st;
   fstat(fileno(f), &st);
   int my_lines = st.st_size / 16;	// estimate number of lines
   
   I = g_new0 (gfloat, my_lines);
   X = g_new0 (gfloat, my_lines);
   Y = g_new0 (gfloat, my_lines);

   float a = 0,b = 0;
   DMSG("loading data...\n");
   int k = 0;
   while(fscanf(f,"%f %f", &a, &b) != EOF) {
	   //printf("got %f ; %f\n", a, b);
	   I[k] = time_step * k;		// TODO: overtake the time (sampling freq.) constant
	   X[k] = a;
	   Y[k] = b;
	   k++;
	   if(k > my_lines) {	// oops, buffer too small
		   my_lines += 0x10000;
		   I = g_realloc(I, sizeof(I[0]) * my_lines);
		   X = g_realloc(X, sizeof(X[0]) * my_lines);
		   Y = g_realloc(Y, sizeof(Y[0]) * my_lines);
	   }
	   a = 0;
	   b = 0;
   }

   fclose(f);
	DMSG("done, %d lines processed\n", k);

	nr_points = k;

   GdkColor color;

   color.red = 0;
   color.green = 65535;
   color.blue = 0;

   graph_ch1 = gtk_databox_lines_new (nr_points, I, X, &color, 1);
   gtk_databox_graph_add (GTK_DATABOX (box), graph_ch1);

   //g_free(X);

   color.red = 65535;
   color.green = 0;
   color.blue = 0;

   graph_ch2 = gtk_databox_lines_new (nr_points, I, Y, &color, 1);
   gtk_databox_graph_add (GTK_DATABOX (box), graph_ch2);

  // g_free(Y);
  // g_free(I);

   //gtk_databox_auto_rescale (GTK_DATABOX (box), 0.10);
   //gtk_databox_set_total_limits(GTK_DATABOX(box), 0,time_step * k,256/32, 0);
   //gtk_databox_set_visible_limits(GTK_DATABOX(box), 0,time_step * k / 5,256/32,0);
   gtk_databox_set_total_limits(GTK_DATABOX(box), 0,time_step * k,256, 0);
   gtk_databox_set_visible_limits(GTK_DATABOX(box), 0,time_step * k / 5,256,0);
}

static
int find_trigger(unsigned triggerPoint)
{
/*    unsigned tp1 = triggerPoint;
    unsigned tp2 = 1;
    while(tp1 > 0) {
        tp1 >>= 1;
        tp2 <<= 1;
    }*/

    //unsigned var_1 = tp2;// - 1;
    unsigned var_1 = 1;// - 1;
	while(var_1 < triggerPoint)
		var_1 <<= 1;
    unsigned var_2 = 0;

    unsigned var_C = 0;
    unsigned var_10 = 0;

    int flag = 1;
    while (var_1 > var_2 + 1) {
        var_C = ((var_1 - var_2 + 1) >> 1) + var_10;
		unsigned m = (var_1 + var_2)>>1;
        if((var_C > triggerPoint) == flag)	{
            if(!flag) {
                var_10 = var_C;
				flag = 1;
            }
            var_1 = m;
        } else {
            if(flag) {
				var_10 = var_C;
				flag = 0;
            }
            var_2 = m;
        }
    }

    return var_2;
}

// redraw signal handler
static
gint update_gui_cb()
{
#define RETVAL TRUE
	//char c;
	int i;
	read(p[0], &i, sizeof(i));

	if(!fl_running)
		return RETVAL;

	if(!dso_buffer_dirty)
		return RETVAL;

	pthread_mutex_lock(&buffer_mutex);
	memcpy(my_buffer, dso_buffer, 2 * dso_buffer_size);
	memset(dso_buffer, 0, dso_buffer_size * 2);
	dso_buffer_dirty = 0;
	trigger_point = dso_trigger_point;
	pthread_mutex_unlock(&buffer_mutex);

	trigger_point = find_trigger(trigger_point);

	if(trigger_point < 1024 || trigger_point > nr_points - 1024)
		return;

	// ooo
//	if(trigger_point < 512)
//		return;

	float time_step = //get_time_step ();
	1000000/nr_sampling_rates[sampling_rate_idx];

	//DMSG("repainting, %d points, timestep %f...\n", my_buffer_size, time_step);

	//if(nr_points)
	//	clear_graph_cb();
   
	for(int k = 0; k < my_buffer_size; k++) {
	   I[k] = (k - (int)trigger_point) * time_step;
	   Y[k] = my_buffer[k * 2];		// ch2
	   X[k] = my_buffer[k * 2 + 1];	// ch1
	}

	nr_points = my_buffer_size;

  // g_free(Y);
  // g_free(I);

	//MX = trigger_point * time_step;

	//gtk_databox_set_total_limits(GTK_DATABOX(box), 0, time_step * nr_points,  256 / 32, 0);
	//gtk_databox_set_visible_limits(GTK_DATABOX(box), 0, time_step * nr_points, 256 / 32, 0);

	//gtk_databox_set_total_limits(GTK_DATABOX(box), 0, time_step * nr_points,  0, 256);

//	if(trigger_point < 1024 || trigger_point > nr_points - 1024)
//		return;

	//gtk_databox_set_total_limits(GTK_DATABOX(box), -trigger_point * time_step, time_step * (nr_points - trigger_point), 0, 256);
	gtk_databox_set_total_limits(GTK_DATABOX(box), -0xffff * time_step, time_step * nr_points, 0, 256);
	gtk_databox_set_visible_limits(GTK_DATABOX(box), -5000 * time_step, 5000 * time_step, 0, 256);

	return RETVAL;
#undef RETVAL
}

void dso_update_gui()
{
	//g_idle_add(update_gui_cb, 0);
	//g_timeout_add(0, update_gui_cb, 0);
	//g_idle_add_full(G_PRIORITY_HIGH_IDLE + 20, update_gui_cb, 0, 0);
	char c = 'x';
	write(p[1], &c, 1);
}

static
void clear_graph_cb()
{
	if(!nr_points)
		return;		// no graph present

	gtk_databox_graph_remove(GTK_DATABOX(box),graph_ch1);
	gtk_databox_graph_remove(GTK_DATABOX(box),graph_ch2);
	// FIXME: release graph_ch1 and graph_ch2 here
	//gtk_object_destroy(GTK_OBJECT(graph_ch1));
	//gtk_object_destroy(GTK_OBJECT(graph_ch2));
	graph_ch1 = 0;
	graph_ch2 = 0;
	nr_points = 0;
	g_free(I);
	g_free(X);
	g_free(Y);
	I = X = Y = 0;

   gtk_databox_set_total_limits(GTK_DATABOX(box), 0,10,-5,5);
   gtk_databox_set_visible_limits(GTK_DATABOX(box), 0,3,-5,5);
	//gtk_databox_auto_rescale(GTK_DATABOX(box), 0.05);	// force repaint
}

static
void load_file_cb()
{
	printf("opening file\n");

	GtkWidget *gw = gtk_file_chooser_dialog_new("Open log file",
		//	GTK_WINDOW(window),
			NULL,
			GTK_FILE_CHOOSER_ACTION_OPEN, 
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GTK_STOCK_OK, GTK_RESPONSE_OK,
			NULL);

	gtk_dialog_run (GTK_DIALOG (gw));

	gchar *fname = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(gw));
	if(!fname) {
		printf("no file chosen\n");
	} else {
		printf("loading '%s'..\n", fname);
		load_file(fname);
	}

	gtk_widget_destroy(gw);
}

static void
create_menu(GtkWidget *parent)
{

    GtkWidget *file_submenu = gtk_menu_new();
	GtkWidget *open_item = gtk_menu_item_new_with_label ("Open");
	g_signal_connect_swapped (open_item, "activate", G_CALLBACK (load_file_cb), 0);
	gtk_menu_shell_append (GTK_MENU_SHELL (file_submenu), open_item);
	GtkWidget *clear_item = gtk_menu_item_new_with_label ("Clear");
	g_signal_connect_swapped (clear_item, "activate", G_CALLBACK (clear_graph_cb), 0);
	gtk_menu_shell_append (GTK_MENU_SHELL (file_submenu), clear_item);
	gtk_menu_shell_append (GTK_MENU_SHELL (file_submenu), gtk_separator_menu_item_new ());
	GtkWidget *quit_item = gtk_menu_item_new_with_label ("Quit");
	g_signal_connect_swapped (quit_item, "activate", G_CALLBACK (gtk_main_quit), 0);
	gtk_menu_shell_append (GTK_MENU_SHELL (file_submenu), quit_item);

    GtkWidget *file_menu = gtk_menu_item_new_with_label ("File");
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (file_menu), file_submenu);

    GtkWidget *help_submenu = gtk_menu_new ();
	GtkWidget *about_item = gtk_menu_item_new_with_label("About");
	gtk_menu_shell_append (GTK_MENU_SHELL (help_submenu), about_item);

	g_signal_connect_swapped (about_item, "activate", G_CALLBACK (gui_about), 0);
	//gtk_widget_show (about_item);

    GtkWidget *help_menu = gtk_menu_item_new_with_label ("Help");
	gtk_menu_item_set_right_justified(GTK_MENU_ITEM(help_menu), 1);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (help_menu), help_submenu);

//    gtk_widget_show (root_menu);

	GtkWidget *menu_bar = gtk_menu_bar_new();
	gtk_menu_shell_append (GTK_MENU_SHELL (menu_bar), file_menu);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu_bar), help_menu);

	gtk_box_pack_start(GTK_BOX(parent), menu_bar, FALSE, FALSE, 0);

	gtk_widget_show(menu_bar);
}

static void
scale_set_value(GtkScale *scale)
{
	gtk_range_set_update_policy (GTK_RANGE (scale), GTK_UPDATE_CONTINUOUS);
    gtk_scale_set_digits (scale, 0);
    gtk_scale_set_value_pos (scale, GTK_POS_TOP);
    gtk_scale_set_draw_value (scale, TRUE);
}


static void
offset_ch1_cb(GtkAdjustment *adj)
{
	int nval = 0xff - adj->value;
	DMSG("ch1 adjusted, 0x%x\n", nval);
	offset_ch1 = nval;
	update_offset();
}

static void
offset_ch2_cb(GtkAdjustment *adj)
{
	int nval = 0xff - adj->value;
	DMSG("ch2 adjusted, 0x%x\n", nval);
	offset_ch2 = nval;
	update_offset();
}


static void
offset_t_cb(GtkAdjustment *adj)
{
	int nval = 0xff - adj->value;
	DMSG("t adjusted, 0x%x\n", nval);
	offset_t = nval;
	update_offset();
}

static void
position_t_cb(GtkAdjustment *adj)
{

}

static GtkWindow *
create_control_window()
{
	GtkWindow *w = gtk_window_new (GTK_WINDOW_TOPLEVEL);

	GtkWidget *box2 = gtk_hbox_new (FALSE, 10);
	GtkWidget *settings_box= gtk_vbox_new(FALSE, 10);

   gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
   gtk_window_set_title (GTK_WINDOW (window), APPNAME);
   gtk_container_add (GTK_CONTAINER (window), box2);

	// buffer size
	set_bsize = gtk_combo_box_new_text();
	int j;
	for(j = 0; j < SCALAR(str_buffer_sizes); j++)
		gtk_combo_box_insert_text(GTK_COMBO_BOX(set_bsize), j, str_buffer_sizes[j]);

	gtk_combo_box_set_active(GTK_COMBO_BOX(set_bsize), 0);
	gtk_box_pack_start (GTK_BOX (settings_box), set_bsize, TRUE, TRUE, 0);

	g_signal_connect (G_OBJECT (set_bsize), "changed", G_CALLBACK (buffer_size_cb), 0);

	// sampling rate
	set_srate = gtk_combo_box_new_text();
	for(j = 0; j < SCALAR(str_sampling_rates); j++)
		gtk_combo_box_insert_text(GTK_COMBO_BOX(set_srate), j, str_sampling_rates[j]);

	gtk_combo_box_set_active(GTK_COMBO_BOX(set_srate), 0);
	gtk_box_pack_start (GTK_BOX (settings_box), set_srate, TRUE, TRUE, 0);

	g_signal_connect (G_OBJECT (set_srate), "changed", G_CALLBACK (sampling_rate_cb), 0);

	// time per window
	time_per_window = gtk_label_new("time/window");
	gtk_box_pack_start(GTK_BOX(settings_box), time_per_window, TRUE, TRUE, 0);

	// trigger frame: slope, source
	GtkWidget *trigger_frame = gtk_frame_new ("Trigger");
	GtkWidget *trigger_vbox = gtk_vbox_new (FALSE, 10);
	gtk_container_add (GTK_CONTAINER(trigger_frame), trigger_vbox);

	GtkWidget *slope = gtk_combo_box_new_text();
	gtk_combo_box_insert_text(GTK_COMBO_BOX(slope), 0, "Slope +");
	gtk_combo_box_insert_text(GTK_COMBO_BOX(slope), 1, "Slope -");
	gtk_combo_box_set_active(GTK_COMBO_BOX(slope), trigger_slope);
	g_signal_connect (G_OBJECT(slope), "changed", G_CALLBACK (trigger_slope_cb), 0);
	gtk_box_pack_start(GTK_BOX(trigger_vbox), slope, TRUE, TRUE, 0);

	GtkWidget *tsource = gtk_combo_box_new_text();
	char *str_tsources[] = {"ch1", "ch2", "alt", "ext", "ext/10"};
	for(int i = 0; i < SCALAR(str_tsources); i++)
		gtk_combo_box_insert_text(GTK_COMBO_BOX(tsource), i, str_tsources[i]);
	gtk_combo_box_set_active(GTK_COMBO_BOX(tsource), trigger_source);
	g_signal_connect (G_OBJECT(tsource), "changed", G_CALLBACK(trigger_source_cb), 0);
	gtk_box_pack_start(GTK_BOX(trigger_vbox), tsource, TRUE, TRUE, 0);

	gtk_box_pack_start(GTK_BOX(settings_box), trigger_frame, TRUE, TRUE, 0);
	
   GtkWidget *control_box = gtk_vbox_new(FALSE, 10);

 //  gtk_container_set_border_width (GTK_CONTAINER (box2), 10);
	gtk_widget_show_all(w);

	GtkWidget *ch1_box = create_channel_box("Channel 1", 0, box);
	GtkWidget *ch2_box = create_channel_box("Channel 2", 1, box);

   gtk_box_pack_start (GTK_BOX (box2), ch1_box, TRUE, TRUE, 0);
   gtk_box_pack_start (GTK_BOX (box2), ch2_box, TRUE, TRUE, 0);
   gtk_box_pack_start (GTK_BOX (box2), settings_box, TRUE, TRUE, 0);
   gtk_box_pack_start (GTK_BOX (box2), control_box, TRUE, TRUE, 0);

   run_button = gtk_toggle_button_new_with_label ("Run");
   g_signal_connect_swapped (GTK_OBJECT (run_button), "toggled",
			     G_CALLBACK (run_clicked),
			     GTK_OBJECT (box));
   gtk_box_pack_start (GTK_BOX (control_box), run_button, TRUE, TRUE, 0);

   GtkWidget *log_button = gtk_check_button_new_with_label("log");
   g_signal_connect_swapped (GTK_OBJECT (log_button), "toggled",
			     G_CALLBACK (log_clicked),
			     GTK_OBJECT (box));
   gtk_box_pack_start (GTK_BOX(control_box), log_button, TRUE, TRUE, 0);

   GTK_WIDGET_SET_FLAGS (run_button, GTK_CAN_DEFAULT);
   gtk_widget_grab_default (run_button);


	return w;
}

static void
create_grid()
{
   GtkWidget *box1;
   GtkWidget *box2;
   GtkWidget *label;
   GtkWidget *table;
   GtkWidget *separator;
   GtkDataboxGraph *graph;
   GdkColor color;

   window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

   GdkPixbuf *icon_pixbuf = gdk_pixbuf_new_from_file("hantek-icon.png", 0);
   gtk_window_set_icon(GTK_WINDOW(window), icon_pixbuf);
	g_object_unref(icon_pixbuf);

   gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
   gtk_widget_set_size_request (window, 500, 500);

   g_signal_connect (GTK_OBJECT (window), "destroy", G_CALLBACK (gtk_main_quit), NULL);

   gtk_window_set_title (GTK_WINDOW (window), APPNAME);
   gtk_container_set_border_width (GTK_CONTAINER (window), 0);

   box1 = gtk_vbox_new (FALSE, 0);
   gtk_container_add (GTK_CONTAINER (window), box1);

   create_menu(box1);

   label = gtk_label_new("HantekGTK");
   gtk_box_pack_start (GTK_BOX (box1), label, FALSE, FALSE, 0);
   separator = gtk_hseparator_new ();
   gtk_box_pack_start (GTK_BOX (box1), separator, FALSE, FALSE, 0);

// channel levels, trigger level
	GtkObject *lev_ch1 = gtk_adjustment_new(offset_ch1, 0.0, 0xff, 1.0, 0x10, 0x30);
	GtkObject *lev_ch2 = gtk_adjustment_new(offset_ch2, 0.0, 0xff, 1.0, 0x10, 0x30);
	GtkObject *lev_t = gtk_adjustment_new(offset_t, 0.0, 0xff, 1.0, 0x10, 0x30);
	GtkObject *pos_t = gtk_adjustment_new(offset_t, 0.0, 0xff, 1.0, 0x10, 0x30);
    GtkWidget *scale_ch1 = gtk_vscale_new(GTK_ADJUSTMENT(lev_ch1));
    GtkWidget *scale_ch2 = gtk_vscale_new(GTK_ADJUSTMENT(lev_ch2));
    GtkWidget *scale_t = gtk_vscale_new(GTK_ADJUSTMENT(lev_t));
    GtkWidget *position_t = gtk_hscale_new(GTK_ADJUSTMENT(pos_t));
    scale_set_value(GTK_SCALE(scale_ch1));
    scale_set_value(GTK_SCALE(scale_ch2));
    scale_set_value(GTK_SCALE(scale_t));
	g_signal_connect(G_OBJECT(lev_ch1), "value_changed", G_CALLBACK(offset_ch1_cb), 0);
	g_signal_connect(G_OBJECT(lev_ch2), "value_changed", G_CALLBACK(offset_ch2_cb), 0);
	g_signal_connect(G_OBJECT(lev_t), "value_changed", G_CALLBACK(offset_t_cb), 0);
	g_signal_connect(G_OBJECT(lev_t), "value_changed", G_CALLBACK(position_t_cb), 0);

   /* Create a GtkDatabox widget along with scrollbars and rulers */
   gtk_databox_create_box_with_scrollbars_and_rulers(&box, &table, TRUE, TRUE, TRUE, TRUE);

   /* Here we start with the first grid */
   color.red = 0;
   color.green = 0;
   color.blue = 65535;

// add the grid
//	float time_step = //get_time_step ();
//	1000000/nr_sampling_rates[sampling_rate_idx];
	graph = gtk_databox_grid_new(10, 32, &color, 1);
	gtk_databox_graph_add(GTK_DATABOX (box), graph);

   color.red = 32768;
   color.green = 32768;
   color.blue = 32768;

	nr_points = 10240;//my_buffer_size;

   I = g_new0 (gfloat, nr_points);
   X = g_new0 (gfloat, nr_points);
   Y = g_new0 (gfloat, nr_points);

	for(int i = 0; i < nr_points; i++) {
		I[i] = X[i] = Y[i] = 0;
	}

   color.red = 0;
   color.green = 65535;
   color.blue = 0;

   graph_ch1 = gtk_databox_lines_new (nr_points, I, X, &color, 1);
   gtk_databox_graph_add (GTK_DATABOX (box), graph_ch1);

   //g_free(X);

   color.red = 65535;
   color.green = 65535;
   color.blue = 0;

   graph_ch2 = gtk_databox_lines_new (nr_points, I, Y, &color, 1);
   gtk_databox_graph_add (GTK_DATABOX (box), graph_ch2);
//   graph = gtk_databox_cross_simple_new (&color, 0);                                            
//   gtk_databox_graph_add (GTK_DATABOX (box), graph);

//   graph = gtk_databox_markers_new (1, &MX, &MY, &color, 15, GTK_DATABOX_MARKERS_TRIANGLE);
//   gtk_databox_graph_add (GTK_DATABOX (box), graph);
//   GtkDataboxMarkers *markers = GTK_DATABOX_MARKERS (graph);
//   gtk_databox_markers_set_position (markers, 0, GTK_DATABOX_MARKERS_W);
//   gtk_databox_markers_set_label (markers, 0, GTK_DATABOX_MARKERS_TEXT_W, "trigger", TRUE);

	GtkDataboxMarkers *markers;
	static gfloat FLOAT_ZERO = 0;
/*
   graph = gtk_databox_markers_new (1, &FLOAT_ZERO, &OFFSET_CH1, &color, 1, GTK_DATABOX_MARKERS_DASHED_LINE);
   gtk_databox_graph_add (GTK_DATABOX (box), graph);
   markers = GTK_DATABOX_MARKERS (graph);
   gtk_databox_markers_set_position (markers, 0, GTK_DATABOX_MARKERS_W);
   gtk_databox_markers_set_label (markers, 0, GTK_DATABOX_MARKERS_TEXT_N, "channel 1", TRUE);

   graph = gtk_databox_markers_new (1, &FLOAT_ZERO, &OFFSET_CH2, &color, 1, GTK_DATABOX_MARKERS_DASHED_LINE);
   gtk_databox_graph_add (GTK_DATABOX (box), graph);
   markers = GTK_DATABOX_MARKERS (graph);
   gtk_databox_markers_set_position (markers, 0, GTK_DATABOX_MARKERS_W);
   gtk_databox_markers_set_label (markers, 0, GTK_DATABOX_MARKERS_TEXT_N, "channel 2", TRUE);*/

   color.red = 0x8080;
   color.green = 0x8080;
   color.blue = 0x8080;
	// trigger line
   graph = gtk_databox_markers_new (1, &FLOAT_ZERO, &OFFSET_T, &color, 1, GTK_DATABOX_MARKERS_DASHED_LINE);
   gtk_databox_graph_add (GTK_DATABOX (box), graph);
   markers = GTK_DATABOX_MARKERS (graph);
   gtk_databox_markers_set_position (markers, 0, GTK_DATABOX_MARKERS_W);
   //gtk_databox_markers_set_label (markers, 0, GTK_DATABOX_MARKERS_TEXT_N, "trigger", TRUE);


   GtkWidget *hbox = gtk_hbox_new (TRUE, 3);
   gtk_box_pack_start (GTK_BOX (box1), hbox, FALSE, TRUE, 0);

   entries = g_new0 (GtkWidget *, SHOW_NUM_ENTRIES);
   entries[SHOW_ACTUAL_X] = show_entry (hbox, "Actual X");
   entries[SHOW_ACTUAL_Y] = show_entry (hbox, "Actual Y");
   entries[SHOW_MARKED_X] = show_entry (hbox, "Marked X");
   entries[SHOW_MARKED_Y] = show_entry (hbox, "Marked Y");
   entries[SHOW_DELTA_X] = show_entry (hbox, "Delta X");
   entries[SHOW_DELTA_Y] = show_entry (hbox, "Delta Y");

   g_signal_connect_swapped (G_OBJECT (box), "motion_notify_event",
			     G_CALLBACK (show_motion_notify_cb), entries);
   g_signal_connect (G_OBJECT (box), "button_press_event",
		     G_CALLBACK (show_button_press_cb), entries);
   g_signal_connect (G_OBJECT (box), "selection-changed",
		     G_CALLBACK (show_changed_cb), entries);

   // scale + graph table (display panel)
   GtkWidget *dp = gtk_hbox_new(FALSE, 0);
   gtk_box_pack_start(GTK_BOX(dp), scale_ch1, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(dp), scale_ch2, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(dp), table, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(dp), scale_t, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(box1), position_t, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(box1), dp, TRUE, TRUE, 0);

   color.red = 0;
   color.green = 0;
   color.blue = 0;
   gtk_widget_modify_bg (box, GTK_STATE_NORMAL, &color);

   separator = gtk_hseparator_new ();
   gtk_box_pack_start (GTK_BOX (box1), separator, FALSE, TRUE, 0);

   create_control_window();
   gtk_widget_show_all (window);
   gdk_window_set_cursor (box->window, gdk_cursor_new (GDK_CROSS));
}

int calData = -3;
//struct offset_ranges offset_ranges;

gint
main (gint argc, char *argv[])
{
	gtk_init (&argc, &argv);

	pipe(p);
	GIOChannel *channel = g_io_channel_unix_new(p[0]);
	g_io_add_watch(channel, G_IO_IN, update_gui_cb, 0);
	g_io_channel_unref(channel);

	dso_init();
	dso_adjust_buffer(nr_buffer_sizes[buffer_size_idx]);

	int fl_noinit = 0;

	if(!fl_noinit) {
		int da;
		dso_get_device_address(&da);

		dso_get_offsets(&offset_ranges);
		for(int i=0; i<2; i++) {
			DMSG("Channel %d\n", i);
			for(int j=0; j<9; j++) {
				DMSG("%x - %x\n", offset_ranges.channel[i][j][0], offset_ranges.channel[i][j][1]);
			}
			DMSG("\n");
		}
		DMSG("trigger: 0x%x - 0x%x\n", offset_ranges.trigger[0], offset_ranges.trigger[1]);

		int cl;
		dso_get_cal_data(&cl);

		dso_set_voltage_and_coupling(voltage_ch[0],voltage_ch[1], coupling_ch[0], coupling_ch[1], trigger_source);
		update_offset();
		//dso_set_offset(offset_ch1, offset_ch2, offset_t);
	}

	update_offset();

	dso_thread_init();

	create_grid ();
	update_time_per_window();

	//gtk_timeout_add(period_usec / 1000, update_gui_cb, (gpointer)box);
	//g_signal_connect(G_OBJECT(), "refresh", G_CALLBACK(update_gui), 0);

	gtk_main ();
	//g_main_loop_run();

	dso_done();

	close(p[0]);
	close(p[1]);

	return 0;
}
