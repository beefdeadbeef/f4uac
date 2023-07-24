#------------------------------------------ -*- tab-width: 8 -*-
CURL		= curl
CONVERT		= convert
SED		= sed

$(addsuffix .svg,$(ICONS)):
	$(Q)$(CURL) -s --output $@ https://api.iconify.design/mdi/$(subst _,-,$@)\?download=1\&color=black\&width=$(ICONSZ)\&height=$(ICONSZ)

%.xbm:	%.svg
	$(Q)$(CONVERT) -rotate $(ROTATE) -equalize $< xbm:$@
	$(Q)$(SED) -ri '/^static/ s,^static,static const,' $@

icons.h: icons.h.in
	$(Q)$(SED) \
		-e "s,@ICON_ENUMS@,$(foreach i,$(ICONS),\n\ticon_$i\,)," \
		$< > $@
icons.c: icons.c.in icons.h
	$(Q)$(SED) \
		-e "s,@ICON_INCLUDES@,$(foreach i,$(ICONS),\n\#include \"$i.xbm\")," \
		-e "s,@ICON_TABLE@,$(foreach i,$(ICONS),\n\t{ .p = $i_bits }\,)," \
		$< > $@

icons.o:	$(addsuffix .xbm,$(ICONS))

.INTERMEDIATE:	$(addsuffix .xbm,$(ICONS))
