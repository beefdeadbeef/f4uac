#------------------------------------------ -*- tab-width: 8 -*-
ifeq		($(DEBUG),1)

VPATH		+= $(OPENCM3_DIR)/tests/shared
CPPFLAGS	+= -DDEBUG
CPPFLAGS	+= -I$(OPENCM3_DIR)/tests/shared
OBJS		+= trace.o trace_stdio.o

endif
