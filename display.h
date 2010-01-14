#ifndef HANTEK_DISPLAY_H
#define HANTEK_DISPLAY_H

#include <gtk/gtk.h>

int display_init(int *argc, char ***argv);
void display_done();
GtkWidget *display_create_widget();
void display_refresh(GtkWidget *da);

#endif
