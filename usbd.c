/* -*- mode: c; mode: folding; tab-width: 8 -*-
 */

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/usb/audio.h>
#include <libopencm3/usb/usbd.h>

#include "common.h"

/* {{{ */

static const char * const usb_strings[] = {
	"Acme Corp",
	"F4UAC"
};

static const struct usb_device_descriptor dev = {
	.bLength = USB_DT_DEVICE_SIZE,
	.bDescriptorType = USB_DT_DEVICE,
	.bcdUSB = 0x0200,
	.bDeviceClass = 0,
	.bDeviceSubClass = 0,
	.bDeviceProtocol = 0,
	.bMaxPacketSize0 = 64,
	.idVendor = 0x6666,
	.idProduct = 0x2701,
	.bcdDevice = 0x0200,
	.iManufacturer = 1,
	.iProduct = 2,
	.iSerialNumber = 0,
	.bNumConfigurations = 1,
};

#define MIN_PACKET_SIZE 64
#define ISO_PACKET_SIZE 384
#define ISO_SYNC_PACKET_SIZE 3
#define ISO_OUT_ENDP_ADDR 0x01
#define ISO_IN_ENDP_ADDR 0x81

static const struct {
	struct usb_config_descriptor cdesc;

	struct usb_interface_descriptor audio_control_iface;
	struct usb_audio_header_descriptor_head header_head;
	struct usb_audio_header_descriptor_body header_body;
	struct usb_audio_input_terminal_descriptor input_terminal_desc;
	struct usb_audio_feature_unit_descriptor_2ch feature_unit_desc;
	struct usb_audio_output_terminal_descriptor output_terminal_desc;

	struct usb_interface_descriptor audio_streaming_iface_0;

	struct usb_interface_descriptor audio_streaming_iface_1;
	struct usb_audio_stream_audio_endpoint_descriptor audio_streaming_cs_ep_desc_1;
	struct usb_audio_stream_interface_descriptor audio_cs_streaming_iface_desc_1;
	struct usb_audio_format_type1_descriptor_1freq audio_type1_format_desc_1;
	struct usb_audio_stream_endpoint_descriptor isochronous_ep_1;
	struct usb_audio_stream_endpoint_descriptor synch_ep_1;

	struct usb_interface_descriptor audio_streaming_iface_2;
	struct usb_audio_stream_audio_endpoint_descriptor audio_streaming_cs_ep_desc_2;
	struct usb_audio_stream_interface_descriptor audio_cs_streaming_iface_desc_2;
	struct usb_audio_format_type1_descriptor_1freq audio_type1_format_desc_2;
	struct usb_audio_stream_endpoint_descriptor isochronous_ep_2;
	struct usb_audio_stream_endpoint_descriptor synch_ep_2;

	struct usb_interface_descriptor audio_streaming_iface_3;
	struct usb_audio_stream_audio_endpoint_descriptor audio_streaming_cs_ep_desc_3;
	struct usb_audio_stream_interface_descriptor audio_cs_streaming_iface_desc_3;
	struct usb_audio_format_type1_descriptor_1freq audio_type1_format_desc_3;
	struct usb_audio_stream_endpoint_descriptor isochronous_ep_3;
	struct usb_audio_stream_endpoint_descriptor synch_ep_3;

} __attribute__((packed)) config = {
	.cdesc = {
		.bLength = USB_DT_CONFIGURATION_SIZE,
		.bDescriptorType = USB_DT_CONFIGURATION,
		.wTotalLength = sizeof(config),
		.bNumInterfaces = 2,
		.bConfigurationValue = 1,
		.iConfiguration = 0,
		.bmAttributes = 0x80,
		.bMaxPower = 0x32,
	},
	.audio_control_iface = {
		.bLength = USB_DT_INTERFACE_SIZE,
		.bDescriptorType = USB_DT_INTERFACE,
		.bInterfaceNumber = 0,
		.bAlternateSetting = 0,
		.bNumEndpoints = 0,
		.bInterfaceClass = USB_CLASS_AUDIO,
		.bInterfaceSubClass = USB_AUDIO_SUBCLASS_CONTROL,
		.bInterfaceProtocol = 0,
		.iInterface = 0,
	},
	.header_head = {
		.bLength = sizeof(struct usb_audio_header_descriptor_head) +
		sizeof(struct usb_audio_header_descriptor_body),
		.bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
		.bDescriptorSubtype = USB_AUDIO_TYPE_HEADER,
		.bcdADC = 0x0100,
		.wTotalLength = sizeof(struct usb_audio_header_descriptor_head) +
		sizeof(struct usb_audio_header_descriptor_body) +
		sizeof(struct usb_audio_input_terminal_descriptor) +
		sizeof(struct usb_audio_feature_unit_descriptor_2ch) +
		sizeof(struct usb_audio_output_terminal_descriptor),
		.binCollection = 1,
	},
	.header_body = {
		.baInterfaceNr = 0x01,
	},
	.input_terminal_desc = {
		.bLength = sizeof(struct usb_audio_input_terminal_descriptor),
		.bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
		.bDescriptorSubtype = USB_AUDIO_TYPE_INPUT_TERMINAL,
		.bTerminalID = 1,
		.wTerminalType = 0x101,
		.bAssocTerminal = 0,
		.bNrChannels = 1,
		.wChannelConfig = 0,
		.iChannelNames = 0,
		.iTerminal = 0,
	},
	.feature_unit_desc = {
		.head = {
			.bLength = sizeof(struct usb_audio_feature_unit_descriptor_2ch),
			.bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
			.bDescriptorSubtype = USB_AUDIO_TYPE_FEATURE_UNIT,
			.bUnitID = 2,
			.bSourceID = 1,
			.bControlSize = 2,
			.bmaControlMaster = 1,
		},
		.channel_control = {
			{
				.bmaControl = 2,
			},
			{
				.bmaControl = 2,
			}
		},
		.tail = {
			.iFeature = 0x00,
		}
	},
	.output_terminal_desc = {
		.bLength = sizeof(struct usb_audio_output_terminal_descriptor),
		.bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
		.bDescriptorSubtype = USB_AUDIO_TYPE_OUTPUT_TERMINAL,
		.bTerminalID = 3,
		.wTerminalType = 0x301,
		.bAssocTerminal = 0,
		.bSourceID = 0x02,
		.iTerminal = 0,
	},
	.audio_streaming_iface_0 = {
		.bLength = USB_DT_INTERFACE_SIZE,
		.bDescriptorType = USB_DT_INTERFACE,
		.bInterfaceNumber = 1,
		.bAlternateSetting = 0,
		.bNumEndpoints = 0,
		.bInterfaceClass = USB_CLASS_AUDIO,
		.bInterfaceSubClass = USB_AUDIO_SUBCLASS_AUDIOSTREAMING,
		.bInterfaceProtocol = 0,
		.iInterface = 0,
	},
	.audio_streaming_iface_1 = {
		.bLength = USB_DT_INTERFACE_SIZE,
		.bDescriptorType = USB_DT_INTERFACE,
		.bInterfaceNumber = 1,
		.bAlternateSetting = 1,
		.bNumEndpoints = 2,
		.bInterfaceClass = USB_CLASS_AUDIO,
		.bInterfaceSubClass = USB_AUDIO_SUBCLASS_AUDIOSTREAMING,
		.bInterfaceProtocol = 0,
		.iInterface = 0,
	},
	.audio_streaming_cs_ep_desc_1 = {
		.bLength = sizeof(struct usb_audio_stream_audio_endpoint_descriptor),
		.bDescriptorType = USB_AUDIO_DT_CS_ENDPOINT,
		.bDescriptorSubtype = 1, /* EP_GENERAL */
		.bmAttributes = 0,
		.bLockDelayUnits = 0x02, /* PCM samples */
		.wLockDelay = 0x0000,
	},
	.audio_cs_streaming_iface_desc_1 = {
		.bLength = sizeof(struct usb_audio_stream_interface_descriptor),
		.bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
		.bDescriptorSubtype = 1,
		.bTerminalLink = 1,
		.bDelay = 1,
		.wFormatTag = 1,
	},
	.audio_type1_format_desc_1 = {
		.head = {
			.bLength = sizeof(struct usb_audio_format_type1_descriptor_1freq),
			.bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
			.bDescriptorSubtype = 2,
			.bFormatType = 1,
			.bNrChannels = 2,
			.bSubFrameSize = 2,
			.bBitResolution = 16,
			.bSamFreqType = 1,
		},
		.freqs = {
			{
				.tSamFreq = 48000,
			}
		},
	},
	.isochronous_ep_1 = {
		.bLength = USB_DT_ENDPOINT_SIZE + 2,
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = ISO_OUT_ENDP_ADDR,
		.bmAttributes = USB_ENDPOINT_ATTR_ISOCHRONOUS | USB_ENDPOINT_ATTR_ASYNC,
		.wMaxPacketSize = ISO_PACKET_SIZE,
		.bInterval = 1,
		.bRefresh = 0,
		.bSynchAddress = ISO_IN_ENDP_ADDR,
	},
	.synch_ep_1 = {
		.bLength = USB_DT_ENDPOINT_SIZE + 2,
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = ISO_IN_ENDP_ADDR,
		.bmAttributes = USB_ENDPOINT_ATTR_ISOCHRONOUS | USB_ENDPOINT_ATTR_FEEDBACK,
		.wMaxPacketSize = ISO_SYNC_PACKET_SIZE,
		.bInterval = 1,
		.bRefresh = SOF_RATE,
		.bSynchAddress = 0,
	},

	.audio_streaming_iface_2 = {
		.bLength = USB_DT_INTERFACE_SIZE,
		.bDescriptorType = USB_DT_INTERFACE,
		.bInterfaceNumber = 1,
		.bAlternateSetting = 2,
		.bNumEndpoints = 2,
		.bInterfaceClass = USB_CLASS_AUDIO,
		.bInterfaceSubClass = USB_AUDIO_SUBCLASS_AUDIOSTREAMING,
		.bInterfaceProtocol = 0,
		.iInterface = 0,
	},
	.audio_streaming_cs_ep_desc_2 = {
		.bLength = sizeof(struct usb_audio_stream_audio_endpoint_descriptor),
		.bDescriptorType = USB_AUDIO_DT_CS_ENDPOINT,
		.bDescriptorSubtype = 1, /* EP_GENERAL */
		.bmAttributes = 0,
		.bLockDelayUnits = 0x02, /* PCM samples */
		.wLockDelay = 0x0000,
	},
	.audio_cs_streaming_iface_desc_2 = {
		.bLength = sizeof(struct usb_audio_stream_interface_descriptor),
		.bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
		.bDescriptorSubtype = 1,
		.bTerminalLink = 1,
		.bDelay = 1,
		.wFormatTag = 1,
	},
	.audio_type1_format_desc_2 = {
		.head = {
			.bLength = sizeof(struct usb_audio_format_type1_descriptor_1freq),
			.bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
			.bDescriptorSubtype = 2,
			.bFormatType = 1,
			.bNrChannels = 2,
			.bSubFrameSize = 3,
			.bBitResolution = 24,
			.bSamFreqType = 1,
		},
		.freqs = {
			{
				.tSamFreq = 48000,
			}
		},
	},
	.isochronous_ep_2 = {
		.bLength = USB_DT_ENDPOINT_SIZE + 2,
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = ISO_OUT_ENDP_ADDR,
		.bmAttributes = USB_ENDPOINT_ATTR_ISOCHRONOUS | USB_ENDPOINT_ATTR_ASYNC,
		.wMaxPacketSize = ISO_PACKET_SIZE,
		.bInterval = 1,
		.bRefresh = 0,
		.bSynchAddress = ISO_IN_ENDP_ADDR,
	},
	.synch_ep_2 = {
		.bLength = USB_DT_ENDPOINT_SIZE + 2,
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = ISO_IN_ENDP_ADDR,
		.bmAttributes = USB_ENDPOINT_ATTR_ISOCHRONOUS | USB_ENDPOINT_ATTR_FEEDBACK,
		.wMaxPacketSize = ISO_SYNC_PACKET_SIZE,
		.bInterval = 1,
		.bRefresh = SOF_RATE,
		.bSynchAddress = 0,
	},

	.audio_streaming_iface_3 = {
		.bLength = USB_DT_INTERFACE_SIZE,
		.bDescriptorType = USB_DT_INTERFACE,
		.bInterfaceNumber = 1,
		.bAlternateSetting = 3,
		.bNumEndpoints = 2,
		.bInterfaceClass = USB_CLASS_AUDIO,
		.bInterfaceSubClass = USB_AUDIO_SUBCLASS_AUDIOSTREAMING,
		.bInterfaceProtocol = 0,
		.iInterface = 0,
	},
	.audio_streaming_cs_ep_desc_3 = {
		.bLength = sizeof(struct usb_audio_stream_audio_endpoint_descriptor),
		.bDescriptorType = USB_AUDIO_DT_CS_ENDPOINT,
		.bDescriptorSubtype = 1,
		.bmAttributes = 0,
		.bLockDelayUnits = 0x02,
		.wLockDelay = 0x0000,
	},
	.audio_cs_streaming_iface_desc_3 = {
		.bLength = sizeof(struct usb_audio_stream_interface_descriptor),
		.bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
		.bDescriptorSubtype = 1,
		.bTerminalLink = 1,
		.bDelay = 1,
		.wFormatTag = 1,
	},
	.audio_type1_format_desc_3 = {
		.head = {
			.bLength = sizeof(struct usb_audio_format_type1_descriptor_1freq),
			.bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
			.bDescriptorSubtype = 2,
			.bFormatType = 1,
			.bNrChannels = 2,
			.bSubFrameSize = 4,
			.bBitResolution = 32,
			.bSamFreqType = 1,
		},
		.freqs = {
			{
				.tSamFreq = 48000,
			}
		},
	},
	.isochronous_ep_3 = {
		.bLength = USB_DT_ENDPOINT_SIZE + 2,
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = ISO_OUT_ENDP_ADDR,
		.bmAttributes = USB_ENDPOINT_ATTR_ISOCHRONOUS | USB_ENDPOINT_ATTR_ASYNC,
		.wMaxPacketSize = ISO_PACKET_SIZE,
		.bInterval = 1,
		.bRefresh = 0,
		.bSynchAddress = ISO_IN_ENDP_ADDR,
	},
	.synch_ep_3 = {
		.bLength = USB_DT_ENDPOINT_SIZE + 2,
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = ISO_IN_ENDP_ADDR,
		.bmAttributes = USB_ENDPOINT_ATTR_ISOCHRONOUS | USB_ENDPOINT_ATTR_FEEDBACK,
		.wMaxPacketSize = ISO_SYNC_PACKET_SIZE,
		.bInterval = 1,
		.bRefresh = SOF_RATE,
		.bSynchAddress = 0,
	}
};

static const struct usb_config_descriptor *configs[] = {
	&config.cdesc,
};

uint8_t usbd_control_buffer[192];

/* }}} */

extern void rb_setup(sample_fmt format);
extern uint16_t rb_put(void *src, uint16_t len);
extern void cmute(uac_rq req, uint8_t *val);
extern void cvolume(uac_rq req, uint8_t ch, int16_t *val);
extern volatile ev_t e;

static usbd_device * usbdev;
static uint32_t delta;
static uint32_t total;
static uint16_t framelen;
static struct {
	bool rts;
	bool cts;
} fb;

static void iso_tx_cb(usbd_device *usbd_dev, uint8_t ep)
{
	(void)usbd_dev;
	(void)ep;

	fb.cts = true;
}

static void iso_rx_cb(usbd_device *usbd_dev, uint8_t ep)
{
	uint8_t buf[ISO_PACKET_SIZE];
	uint16_t len, rb;

	total += len = usbd_ep_read_packet(usbd_dev, ep, buf, ISO_PACKET_SIZE);
	len -= (len % framelen);	/* drop incomplete frame, if any */

	delta += rb = rb_put(buf, len);		/* space left */
	trace(1, len << 16 | rb);

	if (e.state == STATE_FILL && (rb < RBSIZE / 2)) {
		e.seen = false;
	}
}

static void sof_cb(void)
{
	static uint32_t sofn = (1 << SOF_RATE);
	static uint32_t feedback;

	if (e.state != STATE_FILL && e.state != STATE_RUNNING) return;

	if (!--sofn) {
		feedback = (e.state == STATE_FILL) ? FEEDBACK :
			FEEDBACK_MIN + DELTA_SHIFT(delta);
		trace(2, feedback);
		sofn = (1 << SOF_RATE);
		delta = 0;
		fb.rts = true;
	}

	if (!(fb.rts && fb.cts)) return;

	if (usbd_ep_write_packet(usbdev,
				 ISO_IN_ENDP_ADDR,
				 (const void *)&feedback,
				 ISO_SYNC_PACKET_SIZE) == ISO_SYNC_PACKET_SIZE) {
		fb.rts = false;
	}
	fb.cts = false;
}

static enum usbd_request_return_codes control_cb(
	usbd_device *usbd_dev,
	struct usb_setup_data *req,
	uint8_t **buf,
	uint16_t *len,
	usbd_control_complete_callback *complete)
{
	(void) usbd_dev;
	(void) complete;
	(void) buf;
	(void) len;

	debugf("bmRequestType: %02x bRequest: %02x wValue: %d wIndex: %02x\n",
	       req->bmRequestType, req->bRequest, req->wValue, req->wIndex);

	if(req->bmRequestType == USB_REQ_TYPE_INTERFACE &&
	   req->bRequest == USB_REQ_SET_INTERFACE &&
	   req->wIndex == 1) {	/* wIndex: iface # */
		framelen = (uint16_t []){4, 4, 6, 8}[req->wValue];
		rb_setup(req->wValue); /* wValue: alt setting # */
		if (req->wValue) {
			e.state = STATE_FILL;
			fb.rts = fb.cts = true;
		} else {
			e.state = total ? STATE_DRAIN : STATE_CLOSED;
			fb.rts = fb.cts = false;
			total = 0;
		}
		return USBD_REQ_HANDLED;
	}

	return USBD_REQ_NEXT_CALLBACK;
}

static enum usbd_request_return_codes control_cs_cb(
	usbd_device *usbd_dev,
	struct usb_setup_data *req,
	uint8_t **buf,
	uint16_t *len,
	usbd_control_complete_callback *complete)
{
	(void) usbd_dev;
	(void) complete;
	(void) len;

	debugf("bRequest: %02x wValue: %04x wIndex: %04x len: %d data=%02x\n",
	       req->bRequest, req->wValue, req->wIndex, *len, *buf[0]);

	/* wValue: ControlSelector | ChannelNumber */
	switch (req->wValue >> 8) {
	case 1:                 /* master mute control */
		cmute(req->bRequest, *buf);
		return USBD_REQ_HANDLED;
	case 2:                 /* volume control */
		cvolume(req->bRequest, req->wValue, (int16_t *)*buf);
		return USBD_REQ_HANDLED;
	default:
		return USBD_REQ_NOTSUPP;
	}
}

static void usbd_set_config(usbd_device *usbd_dev, uint16_t wValue)
{
	(void)wValue;

	usbd_ep_setup(
		usbd_dev,
		ISO_OUT_ENDP_ADDR,
		USB_ENDPOINT_ATTR_ISOCHRONOUS,
		ISO_PACKET_SIZE,
		iso_rx_cb);

	usbd_ep_setup(
		usbd_dev,
		ISO_IN_ENDP_ADDR,
		USB_ENDPOINT_ATTR_ISOCHRONOUS,
		MIN_PACKET_SIZE,
		iso_tx_cb);

	usbd_register_sof_callback(usbd_dev, sof_cb);

	usbd_register_control_callback(
		usbd_dev,
		USB_REQ_TYPE_STANDARD | USB_REQ_TYPE_INTERFACE,
		USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
		control_cb);

	usbd_register_control_callback(
		usbd_dev,
		USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
		USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
		control_cs_cb);
}

void usbd(void)
{
	usbdev = usbd_init(&otgfs_usb_driver, &dev, configs,
			   usb_strings, sizeof(usb_strings)/sizeof(usb_strings[0]),
			   usbd_control_buffer, sizeof(usbd_control_buffer));

	usbd_register_set_config_callback(usbdev, usbd_set_config);
	nvic_enable_irq(NVIC_OTG_FS_IRQ);
}

void otg_fs_isr(void)
{
	usbd_poll(usbdev);
}
