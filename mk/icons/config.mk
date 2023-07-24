#------------------------------------------ -*- tab-width: 8 -*-
VPATH		+= mk/icons
OBJS		+= icons.o

ICONS_BIG	= 	play_circle_outline \
			pause_circle_outline

ICONS_SMALL	=	headphones_box \
			subwoofer \
			volume_mute \
			usb

ICONS		= 	$(ICONS_BIG) $(ICONS_SMALL)

ROTATE		=	90

$(addsuffix .svg,$(ICONS_BIG)):		ICONSZ = 64
$(addsuffix .svg,$(ICONS_SMALL)):	ICONSZ = 32
