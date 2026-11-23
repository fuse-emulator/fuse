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
unittests_legacyenginelinktest_SOURCES = unittests/legacyenginetest.c \
                                       unittests/hostpreparetest.c \
                                       unittests/hostoperationstest.c \
                                       unittests/snapshotfiletest.c \
                                       unittests/filesystemtest.c \
                                       unittests/legacylifecycletest.c
unittests_legacyenginelinktest_LDADD = libfuselegacy.la
unittests_legacyenginesourcetest_SOURCES = unittests/legacyenginetest.c \
                                        unittests/hostpreparetest.c \
                                        unittests/hostoperationstest.c \
                                        unittests/snapshotfiletest.c \
                                        unittests/filesystemtest.c \
                                        unittests/legacylifecycletest.c \
                                        $(libfuselegacy_la_SOURCES)
unittests_legacyenginesourcetest_CPPFLAGS = $(AM_CPPFLAGS)
unittests_legacyenginesourcetest_LDADD = $(libfuselegacy_la_LIBADD)
unittests_legacyenginesourcetest_DEPENDENCIES = $(libfuselegacy_la_DEPENDENCIES)
$(unittests_legacyenginesourcetest_OBJECTS): config.h

# Exercise the original common UI helper with selected-service doubles; the
# normal engine archive supplies the real tape implementation and teardown.
check_PROGRAMS += unittests/tapesaveuitest
unittests_tapesaveuitest_SOURCES = unittests/tapesaveuitest.c unittests/tapesaveui.c
unittests_tapesaveuitest_LDADD = libfuselegacy.la
LIFECYCLE_CHECK_TARGETS += check-tape-save-ui
.PHONY: check-tape-save-ui
check-tape-save-ui: unittests/tapesaveuitest
	@set -e; program="$(abs_top_builddir)/unittests/tapesaveuitest$(EXEEXT)"; \
	  rom="$(abs_top_srcdir)/roms/48.rom"; \
	  tmp=$$(mktemp -d); trap 'rm -rf "$$tmp"' 0 1 2 3 15; \
	  cd "$$tmp"; export HOME=. XDG_CONFIG_HOME=. USERPROFILE=. WINDIR=. APPDATA=.; \
	  "$$program" "$$rom"; test -z "$$(find . -type f -print)"

# Real recorder with controlled append failures and selected-menu reentry.
check_PROGRAMS += unittests/taperecordtest
unittests_taperecordtest_SOURCES = unittests/taperecordtest.c
unittests_taperecordtest_LDADD = libfuselegacy.la
LIFECYCLE_CHECK_TARGETS += check-tape-recording
.PHONY: check-tape-recording
check-tape-recording: unittests/taperecordtest
	@set -e; program="$(abs_top_builddir)/unittests/taperecordtest$(EXEEXT)"; \
	  tmp=$$(mktemp -d); trap 'rm -rf "$$tmp"' 0 1 2 3 15; \
	  cd "$$tmp"; export HOME=. XDG_CONFIG_HOME=. USERPROFILE=. WINDIR=. APPDATA=.; \
	  "$$program"; test -z "$$(find . -type f -print)"

# Shared floppy save mechanics with controlled chooser/confirmation/writer services.
check_PROGRAMS += unittests/floppysaveuitest unittests/floppysavelegacytest
unittests_floppysaveuitest_SOURCES = unittests/floppysaveuitest.c unittests/floppysaveui.c
unittests_floppysaveuitest_CPPFLAGS = $(AM_CPPFLAGS)
unittests_floppysaveuitest_LDADD = libfuselegacy.la
unittests_floppysavelegacytest_SOURCES = $(unittests_floppysaveuitest_SOURCES)
unittests_floppysavelegacytest_CPPFLAGS = $(AM_CPPFLAGS) -DFUSE_MEDIA_SAVE_BASELINE
unittests_floppysavelegacytest_LDADD = libfuselegacy.la
LIFECYCLE_CHECK_TARGETS += check-floppy-save-ui
.PHONY: check-floppy-save-ui
check-floppy-save-ui: unittests/floppysaveuitest unittests/floppysavelegacytest
	@set -e; root="$(abs_top_builddir)/unittests"; rom="$(abs_top_srcdir)/roms/48.rom"; \
	  tmp=$$(mktemp -d); trap 'rm -rf "$$tmp"' 0 1 2 3 15; \
	  cd "$$tmp"; export HOME=. XDG_CONFIG_HOME=. USERPROFILE=. WINDIR=. APPDATA=.; \
	  for mode in nested unpaused; do \
	    if test "$$mode" = unpaused; then set -- --unpaused; else set --; fi; \
	    "$$root/floppysaveuitest$(EXEEXT)" "$$rom" "$$@" > target.out 2> target.err; \
	    "$$root/floppysavelegacytest$(EXEEXT)" "$$rom" "$$@" > legacy.out 2> legacy.err; \
	    cmp target.out legacy.out; cmp target.err legacy.err; \
	    rm target.out target.err legacy.out legacy.err; \
	    "$$root/floppysaveuitest$(EXEEXT)" "$$rom" "$$@" --lifecycle > target.out 2> target.err; \
	    "$$root/floppysavelegacytest$(EXEEXT)" "$$rom" "$$@" --lifecycle > legacy.out 2> legacy.err; \
	    cmp target.out legacy.out; cmp target.err legacy.err; \
	    rm target.out target.err legacy.out legacy.err; \
	    "$$root/floppysaveuitest$(EXEEXT)" "$$rom" "$$@" --observations; \
	    "$$root/floppysavelegacytest$(EXEEXT)" "$$rom" "$$@" --observations; \
	  done; \
	  test -z "$$(find . -type f -print)"; \
	  echo 'Shared floppy target/chooser saves match; choices, failure retention and pauses passed'

noinst_HEADERS += hacking/memory-filesystem.h

# A separate own-main consumer: no legacy CLI/test dispatcher or core globals.
check_PROGRAMS += unittests/nullhostlink unittests/nullhostsource
unittests_nullhostlink_SOURCES = hacking/null-host.c
unittests_nullhostlink_LDADD = libfuselegacy.la
unittests_nullhostsource_SOURCES = hacking/null-host.c $(libfuselegacy_la_SOURCES)
unittests_nullhostsource_CPPFLAGS = $(AM_CPPFLAGS)
unittests_nullhostsource_LDADD = $(libfuselegacy_la_LIBADD)
unittests_nullhostsource_DEPENDENCIES = $(libfuselegacy_la_DEPENDENCIES)
$(unittests_nullhostsource_OBJECTS): config.h
LIFECYCLE_CHECK_TARGETS += check-null-host
.PHONY: check-null-host

check-null-host: unittests/nullhostlink unittests/nullhostsource unittests/legacyenginelinktest
	@set -e; root="$(abs_top_builddir)/unittests"; roms="$(abs_top_srcdir)/roms"; \
	  tmp=$$(mktemp -d); trap 'rm -rf "$$tmp"' 0 1 2 3 15; \
	  cd "$$tmp"; export HOME=. XDG_CONFIG_HOME=config-blocker USERPROFILE=. WINDIR=. APPDATA=.; \
	  printf 'not a configuration directory\n' > config-blocker; \
	  mkdir empty invalid only48; printf x > invalid/48.rom; \
	  printf '\002\000\377\377' > sample.tap; printf x > malformed.tap; printf x > malformed.udi; \
	  printf 'not a TZX header\n' > malformed.tzx; \
	  printf 'NCustom\nZ 8 32769 256 0\nY\n\000' > sample.pok; \
	  printf '\022\064\000\377' > sample.bin; \
	  "$$root/legacyenginelinktest$(EXEEXT)" --host-operations-test disk-fixture "$$roms"; \
	  "$$root/legacyenginelinktest$(EXEEXT)" --host-operations-test machine-button-fixture "$$roms"; \
	  mkdir button-roms; cp "$$roms/48.rom" button-roms/; \
	  dd if=/dev/zero of=button-roms/mf1.rom bs=8192 count=1 2>/dev/null; \
	  "$$root/legacyenginelinktest$(EXEEXT)" --host-operations-test rzx-joystick-fixture "$$roms"; \
	  "$$root/legacyenginelinktest$(EXEEXT)" --host-operations-test snapshot-joystick-fixture "$$roms"; \
	  cp "$$roms/48.rom" 48.rom; cp "$$roms/48.rom" only48/48.rom; \
	  mkdir interface1; cp 48.rom interface1/48.rom; \
	  dd if=/dev/zero of=interface1/if1-2.rom bs=8192 count=1 2>/dev/null; \
	  printf '%s\n' 'host: prepared 48' \
	    'host: driving execution and handling input between turns' \
	    'host: holding execution; delivering Space and joystick 1 fire' \
	    'host: resuming execution' 'host: focus lost; inputs released' \
	    'host: completed relative 997-T-state budget' 'host: reset 48' \
	    'host: selected 128' \
	    'host: shutdown; frame turns=5; budget turns=1; ROM requests=4; releases=4' > expected; \
	  printf '%s\n' 'host: prepared 48' \
	    'host: keyboard read saw queued Space press; next frame drained release' \
	    'host: shutdown; frame turns=2; budget turns=0; ROM requests=1; releases=1' > expected-queued-input; \
	  printf '%s\n' 'host: prepared 48' \
	    'host: manual debugger break at PC 0000' \
	    'host: debugger stopped at PC 0000' \
	    'host: one instruction stepped; PC 0001 after budget boundary' \
	    'host: Continue reached the same breakpoint again' \
	    'host: copied debugger panes survived Continue and breakpoint deletion' \
	    'host: shutdown; frame turns=0; budget turns=1; ROM requests=1; releases=1' > expected-debugger-step; \
	  printf '%s\n' 'host: prepared 48' \
	    'host: write watchpoint at 8000; stopped at PC 0005' \
	    'host: read watchpoint at 8000; stopped at PC 000a with A=42' \
	    'host: Continue honored the execute breakpoint at PC 000a' \
	    'host: shutdown; frame turns=0; budget turns=1; ROM requests=1; releases=1' > expected-debugger-watchpoint; \
	  printf '%s\n' 'host: prepared 48' \
	    'host: parked for eject; handled focus loss; checked cancel, save and stale replies' \
	    'host: shutdown; frame turns=4; budget turns=0; ROM requests=1; releases=1' > expected-parked-eject; \
	  printf '%s\n' 'host: prepared 48' \
	    'host: tape replacement checked read, cancel, save and decode failures' \
	    'host: shutdown; frame turns=2; budget turns=0; ROM requests=1; releases=1' > expected-tape-replacement; \
	  printf '%s\n' 'host: prepared 48' 'host: inserted tape; deck stopped' \
	    'host: shutdown; frame turns=5; budget turns=0; ROM requests=1; releases=1' > expected-tape; \
	  printf '%s\n' 'host: prepared 48' 'host: File Open returned legacy success' \
	    'host: shutdown; frame turns=5; budget turns=0; ROM requests=2; releases=2' > expected-open; \
	  printf '%s\n' 'host: prepared 48' \
	    'host: snapshot joystick Sinclair 1; map to keyboard' \
	    'host: snapshot restored after joystick decision' \
	    'host: shutdown; frame turns=5; budget turns=0; ROM requests=2; releases=2' > expected-open-joystick; \
	  printf '%s\n' 'host: prepared 48' \
	    'host: playing tape copy; present=1; playing=1; recording=0; modified=0; current=0; blocks=1' \
	    'host: tape row 0; Standard Speed Data; 2 bytes' \
	    'host: saved tape copy; present=1; playing=0; recording=0; modified=0; current=0; blocks=1' \
	    'host: tape row 0; Standard Speed Data; 2 bytes' \
	    'host: ejected tape copy; present=0; playing=0; recording=0; modified=0; current=-1; blocks=0' \
	    'host: tape deck controls; saved and ejected' \
	    'host: retained tape copy after shutdown; present=1; playing=0; recording=0; modified=0; current=0; blocks=1' \
	    'host: tape row 0; Standard Speed Data; 2 bytes' \
	    'host: shutdown; frame turns=2; budget turns=0; ROM requests=1; releases=1' > expected-tape-life; \
	  printf '%s\n' 'host: prepared 48' \
	    'host: recording tape copy; present=0; playing=0; recording=1; modified=0; current=-1; blocks=0' \
	    'host: saved recorded tape copy; present=1; playing=0; recording=0; modified=0; current=0; blocks=1' \
	    'host: tape row 0; Pause; 0 ms' \
	    'host: recorded two frames; saved and ejected tape' \
	    'host: retained tape copy after shutdown; present=1; playing=0; recording=0; modified=0; current=0; blocks=1' \
	    'host: tape row 0; Pause; 0 ms' \
	    'host: shutdown; frame turns=2; budget turns=0; ROM requests=1; releases=1' > expected-record; \
	  printf '%s\n' 'host: prepared 48' 'host: saved snapshot; reset and reopened' \
	    'host: shutdown; frame turns=3; budget turns=0; ROM requests=3; releases=3' > expected-snapshot; \
	  printf '%s\n' 'host: prepared 48' 'host: effective filesystem; tape/snapshot roundtrip; ZXCF lifecycle; handles balanced' \
	    'host: shutdown; frame turns=3; budget turns=0; ROM requests=3; releases=3' > expected-filesystem; \
	  printf '%s\n' 'host: prepared plus3' 'host: saved disk through plus3 drive 0' \
	    'host: shutdown; frame turns=0; budget turns=0; ROM requests=4; releases=4' > expected-disk; \
	  printf '%s\n' 'host: prepared plus3' \
	    'host: new disk copy; controller=0; slot=0; name=+3 Disk A:; available=1; enabled=1; loaded=1; flipped=0; protected=0; target=(none)' \
	    'host: controls copy; controller=0; slot=0; name=+3 Disk A:; available=1; enabled=1; loaded=1; flipped=1; protected=1; target=(none)' \
	    'host: ejected drive copy; controller=0; slot=0; name=+3 Disk A:; available=1; enabled=1; loaded=0; flipped=0; protected=1; target=(none)' \
	    'host: new disk; flip/WP controls; saved and ejected plus3 drive 0' \
	    'host: retained copy after shutdown; controller=0; slot=0; name=+3 Disk A:; available=1; enabled=1; loaded=1; flipped=0; protected=0; target=lifecycle.udi' \
	    'host: shutdown; frame turns=0; budget turns=0; ROM requests=4; releases=4' > expected-lifecycle; \
	  before=$$(find . -type f -exec cksum {} \; | sort); directories=$$(find . -type d | sort); \
	  for consumer in nullhostlink nullhostsource; do \
	    program="$$root/$$consumer$(EXEEXT)"; \
	    "$$program" "$$roms" > "$$consumer-valid.out" 2> "$$consumer-valid.err"; \
	    cmp expected "$$consumer-valid.out"; \
	    "$$program" empty --queued-input > "$$consumer-queued-input.out" 2> "$$consumer-queued-input.err"; \
	    cmp expected-queued-input "$$consumer-queued-input.out"; \
	    "$$program" empty --debugger-step > "$$consumer-debugger-step.out" 2> "$$consumer-debugger-step.err"; \
	    cmp expected-debugger-step "$$consumer-debugger-step.out"; \
	    "$$program" empty --debugger-watchpoint > "$$consumer-debugger-watchpoint.out" 2> "$$consumer-debugger-watchpoint.err"; \
	    cmp expected-debugger-watchpoint "$$consumer-debugger-watchpoint.out"; \
	    "$$program" only48 --parked-tape-eject parked.pzx > "$$consumer-parked.out" 2> "$$consumer-parked.err"; \
	    cmp expected-parked-eject "$$consumer-parked.out"; rm parked.pzx; \
	    "$$program" only48 --replace-tape sample.tap malformed.tzx > "$$consumer-replacement.out" 2> "$$consumer-replacement.err"; \
	    cmp expected-tape-replacement "$$consumer-replacement.out"; \
	    "$$program" only48 sample.tap > "$$consumer-tape.out" 2> "$$consumer-tape.err"; \
	    cmp expected-tape "$$consumer-tape.out"; \
	    "$$program" only48 --open sample.tap > "$$consumer-open.out" 2> "$$consumer-open.err"; \
	    cmp expected-open "$$consumer-open.out"; \
	    "$$program" only48 --diagnostics missing-diagnostic-input.tap > "$$consumer-diagnostics.out" 2> "$$consumer-diagnostics.err"; \
	    grep -F 'host: retained Open diagnostic:' "$$consumer-diagnostics.out" | grep -F 'missing-diagnostic-input.tap' > /dev/null; \
	    "$$program" only48 --open-pok sample.pok > "$$consumer-pok.out" 2> "$$consumer-pok.err"; \
	    grep -Fx 'host: enabled and disabled POK trainer Custom' "$$consumer-pok.out" > /dev/null; \
	    "$$program" only48 --binary-roundtrip sample.bin "$$consumer.bin" > "$$consumer-binary.out" 2> "$$consumer-binary.err"; \
	    grep -Fx 'host: RAM at 8000: 12 34 00 ff' "$$consumer-binary.out" > /dev/null; \
	    cmp sample.bin "$$consumer.bin"; rm "$$consumer.bin"; \
	    printf '\377' > descending.bin; \
	    for input in sample.bin descending.bin; do \
	      "$$program" only48 --poke-finder "$$input" > "$$consumer-poke-finder.out" 2> "$$consumer-poke-finder.err"; \
	      grep -Fx 'host: Poke Finder matched bank 2 offset 0000; trainer restored RAM' "$$consumer-poke-finder.out" > /dev/null; \
	    done; rm descending.bin; \
	    "$$program" only48 --open joystick.szx > "$$consumer-open-joystick.out" 2> "$$consumer-open-joystick.err"; \
	    cmp expected-open-joystick "$$consumer-open-joystick.out"; \
	    "$$program" only48 --open joystick.rzx > "$$consumer-open-rzx.out" 2> "$$consumer-open-rzx.err"; \
	    grep -Fx 'host: snapshot joystick Sinclair 1; map to keyboard' "$$consumer-open-rzx.out" > /dev/null; \
	    "$$program" only48 --rzx-to-movie joystick.rzx "$$consumer.fmf" > "$$consumer-rzx-movie.out" 2> "$$consumer-rzx-movie.err"; \
	    grep -Fx 'host: snapshot joystick Sinclair 1; map to keyboard' "$$consumer-rzx-movie.out" > /dev/null; \
	    test "$$(dd if="$$consumer.fmf" bs=6 count=1 2>/dev/null)" = FMF_V1; rm "$$consumer.fmf"; \
	    "$$program" only48 --record-rzx "$$consumer.rzx" > "$$consumer-record-rzx.out" 2> "$$consumer-record-rzx.err"; \
	    grep -Fx 'host: recorded, finalised and reopened RZX' "$$consumer-record-rzx.out" > /dev/null; \
	    test -s "$$consumer.rzx"; rm "$$consumer.rzx"; \
	    "$$program" interface1 --microdrive "$$consumer.mdr" > "$$consumer-microdrive.out" 2> "$$consumer-microdrive.err"; \
	    grep -Fx 'host: Microdrive 2 kept on Cancel and saved on Quit' "$$consumer-microdrive.out" > /dev/null; \
	    : > serial-rx.dat; : > serial-tx.dat; \
	    "$$program" interface1 --rs232 serial-rx.dat serial-tx.dat > "$$consumer-rs232.out"; \
	    grep -Ex 'host: RS232 (RX and TX connected and disconnected independently|unavailable on Win32)' "$$consumer-rs232.out" > /dev/null; \
	    rm serial-rx.dat serial-tx.dat; \
	    "$$program" button-roms --machine-button multiface button-multiface.szx > "$$consumer-button.out"; \
	    grep -Fx 'host: queued and ran multiface button' "$$consumer-button.out" > /dev/null; \
	    for format in scr mlt png; do \
	      "$$program" "$$roms" --save-screen "$$format" "$$consumer-screen.$$format" > "$$consumer-screen.out"; \
	      if grep -Fx "host: saved $$format screenshot" "$$consumer-screen.out"; then \
	        test -s "$$consumer-screen.$$format"; rm "$$consumer-screen.$$format"; \
	      else \
	        test "$$format" = png; grep -Fx 'host: PNG screenshots unavailable' "$$consumer-screen.out"; \
	        test ! -e "$$consumer-screen.png"; \
	      fi; \
	    done; \
	    test -s "$$consumer.mdr"; rm "$$consumer.mdr"; \
	    "$$program" only48 --open-over-tape sample.tap > "$$consumer-open-tape.out" 2> "$$consumer-open-tape.err"; \
	    grep -Fx 'host: modified tape; discard' "$$consumer-open-tape.out" > /dev/null; \
	    "$$program" "$$roms" --open-over-floppy sample.udi > "$$consumer-open-floppy.out" 2> "$$consumer-open-floppy.err"; \
	    grep -Ex 'host: modified floppy controller=[0-9]+ slot=0; Save needs a path; discard' "$$consumer-open-floppy.out" > /dev/null; \
	    "$$program" only48 --tape-lifecycle sample.tap tape-life.tzx \
	      > "$$consumer-tape-life.out" 2> "$$consumer-tape-life.err"; \
	    cmp expected-tape-life "$$consumer-tape-life.out"; \
	    "$$program" only48 --tape-lifecycle tape-life.tzx tape-roundtrip.tap \
	      > "$$consumer-tape-roundtrip.out" 2> "$$consumer-tape-roundtrip.err"; \
	    cmp expected-tape-life "$$consumer-tape-roundtrip.out"; cmp sample.tap tape-roundtrip.tap; \
	    rm tape-life.tzx tape-roundtrip.tap; \
	    "$$program" only48 --filesystem sample.tap > "$$consumer-filesystem.out" 2> "$$consumer-filesystem.err"; \
	    cmp expected-filesystem "$$consumer-filesystem.out"; \
	    { printf 'RS-IDE\032\021\000\200\000'; printf '\000%.0s' 1 2 3 4 5 6 7 8 9 10 11 12 13; \
	      printf '\040\000\000\000\001\000\000\000\000\000\040'; \
	      dd if=/dev/zero bs=1 count=93 2>/dev/null; dd if=/dev/zero bs=512 count=1024 2>/dev/null; } > divmmc.hdf; \
	    "$$program" only48 --divmmc divmmc.hdf > "$$consumer-divmmc.out" 2> "$$consumer-divmmc.err"; \
	    printf '%s\n' 'host: prepared 48' 'host: DivMMC lifecycle; handles balanced' \
	      'host: shutdown; frame turns=0; budget turns=0; ROM requests=1; releases=1' > expected-divmmc; \
	    cmp expected-divmmc "$$consumer-divmmc.out"; rm divmmc.hdf expected-divmmc; \
	    if "$$program" only48 --filesystem missing.tap > "$$consumer-filesystem-fail.out" 2> "$$consumer-filesystem-fail.err"; \
	    then exit 1; else result=$$?; test "$$result" -eq 1; fi; \
	    test -s "$$consumer-filesystem-fail.err"; \
	    for target in saved.szx fallback.output; do \
	      printf 'old target' > "$$target"; \
	      "$$program" only48 --save-snapshot "$$target" > "$$consumer-snapshot-$$target.out" 2> "$$consumer-snapshot-$$target.err"; \
	      cmp expected-snapshot "$$consumer-snapshot-$$target.out"; rm "$$target"; \
	    done; \
	    if "$$program" only48 --save-snapshot missing-directory/failed.szx \
	      > "$$consumer-snapshot-fail.out" 2> "$$consumer-snapshot-fail.err"; \
	    then exit 1; else result=$$?; test "$$result" -eq 1; fi; \
	    test -s "$$consumer-snapshot-fail.err"; \
	    grep -Fx 'host: stopped; frame turns=2; budget turns=0; ROM requests=1; releases=1' \
	      "$$consumer-snapshot-fail.out" > /dev/null; \
	    "$$program" only48 --record-tape recorded.pzx > "$$consumer-record.out" 2> "$$consumer-record.err"; \
	    cmp expected-record "$$consumer-record.out"; \
	    "$$root/legacyenginelinktest$(EXEEXT)" --host-operations-test recording-readback "$$roms"; \
	    rm recorded.pzx; \
	    if "$$program" only48 --record-tape missing-directory/recorded.pzx \
	      > "$$consumer-record-fail.out" 2> "$$consumer-record-fail.err"; \
	    then exit 1; else result=$$?; test "$$result" -eq 1; fi; \
	    test -s "$$consumer-record-fail.err"; \
	    grep -Fx 'host: stopped; frame turns=2; budget turns=0; ROM requests=1; releases=1' \
	      "$$consumer-record-fail.out" > /dev/null; \
	    "$$program" "$$roms" --save-disk sample.udi "$$consumer-copy.udi" \
	      > "$$consumer-disk.out" 2> "$$consumer-disk.err"; \
	    cmp expected-disk "$$consumer-disk.out"; cmp sample.udi "$$consumer-copy.udi"; \
	    rm "$$consumer-copy.udi"; \
	    "$$program" "$$roms" --disk-lifecycle lifecycle.udi \
	      > "$$consumer-lifecycle.out" 2> "$$consumer-lifecycle.err"; \
	    cmp expected-lifecycle "$$consumer-lifecycle.out"; \
	    "$$root/legacyenginelinktest$(EXEEXT)" --host-operations-test disk-lifecycle-readback "$$roms" \
	      > "$$consumer-lifecycle-check.out" 2> "$$consumer-lifecycle-check.err"; \
	    rm lifecycle.udi; \
	    printf 'old output\n' > existing.udi; \
	    "$$program" "$$roms" --save-disk sample.udi existing.udi \
	      > "$$consumer-disk-overwrite.out" 2> "$$consumer-disk-overwrite.err"; \
	    cmp expected-disk "$$consumer-disk-overwrite.out"; cmp sample.udi existing.udi; \
	    rm existing.udi; \
	    for file in missing.udi malformed.udi sample.udi; do \
	      if "$$program" "$$roms" --save-disk "$$file" missing-directory/out.udi \
	        > "$$consumer-disk-$$file.out" 2> "$$consumer-disk-$$file.err"; \
	      then exit 1; else result=$$?; test "$$result" -eq 1; fi; \
	      test -s "$$consumer-disk-$$file.err"; \
	      grep -Fx 'host: stopped; frame turns=0; budget turns=0; ROM requests=4; releases=4' \
	        "$$consumer-disk-$$file.out" > /dev/null; \
	    done; \
	    for file in missing.tap malformed.tap; do \
	      if "$$program" only48 --open "$$file" > "$$consumer-open-$$file.out" 2> "$$consumer-open-$$file.err"; \
	      then exit 1; else result=$$?; test "$$result" -eq 1; fi; \
	      test -s "$$consumer-open-$$file.err"; \
	      grep -Fx 'host: stopped; frame turns=0; budget turns=0; ROM requests=1; releases=1' \
	        "$$consumer-open-$$file.out" > /dev/null; \
	    done; \
	    for tape in missing.tap malformed.tap; do \
	      if "$$program" only48 "$$tape" > "$$consumer-$$tape.out" 2> "$$consumer-$$tape.err"; \
	      then exit 1; else result=$$?; test "$$result" -eq 1; fi; \
	      test -s "$$consumer-$$tape.err"; \
	      grep -Fx 'host: stopped; frame turns=0; budget turns=0; ROM requests=1; releases=1' \
	        "$$consumer-$$tape.out" > /dev/null; \
	    done; \
	    for mode in empty invalid only48; do \
	      if "$$program" "$$mode" > "$$consumer-$$mode.out" 2> "$$consumer-$$mode.err"; \
	      then exit 1; else result=$$?; test "$$result" -eq 1; fi; \
	      test -s "$$consumer-$$mode.err"; \
	    done; \
	    for mode in empty invalid; do \
	      grep -Fx 'host: stopped; frame turns=0; budget turns=0; ROM requests=1; releases=1' \
	        "$$consumer-$$mode.out" > /dev/null; \
	    done; \
	    grep -Fx 'host: stopped; frame turns=3; budget turns=1; ROM requests=3; releases=3' \
	      "$$consumer-only48.out" > /dev/null; \
	    if "$$program" > "$$consumer-usage.out" 2> "$$consumer-usage.err"; \
	    then exit 1; else result=$$?; test "$$result" -eq 1; fi; \
	    test -s "$$consumer-usage.err"; \
	  done; \
	  test "$$directories" = "$$(find . -type d | sort)"; \
	  test "$$before" = "$$(find . -type f ! -name '*.out' ! -name '*.err' -exec cksum {} \; | sort)"; \
	  echo 'Direct null-backed host passed for archive/source consumers and ROM/media failures'

LIFECYCLE_CHECK_TARGETS += check-legacy-engine-link check-startup-ownership \
                           check-legacy-lifecycle check-host-prepare
.PHONY: check-legacy-lifecycle check-host-prepare check-host-operations
LIFECYCLE_CHECK_TARGETS += check-host-operations check-snapshot-file
.PHONY: check-snapshot-file

check-snapshot-file: unittests/legacyenginelinktest unittests/legacyenginesourcetest
	@set -e; root="$(abs_top_builddir)/unittests"; \
	  rom="$(abs_top_srcdir)/roms/48.rom"; \
	  tmp=$$(mktemp -d); trap 'rm -rf "$$tmp"' 0 1 2 3 15; \
	  cd "$$tmp"; export HOME=. XDG_CONFIG_HOME=. USERPROFILE=. WINDIR=. APPDATA=.; \
	  for consumer in legacyenginelinktest legacyenginesourcetest; do \
	    "$$root/$$consumer$(EXEEXT)" --snapshot-file-test --machine 48 \
	      --rom-48 "$$rom" --no-sound --no-autosave-settings; \
	  done; \
	  test -z "$$(find . -type f -print)"; \
	  echo 'Host/legacy snapshot saves, formats, IO failures, pause and cleanup passed for both consumers'

check-host-operations: unittests/legacyenginelinktest unittests/legacyenginesourcetest
	@set -e; root="$(abs_top_builddir)/unittests"; \
	  roms="$(abs_top_srcdir)/roms"; \
	  tmp=$$(mktemp -d); trap 'rm -rf "$$tmp"' 0 1 2 3 15; \
	  cd "$$tmp"; export HOME=. XDG_CONFIG_HOME=. USERPROFILE=. WINDIR=. APPDATA=.; \
	  for consumer in legacyenginelinktest legacyenginesourcetest; do \
	    for mode in success filesystem-oracle filesystem tape-contract tape-info trapped-recording snapshot-roundtrip autoload-success autoload-failure \
	      open-auto open-noauto open-custom open-reset-failure open-snapshot open-snapshot-joystick open-fallback open-rzx-stop open-rzx-embedded open-rzx-external record-rzx record-rzx-restore record-rzx-rollback record-rzx-movie open-pok host-binary host-poke-finder open-floppy floppy-merge cartridge-lifecycle \
	      disk-save disk-save-beta microdrive rs232 machine-buttons screenshots host-quit host-quit-shutdown \
	      reset-failure select-failure peripheral-reset-failure peripheral-select-failure; do \
	      "$$root/$$consumer$(EXEEXT)" --host-operations-test "$$mode" "$$roms"; \
	    done; \
	    for kind in tape-deck tape-recording; do \
	      "$$root/$$consumer$(EXEEXT)" --host-operations-test "legacy-$$kind" "$$roms" > deck-legacy.out 2> deck-legacy.err; \
	      "$$root/$$consumer$(EXEEXT)" --host-operations-test "$$kind" "$$roms" > deck-host.out 2> deck-host.err; \
	      cmp deck-legacy.out deck-host.out; cmp deck-legacy.err deck-host.err; \
	    done; \
	    rm deck-legacy.out deck-host.out deck-legacy.err deck-host.err; \
	  done; \
	  test -z "$$(find . -type f -print)"; \
	  echo 'Private machine/input/tape operations passed for both consumers'

check-host-prepare: unittests/legacyenginelinktest unittests/legacyenginesourcetest
	@set -e; root="$(abs_top_builddir)/unittests"; \
	  rom="$(abs_top_srcdir)/roms/48.rom"; \
	  tmp=$$(mktemp -d); trap 'rm -rf "$$tmp"' 0 1 2 3 15; \
	  cd "$$tmp"; export HOME=. XDG_CONFIG_HOME=blocked USERPROFILE=. WINDIR=. APPDATA=.; \
	  printf 'not a directory\n' > blocked; cp "$$rom" 48.rom; \
	  before=$$(find . -type f -exec cksum {} \;); \
	  for consumer in legacyenginelinktest legacyenginesourcetest; do \
	    for mode in success custom missing bad-size error-bytes bad-provenance null-bytes \
	      joystick-0 joystick-1 joystick-2 joystick-3 joystick-4 joystick-5 joystick-6 joystick-7; do \
	      "$$root/$$consumer$(EXEEXT)" --host-prepare-test "$$mode" "$$rom"; \
	    done; \
	  done; \
	  test "$$before" = "$$(find . -type f -exec cksum {} \;)"; \
	  test -z "$$(find . -mindepth 1 -type d -print)"; \
	  export XDG_CONFIG_HOME=.; \
	  for consumer in legacyenginelinktest legacyenginesourcetest; do \
	    for mode in host-state legacy-state; do \
	      mkdir "$$consumer-$$mode"; cd "$$consumer-$$mode"; cp "$$rom" 48.rom; \
	      "$$root/$$consumer$(EXEEXT)" --host-prepare-test "$$mode" "$$rom" > /dev/null; \
	      cd ..; \
	    done; \
	    cmp "$$consumer-host-state/state.szx" "$$consumer-legacy-state/state.szx"; \
	  done; \
	  echo 'Private host configuration and ROM lifetimes passed for both consumers'

check-legacy-lifecycle: unittests/legacyenginelinktest unittests/legacyenginesourcetest
	@set -e; root="$(abs_top_builddir)/unittests"; \
	  rom="$(abs_top_srcdir)/roms/48.rom"; \
	  tmp=$$(mktemp -d); trap 'rm -rf "$$tmp"' 0 1 2 3 15; \
	  cd "$$tmp"; export HOME=. XDG_CONFIG_HOME=. USERPROFILE=. WINDIR=. APPDATA=.; \
	  for consumer in legacyenginelinktest legacyenginesourcetest; do \
	    mkdir "$$consumer"; cd "$$consumer"; program="$$root/$$consumer$(EXEEXT)"; \
	    for kind in arguments help version both failure; do \
	      "$$program" --lifecycle-test $$kind > /dev/null; \
	    done; \
	    "$$program" --lifecycle-test zero "$$rom" > /dev/null; \
	    for kind in frame-contract frame-shutdown host-pause budget-contract debugger-stop \
	                debugger-step debugger-step-frame debugger-other-stops breakpoint-action-removal; do \
	      "$$program" --lifecycle-test "$$kind" --machine 48 --rom-48 "$$rom" \
	        --no-autosave-settings --no-sound > /dev/null; \
	    done; \
	    "$$program" --lifecycle-test prepare --machine 48 --rom-48 "$$rom" \
	      --no-autosave-settings > /dev/null; \
	    $(LEGACY_LIFECYCLE_EXECUTION_CHECK) \
	    test -z "$$(find . -type f \( -name fuserc -o -name fuse.cfg \) -print)"; \
	    mkdir empty-preferences; cd empty-preferences; \
	    "$$program" --lifecycle-test core-preferences "$$rom" > /dev/null; \
	    "$$program" --lifecycle-test core-executed-preferences "$$rom" > /dev/null; \
	    "$$program" --lifecycle-test app-no-preferences "$$rom" > /dev/null; \
	    test -z "$$(find . -type f -print)"; cd ..; \
	    mkdir saved-preferences; cd saved-preferences; \
	    "$$program" --lifecycle-test app-preferences "$$rom" > /dev/null; \
	    test -n "$$(find . -type f -print)"; \
	    before=$$(find . -type f -exec cksum {} \;); \
	    "$$program" --lifecycle-test core-preferences "$$rom" > /dev/null; \
	    test "$$before" = "$$(find . -type f -exec cksum {} \;)"; \
	    "$$program" --lifecycle-test core-executed-preferences "$$rom" > /dev/null; \
	    test "$$before" = "$$(find . -type f -exec cksum {} \;)"; \
	    "$$program" --lifecycle-test app-no-preferences "$$rom" > /dev/null; \
	    test "$$before" = "$$(find . -type f -exec cksum {} \;)"; \
	    cd ../..; \
	  done; \
	  echo 'Private lifecycle, preference persistence and shutdown passed for both consumers'

# Keep every preference read/write inside an isolated child-process sandbox.
check-startup-ownership: unittests/startupownershiptest
	@set -e; program="$(abs_top_builddir)/unittests/startupownershiptest$(EXEEXT)"; \
	  tmp=$$(mktemp -d); trap 'rm -rf "$$tmp"' 0 1 2 3 15; \
	  cd "$$tmp"; export HOME=. XDG_CONFIG_HOME=. USERPROFILE=. WINDIR=. APPDATA=.; \
	  "$$program" storage; "$$program" no-save; \
	  "$$program" ide-unwind --no-sound --no-autosave-settings \
	    --simpleide-masterfile master.hdf --simpleide-slavefile missing.hdf; \
	  test -z "$$(find . -type f -print)"; \
	  "$$program" save; test -n "$$(find . -type f -print)"; \
	  before=$$(find . -type f -exec cksum {} \;); \
	  "$$program" help > /dev/null; "$$program" version > /dev/null; \
	  $(STARTUP_OWNERSHIP_FAILURE_CHECK) \
	  test "$$before" = "$$(find . -type f ! -path './output/*' -exec cksum {} \;)"; \
	  echo 'Startup storage, settings policy and early-runner ownership passed'

if ENABLE_AUTOMATION
LEGACY_LIFECYCLE_EXECUTION_CHECK = mkdir prepared-output; \
  "$$program" --lifecycle-test prepare --machine 48 --rom-48 "$$rom" \
  --no-autosave-settings --automation-output prepared-output --automation-frames 1 > /dev/null; \
  test -z "$$(find prepared-output -type f -print)"; mkdir output; \
  "$$program" --lifecycle-test run --machine 48 --rom-48 "$$rom" \
  --no-autosave-settings --automation-output output --automation-frames 1 > /dev/null; \
  mkdir failed-output; "$$program" --lifecycle-test run-failure --machine 48 \
  --rom-48 "$$rom" --no-autosave-settings --automation-output failed-output \
  --automation-max-frames 1 --automation-success-pc 65535 > /dev/null; \
  mkdir frame-reference; "$$program" --lifecycle-test run --machine 48 \
  --rom-48 "$$rom" --no-autosave-settings --automation-output frame-reference \
  --automation-frames 10 > /dev/null; \
  for mode in frames frames-prefix budgets budgets-wide budgets-mixed budgets-prefix; do mkdir "$$mode"; \
    "$$program" --lifecycle-test "$$mode" --machine 48 --rom-48 "$$rom" \
    --no-autosave-settings --automation-output "$$mode" --automation-frames 10 > /dev/null; \
    cmp frame-reference/result.json "$$mode/result.json"; \
  done; \
  mkdir frame-failure; "$$program" --lifecycle-test frames-failure --machine 48 \
  --rom-48 "$$rom" --no-autosave-settings --automation-output frame-failure \
  --automation-max-frames 1 --automation-success-pc 65535 > /dev/null; \
  cmp failed-output/result.json frame-failure/result.json; \
  mkdir budget-failure; "$$program" --lifecycle-test budgets-failure --machine 48 \
  --rom-48 "$$rom" --no-autosave-settings --automation-output budget-failure \
  --automation-max-frames 1 --automation-success-pc 65535 > /dev/null; \
  cmp failed-output/result.json budget-failure/result.json; \
  "$$program" --lifecycle-test budget-rzx-fixture "$$rom" budget-fixture.rzx > /dev/null; \
  for mode in run frames budgets-rzx budgets-wide budgets-mixed; do mkdir "rzx-$$mode"; \
    "$$program" --lifecycle-test "$$mode" --machine 48 --rom-48 "$$rom" \
    --no-autosave-settings --no-sound --playback budget-fixture.rzx \
    --automation-output "rzx-$$mode" --automation-until-rzx-end \
    --automation-max-frames 100 > /dev/null; \
    cmp rzx-run/result.json "rzx-$$mode/result.json"; \
  done;
STARTUP_OWNERSHIP_FAILURE_CHECK = "$$program" settings-failure; \
  mkdir output; "$$program" session --machine 48 \
  --rom-48 "$(abs_top_srcdir)/roms/48.rom" \
  --automation-output output --automation-frames 1 > /dev/null;
else
LEGACY_LIFECYCLE_EXECUTION_CHECK =
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
	  for point in none display.ready machine.selected; do \
	    "$$program" host-ui "$$point" "$$rom" > /dev/null; \
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
