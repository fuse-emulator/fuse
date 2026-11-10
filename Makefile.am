## Process this file with automake to produce Makefile.in
## Copyright (c) 1999-2017 Philip Kendall
## Copyright (c) 2026 Fredrick Meunier

## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 2 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License along
## with this program; if not, write to the Free Software Foundation, Inc.,
## 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
##
## Author contact information:
##
## E-mail: philip-fuse@shadowmagic.org.uk

ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = fuse

noinst_PROGRAMS =
check_PROGRAMS =

# Configured, UI-bound legacy engine; no installed library or SDK.
noinst_LTLIBRARIES = libfuselegacy.la
fuse_SOURCES = fuse-main.c
FUSE_APP_LIBS =
fuse_LDADD = libfuselegacy.la $(FUSE_APP_LIBS)
fuse_DEPENDENCIES = libfuselegacy.la

libfuselegacy_la_SOURCES = display.c \
	display_border.c \
	display_dirty.c \
	display_internal.h \
	display_render.c \
	event.c \
	fuse.c \
	input.c \
	input_refresh.c \
	keyboard.c \
	keyboard_refresh.c \
	loader.c \
	loader_detector.c \
	loader_internals.h \
	unittests/loader.c \
	machine.c \
	memory_pages.c \
	mempool.c \
	menu.c \
	movie.c \
	module.c \
	periph.c \
	phantom_typist.c \
	profile.c \
	psg.c \
	rectangle.c \
	rzx.c \
	rzx_recording.c \
	rzx_internal.h \
	screenshot.c \
	settings.c \
	slt.c \
	snapshot.c \
	sound.c \
	spectrum.c \
	svg.c \
	tape.c \
	tape_block.c \
	tape_internals.h \
	tape_record.c \
	tape_traps.c \
	unittests/tape.c \
	ui.c \
	uidisplay.c \
	uimedia.c \
	utils.c \
	utils_string.c

FUSE_LEGACY_LIBS = \
             $(PTHREAD_LIBS) \
             $(LIBSPECTRUM_LIBS) \
             $(GTK_LIBS) \
             $(GLIB_LIBS) \
             $(PNG_LIBS) \
             $(X_LIBS) \
             $(XML_LIBS)

if USE_SDL2
FUSE_LEGACY_LIBS += $(SDL2_LIBS)
endif

LEGACY_ENGINE_OBJECTS = $(SOUND_LIBADD) $(TIMER_LIBADD) $(LEGACY_SCALER_OBJECTS)
libfuselegacy_la_LIBADD = $(LEGACY_ENGINE_OBJECTS) $(FUSE_LEGACY_LIBS)
libfuselegacy_la_DEPENDENCIES = $(LEGACY_ENGINE_OBJECTS)

if EMSCRIPTEN
fuse_LDFLAGS = -sUSE_SDL=2 -sASYNCIFY
fuse_LDFLAGS += --preload-file emscripten-data@/
fuse_DEPENDENCIES += emscripten-data.stamp

emscripten-data.stamp: $(ROMS) $(lib_files) ui/widget/fuse.font
	$(AM_V_GEN)rm -rf emscripten-data
	$(AM_V_at)$(MKDIR_P) emscripten-data/roms emscripten-data/lib emscripten-data/ui/widget
	$(AM_V_at)for file in $(ROMS) $(lib_files); do \
	  $(INSTALL_DATA) $(srcdir)/$$file emscripten-data/$$file || exit 1; \
	done
	$(AM_V_at)$(INSTALL_DATA) ui/widget/fuse.font emscripten-data/ui/widget/fuse.font
	$(AM_V_at)touch $@

clean-local:
	-rm -rf emscripten-data emscripten-data.stamp
endif

EXTRA_libfuselegacy_la_SOURCES =

BUILT_SOURCES = options.h settings.c settings.h

# Rebuild even sources without config.h when the assertion policy changes.
$(fuse_OBJECTS) $(libfuselegacy_la_OBJECTS) $(SOUND_LIBADD) $(TIMER_LIBADD): config.h

settings.c: settings.pl settings.dat
	$(AM_V_GEN)$(PERL) -I$(srcdir)/perl $(srcdir)/settings.pl $(srcdir)/settings.dat > $@.tmp && mv $@.tmp $@

settings.h: settings-header.pl settings.dat
	$(AM_V_GEN)$(PERL) -I$(srcdir)/perl $(srcdir)/settings-header.pl $(srcdir)/settings.dat > $@.tmp && mv $@.tmp $@

options.h: $(srcdir)/perl/cpp-perl.pl config.h $(srcdir)/ui/@OPTIONS_DIR@/options-header.pl $(srcdir)/ui/options.dat $(srcdir)/perl/Fuse.pm $(srcdir)/perl/Fuse/Dialog.pm
	$(AM_V_GEN)$(PERL) $(srcdir)/perl/cpp-perl.pl config.h $(srcdir)/ui/options.dat | $(PERL) -I$(srcdir)/perl $(srcdir)/ui/@OPTIONS_DIR@/options-header.pl - public > $@.tmp && mv $@.tmp $@

AM_CPPFLAGS = \
              $(GLIB_CFLAGS) \
              $(GTK_CFLAGS) \
              $(LIBSPECTRUM_CFLAGS) \
              $(XML_CFLAGS) \
              -DFUSEDATADIR="\"${pkgdatadir}\"" \
              $(PNG_CFLAGS)

if USE_SDL2
AM_CPPFLAGS += $(SDL2_CFLAGS)
endif

AM_CFLAGS = $(WARN_CFLAGS) $(PTHREAD_CFLAGS)

noinst_HEADERS = bitmap.h \
	compat.h \
	display.h \
	event.h \
	fuse.h \
	fuse-internal.h \
	input.h \
	input_refresh.h \
	keyboard.h \
	keyboard_refresh.h \
	loader.h \
	machine.h \
	memory_pages.h \
	mempool.h \
	menu.h \
	movie.h \
	movie_tables.h \
	module.h \
	periph.h \
	phantom_typist.h \
	psg.h \
	rectangle.h \
	rzx.h \
	screenshot.h \
	settings.h \
	slt.h \
	snapshot.h \
	sound.h \
	spectrum.h \
	svg.h \
	tape.h \
	tape_internals.h \
	utils.h \
	options.h \
	profile.h

EXTRA_DIST = AUTHORS \
             .github/scripts/test_sfifo.sh \
             .github/scripts/test_coreaudio.sh \
	     INSTALL \
	     PORTING \
	     README \
	     THANKS \
	     keysyms.dat \
	     keysyms.pl \
	     m4/ax_pthread.m4 \
	     m4/ax_string_strcasecmp.m4 \
	     m4/pkg.m4 \
	     menu_data.dat \
	     menu_data.pl \
	     hacking/emscripten.md \
	     settings.dat \
	     settings.pl \
	     settings-header.pl \
	     update-release.py \
	     tools/metrics \
	     tools/test_metrics.py \
	     hacking/input-refresh.md \
	     .agents/skills/README.md \
	     .agents/skills/update-changelog/SKILL.md

CLEANFILES = options.h \
	     settings.c \
	     settings.h

DISTCLEANFILES =

pkgdata_DATA =

if UI_NULL
test: z80/coretest unittests/displaytest
	z80/coretest $(srcdir)/z80/tests/tests.in > z80/tests.actual
	cmp z80/tests.actual $(srcdir)/z80/tests/tests.expected
	./unittests/displaytest
else
test:
	@echo "SKIP: internal unit tests require a null-UI build"
endif

metrics:
	@if test -n "$(FILE)"; then $(srcdir)/tools/metrics "$(FILE)"; else $(srcdir)/tools/metrics; fi

check-metrics:
	python3 $(srcdir)/tools/test_metrics.py

# Inspect the configured source/object boundary without a separate manifest.
# Special objects retain their recipes (notably the two scaler data sizes).
.PHONY: print-legacy-engine-inputs
print-legacy-engine-inputs:
	@printf '%s\n' 'Sources:' $(libfuselegacy_la_SOURCES) \
	  'Special objects:' $(LEGACY_ENGINE_OBJECTS) \
	  'External link flags:' '$(FUSE_LEGACY_LIBS)'

.PHONY: metrics check-metrics check-sfifo check-coreaudio
.PHONY: install-win32 install-win32-strip dist-win32 dist-win32-dir \
	dist-win32-zip dist-win32-7z dist-win32-exe distclean-win32


## Resources for Windows executables
if COMPAT_WIN32

fuse_LDFLAGS = -mwindows
fuse_SOURCES += windres.rc
FUSE_LEGACY_LIBS += -lcomctl32 -lwinmm
FUSE_APP_LIBS += windres.o
fuse_DEPENDENCIES += windres.o

windres.o: windres.rc data/win32/winfuse.ico data/win32/fuse.manifest $(ui_win32_res)
	$(AM_V_GEN)$(WINDRES) -I$(srcdir) -I. $(srcdir)/windres.rc $(LIBSPECTRUM_CFLAGS) $(CPPFLAGS) windres.o

endif


include automation/Makefile.am
include compat/Makefile.am
include data/Makefile.am
include debugger/Makefile.am
include hacking/Makefile.am
include infrastructure/Makefile.am
include lib/Makefile.am
include machines/Makefile.am
include man/Makefile.am
include peripherals/Makefile.am
include perl/Makefile.am
include pokefinder/Makefile.am
include roms/Makefile.am
include sound/Makefile.am
include timer/Makefile.am
include ui/Makefile.am
include ui/fb/Makefile.am
include ui/gtk3/Makefile.am
include ui/null/Makefile.am
include ui/scaler/Makefile.am
include ui/sdl2/Makefile.am
include ui/widget/Makefile.am
include ui/wii/Makefile.am
include ui/win32/Makefile.am
include ui/xlib/Makefile.am
include unittests/Makefile.am
include z80/Makefile.am

if UI_NULL
check-local: $(LIFECYCLE_CHECK_TARGETS) $(INPUT_CHECK_TARGETS) check-widget-utf8 $(AUDIO_CHECK_TARGETS) fuse unittests/displaytest unittests/sdl2displaytest unittests/sdl2scalerstatetest unittests/sdl2joysticktest unittests/sdl2mousetest unittests/palcompositetest unittests/scalerexpandtest z80/coretest
	@test -e $(top_builddir)/roms || ln -sf "$(abs_top_srcdir)/roms" $(top_builddir)/roms
	FUSE_TEST_SRCDIR="$(abs_top_srcdir)" $(top_builddir)/fuse --unittests
	$(top_builddir)/unittests/displaytest
	$(top_builddir)/unittests/sdl2displaytest
	$(top_builddir)/unittests/sdl2scalerstatetest
	$(top_builddir)/unittests/sdl2joysticktest
	$(top_builddir)/unittests/sdl2mousetest
	$(top_builddir)/unittests/palcompositetest
	$(top_builddir)/unittests/scalerexpandtest
if ENABLE_AUTOMATION
	$(top_builddir)/unittests/automation_json_test
endif
	$(top_builddir)/z80/coretest $(srcdir)/z80/tests/tests.in > $(top_builddir)/z80/tests.actual
	cmp $(top_builddir)/z80/tests.actual $(srcdir)/z80/tests/tests.expected
else
check-local: $(LIFECYCLE_CHECK_TARGETS) $(INPUT_CHECK_TARGETS) check-widget-utf8 $(AUDIO_CHECK_TARGETS)
	@echo "SKIP: internal unit tests require a null-UI build"
endif
