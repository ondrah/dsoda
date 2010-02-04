/*
     _ _       _ _        _                 _       
  __| (_) __ _(_) |_ __ _| |  ___  ___   __| | __ _ 
 / _` | |/ _` | | __/ _` | | / __|/ _ \ / _` |/ _` |
| (_| | | (_| | | || (_| | | \__ \ (_) | (_| | (_| |
 \__,_|_|\__, |_|\__\__,_|_| |___/\___/ \__,_|\__,_|
         |___/                written by Ondra Havel

*/

#ifndef DSODA_DISPLAY_H
#define DSODA_DISPLAY_H

#include <gtk/gtk.h>

int display_init(int *argc, char ***argv);
void display_done();
GtkWidget *display_create_widget(GtkWidget *parent);
void display_refresh(GtkWidget *da);

#endif
