# host-consumer.mk: external host source and archive link recipes
# Copyright (c) 2026 Fredrick Meunier
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, see <https://www.gnu.org/licenses/>.

# Include after the configured Fuse Makefile. Build the engine first to
# generate its own headers. The consumer only sees the source interface.
# Use absolute HOST_INPUT (C file) and HOST_OUTPUT (executable) paths.
HOST_HEADER_NAMES = fuse-host.h fuse-disk.h fuse-peripheral.h fuse-tape.h \
                    fuse-host-input.h fuse-host-media.h fuse-host-memory.h \
                    fuse-joystick.h input.h
HOST_HEADER_DIR = $(abs_builddir)/host-include
HOST_HEADERS = $(addprefix $(HOST_HEADER_DIR)/,$(HOST_HEADER_NAMES))
HOST_CPPFLAGS ?= $(GLIB_CFLAGS) $(SDL2_CFLAGS)

$(HOST_HEADER_DIR)/%.h: $(srcdir)/%.h
	$(MKDIR_P) "$(HOST_HEADER_DIR)"
	cp "$<" "$@"

# Alternate adapter for the SDL-configured same-archive check.
FUSE_TEST_UI_OBJECTS = ui.$(OBJEXT) menu.$(OBJEXT) uimedia.$(OBJEXT) \
                       ui/no_widget.$(OBJEXT) \
                       ui/null/null_ui.$(OBJEXT) ui/null/options.$(OBJEXT)

.PHONY: host-consumer-archive host-consumer-source host-consumer-object host-consumer-test-adapter
host-consumer-test-adapter: host-consumer-object libfuse.la $(FUSE_TEST_UI_OBJECTS)
	$(LINK) -o "$(HOST_OUTPUT)" "$(HOST_OUTPUT).o" $(FUSE_TEST_UI_OBJECTS) \
	  libfuse.la $(FUSE_CORE_LIBS) $(FUSE_APP_LIBS) $(LIBS)

host-consumer-object: $(HOST_HEADERS)
	$(CC) $(CFLAGS) -I"$(HOST_HEADER_DIR)" $(LIBSPECTRUM_CFLAGS) \
	  $(HOST_CPPFLAGS) -c "$(HOST_INPUT)" -o "$(HOST_OUTPUT).o"

host-consumer-archive: host-consumer-object libfuse.la $(FUSE_UI_OBJECTS)
	$(LINK) -o "$(HOST_OUTPUT)" "$(HOST_OUTPUT).o" \
	  libfuse.la $(FUSE_UI_LINK) $(FUSE_CORE_LIBS) $(FUSE_APP_LIBS) $(LIBS)

host-consumer-source: host-consumer-object $(libfuse_la_OBJECTS) $(FUSE_UI_OBJECTS) $(LEGACY_ENGINE_OBJECTS)
	$(LINK) -o "$(HOST_OUTPUT)" "$(HOST_OUTPUT).o" \
	  $(libfuse_la_OBJECTS) $(FUSE_UI_OBJECTS) $(libfuse_la_LIBADD) $(FUSE_UI_LIBS) $(FUSE_APP_LIBS) $(LIBS)
