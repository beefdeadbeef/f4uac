/* -*- mode: c; tab-width: 8 -*-
 *  SPDX-License-Identifier: MIT
 *  Copyright (C) 2021-2022 Sergey Bolshakov <beefdeadbeef@gmail.com>
 */

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/usb/audio.h>
#include <libopencm3/usb/usbd.h>

#include "common.h"
#include "tables.h"

#define __usb_isr usb_lp_isr
#define __usb_driver st_usbfs_v1_usb_driver
#define __usb_irq NVIC_USB_LP_IRQ

#define PKTSIZE0 16
#define MIN_PACKET_SIZE 8

#define ISO_PACKET_SIZE 576
#define ISO_SYNC_PACKET_SIZE 3
#define ISO_OUT_ENDP_ADDR 0x01
#define ISO_IN_ENDP_ADDR 0x84

#define INTR_PACKET_SIZE 2
#define INTR_IN_ENDP_ADDR 0x86

typedef enum  {
	UAC_SET_CUR = 1,
	UAC_SET_MIN,
	UAC_SET_MAX,
	UAC_SET_RES,
	UAC_GET_CUR = 0x81,
	UAC_GET_MIN,
	UAC_GET_MAX,
	UAC_GET_RES
} uac_request_t;

typedef enum {
	UAC_FU_MUTE = 1,
	UAC_FU_VOLUME = 2,
	UAC_FU_BASS_BOOST = 9
} uac_fu_sc_t;

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
	.bMaxPacketSize0 = PKTSIZE0,
	.idVendor = 0x6666,
	.idProduct = 0x2701,
	.bcdDevice = 0x0200,
	.iManufacturer = 1,
	.iProduct = 2,
	.iSerialNumber = 0,
	.bNumConfigurations = 1,
};

struct usb_audio_format_type1_descriptor_2freq {
        struct usb_audio_format_type1_descriptor_head head;
        struct usb_audio_format_discrete_sampling_frequency freqs[2];
} __attribute__((packed));

struct usb_audio_format_type1_descriptor_4freq {
        struct usb_audio_format_type1_descriptor_head head;
        struct usb_audio_format_discrete_sampling_frequency freqs[4];
} __attribute__((packed));

static const struct {
	struct usb_config_descriptor cdesc;

	struct usb_interface_descriptor audio_control_iface;
	struct usb_audio_header_descriptor_head header_head;
	struct usb_audio_header_descriptor_body header_body;
	struct usb_audio_input_terminal_descriptor input_terminal_desc;
	struct usb_audio_feature_unit_descriptor_2ch feature_unit_desc;
	struct usb_audio_feature_unit_descriptor_2ch feature_unit_desc_speaker;
	struct usb_audio_output_terminal_descriptor headset_desc;
	struct usb_audio_output_terminal_descriptor speaker_desc;
	struct usb_audio_stream_endpoint_descriptor intr_ep;

	struct usb_interface_descriptor audio_streaming_iface_0;

	struct usb_interface_descriptor audio_streaming_iface_1;
	struct usb_audio_stream_audio_endpoint_descriptor audio_streaming_cs_ep_desc_1;
	struct usb_audio_stream_interface_descriptor audio_cs_streaming_iface_desc_1;
	struct usb_audio_format_type1_descriptor_4freq audio_type1_format_desc_1;
	struct usb_audio_stream_endpoint_descriptor isochronous_ep_1;
	struct usb_audio_stream_endpoint_descriptor synch_ep_1;

	struct usb_interface_descriptor audio_streaming_iface_2;
	struct usb_audio_stream_audio_endpoint_descriptor audio_streaming_cs_ep_desc_2;
	struct usb_audio_stream_interface_descriptor audio_cs_streaming_iface_desc_2;
	struct usb_audio_format_type1_descriptor_4freq audio_type1_format_desc_2;
	struct usb_audio_stream_endpoint_descriptor isochronous_ep_2;
	struct usb_audio_stream_endpoint_descriptor synch_ep_2;

	struct usb_interface_descriptor audio_streaming_iface_3;
	struct usb_audio_stream_audio_endpoint_descriptor audio_streaming_cs_ep_desc_3;
	struct usb_audio_stream_interface_descriptor audio_cs_streaming_iface_desc_3;
	struct usb_audio_format_type1_descriptor_2freq audio_type1_format_desc_3;
	struct usb_audio_stream_endpoint_descriptor isochronous_ep_3;
	struct usb_audio_stream_endpoint_descriptor synch_ep_3;

	struct usb_interface_descriptor audio_streaming_iface_4;
	struct usb_audio_stream_audio_endpoint_descriptor audio_streaming_cs_ep_desc_4;
	struct usb_audio_stream_interface_descriptor audio_cs_streaming_iface_desc_4;
	struct usb_audio_format_type1_descriptor_2freq audio_type1_format_desc_4;
	struct usb_audio_stream_endpoint_descriptor isochronous_ep_4;
	struct usb_audio_stream_endpoint_descriptor synch_ep_4;

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
		.bNumEndpoints = 1,
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
		sizeof(struct usb_audio_feature_unit_descriptor_2ch) +
		sizeof(struct usb_audio_output_terminal_descriptor) +
		sizeof(struct usb_audio_output_terminal_descriptor) +
		sizeof(struct usb_audio_stream_endpoint_descriptor),
		.binCollection = 1,
	},
	.header_body = {
		.baInterfaceNr = 0x01,
	},
	.input_terminal_desc = {
		.bLength = sizeof(struct usb_audio_input_terminal_descriptor),
		.bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
		.bDescriptorSubtype = USB_AUDIO_TYPE_INPUT_TERMINAL,
		.bTerminalID = UAC_IT_PCM_ID,
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
			.bUnitID = UAC_FU_MAIN_ID,
			.bSourceID = UAC_IT_PCM_ID,
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
			.iFeature = 0,
		}
	},
	.feature_unit_desc_speaker = {
		.head = {
			.bLength = sizeof(struct usb_audio_feature_unit_descriptor_2ch),
			.bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
			.bDescriptorSubtype = USB_AUDIO_TYPE_FEATURE_UNIT,
			.bUnitID = UAC_FU_SPEAKER_ID,
			.bSourceID = UAC_FU_MAIN_ID,
			.bControlSize = 2,
			.bmaControlMaster = 0x101,
		},
		.tail = {
			.iFeature = 0,
		}
	},

	.headset_desc = {
		.bLength = sizeof(struct usb_audio_output_terminal_descriptor),
		.bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
		.bDescriptorSubtype = USB_AUDIO_TYPE_OUTPUT_TERMINAL,
		.bTerminalID = UAC_OT_HEADSET_ID,
		.wTerminalType = 0x302,
		.bAssocTerminal = 0,
		.bSourceID = UAC_FU_MAIN_ID,
		.iTerminal = 0,
	},

	.speaker_desc = {
		.bLength = sizeof(struct usb_audio_output_terminal_descriptor),
		.bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
		.bDescriptorSubtype = USB_AUDIO_TYPE_OUTPUT_TERMINAL,
		.bTerminalID = UAC_OT_SPEAKER_ID,
		.wTerminalType = 0x301,
		.bAssocTerminal = 0,
		.bSourceID = UAC_FU_SPEAKER_ID,
		.iTerminal = 0,
	},
	.intr_ep = {
		.bLength = USB_DT_ENDPOINT_SIZE + 2,
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = INTR_IN_ENDP_ADDR,
		.bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
		.wMaxPacketSize = INTR_PACKET_SIZE,
		.bInterval = 10,
		.bRefresh = 0,
		.bSynchAddress = 0,
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
		.bmAttributes = 1,
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
			.bLength = sizeof(struct usb_audio_format_type1_descriptor_4freq),
			.bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
			.bDescriptorSubtype = 2,
			.bFormatType = 1,
			.bNrChannels = 2,
			.bSubFrameSize = 2,
			.bBitResolution = 16,
			.bSamFreqType = 4,
		},
		.freqs = {
			{
				.tSamFreq = 44100,
			},
			{
				.tSamFreq = 48000,
			},
			{
				.tSamFreq = 88200,
			},
			{
				.tSamFreq = 96000,
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
		.bRefresh = SOF_SHIFT,
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
		.bmAttributes = 1,
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
			.bLength = sizeof(struct usb_audio_format_type1_descriptor_4freq),
			.bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
			.bDescriptorSubtype = 2,
			.bFormatType = 1,
			.bNrChannels = 2,
			.bSubFrameSize = 3,
			.bBitResolution = 24,
			.bSamFreqType = 4,
		},
		.freqs = {
			{
				.tSamFreq = 44100,
			},
			{
				.tSamFreq = 48000,
			},
			{
				.tSamFreq = 88200,
			},
			{
				.tSamFreq = 96000,
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
		.bRefresh = SOF_SHIFT,
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
		.bmAttributes = 1,
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
			.bLength = sizeof(struct usb_audio_format_type1_descriptor_2freq),
			.bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
			.bDescriptorSubtype = 2,
			.bFormatType = 1,
			.bNrChannels = 2,
			.bSubFrameSize = 4,
			.bBitResolution = 32,
			.bSamFreqType = 2,
		},
		.freqs = {
			{
				.tSamFreq = 44100,
			},
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
		.bRefresh = SOF_SHIFT,
		.bSynchAddress = 0,
	},

	.audio_streaming_iface_4 = {
		.bLength = USB_DT_INTERFACE_SIZE,
		.bDescriptorType = USB_DT_INTERFACE,
		.bInterfaceNumber = 1,
		.bAlternateSetting = 4,
		.bNumEndpoints = 2,
		.bInterfaceClass = USB_CLASS_AUDIO,
		.bInterfaceSubClass = USB_AUDIO_SUBCLASS_AUDIOSTREAMING,
		.bInterfaceProtocol = 0,
		.iInterface = 0,
	},
	.audio_streaming_cs_ep_desc_4 = {
		.bLength = sizeof(struct usb_audio_stream_audio_endpoint_descriptor),
		.bDescriptorType = USB_AUDIO_DT_CS_ENDPOINT,
		.bDescriptorSubtype = 1,
		.bmAttributes = 1,
		.bLockDelayUnits = 0x02,
		.wLockDelay = 0x0000,
	},
	.audio_cs_streaming_iface_desc_4 = {
		.bLength = sizeof(struct usb_audio_stream_interface_descriptor),
		.bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
		.bDescriptorSubtype = 1,
		.bTerminalLink = 1,
		.bDelay = 1,
		.wFormatTag = 3,
	},
	.audio_type1_format_desc_4 = {
		.head = {
			.bLength = sizeof(struct usb_audio_format_type1_descriptor_2freq),
			.bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
			.bDescriptorSubtype = 2,
			.bFormatType = 1,
			.bNrChannels = 2,
			.bSubFrameSize = 4,
			.bBitResolution = 32,
			.bSamFreqType = 2,
		},
		.freqs = {
			{
				.tSamFreq = 44100,
			},
			{
				.tSamFreq = 48000,
			}
		},
	},
	.isochronous_ep_4 = {
		.bLength = USB_DT_ENDPOINT_SIZE + 2,
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = ISO_OUT_ENDP_ADDR,
		.bmAttributes = USB_ENDPOINT_ATTR_ISOCHRONOUS | USB_ENDPOINT_ATTR_ASYNC,
		.wMaxPacketSize = ISO_PACKET_SIZE,
		.bInterval = 1,
		.bRefresh = 0,
		.bSynchAddress = ISO_IN_ENDP_ADDR,
	},
	.synch_ep_4 = {
		.bLength = USB_DT_ENDPOINT_SIZE + 2,
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = ISO_IN_ENDP_ADDR,
		.bmAttributes = USB_ENDPOINT_ATTR_ISOCHRONOUS | USB_ENDPOINT_ATTR_FEEDBACK,
		.wMaxPacketSize = ISO_SYNC_PACKET_SIZE,
		.bInterval = 1,
		.bRefresh = SOF_SHIFT,
		.bSynchAddress = 0,
	}
};

static const struct usb_config_descriptor *configs[] = {
	&config.cdesc,
};

uint8_t usbd_control_buffer[64];

extern void pll_setup(sample_rate freq);
extern void rb_setup(sample_fmt format, bool dr);
extern uint16_t rb_put(void *src, uint16_t len);
extern void set_scale();
extern void speaker();
extern volatile ev_t e;
extern volatile cs_t cstate;

static usbd_device * usbdev;
static uint32_t delta;
static uint32_t total;
static uint16_t framelen;

static struct {
	bool rts;
	bool cts;
} fb, ac = { false, true };

static uint8_t acstatus[2];

static inline bool doubleratep(sample_rate rate)
{
	return rate == SAMPLE_RATE_88200 || rate == SAMPLE_RATE_96000;
}

void uac_notify(uac_id_t id)
{
	acstatus[0] = 0x80;
	acstatus[1] = id;
	ac.rts = true;
}

static void intr_tx_cb(usbd_device *usbd_dev, uint8_t ep)
{
	(void)usbd_dev;
	(void)ep;

	ac.cts = true;
}

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
	static uint32_t sofn = (1 << SOF_SHIFT);
	static uint32_t feedback;

	if (!(ac.rts && ac.cts)) goto feedback;

	if (usbd_ep_write_packet(usbdev, INTR_IN_ENDP_ADDR,
				 (const void *)acstatus,
				 INTR_PACKET_SIZE)) {
		trace(3, *(uint16_t *)acstatus);
		ac.rts = false;
	}
	ac.cts = false;

feedback:

	if (cstate.format == SAMPLE_FORMAT_NONE) return;

	if (!--sofn) {
		feedback = ((e.state == STATE_FILL) ? FEEDBACK :
			    FEEDBACK_MIN + DELTA_SHIFT(delta)) << doubleratep(cstate.rate);
		trace(2, feedback);
		sofn = (1 << SOF_SHIFT);
		delta = 0;
		fb.rts = true;
	}

	if (!(fb.rts && fb.cts)) return;

	if (usbd_ep_write_packet(usbdev, ISO_IN_ENDP_ADDR,
				 (const void *)&feedback,
				 ISO_SYNC_PACKET_SIZE)) {
		fb.rts = false;
	}
	fb.cts = false;
}

static void altset_cb(usbd_device *usbd_dev,
		      uint16_t wIndex, uint16_t wValue)
{
	(void) usbd_dev;
	debugf("wIndex: %d wValue: %d\n", wIndex, wValue);

	switch (wIndex) {		/* wIndex: iface # */
	case 1:				/* wValue: alt setting # */
		cstate.format = wValue;
		framelen = framesize(wValue);
		if (wValue) {
			rb_setup(wValue, doubleratep(cstate.rate));
			e.state = STATE_FILL;
			fb.rts = fb.cts = true;
		} else {
			rb_setup(wValue, false);
			e.state = total ? STATE_DRAIN : STATE_CLOSED;
			fb.rts = fb.cts = false;
			total = 0;
		}
	}
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
	switch ((req->wIndex & 0xff00) | (req->wValue >> 8)) {
	case (UAC_FU_MAIN_ID <<8 | UAC_FU_MUTE):
		switch(req->bRequest) {
		case UAC_SET_CUR:
			cstate.on[muted] = **buf;
			set_scale();
			return USBD_REQ_HANDLED;
		case UAC_GET_CUR:
			**buf = cstate.on[muted];
			return USBD_REQ_HANDLED;
		default:
			return USBD_REQ_NOTSUPP;
		}
	case (UAC_FU_MAIN_ID << 8 | UAC_FU_VOLUME):
		switch (req->bRequest) {
		case UAC_SET_CUR:
		{
			uint16_t i = 0;
			while (i < VOLSTEPS && db[i] > *(int16_t *)*buf) i++;
			cstate.attn = i;
			set_scale();
			break;
		}
		case UAC_GET_CUR:
			*(int16_t *)*buf = db[cstate.attn];
			break;
		case UAC_GET_MIN:
			*(int16_t *)*buf = db[VOLSTEPS - 1];
			break;
		case UAC_GET_MAX:
			*(int16_t *)*buf = 0;
			break;
		case UAC_GET_RES:
			*(int16_t *)*buf = 256;     /* 1dB step */
			break;
		default:
			return USBD_REQ_NOTSUPP;
		}
		debugf("req: %02x val: %d (%d)\n",
		       req->bRequest, *(int16_t *)*buf, cstate.attn);
		return USBD_REQ_HANDLED;
	case (UAC_FU_SPEAKER_ID << 8 | UAC_FU_MUTE):
		switch (req->bRequest) {
		case UAC_SET_CUR:
			cstate.on[spmuted] = **buf;
			speaker();
			return USBD_REQ_HANDLED;
		case UAC_GET_CUR:
			**buf = cstate.on[spmuted];
			return USBD_REQ_HANDLED;
		default:
			return USBD_REQ_NOTSUPP;
		}
	case (UAC_FU_SPEAKER_ID << 8 | UAC_FU_BASS_BOOST):
		switch (req->bRequest) {
		case UAC_SET_CUR:
			cstate.on[boost] = **buf;
			speaker();
			return USBD_REQ_HANDLED;
		case UAC_GET_CUR:
			**buf = cstate.on[boost];
			return USBD_REQ_HANDLED;
		default:
			break;
		}
		return USBD_REQ_NOTSUPP;
	default:
		return USBD_REQ_NOTSUPP;
	}
}

static enum usbd_request_return_codes control_cs_ep_cb(
	usbd_device *usbd_dev,
	struct usb_setup_data *req,
	uint8_t **buf,
	uint16_t *len,
	usbd_control_complete_callback *complete)
{
	(void) usbd_dev;
	(void) complete;
	(void) len;

	debugf("bRequest: %02x wValue: %04x wIndex: %04x len: %d\n",
	       req->bRequest, req->wValue, req->wIndex, *len);

	/* wValue: 0100 sampling freq control, wIndex: ep address */
	if (req->wValue == 0x100 && req->wIndex == ISO_OUT_ENDP_ADDR) {
		struct __attribute__((packed)) {
			uint32_t freq : 24;
		} *r = (typeof(r))*buf;

		switch (req->bRequest) {
		case UAC_SET_CUR:
			debugf("set_cur: freq: %d new: %d\n", cstate.rate , r->freq);
			rb_setup(cstate.format, doubleratep(r->freq));
			if (cstate.rate == r->freq) break;
			pll_setup(r->freq);
			cstate.rate = r->freq;
			break;
		case UAC_GET_CUR:
			debugf("get_cur: freq: %d\n", cstate.rate);
			r->freq = cstate.rate;
		default:
			break;
		}

		return USBD_REQ_HANDLED;
	}

	return USBD_REQ_NOTSUPP;
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

	usbd_ep_setup(
		usbd_dev,
		INTR_IN_ENDP_ADDR,
		USB_ENDPOINT_ATTR_INTERRUPT,
		MIN_PACKET_SIZE,
		intr_tx_cb);

	usbd_register_sof_callback(usbd_dev, sof_cb);

	usbd_register_set_altsetting_callback(usbd_dev, altset_cb);

	usbd_register_control_callback(
		usbd_dev,
		USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
		USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
		control_cs_cb);

	usbd_register_control_callback(
		usbd_dev,
		USB_REQ_TYPE_CLASS | USB_REQ_TYPE_ENDPOINT,
		USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
		control_cs_ep_cb);

	cstate.on[usb] = true;
}

void usbd(void)
{
	usbdev = usbd_init(&__usb_driver, &dev, configs,
			   usb_strings, sizeof(usb_strings)/sizeof(usb_strings[0]),
			   usbd_control_buffer, sizeof(usbd_control_buffer));

	usbd_register_set_config_callback(usbdev, usbd_set_config);
	nvic_enable_irq(__usb_irq);
}

void __usb_isr(void)
{
	usbd_poll(usbdev);
}
