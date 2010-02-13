/*
     _ _       _ _        _                 _       
  __| (_) __ _(_) |_ __ _| |  ___  ___   __| | __ _ 
 / _` | |/ _` | | __/ _` | | / __|/ _ \ / _` |/ _` |
| (_| | | (_| | | || (_| | | \__ \ (_) | (_| | (_| |
 \__,_|_|\__, |_|\__\__,_|_| |___/\___/ \__,_|\__,_|
         |___/                written by Ondra Havel

*/

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <usb.h>

#include "io.h"
#include "local.h"

typedef unsigned char u8;

unsigned int dso_trigger_point = 0;

pthread_mutex_t dso_mutex = PTHREAD_MUTEX_INITIALIZER; 

#define FAIL_HANDLER(r)	{\
	if(r >= 0)\
		return r;\
\
	DMSG("failed\n");\
	return r;\
}

#define CHECK(c)	if(c) {
#define END_CHECK	}
#define END_CHECK_2	}}

void dso_lock()
{
	pthread_mutex_lock(&dso_mutex);
}

void dso_unlock()
{
	pthread_mutex_unlock(&dso_mutex);
}

static struct usb_dev_handle *udh = 0;
static int input_mtu = 512;
static int timeout = 250;
static int max_attempts = 3;

unsigned char *dso_buffer = 0;
unsigned char *my_buffer;
int dso_buffer_dirty = 0;
unsigned int dso_buffer_size = 0;

int dso_initialized = 0;

int dso_adjust_buffer(unsigned int size)
{
	pthread_mutex_lock(&buffer_mutex);
	if(dso_initialized) {
		free(dso_buffer);
		dso_buffer = malloc(size * 2);
	}
	dso_buffer_size = size;
	pthread_mutex_unlock(&buffer_mutex);

	free(my_buffer);
	my_buffer = malloc(size * 2);
	return 0;
}

static
struct usb_device *dso_open_dev()
{
	struct usb_bus *bus, *busses;

	usb_init();
	usb_find_busses();
	usb_find_devices();

	if(!(busses = usb_get_busses()))
		return 0;

	for (bus = busses; bus; bus = bus->next) {
		struct usb_device *dev;

		for (dev = bus->devices; dev; dev = dev->next) {
			if(dev->descriptor.idVendor == DEVICE_VENDOR && dev->descriptor.idProduct == PRODUCT_ID) {
				return dev;
			}
		}
	}
	return 0;
}

static
struct usb_dev_handle *dso_prepare()
{
	int c, i;
	struct usb_dev_handle *res;
	struct usb_device *dev = dso_open_dev();

	if(!dev) {
		DMSG("DSO not found\n");
		return 0;
	}

	dso_lock();
	for (c = 0; c < dev->descriptor.bNumConfigurations; c++) {
		for (i = 0; i < dev->config[c].bNumInterfaces; i++) {
			struct usb_interface *ui = &dev->config[c].interface[i];
			if (ui->num_altsetting < 1)
				continue;

			struct usb_interface_descriptor *id = &ui->altsetting[0];
			if(id->bInterfaceClass == USB_CLASS_VENDOR_SPEC
			&& id->bInterfaceSubClass == 0
			&& id->bInterfaceProtocol == 0
			&& id->bNumEndpoints == 2) {
				if(!(res = usb_open(dev))) {
					DMSG("Failed opening USB device: %s\n", usb_strerror());
					return 0;
				}
				if (usb_claim_interface(res, id->bInterfaceNumber)) {
					DMSG("%s\n", usb_strerror());
					usb_close(res);
					dso_unlock();
					return 0;
				}

				for (i = 0; i < id->bNumEndpoints; i++) {
					struct usb_endpoint_descriptor *ep = &id->endpoint[i];
					switch (ep->bEndpointAddress) {
						case 0x86:  // in
							input_mtu = ep->wMaxPacketSize;
							break;
						case 0x02:	// out
							break;
						default:
							DMSG("unknown endpoint 0x%02x\n", ep->bEndpointAddress);
					}
				}
				dso_unlock();
				return res;
			}
		}
	}
	dso_unlock();
	return 0;
}

int dso_init()
{
	if(!(udh = dso_prepare())) {
		DMSG("Suitable DSO not found");
		return -1;
	}

	DMSG("DSO found\n");
	dso_initialized = 1;
	return 0;
}

void dso_done()
{
	dso_initialized = 0;
	usb_close(udh);
}

static
int dso_write_bulk(void *buffer, int len)
{
	int r = -ETIMEDOUT;
	for(int i = 0; (r == -ETIMEDOUT) && (i < max_attempts); i++)
		r = usb_bulk_write(udh, EP_BULK_OUT | USB_ENDPOINT_OUT, (char*)buffer, len, 1000);

	FAIL_HANDLER(r);
}

static
int dso_read_bulk(void *buffer, int len)
{
	int r = -ETIMEDOUT;
	for(int i = 0; (r == -ETIMEDOUT) && (i < max_attempts); i++)
		r = usb_bulk_read(udh, EP_BULK_IN | USB_ENDPOINT_IN, (char*)buffer, len, 1000);

	FAIL_HANDLER(r);
}

static
int dso_write_control(unsigned char request, void *buffer, int len, int value, int index)
{
	int r = -ETIMEDOUT;
	for(int i = 0; (r == -ETIMEDOUT) && (i < max_attempts); i++)
		r = usb_control_msg(udh, USB_ENDPOINT_OUT | USB_TYPE_VENDOR, request, value, index, (char*)buffer, len, timeout);

	FAIL_HANDLER(r);
}

static
int dso_read_control(unsigned char request, void *buffer, int len, int value, int index)
{
	int r = -ETIMEDOUT;
	for(int i = 0; (r == -ETIMEDOUT) && (i < max_attempts); i++)
		r = usb_control_msg(udh, USB_ENDPOINT_IN | USB_TYPE_VENDOR, request, value, index, (char*)buffer, len, timeout);

	FAIL_HANDLER(r);
}

int dso_begin_command()
{
	int r;
	char c = 0;
	CHECK((r = dso_write_control(C_BEGINCOMMAND, &c, 1, 0, 0)) >= 0);
	r = dso_read_control(C_GETSPEED, &c, 1, 0, 0);
	END_CHECK;
	FAIL_HANDLER(r);
}

int dso_set_filter(int hf_reject)
{
	dso_lock();
	int r;
   	CHECK((r = dso_begin_command()) >= 0);

	unsigned char command[4] = {B_SET_FILTER, 0x0F, 0, 0};
	command[2] = hf_reject ? 0x04 : 0x00;

	r = dso_write_bulk(command, sizeof(command));
	END_CHECK;

	dso_unlock();
	FAIL_HANDLER(r);
}

int dso_set_trigger_sample_rate(int my_speed, int selected_channels, int trigger_source, int trigger_slope, int trigger_position, int buffer_size)
{
	const u8 sampling_speed[][3] = {
	{ 0xa0, 0x00, 0x00 },	// 125MS/s
	{ 0x80, 0x00, 0x00 },	// 100MS/s
	{ 0xc0, 0xff, 0xff },	// 50MS/s
	{ 0xc0, 0xfd, 0xff },	// 25MS/s
	{ 0xc0, 0xf7, 0xff },	// 10MS/s
	{ 0xc0, 0xed, 0xff },	// 5MS/s
	{ 0xc0, 0xd9, 0xff },	// 2.5MS/s
	{ 0xc0, 0x9d, 0xff },	// 1MS/s
	{ 0xc0, 0x39, 0xff },	// 500KS/s
	{ 0xc0, 0x71, 0xfe },	// 250KS/s
	{ 0xc0, 0x1a, 0xfc },	// 100KS/s
	{ 0xc0, 0x3e, 0xf8 },	// 50KS/s
	{ 0xc0, 0x6e, 0xf0 },	// 25KS/s
	{ 0xc0, 0xcc, 0xd8 },	// 10KS/s
	{ 0xc0, 0xc8, 0xaf },	// 5KS/s
	{ 0xc0, 0x00, 0x7d },	// 2.5KS/s
	{ 0x40, 0xed, 0xff },	// 50S/s
	};

	dso_lock();
	int r;
   	CHECK((r = dso_begin_command()) >= 0);

	int sc, ts, bs;
	switch(selected_channels) {
		case SELECT_CH1:
			sc = 0x4;
			break;
		case SELECT_CH2:
			sc = 0x7;
			break;
		case SELECT_CH1CH2:
			sc = 0x6;
			break;
	}

	ts = trigger_slope == SLOPE_PLUS ? 0x0 : 0x8;
	bs = buffer_size == 10240 ? 0x0 : 0x8;	// FIXME: srolling mode, short buffer
	u8 tsrc;
	switch(trigger_source) {
		case TRIGGER_CH1:
		case TRIGGER_ALT:
			tsrc = 0x6;
			break;
		case TRIGGER_CH2:
			tsrc = 0x7;
			break;
		case TRIGGER_EXT:
		case TRIGGER_EXT10:
			tsrc = 0x4;
			break;
	}

	u8 c[11];
	c[0] = B_CONFIGURE;
	c[1] = 0x00;
	c[2] = sampling_speed[my_speed][0] | bs | tsrc;
	c[3] = sc | ts;
	c[4] = sampling_speed[my_speed][1];
	c[5] = sampling_speed[my_speed][2];
	c[6] = (u8)trigger_position;
	c[7] = (u8)(trigger_position >> 8);
	c[8] = c[9] = 0;
	c[10] = 0x7;	// trigger position adjustment

	r = dso_write_bulk(c, sizeof(c));
	END_CHECK;

	dso_unlock();
	FAIL_HANDLER(r);
}

int dso_force_trigger()
{
	int r;
	dso_lock();
	CHECK((r = dso_begin_command()) >= 0);

	unsigned char command[2] = {B_FORCE_TRIGGER, 0};
	r = dso_write_bulk(command, sizeof(command));

	END_CHECK;

	dso_unlock();
	FAIL_HANDLER(r);
}

int dso_capture_start()
{
	int r;
	CHECK((r = dso_begin_command(0)) >= 0);

	unsigned char command[2] = {B_CAPTURE_START, 0};
	r = dso_write_bulk(command, sizeof(command));
	END_CHECK;
		
	dso_unlock();
	FAIL_HANDLER(r);
}

int dso_trigger_enabled()
{
	int r;
	dso_lock();

	CHECK((r = dso_begin_command()) >= 0);

	unsigned char command[2] = {B_TRIGGER_ENABLED, 0};
	r = dso_write_bulk(command, sizeof(command));
	END_CHECK;

	dso_unlock();
	FAIL_HANDLER(r);
}

int dso_get_channel_data(void *buffer, int buffer_size)
{
	int r;
	dso_lock();

	CHECK((r = dso_begin_command()) >= 0);

	unsigned char command[2] = {B_CAPTURE_GET_DATA, 0};
	CHECK((r = dso_write_bulk(command, sizeof(command))) >= 0);

	int nr_packets = 2 * buffer_size / input_mtu;

	for(int i = 0; i < nr_packets; i++)
		if((r = dso_read_bulk(buffer + i*input_mtu, input_mtu)) < 0)
			break;

	END_CHECK_2;

	dso_unlock();
	FAIL_HANDLER(r);
}

int dso_get_capture_state(int *tp)
{
	int r;
	unsigned char temp[input_mtu];

	dso_lock();

	CHECK((r = dso_begin_command()) != 0);

	unsigned char c[2] = {B_CAPTURE_GET_STATE, 0};
	CHECK((r = dso_write_bulk(c, sizeof(c))) >= 0);

	r = dso_read_bulk(temp, input_mtu);

	END_CHECK_2;
	dso_unlock();

	*tp = (temp[1] << 16) | (temp[3] << 8) | temp[2];
	return temp[0];
}

int dso_set_voltage_and_coupling(int voltage_ch1, int voltage_ch2, int coupling_ch1, int coupling_ch2, int trigger)
{
	dso_lock();

	int r;
	CHECK((r = dso_begin_command()) >= 0)

    u8 c0[8] = {B_SET_VOLTAGE, 0x0f, 0, 0, 0, 0, 0, 0};
	int m3_1 = voltage_ch1 % 3;
	int m3_2 = voltage_ch2 % 3;
	c0[2] = (((0xC >> m3_2) & 3) << 2) | ((0xC >> m3_1) & 3);

    CHECK((r = dso_write_bulk(c0, sizeof(c0))) >= 0)

	int step_ch1 = voltage_ch1 / 3;
	int step_ch2 = voltage_ch2 / 3;
	
	static const u8 relays[][2][2] = {
		{{ 0xfb, 0xf7 }, { 0xdf, 0xbf }},	// 10mV
		{{ 0xfb, 0x08 }, { 0xdf, 0x40 }},	// 100mV
		{{ 0x04, 0x08 }, { 0x20, 0x40 }},	// 1V
	};

	u8 c[8];
	c[0] = 0x00;
	c[1] = relays[step_ch1][0][0];
	c[2] = relays[step_ch1][0][1];
	c[3] = coupling_ch1 == COUPLING_AC ? 0x02 : 0xfd;
	c[4] = relays[step_ch2][1][0];
	c[5] = relays[step_ch2][1][1];
	c[6] = coupling_ch2 == COUPLING_AC ? 0x10 : 0xef;
	c[7] = trigger == TRIGGER_EXT ? 0xfe : 0x01;

	r = dso_write_control(C_SETRELAYS, c, sizeof(c), 0, 0);
	dso_unlock();
	END_CHECK_2;

	FAIL_HANDLER(r);
}

int dso_get_offsets(struct offset_ranges *or)
{
	dso_lock();
	int r = dso_read_control(C_COMMAND, or, sizeof(*or), 0x08, 0);
	dso_unlock();

	FAIL_HANDLER(r);
}

int dso_set_offsets(int offset_ch1, int offset_ch2, int offset_t)
{
	unsigned char offset[7] = {0,0,0,0,0,0,0x12};

	offset[1] = offset_ch1 >> 8;
	offset[3] = offset_ch2 >> 8;
	offset[5] = offset_t;

	dso_lock();
	int r = dso_write_control(C_SETOFFSET, offset, sizeof(offset), 0, 0);
	dso_unlock();

	FAIL_HANDLER(r);
}
