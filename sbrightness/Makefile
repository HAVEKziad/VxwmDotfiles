PREFIX     ?= /usr/local
UDEV_RULES ?= /etc/udev/rules.d
CC         ?= cc
CFLAGS     += -Wall -Wextra -Wno-unused-parameter -O2


IS_MUSL := $(shell $(CC) -dumpmachine 2>/dev/null | grep -c musl)
ifneq ($(IS_MUSL),0)
  LDFLAGS += -static
endif

BINS  = sbrightness
RULES = backlight.rules

all: $(BINS)

clean:
	rm -f $(BINS)

install: all
	install -Dm755 $(BINS) $(PREFIX)/bin/$(BINS)
	install -Dm644 $(RULES) $(UDEV_RULES)/99-$(RULES)

sbrightness: sbrightness.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
