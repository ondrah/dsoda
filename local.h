#ifndef DSODA_LOCAL_H
#define DSODA_LOCAL_H

#include <stdio.h>

#define DMSG(x...)	{ fprintf(stderr, "%s: ", __FUNCTION__); fprintf(stderr, x); }

#endif
