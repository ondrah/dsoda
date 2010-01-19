#include <unistd.h>
#include <string.h>
#include "thread.h"
#include "io.h"
#include "local.h"

static int fl_terminate = 0;
static int fl_running = 0;

pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t thread_mutex = PTHREAD_MUTEX_INITIALIZER;

static
void *dso_thread(void *ptr)
{
	DMSG("DSO thread started\n");
    while(!fl_terminate) {
		if(!fl_running) {
			pthread_mutex_lock(&thread_mutex);	// wait on signal
			pthread_mutex_unlock(&thread_mutex);
			if(fl_terminate)
				return 0;
		}

		if(dso_trigger_mode == TRIGGER_SINGLE) {
			dso_thread_pause();
		}

		//DMSG("period = %d\n", dso_period_usec);

		dso_capture_start();
		usleep(dso_period_usec);
		dso_trigger_enabled();

		int fl_complete = 0;
        int trPoint = 0;
		int nr_empty = 0;

		while(!fl_complete) {
			int cs = dso_get_capture_state(&trPoint);
			if (cs < 0) {
				DMSG("dso_get_capture_state io error\n");
				continue;
			}

			switch(cs) {
				case 0:	// empty
					if(nr_empty == 3) {
						dso_capture_start();
						nr_empty = 0;
					}
					nr_empty++;
					dso_trigger_enabled();
					if(dso_trigger_mode != TRIGGER_NORMAL)	// force trigger for single and auto
						dso_force_trigger();
					usleep(dso_period_usec);
					break;

				case 1: // in progress
					usleep(dso_period_usec >> 1);
					break;

				case 2: // full
					pthread_mutex_lock(&buffer_mutex);
					if (dso_get_channel_data(dso_buffer, dso_buffer_size) < 0) {
						DMSG("Error in command GetChannelData\n");
					}
					dso_buffer_dirty = 1;
					dso_trigger_point = trPoint;
					pthread_mutex_unlock(&buffer_mutex);

					dso_update_gui();
					fl_complete = 1;
					break;

				default:
					DMSG("unknown capture state %i\n", cs);
					break;
			}
		}
    }
	return 0;
}

void dso_thread_terminate()
{
	if(!fl_running)
		dso_thread_resume();
    fl_terminate = 1;
}

#include <pthread.h>

pthread_t my_thread;

void dso_thread_init()
{
	if(dso_buffer)
		memset(dso_buffer, 0, sizeof(dso_buffer));

	pthread_mutex_lock(&thread_mutex);

	pthread_create(&my_thread, 0, &dso_thread, 0);
}

// called from a different thread
void dso_thread_resume()
{
	fl_running = 1;
	pthread_mutex_unlock(&thread_mutex);
}

// called from a different thread
void dso_thread_pause()
{
	pthread_mutex_lock(&thread_mutex);
	fl_running = 0;
}
