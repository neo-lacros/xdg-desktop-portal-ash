# Build the xdg-desktop-portal-ash daemon and install its files.
#
# Built with plain make + g++ against libdbus-1 (pkg-config flags when
# available, well-known fallbacks otherwise) so it builds offline with only
# the system toolchain installed.

BIN := xdg-desktop-portal-ash

CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

DBUS_CFLAGS := $(shell pkg-config --cflags dbus-1 2>/dev/null || echo -I/usr/include/dbus-1.0 -I/usr/lib/dbus-1.0/include)
DBUS_LIBS := $(shell pkg-config --libs dbus-1 2>/dev/null || echo -ldbus-1)

SRCS := portal.cpp daemon.cpp xdg-desktop-portal-ash.cpp
OBJS := $(SRCS:.cpp=.o)

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(DBUS_LIBS)

%.o: %.cpp portal.h daemon.h
	$(CXX) $(CXXFLAGS) $(DBUS_CFLAGS) -c -o $@ $<

DESTDIR ?=
PREFIX ?= /usr

install: $(BIN)
	install -Dm755 $(BIN) "$(DESTDIR)$(PREFIX)/libexec/xdg-desktop-portal-ash"
	install -Dm644 ash.portal "$(DESTDIR)$(PREFIX)/share/xdg-desktop-portal/portals/ash.portal"
	install -Dm644 ash-portals.conf "$(DESTDIR)$(PREFIX)/share/xdg-desktop-portal/ash-portals.conf"
	install -Dm644 xdg-desktop-portal-ash.service "$(DESTDIR)$(PREFIX)/lib/systemd/user/xdg-desktop-portal-ash.service"
	install -Dm644 org.freedesktop.impl.portal.desktop.ash.service "$(DESTDIR)$(PREFIX)/share/dbus-1/services/org.freedesktop.impl.portal.desktop.ash.service"

clean:
	rm -f $(OBJS) $(BIN)

.PHONY: all clean install
