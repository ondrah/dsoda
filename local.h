/*
     _ _       _ _        _                 _       
  __| (_) __ _(_) |_ __ _| |  ___  ___   __| | __ _ 
 / _` | |/ _` | | __/ _` | | / __|/ _ \ / _` |/ _` |
| (_| | | (_| | | || (_| | | \__ \ (_) | (_| | (_| |
 \__,_|_|\__, |_|\__\__,_|_| |___/\___/ \__,_|\__,_|
         |___/                written by Ondra Havel

*/

#ifndef DSODA_LOCAL_H
#define DSODA_LOCAL_H

#include <stdio.h>
#include "config.h"

#define DMSG(x...)	{ fprintf(stderr, "%s: ", __FUNCTION__); fprintf(stderr, x); }

#endif
