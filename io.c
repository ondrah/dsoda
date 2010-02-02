#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <usb.h>

#include "io.h"
#include "local.h"

typedef unsigned char u8;

unsigned int dso_trigger_point = 0;

pthread_mutex_t dso_mutex = PTHREAD_MUTEX_INITIALIZER; 

void dso_lock()
{
	pthread_mutex_lock(&dso_mutex);
}

void dso_unlock()
{
	pthread_mutex_unlock(&dso_mutex);
}

static struct usb_dev_handle *udh = 0;
static int epOutMaxPacketLen, epInMaxPacketLen;
static int timeout = 250, attempts = 3;

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
			if(dev->descriptor.idVendor == DEVICE_VENDOR && dev->descriptor.idProduct == 0x2250) {
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
					if (usb_close(res)) {
						DMSG("Can't close USB handle");
					}

					dso_unlock();
					return 0;
				}

				for (i = 0; i < id->bNumEndpoints; i++) {
					struct usb_endpoint_descriptor *ep = &id->endpoint[i];
					switch (ep->bEndpointAddress) {
						case 0x02:  // EP OUT
							epOutMaxPacketLen = ep->wMaxPacketSize;
							DMSG("EP OUT MaxPacketLen = %i\n", epOutMaxPacketLen);
							break;
						case 0x86:  // EP IN
							epInMaxPacketLen = ep->wMaxPacketSize;
							DMSG("EP IN MaxPacketLen = %i\n", epInMaxPacketLen);
							break;
						default:
							DMSG("Unknown endpoint #%02X\n", ep->bEndpointAddress);
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
    if (!(udh = dso_prepare()))
    {
        DMSG("Can't find USB interface (Class:0xFF, SubClass:0, Protocol:0) with two endpoints");
		return -1;
    }

	DMSG("properly initialized\n");
	dso_initialized = 1;
    return 0;
}

void dso_done()
{
	dso_initialized = 0;
	usb_close(udh);
}

int dso_write_bulk(void *buffer, int len)
{
    int i, rv = -ETIMEDOUT;
    for(i = 0; (rv == -ETIMEDOUT) && (i < attempts); i++) {
		rv = usb_bulk_write(udh, EP_BULK_OUT | USB_ENDPOINT_OUT, (char*)buffer, len, 1000);
    }

    if(rv >= 0)
		return rv;

	DMSG("Usb write bulk returns error %i\n", rv);
	DMSG("Error: %s", usb_strerror());
	return rv;
}

int dso_read_bulk(void *buffer, int len)
{
    int i, rv = -ETIMEDOUT;
    for(i = 0; (rv == -ETIMEDOUT) && (i < attempts); i++)
    {
        rv = usb_bulk_read(udh, EP_BULK_IN | USB_ENDPOINT_IN, (char*)buffer, len, 1000);
    }

	if(rv >= 0)
		return rv;

	DMSG("Usb read bulk returns error %i\n", rv);
	DMSG("Error: %s", usb_strerror());
	return rv;
}

int dso_write_control(unsigned char request, void *buffer, int len, int value, int index)
{
    int i, rv = -ETIMEDOUT;
    for(i = 0; (rv == -ETIMEDOUT) && (i < attempts); i++) {
        rv = usb_control_msg(udh, USB_ENDPOINT_OUT | USB_TYPE_VENDOR, request, value, index, (char*)buffer, len, timeout);
    }

	if(rv >= 0)
		return rv;

	DMSG("Usb write control message %02X returns error %i\n", request, rv);
	DMSG("Error: %s\n", usb_strerror());
	return rv;
}

int dso_read_control(unsigned char request, void *buffer, int len, int value, int index)
{
    int i, rv = -ETIMEDOUT;
    for(i = 0; (rv == -ETIMEDOUT) && (i < attempts); i++) {
        rv = usb_control_msg(udh, USB_ENDPOINT_IN | USB_TYPE_VENDOR,
				request, value, index, (char*)buffer, len, timeout);
    }

	if(rv>=0)
		return rv;

	DMSG("Usb read control message %02X returns error %i\n", request, rv);
	DMSG("Error: %s\n", usb_strerror());
	return rv;
}

int dso_begin_command()
{
	int ret;
    //unsigned char c[10] = { 0x0F, 0x03, 0x03, 0x03, 0xec, 0xfc, 0xf4, 0x00, 0xec, 0xfc};
    unsigned char c[10] = { 0x0F, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    if((ret = dso_write_control(C_BEGINCOMMAND, c, sizeof(c), 0, 0)) < 0) {
		DMSG("dso_write_control failed\n");
		return ret;
	}

    if((ret = dso_read_control(C_GETSPEED, c, sizeof(c), 0, 0)) < 0) {
		DMSG("dso_read_control failed\n");
		return ret;
    }

	return 0;
}

/*!
    \fn HantekDSOIO::dsoSetFilter(int channel1, int channel2, int trigger)
 */
int dso_set_filter(int hf_reject)
{
	dso_lock();
    int rv = dso_begin_command();
    if (rv < 0) {
		dso_unlock();
        return rv;
    }

    unsigned char command[8] = {B_SET_FILTER, 0x0F, 0, 0, 0, 0, 0, 0};
    command[2] = hf_reject ? 0x04 : 0x00;

    rv = dso_write_bulk(command, sizeof(command));
    if (rv < 0) {
        DMSG("In function %s", __FUNCTION__);
		dso_unlock();
        return rv;
    }

	dso_unlock();
	DMSG("ok\n");
    return 0;
}


int dso_set_trigger_sample_rate(int my_speed, int selectedChannel, int triggerSource, int triggerSlope, int triggerPosition, int bufferSize)
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
    int rv = dso_begin_command();
    if (rv < 0) {
        dso_unlock();
        return rv;
    }

	int sc, ts, bs;
	switch(selectedChannel) {
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

	ts = triggerSlope == SLOPE_PLUS ? 0x0 : 0x8;
	bs = bufferSize == 10240 ? 0x06 : 0x0A;		// short buffer 0x02

    u8 c[12];
	c[0] = B_CONFIGURE;
	c[1] = 0x00;
	c[2] = sampling_speed[my_speed][0] | bs;
	c[3] = sc | ts;
	c[4] = sampling_speed[my_speed][1];
	c[5] = sampling_speed[my_speed][2];
    c[6] = (u8)triggerPosition;
    c[7] = (u8)(triggerPosition >> 8);
	c[8] = 0xcd;
	c[9] = 0xcd;
	c[10] = 0x07;	// buffer 10k (?)
	c[11] = 0xcd;

	//cdcd07cd
 //   c[10] = (u8)(triggerPosition >> 16);

    rv = dso_write_bulk(c, sizeof(c));
    if (rv < 0) {
        dso_unlock();
        return rv;
    }

    dso_unlock();
    return 0;
}


/*!
    \fn HantekDSOIO::dsoForceTrigger()
 */
int dso_force_trigger()
{
	dso_lock();

    if(dso_begin_command() < 0) {
        dso_unlock();
		return -1;
    }

    unsigned char command[2] = {B_FORCE_TRIGGER, 0};
    if(dso_write_bulk(command, sizeof(command)) < 0) {
        DMSG("In function %s", __FUNCTION__);
		dso_unlock();
		return -1;
    }

	dso_unlock();
    return 0;
}

int dso_capture_start()
{
    if (dso_begin_command(0) < 0)
    {
		dso_unlock();
		return -1;
    }

    unsigned char command[2] = {B_CAPTURE_START, 0};
    if (dso_write_bulk(command, sizeof(command)) < 0) {
		dso_unlock();
		return -1;
    }

	dso_unlock();

	//DMSG("capture started\n");
    return 0;
}


/*!
    \fn HantekDSOIO::dsoTriggerEnabled()
 */
int dso_trigger_enabled()
{
    dso_lock();

    int rv = dso_begin_command();
    if (rv < 0)
    {
        dso_unlock();
        return rv;
    }

    unsigned char command[2] = {B_TRIGGER_ENABLED, 0};
    rv = dso_write_bulk(command, sizeof(command));
    if (rv < 0)
    {
        DMSG("In function %s", __FUNCTION__);
        dso_unlock();
        return rv;
    }

    dso_unlock();
    return 0;
}


/*!
    \fn HantekDSOIO::dsoGetChannelData(void *buffer)
 */
int dso_get_channel_data(void *buffer, int bufferSize)
{
    dso_lock();

    int rv = dso_begin_command();
    if (rv < 0) {
        dso_unlock();
        return rv;
    }

    unsigned char command[2] = {B_CAPTURE_GET_DATA, 0};
    rv = dso_write_bulk(command, sizeof(command));
    if (rv < 0) {
		DMSG("write error\n");
        dso_unlock();
        return rv;
    }

//    rv = dso_get_connection_speed();
//    if (rv < 0) {
//        dso_unlock();
//        return rv;
//    }

    int packets = 2 * bufferSize / epInMaxPacketLen;
//    DMSG("Getting %i packets (%i bytes length), buffer len = %i bytes", packets, epInMaxPacketLen, bufferSize);

    for(int i=0; i<packets; i++) {
        rv = dso_read_bulk(buffer + i*epInMaxPacketLen, epInMaxPacketLen);
		//DMSG("rv = %d\n", rv);
        if (rv < 0) {
            DMSG("read failed\n");
            dso_unlock();
            return rv;
        }
    }

    dso_unlock();
    return 0;
}

int dso_get_capture_state(int *tp)
{
	dso_lock();

    if (dso_begin_command()) {
		dso_unlock();
        return -1;
    }

    unsigned char command[2] = {B_CAPTURE_GET_STATE, 0};
    if (dso_write_bulk(command, sizeof(command)) < 0) {
		dso_unlock();
		return -1;
    }

	/*
	rv = dso_get_connection_speed();
    if (rv < 0) {
        dso_unlock();
        return rv;
    }*/

    unsigned char temp[epInMaxPacketLen];

	int len;
    if ((len = dso_read_bulk(temp, epInMaxPacketLen)) < 0) {
        DMSG("dso_read_bulk failed: %s", usb_strerror());
        dso_unlock();
		return len;
    }
	
	dso_unlock();

	*tp = (temp[1] << 16) | (temp[3] << 8) | temp[2];
    return temp[0];
}

int dso_set_voltage_and_coupling(int voltage_ch1, int voltage_ch2, int coupling_ch1, int coupling_ch2, int trigger)
{
	const u8 relays[][2][2] = {
		{{ 0xfb, 0xf7 }, { 0xdf, 0xbf }},	// 10mV
		{{ 0xfb, 0x08 }, { 0xdf, 0x40 }},	// 100mV
		{{ 0x04, 0x08 }, { 0x20, 0x40 }},	// 1V
	};

	dso_lock();
    int rv = dso_begin_command();
    if (rv < 0) {
        dso_unlock();
        return rv;
    }

    u8 c0[8] = {B_SET_VOLTAGE, 0x0f, 0, 0, 0, 0, 0, 0};
	int m3_1 = voltage_ch1 % 3;
	int m3_2 = voltage_ch2 % 3;
	c0[2] = (((0xC >> m3_2) & 3) << 2) | ((0xC >> m3_1) & 3);

    rv = dso_write_bulk(c0, sizeof(c0));
    if (rv < 0) {
        dso_unlock();
        return rv;
    }

	int step_ch1 = voltage_ch1 / 3;
	int step_ch2 = voltage_ch2 / 3;
	
    u8 c[17];
	c[0] = 0x00;
	c[1] = relays[step_ch1][0][0];
	c[2] = relays[step_ch1][0][1];
	c[3] = coupling_ch1 == COUPLING_AC ? 0x02 : 0xfd;
	c[4] = relays[step_ch2][1][0];
	c[5] = relays[step_ch2][1][1];
	c[6] = coupling_ch1 == COUPLING_AC ? 0x10 : 0xef;
	c[7] = trigger == TRIGGER_EXT ? 0xfe : 0x01;
	c[8] = c[9] = c[10] = c[11] = c[12] = c[13] = c[14] = c[15] = c[16] = 0;

    if(dso_write_control(C_SETRELAYS, c, sizeof(c), 0, 0) < 0) {
		dso_unlock();
		return -1;
    }

	dso_unlock();
    return 0;
}

int dso_get_offsets(struct offset_ranges *or)
{
    dso_lock();

    int rv = dso_read_control(C_COMMAND, or, sizeof(*or), VALUE_CHANNELLEVEL, 0);

    if (rv < 0) {
        dso_unlock();
        return rv;
    }

    dso_unlock();
    return 0;
}

int dso_set_offset(int ch1Offset, int ch2Offset, int extOffset)
{
    unsigned char offset[17] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

    //offset[0] = 0x20;
    offset[1] = ch1Offset >> 8;
    //offset[2] = 0x30;
    offset[3] = ch2Offset >> 8;
    //offset[4] = 0x20;
    offset[5] = extOffset;

    dso_lock();
    int rv = dso_write_control(C_SETOFFSET, offset, sizeof(offset), 0, 0);
	dso_unlock();

    if (rv >= 0)
		return rv;

	DMSG("dso_set_offset failed\n");
	return rv;
}
