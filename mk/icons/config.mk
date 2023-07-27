#------------------------------------------ -*- tab-width: 8 -*-
VPATH		+= mk/icons
OBJS		+= icons.o

ICONS_BIG	= 	play_circle \
			pause_circle \
			skip_backward_circle \
			fast_forward_circle

ICONS_SMALL	=	headphones \
			speaker \
			volume_mute \
			usb_symbol \
			caret_down \
			caret_left \
			caret_right \
			caret_up

ICONS_UPDN	=  	headphones speaker usb_symbol \
			caret_up caret_down

ICONS		= 	$(ICONS_BIG) $(ICONS_SMALL)

ROTATE		=	90

$(addsuffix .svg,$(ICONS_BIG)):		ICONSZ = 64
$(addsuffix .svg,$(ICONS_SMALL)):	ICONSZ = 32
$(addsuffix .xbm,$(ICONS_UPDN)):	ROTATE = 270
