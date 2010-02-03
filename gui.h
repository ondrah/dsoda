#ifndef HANTEK_GUI_H
#define HANTEK_GUI_H

//gint dso_update_gui();
void gui_channel_set_color(unsigned int channel_id, int red, int green, int blue);

enum { M_ADD = 0, M_SUB, M_MUL };

extern char math_source[];
extern char math_op;

enum { CH_1 = 0, CH_2, CH_M };

extern float nr_voltages[];
extern int voltage_ch[3];

void gui_update_buffer_size(unsigned int buffer_size);

#endif
