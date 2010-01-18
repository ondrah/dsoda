#define _XOPEN_SOURCE
#define _POSIX_SOURCE

#include <unistd.h>
#include <gtk/gtk.h>
#include <gtk/gtkitemfactory.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>

#include <stdio.h>
#include <math.h>

#include "io.h"
#include "thread.h"

#include "local.h"
#include "display.h"

#define APPNAME0	"Digital"
#define APPNAME1	"Soda"
#define APPNAME		APPNAME0 " " APPNAME1
#define ICON_FILE	"dsoda-icon.png"
#define DSODA_URL	"http://dsoda.sf.net"

static int fl_running = 0;


#define COMPUTE_TRIGGER_POSITION(tp)	(tp * 0xFFFE + 0xD7FE * (1 - tp))

static int voltage_ch[2] = { VOLTAGE_5V, VOLTAGE_5V };
static int trigger_slope = SLOPE_PLUS;
static int trigger_source = TRIGGER_CH1;
static int trigger_position;
static int selected_channels = SELECT_CH1CH2;
static int reject_hf = 0;		//< reject high frequencies
static int coupling_ch[2] = { COUPLING_AC, COUPLING_AC };
//static float offset_ch1 = 0xb9, offset_ch2 = 0x89, offset_t = 0x99;
static float offset_ch1 = 0.8, offset_ch2 = 0.4, offset_t = 0.7, position_t = 0.5;
static struct offset_ranges offset_ranges;
static int attenuation_ch[2] = { 1, 1};
int capture_ch[2] = { 1, 1 };
int fl_math = 0;

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

static const char *str_graph_types[] = { "X(t)", "X(y)", "FFT" };
int graph_type = 0;

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

static int buffer_size_idx = 0, sampling_rate_idx = 5;
#define COMPUTE_PERIOD_USEC	(1000000 / nr_sampling_rates[sampling_rate_idx] * nr_buffer_sizes[buffer_size_idx])
volatile unsigned int dso_period_usec;
volatile int dso_fl_single_sample;

static GtkWidget *display_area;
static GtkWidget *box;
static GtkWidget *time_per_window, *set_srate, *set_bsize, *stop_button;

#define VOID_PTR(a)		((void *)a)

static
void start_clicked()
{
	if(!dso_initialized)
		return;

	DMSG("running capture\n");
	fl_running = 1;	// start updating the screen, FIXME

	dso_set_trigger_sample_rate(sampling_rate_idx, selected_channels, trigger_source, trigger_slope, trigger_position, nr_buffer_sizes[buffer_size_idx]);
	dso_set_filter(reject_hf);
	dso_thread_resume();
}

static
void stop_clicked()
{
	if(dso_fl_single_sample)
		return;

	DMSG("stopping capture\n");
	fl_running = 0;

	if(dso_initialized)
		dso_thread_pause();
}

static
void single_clicked(GtkWidget *w)
{
	dso_fl_single_sample = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
	gtk_widget_set_sensitive(stop_button, !dso_fl_single_sample);
}

static void
update_offset()
{
	int ro_ch1, ro_ch2, ro_t;	// real offsets
	ro_ch1 = (offset_ranges.channel[0][voltage_ch[0]][1] - offset_ranges.channel[0][voltage_ch[0]][0]) * offset_ch1 + offset_ranges.channel[0][voltage_ch[0]][0];
	ro_ch2 = (offset_ranges.channel[1][voltage_ch[1]][1] - offset_ranges.channel[1][voltage_ch[1]][0]) * offset_ch2 + offset_ranges.channel[1][voltage_ch[1]][0];
	ro_t = (offset_ranges.trigger[1] - offset_ranges.trigger[0]) * offset_t + offset_ranges.trigger[0];

	//OFFSET_T = ro_t;

	//FIXME repaint

	if(!dso_initialized)
		return;

	dso_set_offset(ro_ch1, ro_ch2, ro_t);
}

static void
attenuation_cb(GtkWidget *w, int ch)
{
	DMSG("ch %d\n", ch);

	int nval = gtk_combo_box_get_active(GTK_COMBO_BOX(w));
	attenuation_ch[ch] = nr_attenuations[nval];
}

static
void capture_cb(GtkWidget *w, int ch)
{
	int nval = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
	capture_ch[ch] = nval;

}

static
void coupling_cb(GtkWidget *w, int ch)
{
	DMSG("ch %d\n", ch);

	int nval = gtk_combo_box_get_active(GTK_COMBO_BOX(w));
	coupling_ch[ch] = nval;

	if(!dso_initialized)
		return;

	dso_set_voltage_and_coupling(voltage_ch[0], voltage_ch[1], coupling_ch[0], coupling_ch[1], trigger_source);
}

static
void voltage_changed_cb(GtkWidget *v, int ch)
{
	DMSG("ch %d\n", ch);

	int nval = gtk_combo_box_get_active(GTK_COMBO_BOX(v));

	voltage_ch[ch] = nval;

	if(!dso_initialized)
		return;

	dso_set_voltage_and_coupling(voltage_ch[0],voltage_ch[1], coupling_ch[0], coupling_ch[1], trigger_source);
	update_offset();
}

static
void trigger_slope_cb(GtkWidget *v, int ch)
{
	trigger_slope = gtk_combo_box_get_active(GTK_COMBO_BOX(v));

	if(!dso_initialized)
		return;

	if(fl_running) {
		dso_set_trigger_sample_rate(sampling_rate_idx, selected_channels, trigger_source, trigger_slope, trigger_position, nr_buffer_sizes[buffer_size_idx]);
	}
}

static
void trigger_source_cb(GtkWidget *v, int ch)
{
	trigger_source = gtk_combo_box_get_active(GTK_COMBO_BOX(v));

	if(!dso_initialized)
		return;

	if(fl_running) {
		dso_set_trigger_sample_rate(sampling_rate_idx, selected_channels, trigger_source, trigger_slope, trigger_position, nr_buffer_sizes[buffer_size_idx]);
	}
}

static
void graph_type_cb(GtkWidget *w, int ch)
{
	graph_type = gtk_combo_box_get_active(GTK_COMBO_BOX(w));
}

static
void math_cb(GtkWidget *w, int ch)
{
	fl_math = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
}

static
GtkWidget *create_channel_box(const char *name, int channel_id, GtkWidget *parent)
{
	GtkWidget *frame = gtk_frame_new(name);

	GtkWidget *gw = gtk_vbox_new (FALSE, 5);
	gtk_container_add(GTK_CONTAINER(frame), gw);

	GtkWidget *c_voltage = gtk_combo_box_new_text();
	for(int i=0;i<SCALAR(str_voltages);i++)
		gtk_combo_box_insert_text(GTK_COMBO_BOX(c_voltage), i, str_voltages[i]);
	gtk_combo_box_set_active(GTK_COMBO_BOX(c_voltage), voltage_ch[channel_id]);

	g_signal_connect(G_OBJECT(c_voltage), "changed", G_CALLBACK(voltage_changed_cb), VOID_PTR(channel_id));

	GtkWidget *c_attenuation = gtk_combo_box_new_text();
	for(int i=0;i<SCALAR(str_attenuations);i++)
		gtk_combo_box_insert_text(GTK_COMBO_BOX(c_attenuation), i, str_attenuations[i]);
	gtk_combo_box_set_active(GTK_COMBO_BOX(c_attenuation), 0);
	g_signal_connect(GTK_OBJECT(c_attenuation), "changed", G_CALLBACK (attenuation_cb), VOID_PTR(channel_id));

	GtkWidget *enabled = gtk_check_button_new_with_label("capture");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enabled), 1);
	g_signal_connect(GTK_OBJECT (enabled), "toggled", G_CALLBACK (capture_cb),
		VOID_PTR(channel_id));

	gtk_box_pack_start (GTK_BOX (gw), enabled, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (gw), c_voltage, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (gw), c_attenuation, FALSE, FALSE, 0);
	
	GtkWidget *c_coupling= gtk_combo_box_new_text();
	gtk_combo_box_insert_text(GTK_COMBO_BOX(c_coupling), 0, "AC");
	gtk_combo_box_insert_text(GTK_COMBO_BOX(c_coupling), 1, "DC");
	gtk_combo_box_set_active(GTK_COMBO_BOX(c_coupling), coupling_ch[channel_id]);
	gtk_box_pack_start (GTK_BOX (gw), c_coupling, TRUE, TRUE, 0);
	g_signal_connect(GTK_OBJECT (c_coupling), "changed", G_CALLBACK (coupling_cb), VOID_PTR(channel_id));

	return frame;
}

static
void update_time_per_window()
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
	dso_adjust_buffer(nr_buffer_sizes[buffer_size_idx]);
	//DMSG("buffer sizes other than 10240 not supported\n");
	if(dso_initialized)
		dso_set_trigger_sample_rate(sampling_rate_idx, selected_channels, trigger_source, trigger_slope, trigger_position, nr_buffer_sizes[buffer_size_idx]);
	update_time_per_window();
}

static void simul_generate();

static
void sampling_rate_cb()
{
	sampling_rate_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(set_srate));
	update_time_per_window();

	dso_period_usec = COMPUTE_PERIOD_USEC;
		DMSG("period = %d\n", dso_period_usec);

	if(!dso_initialized) {
		simul_generate();
		return;
	}

	dso_set_trigger_sample_rate(sampling_rate_idx, selected_channels, trigger_source, trigger_slope, trigger_position, nr_buffer_sizes[buffer_size_idx]);
	//dso_set_filter(reject_hf);
}

static
void gui_about()
{
	GtkWidget *dialog = gtk_about_dialog_new();
	gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dialog), APPNAME);
	gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), "0.1"); 
	gtk_about_dialog_set_copyright(GTK_ABOUT_DIALOG(dialog), "(c) ond");
	gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dialog), APPNAME " is a simple frontend for the \"DSO-2250 USB\".");
	gtk_about_dialog_set_website(GTK_ABOUT_DIALOG(dialog), DSODA_URL);
	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(ICON_FILE, NULL);
	gtk_about_dialog_set_logo(GTK_ABOUT_DIALOG(dialog), pixbuf);
	g_object_unref(pixbuf);
	gtk_dialog_run(GTK_DIALOG (dialog));
	gtk_widget_destroy(dialog);
}


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
}

static
unsigned int find_trigger(unsigned int triggerPoint)
{
    unsigned int var_1 = 1;
	while(var_1 < triggerPoint)
		var_1 <<= 1;
    unsigned int var_2 = 0;
    unsigned int var_C = 0;
    unsigned int var_10 = 0;

    int flag = 1;
    while (var_1 > var_2 + 1) {
        var_C = ((var_1 - var_2 + 1) >> 1) + var_10;
		unsigned int m = (var_1 + var_2)>>1;
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

//	if(!fl_running)
//		return RETVAL;

	if(!dso_buffer_dirty)
		return RETVAL;

	pthread_mutex_lock(&buffer_mutex);
	memcpy(my_buffer, dso_buffer, 2 * dso_buffer_size);
	//memset(dso_buffer, 0, dso_buffer_size * 2);
	dso_buffer_dirty = 0;
	trigger_point = dso_trigger_point;
	pthread_mutex_unlock(&buffer_mutex);

	trigger_point = find_trigger(trigger_point);
	display_refresh(display_area);

	return RETVAL;
#undef RETVAL
}

static
gboolean simul_dso()
{
	trigger_point += 10;
	trigger_point %= 10240;

	if(fl_running)
		display_refresh(display_area);

	return TRUE;
}

static
void simul_generate()
{
#define SPEED_FAC	10*3.1415/10240
#define AX	80
#define AY	80
	int dr = nr_sampling_rates[5], sr = nr_sampling_rates[sampling_rate_idx];
	float fac = (float)dr / sr;
	for(int i = 0; i < my_buffer_size; i++) {
		my_buffer[2 * i + 0] = AX * sin(i * fac / 3 * SPEED_FAC) + 128;
		my_buffer[2 * i + 1] = AY * cos(i * fac / 2 * SPEED_FAC) + 128;
	}
#undef SPEED_FAC
#undef AX
#undef AY
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
void load_file_cb()
{
	//DMSG("opening file\n");

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

static
void save_file_cb()
{
	GtkWidget *gw = gtk_file_chooser_dialog_new("Save current buffer",
		//	GTK_WINDOW(window),
			NULL,
			GTK_FILE_CHOOSER_ACTION_SAVE, 
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GTK_STOCK_OK, GTK_RESPONSE_OK,
			NULL);

	gtk_dialog_run(GTK_DIALOG (gw));

	gchar *fname = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(gw));
	gtk_widget_destroy(gw);

	if(!fname) {
		DMSG("no file chosen\n");
		return;
	}

	DMSG("saving '%s'..\n", fname);

	struct stat st;
	if(!stat(fname, &st)) {
		GtkWidget *yn = gtk_message_dialog_new(0, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK_CANCEL,
				"File %s already exists. Overwrite?", fname);
		int r = gtk_dialog_run(GTK_DIALOG(yn));
		gtk_widget_destroy(yn);

		if(r != GTK_RESPONSE_OK)
			return;
	}

	FILE *f;
	if(!(f = fopen(fname, "w"))) {
		DMSG("failed opening file for writing\n");
		return;
	}
	g_free(fname);

	fprintf(f,
			"# nr_samples = %d\n"
			"# speed = %s\n",
			my_buffer_size,
			str_sampling_rates[sampling_rate_idx]
			);
//	int c1d = offset_ranges[0][voltage_ch[0]][1] - offset_ranges[0][voltage_ch[0]][0];
//	int c2d = offset_ranges[1][voltage_ch[1]][1] - offset_ranges[1][voltage_ch[1]][0];
	for(int i = 0; i < my_buffer_size; i++) {
		fprintf(f, "%8d %f %f\n", i, (my_buffer[2*i + 1] - offset_ch1) / 32.0, (my_buffer[2*i] - offset_ch2) / 32.0);
	}
	fclose(f);
}

static void
create_menu(GtkWidget *parent)
{
    GtkWidget *file_submenu = gtk_menu_new();
	GtkWidget *open_item = gtk_menu_item_new_with_label("Open");
	GtkWidget *save_item = gtk_menu_item_new_with_label("Save");
	g_signal_connect(open_item, "activate", G_CALLBACK(load_file_cb), 0);
	g_signal_connect(save_item, "activate", G_CALLBACK(save_file_cb), 0);
	gtk_menu_shell_append(GTK_MENU_SHELL(file_submenu), open_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(file_submenu), save_item);
	//GtkWidget *clear_item = gtk_menu_item_new_with_label ("Clear");
	//g_signal_connect_swapped (clear_item, "activate", G_CALLBACK (clear_graph_cb), 0);
	//gtk_menu_shell_append (GTK_MENU_SHELL (file_submenu), clear_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(file_submenu), gtk_separator_menu_item_new());
	GtkWidget *quit_item = gtk_menu_item_new_with_label ("Quit");
	g_signal_connect_swapped(quit_item, "activate", G_CALLBACK(gtk_main_quit), 0);
	gtk_menu_shell_append(GTK_MENU_SHELL(file_submenu), quit_item);

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
scale_configure(GtkScale *scale)
{
	gtk_range_set_update_policy (GTK_RANGE (scale), GTK_UPDATE_CONTINUOUS);
    gtk_scale_set_digits (scale, 0);
    //gtk_scale_configure_pos (scale, GTK_POS_TOP);
    gtk_scale_set_draw_value (scale, FALSE);
}

static void
offset_ch1_cb(GtkAdjustment *adj)
{
	offset_ch1 = 1 - adj->value;
	update_offset();
}

static void
offset_ch2_cb(GtkAdjustment *adj)
{
	offset_ch2 = 1 - adj->value;
	update_offset();
}


static void
offset_t_cb(GtkAdjustment *adj)
{
	int nval = 1 - adj->value;
	offset_t = nval;
	update_offset();
}

static void
position_t_cb(GtkAdjustment *adj)
{
	float nval = adj->value;

	position_t = nval;
	trigger_position = COMPUTE_TRIGGER_POSITION(nval);
	DMSG("trigger position adjusted, 0x%x (%f)\n", trigger_position, nval);
	dso_set_trigger_sample_rate(sampling_rate_idx, selected_channels, trigger_source, trigger_slope, trigger_position, nr_buffer_sizes[buffer_size_idx]);
}

static
GtkWidget *create_display_window()
{
   GtkWidget *box1;

   GtkWidget *w = gtk_window_new (GTK_WINDOW_TOPLEVEL);

   GdkPixbuf *icon_pixbuf = gdk_pixbuf_new_from_file(ICON_FILE, 0);
   gtk_window_set_icon(GTK_WINDOW(w), icon_pixbuf);
	g_object_unref(icon_pixbuf);

   gtk_window_set_position(GTK_WINDOW(w), GTK_WIN_POS_CENTER);
   gtk_widget_set_size_request (w, 500, 500);

   g_signal_connect (GTK_OBJECT (w), "destroy", G_CALLBACK (gtk_main_quit), NULL);

   gtk_window_set_title (GTK_WINDOW (w), APPNAME0);
   gtk_container_set_border_width (GTK_CONTAINER (w), 0);

   box1 = gtk_vbox_new(FALSE, 0);
   gtk_container_add (GTK_CONTAINER (w), box1);

   create_menu(box1);

   gtk_box_pack_start (GTK_BOX (box1), gtk_hseparator_new (), FALSE, FALSE, 0);

// channel levels, trigger level, trigger position, display area
	GtkObject *lev_ch1 = gtk_adjustment_new(1-offset_ch1, 0.0, 1.0, 0, 0, 0);
	GtkObject *lev_ch2 = gtk_adjustment_new(1-offset_ch2, 0.0, 1.0, 0, 0, 0);
	GtkObject *lev_t = gtk_adjustment_new(1-offset_t, 0.0, 1.0, 0, 0, 0);
	GtkObject *pos_t = gtk_adjustment_new(position_t, 0.0, 1.0, 0, 0, 0);
    GtkWidget *scale_ch1 = gtk_vscale_new(GTK_ADJUSTMENT(lev_ch1));
    GtkWidget *scale_ch2 = gtk_vscale_new(GTK_ADJUSTMENT(lev_ch2));
    GtkWidget *scale_t = gtk_vscale_new(GTK_ADJUSTMENT(lev_t));
    GtkWidget *position_t = gtk_hscale_new(GTK_ADJUSTMENT(pos_t));
    scale_configure(GTK_SCALE(scale_ch1));
    scale_configure(GTK_SCALE(scale_ch2));
    scale_configure(GTK_SCALE(scale_t));
	scale_configure(GTK_SCALE(position_t));
	g_signal_connect(G_OBJECT(lev_ch1), "value_changed", G_CALLBACK(offset_ch1_cb), 0);
	g_signal_connect(G_OBJECT(lev_ch2), "value_changed", G_CALLBACK(offset_ch2_cb), 0);
	g_signal_connect(G_OBJECT(lev_t), "value_changed", G_CALLBACK(offset_t_cb), 0);
	g_signal_connect(G_OBJECT(pos_t), "value_changed", G_CALLBACK(position_t_cb), 0);

	display_area = display_create_widget();

	// scale + graph table (display panel)
	GtkWidget *dp = gtk_table_new(3, 2, FALSE);
	GtkWidget *hb = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hb), scale_ch1, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hb), scale_ch2, FALSE, FALSE, 0);

	gtk_table_attach(GTK_TABLE(dp), position_t, 1, 2, 0, 1, GTK_FILL, GTK_SHRINK, 0, 0);
	gtk_table_attach(GTK_TABLE(dp), hb, 0, 1, 1, 2, GTK_SHRINK, GTK_FILL, 0, 0);
	gtk_table_attach(GTK_TABLE(dp), display_area, 1, 2, 1, 2, GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);
	gtk_table_attach(GTK_TABLE(dp), scale_t, 2, 3, 1, 2, GTK_SHRINK, GTK_FILL, 0, 0);
//----
//	GtkWidget *dp = gtk_hbox_new(FALSE, 0);
//	gtk_box_pack_start(GTK_BOX(dp), scale_ch1, FALSE, FALSE, 0);
//	gtk_box_pack_start(GTK_BOX(dp), scale_ch2, FALSE, FALSE, 0);
//	gtk_box_pack_start(GTK_BOX(dp), display_area, TRUE, TRUE, 0);
//	gtk_box_pack_start(GTK_BOX(dp), scale_t, FALSE, FALSE, 0);
//	gtk_box_pack_start(GTK_BOX(box1), position_t, FALSE, FALSE, 0);
//	gtk_box_pack_start(GTK_BOX(box1), dp, TRUE, TRUE, 0);
//----
	gtk_box_pack_start(GTK_BOX(box1), dp, TRUE, TRUE, 0);

	gtk_widget_show_all(w);

	gdk_window_set_cursor(display_area->window, gdk_cursor_new(GDK_CROSS));

	return w;
}

static GtkWidget *
create_control_window(int x, int y)
{
	GtkWidget *w = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	g_signal_connect(GTK_OBJECT(w), "destroy", G_CALLBACK(gtk_main_quit), NULL);

	GtkWidget *box2 = gtk_vbox_new(FALSE, 10);

	GtkWidget *settings_box = gtk_vbox_new(FALSE, 10);


	//gtk_window_set_position(GTK_WINDOW(w), GTK_WIN_POS_CENTER);
	gtk_window_set_title (GTK_WINDOW(w), APPNAME1);
	GdkPixbuf *icon_pixbuf = gdk_pixbuf_new_from_file(ICON_FILE, 0);
	gtk_window_set_icon(GTK_WINDOW(w), icon_pixbuf);
	g_object_unref(icon_pixbuf);
	gtk_window_move(GTK_WINDOW(w), x, y);

	gtk_container_add (GTK_CONTAINER(w), box2);

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
	gtk_combo_box_set_active(GTK_COMBO_BOX(set_srate), sampling_rate_idx);
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
	gtk_combo_box_insert_text(GTK_COMBO_BOX(slope), 0, "↗");
	gtk_combo_box_insert_text(GTK_COMBO_BOX(slope), 1, "↘");
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
	GtkWidget *hbox = gtk_hbox_new(FALSE, 5);
	GtkWidget *ch1_box = create_channel_box("Channel 1", 0, box);
	GtkWidget *ch2_box = create_channel_box("Channel 2", 1, box);

	gtk_box_pack_start (GTK_BOX(hbox), ch1_box, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX(hbox), ch2_box, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX(box2), hbox, TRUE, TRUE, 0);

	GtkWidget *c_graph = gtk_combo_box_new_text();
	for(int i=0;i<SCALAR(str_graph_types);i++)
		gtk_combo_box_insert_text(GTK_COMBO_BOX(c_graph), i, str_graph_types[i]);
	gtk_combo_box_set_active(GTK_COMBO_BOX(c_graph), graph_type);
	g_signal_connect(G_OBJECT(c_graph), "changed", G_CALLBACK(graph_type_cb), 0);
	gtk_box_pack_start(GTK_BOX(box2), c_graph, TRUE, TRUE, 0);

	GtkWidget *b_math = gtk_check_button_new_with_label("Math");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b_math), 0);
	g_signal_connect(GTK_OBJECT(b_math), "toggled", G_CALLBACK(math_cb), 0);
	gtk_box_pack_start(GTK_BOX(box2), b_math, TRUE, TRUE, 0);

	gtk_box_pack_start (GTK_BOX (box2), settings_box, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (box2), control_box, TRUE, TRUE, 0);

	GtkWidget *start_button = gtk_button_new_with_label("Start");
	g_signal_connect(GTK_OBJECT(start_button), "pressed", G_CALLBACK(start_clicked), 0);

	stop_button = gtk_button_new_with_label("Stop");
	g_signal_connect(GTK_OBJECT(stop_button), "pressed", G_CALLBACK(stop_clicked), 0);

	GtkWidget *nh = gtk_hbox_new(TRUE, 5);
	gtk_box_pack_start(GTK_BOX(nh), start_button, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(nh), stop_button, TRUE, TRUE, 0);

	GtkWidget *single_button = gtk_check_button_new_with_label("single");
	g_signal_connect(GTK_OBJECT(single_button), "toggled", G_CALLBACK(single_clicked), 0);

	gtk_box_pack_start(GTK_BOX(control_box), nh, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(control_box), single_button, TRUE, TRUE, 0);

	GTK_WIDGET_SET_FLAGS(start_button, GTK_CAN_DEFAULT);
	gtk_widget_grab_default(nh);
	gtk_widget_show_all(w);

	return w;
}

static
GtkWidget *create_math_window()
{
	GtkWidget *w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title (GTK_WINDOW (w), "Math");
	GtkWidget *hbox = gtk_hbox_new(FALSE, 5);

	GtkWidget *c_src[2];
	
	for(int i = 0; i < 2; i++) {
		c_src[i] = gtk_combo_box_new_text();
		gtk_combo_box_insert_text(GTK_COMBO_BOX(c_src[i]), 0, "CH1");
		gtk_combo_box_insert_text(GTK_COMBO_BOX(c_src[i]), 1, "CH2");
		gtk_combo_box_set_active(GTK_COMBO_BOX(c_src[i]), i);
	}

	char *fn[] = {"+", "-", "*", "/"};

	GtkWidget *c_op = gtk_combo_box_new_text();
	for(int i = 0; i < SCALAR(fn); i++)
		gtk_combo_box_insert_text(GTK_COMBO_BOX(c_op), i, fn[i]);
	gtk_combo_box_set_active(GTK_COMBO_BOX(c_op), 0);
	
	
	gtk_box_pack_start(GTK_BOX(hbox), c_src[0], FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), c_op, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), c_src[1], FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(w), hbox);

	gtk_widget_show_all(w);

	return w;
}

static
void create_windows()
{
	GtkWidget *w = create_display_window();

	int rx, ry;
	gtk_window_get_position(GTK_WINDOW(w), &rx, &ry);
	create_control_window(rx + w->allocation.width, ry);

	create_math_window();
}

int calData = -3;
//struct offset_ranges offset_ranges;

gint
main (gint argc, char *argv[])
{
	gtk_init(&argc, &argv);
	display_init(&argc, &argv);

	pipe(p);
	GIOChannel *channel = g_io_channel_unix_new(p[0]);
	g_io_add_watch(channel, G_IO_IN, update_gui_cb, 0);
	g_io_channel_unref(channel);

	dso_init();
	int fl_noinit = 0;

	dso_period_usec = COMPUTE_PERIOD_USEC;

	dso_adjust_buffer(nr_buffer_sizes[buffer_size_idx]);

	if(!fl_noinit && dso_initialized) {
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

		//int cl;
		//dso_get_cal_data(&cl);

		dso_set_voltage_and_coupling(voltage_ch[0],voltage_ch[1], coupling_ch[0], coupling_ch[1], trigger_source);
		update_offset();
		//dso_set_offset(offset_ch1, offset_ch2, offset_t);
	}

	update_offset();
	
	if(dso_initialized)
		dso_thread_init();

	create_windows();

	update_time_per_window();

	trigger_position = COMPUTE_TRIGGER_POSITION(position_t);
	if(!dso_initialized) {
		simul_generate();
		g_timeout_add(1000 / 30, simul_dso, 0);
	}

	gtk_main ();
	//g_main_loop_run();

	if(dso_initialized)
		dso_done();

	close(p[0]);
	close(p[1]);

	return 0;
}
