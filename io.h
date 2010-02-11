/*
     _ _       _ _        _                 _       
  __| (_) __ _(_) |_ __ _| |  ___  ___   __| | __ _ 
 / _` | |/ _` | | __/ _` | | / __|/ _ \ / _` |/ _` |
| (_| | | (_| | | || (_| | | \__ \ (_) | (_| | (_| |
 \__,_|_|\__, |_|\__\__,_|_| |___/\___/ \__,_|\__,_|
         |___/                written by Ondra Havel

*/

#ifndef DSODA_IO_H
#define DSODA_IO_H

#include <pthread.h>

#define DEVICE_VENDOR 0x04b5
#define EP_BULK_OUT 2
#define EP_BULK_IN  6

#define	B_SET_FILTER	0x00
#define B_CONFIGURE		0x01
#define	B_FORCE_TRIGGER	0x02
#define B_CAPTURE_START	0x03
#define B_TRIGGER_ENABLED	0x04
#define B_CAPTURE_GET_DATA	0x05
#define B_CAPTURE_GET_STATE	0x06
#define B_SET_VOLTAGE	0x07

#define C_COMMAND	0xA2
#define C_GETSPEED	0xB2
#define C_BEGINCOMMAND	0xB3
#define C_SETOFFSET	0xB4
#define C_SETRELAYS	0xB5

#define COUPLING_AC	0
#define COUPLING_DC	1

enum {
    SELECT_CH1 = 0,
    SELECT_CH2,
    SELECT_CH1CH2
};

enum {
    TRIGGER_CH1 = 0,
    TRIGGER_CH2,
    TRIGGER_ALT,
    TRIGGER_EXT,
    TRIGGER_EXT10
};

enum {
	VOLTAGE_10mV=0,		// a
	VOLTAGE_20mV,		// a
	VOLTAGE_50mV,		// a
	VOLTAGE_100mV,		// b
	VOLTAGE_200mV,		// b
	VOLTAGE_500mV,		// b
	VOLTAGE_1V,			// c
	VOLTAGE_2V,			// c
	VOLTAGE_5V,			// c
};

#define	SLOPE_PLUS	0
#define	SLOPE_MINUS 1

struct offset_ranges {
	unsigned short channel[2][9][2];		// [channel][voltage][low; high]
	unsigned char trigger[2];	// [low; high]
} __attribute__((__packed__));


extern unsigned char *dso_buffer;
extern unsigned char *my_buffer;
extern unsigned int trigger_point;
extern unsigned int dso_buffer_size;
#define my_buffer_size dso_buffer_size
extern int dso_buffer_dirty;
extern unsigned int dso_trigger_point;
extern int fl_gui_running;
extern int capture_ch[2];
extern int fl_math;
extern int graph_type;

int dso_get_capture_state();
int dso_capture_start();
int dso_get_channel_data(void *buffer, int bufferSize);
int dso_init();
void dso_done();
int dso_adjust_buffer(unsigned int size);
void dso_lock();
void dso_unlock();
int dso_set_voltage_and_coupling(int ch1Voltage, int ch2Voltage, int ch1Coupling, int ch2Coupling, int triggerSource);
int dso_get_offsets(struct offset_ranges *or);
int dso_set_filter(int hf_reject);
int dso_set_trigger_sample_rate(int my_speed, int selectedChannel, int triggerSource, int triggerSlope, int triggerPosition, int bufferSize);
int dso_begin_command();
int dso_get_device_address(int *deviceAddress);
int dso_set_offset(int ch1Offset, int ch2Offset, int extOffset);
int dso_get_capture_state(int *tp);
int dso_trigger_enabled();
int dso_force_trigger();

void dso_update_gui();

extern pthread_mutex_t buffer_mutex;
extern volatile unsigned int dso_period_usec;
extern volatile int dso_trigger_mode;
extern int dso_initialized;

enum {
	TRIGGER_AUTO = 0,
	TRIGGER_NORMAL,
	TRIGGER_SINGLE,
};

unsigned int gui_get_sampling_rate();
float get_channel_voltage(int ch);
float get_channel_offset(int ch);

typedef void (*cb_fn)();

void dso_thread_set_cb(cb_fn cb);

extern float offset_ch[3], offset_t, position_t;

#endif
