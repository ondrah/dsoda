#ifndef HANTEK_IO_H
#define HANTEK_IO_H

#include <pthread.h>

#define EP_BULK_OUT 2   // Endpoint for sending commands to DSO
#define EP_BULK_IN  6   // Endpoint for reading data from DSO

enum dso_commands
{
    cmdSetFilter = 0,
    cmdSetTriggerAndSampleRate,
    cmdForceTrigger,
    cmdCaptureStart,
    cmdTriggerEnabled,
    cmdGetChannelData,
    cmdGetCaptureState,
    cmdSetVoltageAndCoupling,
    cmdSetLogicalData,
    cmdGetLogicalData,
    cmdLast
};

#define CONTROL_COMMAND	0xA2
#define CONTROL_GETSPEED	0xB2
#define CONTROL_BEGINCOMMAND	0xB3
#define CONTROL_SETOFFSET	0xB4
#define CONTROL_SETRELAYS	0xB5

enum control_values
{
    VALUE_CHANNELLEVEL = 0x08,
    VALUE_DEVICEADDRESS = 0x0A,
    VALUE_CALDATA = 0x60
};

#define COUPLING_AC	0
#define COUPLING_DC	1

enum selected_channels
{
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

enum level_offsets
{
    OFFSET_START = 0,
    OFFSET_END
};

struct filter_bits
{
    unsigned char channel1:1;
    unsigned char channel2:1;
    unsigned char trigger:1;
    unsigned char reserved:5;
};

union filter_byte
{
    struct filter_bits bits;
    unsigned char byte;
};

struct voltage_bits
{
    unsigned char channel1:2;
    unsigned char channel2:2;
    unsigned char constant:4;
};

union voltage_byte
{
    struct voltage_bits bits;
    unsigned char byte;
};

struct tsr_bits1
{
    unsigned char triggerSource:2;
    unsigned char sampleSize:3;
    unsigned char timeBaseFast:3;
};

union tsr_byte1
{
    struct tsr_bits1 bits;
    unsigned char byte;
};

struct tsr_bits2
{
    unsigned char selectedChannel:2;
    unsigned char fastRatesChannel:1;
    unsigned char triggerSlope:1;
    unsigned char reserved:4;
};

union tsr_byte2
{
    struct tsr_bits2 bits;
    unsigned char byte;
};

struct offset_ranges {
	unsigned short channel[2][9][2];		// [channel][voltage][low; high]
	unsigned char trigger[2];	// [low; high]
} __attribute__((__packed__));

#define DEVICE_VENDOR 0x04b5

extern unsigned char *dso_buffer;
extern unsigned char *my_buffer;
extern unsigned int trigger_point;
extern unsigned int dso_buffer_size;
#define my_buffer_size dso_buffer_size
extern int dso_buffer_dirty;
extern unsigned int dso_trigger_point;
extern int fl_gui_running;
extern int capture_ch[2];

int dso_get_capture_state();
int dso_capture_start();
int dso_get_channel_data(void *buffer, int bufferSize);
int dso_init();
void dso_done();
int dso_adjust_buffer(unsigned int size);
void dso_lock();
void dso_unlock();
int dso_set_voltage_and_coupling(int ch1Voltage, int ch2Voltage, int ch1Coupling, int ch2Coupling, int triggerSource);
int dso_get_cal_data(int *calData);
int dso_get_offsets(struct offset_ranges *or);
int dso_set_filter(int hf_reject);
int dso_set_trigger_sample_rate(int my_speed, int selectedChannel, int triggerSource, int triggerSlope, int triggerPosition, int bufferSize);
int dso_begin_command();
int dso_get_device_address(int *deviceAddress);
int dso_set_offset(int ch1Offset, int ch2Offset, int extOffset);
int dso_get_capture_state(int *tp);

extern pthread_mutex_t buffer_mutex;
extern volatile unsigned int dso_period_usec;

#endif
