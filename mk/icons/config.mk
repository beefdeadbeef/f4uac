#------------------------------------------ -*- tab-width: 8 -*-
VPATH		+= mk/icons
OBJS		+= icons.o

ICONS_BIG	= 	play pause

ICONS_SMALL	=	headphones_box \
			sine_wave \
			subwoofer \
			volume_mute \
			usb

ICONS		= 	$(ICONS_BIG) $(ICONS_SMALL)

ROTATE		=	90

$(addsuffix .svg,$(ICONS_BIG)):		ICONSZ = 40
$(addsuffix .svg,$(ICONS_SMALL)):	ICONSZ = 24
