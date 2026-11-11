## Process this file with automake to produce Makefile.in
## Copyright (c) 2008 Philip Kendall
## Copyright (c) 2015 Sergio Baldoví

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

libfuselegacy_la_SOURCES += unittests/helpers.c unittests/floatingbus.c
noinst_HEADERS += unittests/helpers.h
EXTRA_DIST += unittests/fixtures/floatingbus/README.md \
	unittests/fixtures/floatingbus/.gitignore

LIFECYCLE_CHECK_TARGETS = check-startup-manager
.PHONY: $(LIFECYCLE_CHECK_TARGETS) check-legacy-engine-link check-startup-ownership

# Per-test CPPFLAGS give shared sources distinct non-Libtool object names.
noinst_PROGRAMS += unittests/startupmanagertest
unittests_startupmanagertest_SOURCES = unittests/startupmanagertest.c \
                                     infrastructure/startup_manager.c
unittests_startupmanagertest_CPPFLAGS = $(AM_CPPFLAGS)
unittests_startupmanagertest_LDADD = $(LIBSPECTRUM_LIBS) $(GLIB_LIBS)

check-startup-manager: unittests/startupmanagertest
	$(top_builddir)/unittests/startupmanagertest

INPUT_CHECK_TARGETS = check-input-refresh check-input-dispatch \
                      check-keyboard-refresh check-sdl2-events
.PHONY: $(INPUT_CHECK_TARGETS)

noinst_PROGRAMS += unittests/keyboardrefreshtest
unittests_keyboardrefreshtest_SOURCES = unittests/keyboardrefreshtest.c \
                                      keyboard_refresh.c input_refresh.c
unittests_keyboardrefreshtest_CPPFLAGS = $(AM_CPPFLAGS) -DENABLE_AUTOMATION
unittests_keyboardrefreshtest_LDADD =

check-keyboard-refresh: unittests/keyboardrefreshtest
	$(top_builddir)/unittests/keyboardrefreshtest

if USE_SDL2
noinst_PROGRAMS += unittests/sdl2eventstest
unittests_sdl2eventstest_SOURCES = unittests/sdl2eventstest.c ui/sdl2/sdl2_events.c
unittests_sdl2eventstest_CPPFLAGS = $(AM_CPPFLAGS)
unittests_sdl2eventstest_LDADD = $(SDL2_LIBS)
check-sdl2-events: unittests/sdl2eventstest
	$(top_builddir)/unittests/sdl2eventstest
else
check-sdl2-events:
	@echo "SKIP: SDL2 event collection tests require SDL2"
endif

noinst_PROGRAMS += unittests/inputdispatchtest
unittests_inputdispatchtest_SOURCES = unittests/inputdispatchtest.c input.c
unittests_inputdispatchtest_CPPFLAGS = $(AM_CPPFLAGS)
unittests_inputdispatchtest_LDADD =

check-input-dispatch: unittests/inputdispatchtest
	$(top_builddir)/unittests/inputdispatchtest

noinst_PROGRAMS += unittests/inputrefreshtest
unittests_inputrefreshtest_SOURCES = unittests/inputrefreshtest.c input_refresh.c
unittests_inputrefreshtest_CPPFLAGS = $(AM_CPPFLAGS)
unittests_inputrefreshtest_LDADD =

check-input-refresh: unittests/inputrefreshtest
	$(top_builddir)/unittests/inputrefreshtest

# SDK-specific cooperative wait test; see hacking/emscripten.md for Node usage.
EXTRA_DIST += unittests/emscriptenwaittest.c

noinst_PROGRAMS += unittests/widgetutf8test
unittests_widgetutf8test_SOURCES = unittests/widgetutf8test.c ui/widget/utf8.c
unittests_widgetutf8test_CPPFLAGS = $(AM_CPPFLAGS)
unittests_widgetutf8test_LDADD =
noinst_HEADERS += ui/widget/utf8.h

check-widget-utf8: unittests/widgetutf8test
	$(top_builddir)/unittests/widgetutf8test

noinst_PROGRAMS += unittests/sfifotest
unittests_sfifotest_SOURCES = unittests/sfifotest.c sound/sfifo.c
unittests_sfifotest_LDADD =
unittests_sfifotest_CPPFLAGS = $(AM_CPPFLAGS)
if HAVE_PTHREAD
unittests_sfifotest_CPPFLAGS += -DSFIFO_TEST_THREADS
unittests_sfifotest_LDADD += $(PTHREAD_LIBS)
endif

check-sfifo: unittests/sfifotest
	$(top_builddir)/unittests/sfifotest

noinst_PROGRAMS += unittests/pcm_fifotest
unittests_pcm_fifotest_SOURCES = unittests/pcm_fifotest.c sound/sfifo.c
unittests_pcm_fifotest_CPPFLAGS = $(AM_CPPFLAGS)
unittests_pcm_fifotest_LDADD =
if HAVE_PTHREAD
unittests_pcm_fifotest_LDADD += $(PTHREAD_LIBS)
endif

check-pcm-fifo: unittests/pcm_fifotest
	$(top_builddir)/unittests/pcm_fifotest

noinst_PROGRAMS += unittests/blippreviewtest
unittests_blippreviewtest_SOURCES = unittests/blippreviewtest.c sound/blipbuffer.c
unittests_blippreviewtest_CPPFLAGS = $(AM_CPPFLAGS)
unittests_blippreviewtest_LDADD = -lm
check-blip-preview: unittests/blippreviewtest
	$(top_builddir)/unittests/blippreviewtest

noinst_PROGRAMS += unittests/soundpartitiontest
unittests_soundpartitiontest_SOURCES = unittests/soundpartitiontest.c \
        utils_string.c \
        sound/sfifo.c sound/blipbuffer.c sound/dc_filter.c \
        sound/speaker_filter.c sound/tv_filter.c sound/ula_filter.c
unittests_soundpartitiontest_CPPFLAGS = $(AM_CPPFLAGS)
unittests_soundpartitiontest_LDADD = $(LIBSPECTRUM_LIBS) $(GLIB_LIBS) -lm
EXTRA_DIST += unittests/audio-frame-equivalence.py

noinst_PROGRAMS += unittests/soundframecadencetest unittests/soundsubframecadencetest
unittests_soundframecadencetest_SOURCES = $(unittests_soundpartitiontest_SOURCES)
unittests_soundframecadencetest_CPPFLAGS = $(AM_CPPFLAGS) -DSOUND_TEST_BACKEND_CADENCE=0
unittests_soundframecadencetest_LDADD = $(unittests_soundpartitiontest_LDADD)
unittests_soundsubframecadencetest_SOURCES = $(unittests_soundpartitiontest_SOURCES)
unittests_soundsubframecadencetest_CPPFLAGS = $(AM_CPPFLAGS) -DSOUND_TEST_BACKEND_CADENCE=1
unittests_soundsubframecadencetest_LDADD = $(unittests_soundpartitiontest_LDADD)

check-sound-scheduler: unittests/soundpartitiontest unittests/soundframecadencetest unittests/soundsubframecadencetest
	$(top_builddir)/unittests/soundpartitiontest 48000 --scheduler-only
	$(top_builddir)/unittests/soundframecadencetest
	$(top_builddir)/unittests/soundsubframecadencetest

# Focused scheduler, discontinuity and physical admission regressions.
check-sound-renderer: unittests/soundpartitiontest
	$(top_builddir)/unittests/soundpartitiontest
	$(top_builddir)/unittests/soundpartitiontest 44100
	$(top_builddir)/unittests/soundpartitiontest 96000

noinst_PROGRAMS += unittests/audiopacingtest
unittests_audiopacingtest_SOURCES = unittests/audiopacingtest.c sound/sfifo.c
unittests_audiopacingtest_CPPFLAGS = $(AM_CPPFLAGS)
unittests_audiopacingtest_LDADD =
if HAVE_PTHREAD
unittests_audiopacingtest_CPPFLAGS += -DHAVE_PTHREAD
unittests_audiopacingtest_LDADD += $(PTHREAD_LIBS)
endif
check-audio-pacing: unittests/audiopacingtest
	$(top_builddir)/unittests/audiopacingtest

noinst_PROGRAMS += unittests/alsapacingtest
unittests_alsapacingtest_SOURCES = unittests/alsapacingtest.c
unittests_alsapacingtest_CPPFLAGS = $(AM_CPPFLAGS)
unittests_alsapacingtest_LDADD =
check-alsa-pacing: unittests/alsapacingtest
	$(top_builddir)/unittests/alsapacingtest

noinst_PROGRAMS += unittests/silencenoisetest
unittests_silencenoisetest_SOURCES = unittests/silencenoisetest.c
unittests_silencenoisetest_CPPFLAGS = $(AM_CPPFLAGS)
unittests_silencenoisetest_LDADD =
check-silence-noise: unittests/silencenoisetest
	$(top_builddir)/unittests/silencenoisetest

AUDIO_CHECK_TARGETS = check-silence-noise check-sfifo check-pcm-fifo check-blip-preview \
                     check-sound-renderer check-audio-pacing check-alsa-pacing

if HAVE_ALSA_AUDIO
noinst_PROGRAMS += unittests/alsasoundtest
unittests_alsasoundtest_SOURCES = unittests/alsasoundtest.c
unittests_alsasoundtest_CPPFLAGS = $(AM_CPPFLAGS)
unittests_alsasoundtest_LDADD = -lasound
check-alsasound: unittests/alsasoundtest
	$(top_builddir)/unittests/alsasoundtest
AUDIO_CHECK_TARGETS += check-alsasound
endif

if HAVE_PCM_ADMISSION
noinst_PROGRAMS += unittests/soundadmissiontest
unittests_soundadmissiontest_SOURCES = unittests/soundadmissiontest.c \
        utils_string.c \
        sound/sfifo.c sound/blipbuffer.c sound/ay_engine.c \
        sound/source_synths.c sound/output_mixer.c sound/dc_filter.c \
        sound/speaker_filter.c sound/tv_filter.c sound/ula_filter.c
unittests_soundadmissiontest_CPPFLAGS = $(AM_CPPFLAGS)
unittests_soundadmissiontest_LDADD = $(LIBSPECTRUM_LIBS) $(GLIB_LIBS) -lm
check-sound-admission: unittests/soundadmissiontest
	$(top_builddir)/unittests/soundadmissiontest

AUDIO_CHECK_TARGETS += check-sound-admission
endif

noinst_HEADERS += unittests/audio-wakeup-cases.h

if USE_SDL2
noinst_PROGRAMS += unittests/sdl2soundtest
unittests_sdl2soundtest_SOURCES = unittests/sdl2soundtest.c sound/sfifo.c \
                                $(COMPAT_WAIT_SOURCES)
unittests_sdl2soundtest_CPPFLAGS = $(AM_CPPFLAGS)
unittests_sdl2soundtest_LDADD = $(SDL2_LIBS)
if HAVE_PTHREAD
unittests_sdl2soundtest_CPPFLAGS += -DSDL2_TEST_THREADS
unittests_sdl2soundtest_LDADD += $(PTHREAD_LIBS)
endif
check-sdl2sound: unittests/sdl2soundtest
	$(top_builddir)/unittests/sdl2soundtest
AUDIO_CHECK_TARGETS += check-sdl2sound

noinst_PROGRAMS += unittests/sdl2wakeuptest
unittests_sdl2wakeuptest_SOURCES = unittests/sdl2wakeuptest.c sound/sfifo.c \
                                 $(COMPAT_WAIT_SOURCES)
unittests_sdl2wakeuptest_CPPFLAGS = $(AM_CPPFLAGS)
unittests_sdl2wakeuptest_LDADD =
if HAVE_PTHREAD
unittests_sdl2wakeuptest_CPPFLAGS += -DSDL2_TEST_THREADS
unittests_sdl2wakeuptest_LDADD += $(PTHREAD_LIBS)
endif
check-sdl2-wakeup: unittests/sdl2wakeuptest
	$(top_builddir)/unittests/sdl2wakeuptest
AUDIO_CHECK_TARGETS += check-sdl2-wakeup
endif

if HAVE_COREAUDIO
noinst_PROGRAMS += unittests/coreaudiofilltest
unittests_coreaudiofilltest_SOURCES = unittests/coreaudiofilltest.c sound/sfifo.c \
                                   $(COMPAT_WAIT_SOURCES)
unittests_coreaudiofilltest_CPPFLAGS = $(AM_CPPFLAGS)
unittests_coreaudiofilltest_LDFLAGS = -framework CoreAudio -framework AudioUnit
unittests_coreaudiofilltest_LDADD =
if HAVE_PTHREAD
unittests_coreaudiofilltest_CPPFLAGS += -DCOREAUDIO_TEST_THREADS
unittests_coreaudiofilltest_LDADD += $(PTHREAD_LIBS)
endif

check-coreaudio: unittests/coreaudiofilltest
	$(top_builddir)/unittests/coreaudiofilltest

AUDIO_CHECK_TARGETS += check-coreaudio
endif

if UI_NULL

libfuselegacy_la_SOURCES += unittests/unittests.c
noinst_HEADERS += unittests/unittests.h

## Standalone internal test programs

# Both consumers use the selected legacy UI/backends. The source form links no
# engine archive: it reuses the canonical sources and configured special objects.
check_PROGRAMS += unittests/legacyenginelinktest unittests/legacyenginesourcetest \
                  unittests/startupownershiptest
unittests_startupownershiptest_SOURCES = unittests/startupownershiptest.c
unittests_startupownershiptest_LDADD = libfuselegacy.la
unittests_legacyenginelinktest_SOURCES = unittests/legacyenginetest.c
unittests_legacyenginelinktest_LDADD = libfuselegacy.la
unittests_legacyenginesourcetest_SOURCES = unittests/legacyenginetest.c \
                                        $(libfuselegacy_la_SOURCES)
unittests_legacyenginesourcetest_CPPFLAGS = $(AM_CPPFLAGS)
unittests_legacyenginesourcetest_LDADD = $(libfuselegacy_la_LIBADD)
unittests_legacyenginesourcetest_DEPENDENCIES = $(libfuselegacy_la_DEPENDENCIES)
$(unittests_legacyenginesourcetest_OBJECTS): config.h

LIFECYCLE_CHECK_TARGETS += check-legacy-engine-link check-startup-ownership

# Keep every preference read/write inside an isolated child-process sandbox.
check-startup-ownership: unittests/startupownershiptest
	@set -e; program="$(abs_top_builddir)/unittests/startupownershiptest$(EXEEXT)"; \
	  tmp=$$(mktemp -d); trap 'rm -rf "$$tmp"' 0 1 2 3 15; \
	  cd "$$tmp"; export HOME=. XDG_CONFIG_HOME=. USERPROFILE=. WINDIR=. APPDATA=.; \
	  "$$program" storage; "$$program" no-save; \
	  test -z "$$(find . -type f -print)"; \
	  "$$program" save; test -n "$$(find . -type f -print)"; \
	  before=$$(find . -type f -exec cksum {} \;); \
	  "$$program" help > /dev/null; "$$program" version > /dev/null; \
	  $(STARTUP_OWNERSHIP_FAILURE_CHECK) \
	  test "$$before" = "$$(find . -type f ! -path './output/*' -exec cksum {} \;)"; \
	  echo 'Startup storage, settings policy and early-runner ownership passed'

if ENABLE_AUTOMATION
STARTUP_OWNERSHIP_FAILURE_CHECK = "$$program" settings-failure; \
  mkdir output; "$$program" session --machine 48 \
  --rom-48 "$(abs_top_srcdir)/roms/48.rom" \
  --automation-output output --automation-frames 1 > /dev/null;
else
STARTUP_OWNERSHIP_FAILURE_CHECK =
endif
check-legacy-engine-link: unittests/legacyenginelinktest unittests/legacyenginesourcetest
	$(top_builddir)/unittests/legacyenginelinktest --help > /dev/null
	$(top_builddir)/unittests/legacyenginelinktest --version > /dev/null
	$(top_builddir)/unittests/legacyenginesourcetest --help > /dev/null
	$(top_builddir)/unittests/legacyenginesourcetest --version > /dev/null

noinst_PROGRAMS += unittests/displaytest
noinst_PROGRAMS += unittests/sdl2displaytest
noinst_PROGRAMS += unittests/sdl2scalerstatetest
noinst_PROGRAMS += unittests/sdl2joysticktest
noinst_PROGRAMS += unittests/sdl2mousetest
noinst_PROGRAMS += unittests/palcompositetest
noinst_PROGRAMS += unittests/scalerexpandtest
if ENABLE_AUTOMATION
# Compile-time interval variants share the real execution/event engine. These
# executables are test artifacts, not installed runtime configuration paths.
AUDIO_TEST_BINARIES = unittests/fuse-audio-0 unittests/fuse-audio-35000 \
                     unittests/fuse-audio-17500 unittests/fuse-audio-8750 \
                     unittests/fuse-audio-997 unittests/fuse-audio-1
AUDIO_TEST_OBJECTS = unittests/audio-cut-0.lo \
                    unittests/audio-cut-35000.lo \
                    unittests/audio-cut-17500.lo \
                    unittests/audio-cut-8750.lo \
                    unittests/audio-cut-997.lo \
                    unittests/audio-cut-1.lo
CLEANFILES += $(AUDIO_TEST_BINARIES) $(AUDIO_TEST_OBJECTS)

$(AUDIO_TEST_OBJECTS): $(srcdir)/sound.c $(fuse_OBJECTS) $(libfuselegacy_la_OBJECTS)
	$(AM_V_CC)interval=$@; interval=$${interval#unittests/audio-cut-}; \
	  interval=$${interval%.lo}; \
	  $(LTCOMPILE) -DFUSE_SOUND_TEST_INTERVAL=$$interval -c -o $@ $(srcdir)/sound.c

unittests/fuse-audio-0: unittests/audio-cut-0.lo
unittests/fuse-audio-35000: unittests/audio-cut-35000.lo
unittests/fuse-audio-17500: unittests/audio-cut-17500.lo
unittests/fuse-audio-8750: unittests/audio-cut-8750.lo
unittests/fuse-audio-997: unittests/audio-cut-997.lo
unittests/fuse-audio-1: unittests/audio-cut-1.lo

$(AUDIO_TEST_BINARIES): $(fuse_OBJECTS) $(fuse_DEPENDENCIES) $(libfuselegacy_la_OBJECTS) $(libfuselegacy_la_DEPENDENCIES)
	$(AM_V_CCLD)objects=; for object in $(libfuselegacy_la_OBJECTS); do \
	  case $$object in sound.lo) ;; \
	    *) objects="$$objects $$object" ;; esac; \
	  done; \
	  interval=$@; interval=$${interval#unittests/fuse-audio-}; \
	  $(fuse_LINK) $(fuse_OBJECTS) $$objects unittests/audio-cut-$$interval.lo $(libfuselegacy_la_LIBADD) $(FUSE_APP_LIBS) $(LIBS)

check-sound-engine-equivalence: fuse$(EXEEXT) $(AUDIO_TEST_BINARIES)
	python3 $(srcdir)/unittests/audio-frame-equivalence.py \
	  ./unittests/fuse-audio-0 ./fuse$(EXEEXT) --interval-builds \
	  ./unittests/fuse-audio-35000 ./unittests/fuse-audio-17500 \
	  ./unittests/fuse-audio-8750 ./unittests/fuse-audio-997 \
	  ./unittests/fuse-audio-1

noinst_PROGRAMS += unittests/automation_json_test
unittests_automation_json_test_CPPFLAGS = $(AM_CPPFLAGS)
unittests_automation_json_test_SOURCES = \
	unittests/automation_json.c \
	automation/json.c \
	automation/json.h
endif

unittests_displaytest_SOURCES = \
        unittests/displaytest.c \
        display.c \
        display_border.c \
        display_dirty.c \
        display_internal.h \
        display_render.c
unittests_displaytest_LDADD = $(LIBSPECTRUM_LIBS) $(GLIB_LIBS)
unittests_displaytest_CPPFLAGS = $(AM_CPPFLAGS) -DDISPLAYTEST

unittests_sdl2displaytest_SOURCES = \
        unittests/sdl2displaytest.c \
        ui/sdl2/sdl2_display_internal.c
unittests_sdl2displaytest_LDADD = $(LIBSPECTRUM_LIBS)
unittests_sdl2displaytest_CPPFLAGS = $(AM_CPPFLAGS)

unittests_sdl2scalerstatetest_SOURCES = \
        unittests/sdl2scalerstatetest.c \
        ui/sdl2/sdl2_scaler_state.c
unittests_sdl2scalerstatetest_LDADD = $(LIBSPECTRUM_LIBS)
unittests_sdl2scalerstatetest_CPPFLAGS = $(AM_CPPFLAGS)

unittests_sdl2joysticktest_SOURCES = \
        unittests/sdl2joysticktest.c \
        ui/sdl2/sdl2_joystick_internal.c
unittests_sdl2joysticktest_LDADD = $(LIBSPECTRUM_LIBS)
unittests_sdl2joysticktest_CPPFLAGS = $(AM_CPPFLAGS)

unittests_sdl2mousetest_SOURCES = \
        unittests/sdl2mousetest.c \
        ui/sdl2/sdl2_mouse_internal.c
unittests_sdl2mousetest_LDADD = $(LIBSPECTRUM_LIBS)
unittests_sdl2mousetest_CPPFLAGS = $(AM_CPPFLAGS)

unittests_palcompositetest_SOURCES = \
        unittests/palcompositetest.c \
        ui/scaler/snes_ntsc.c
unittests_palcompositetest_LDADD = $(LIBSPECTRUM_LIBS) \
        ui/scaler/scalers16.lo ui/scaler/scalers32.lo
unittests_palcompositetest_CPPFLAGS = $(AM_CPPFLAGS)

unittests_scalerexpandtest_SOURCES = \
        unittests/scalerexpandtest.c \
        ui/scaler/scaler.c
unittests_scalerexpandtest_LDADD = $(LIBSPECTRUM_LIBS) \
        ui/scaler/scalers16.lo ui/scaler/scalers32.lo ui/scaler/snes_ntsc.o
unittests_scalerexpandtest_CPPFLAGS = $(AM_CPPFLAGS)

else
check-startup-ownership:
	@echo "SKIP: startup ownership tests require a null-UI build"
endif

# A compile-time-instrumented source consumer; production has no failure switch.
unittests_startupunwindtest_SOURCES = unittests/startupunwindtest.c \
                                    $(libfuselegacy_la_SOURCES)
unittests_startupunwindtest_CPPFLAGS = $(AM_CPPFLAGS) -DFUSE_STARTUP_TEST
unittests_startupunwindtest_LDADD = $(libfuselegacy_la_LIBADD)
unittests_startupunwindtest_DEPENDENCIES = $(libfuselegacy_la_DEPENDENCIES)
$(unittests_startupunwindtest_OBJECTS): config.h
.PHONY: check-startup-unwind

if UI_NULL
if ENABLE_AUTOMATION
check_PROGRAMS += unittests/startupunwindtest
LIFECYCLE_CHECK_TARGETS += check-startup-unwind
check-startup-unwind: unittests/startupunwindtest
	@set -e; program="$(abs_top_builddir)/unittests/startupunwindtest$(EXEEXT)"; \
	  rom="$(abs_top_srcdir)/roms/48.rom"; \
	  tmp=$$(mktemp -d); trap 'rm -rf "$$tmp"' 0 1 2 3 15; \
	  cd "$$tmp"; export HOME=. XDG_CONFIG_HOME=. USERPROFILE=. WINDIR=. APPDATA=.; \
	  run() { kind=$$1; target=$$2; shift 2; status=0; \
	    "$$program" "$$kind" "$$target" "$$rom" "$$@" > /dev/null || status=$$?; \
	    if test "$$status" -ne 0 && test "$$status" -ne 77; then \
	      echo "FAILED: startup unwind $$kind $$target ($$status)" >&2; return 1; \
	    fi; }; \
	  count=$$("$$program" count); module=0; \
	  while test $$module -lt $$count; do \
	    run before $$module; run after $$module; module=$$((module + 1)); \
	  done; \
	  count=$$("$$program" models none "$$rom" | tail -n 1); test -n "$$count"; \
	  test "$$count" -gt 0; model=1; \
	  while test $$model -le $$count; do \
	    run point machine.model $$model; model=$$((model + 1)); \
	  done; \
	  for point in display.ui display.ready machine.selected startup.files \
	    sound.backend sound.left sound.right sound.mixer.buffers sound.samples; do \
	    run point $$point; \
	  done; \
	  run media none; run record none; \
	  for failure in rom scaler snapshot; do run natural $$failure; done; \
	  test -z "$$(find . -type f \( -name fuserc -o -name fuse.cfg \) -print)"; \
	  echo 'Manager acquisition/reverse-unwind, partial setup and repeated shutdown passed'
else
check-startup-unwind:
	@echo "SKIP: startup unwind matrix requires automation"
endif
else
check-startup-unwind:
	@echo "SKIP: startup unwind matrix requires a null-UI build"
endif
