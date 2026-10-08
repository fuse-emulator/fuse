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

fuse_SOURCES += unittests/helpers.c
noinst_HEADERS += unittests/helpers.h

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

check-sound-scheduler: unittests/soundpartitiontest
	$(top_builddir)/unittests/soundpartitiontest 48000 --scheduler-only

check-sound-partition: unittests/soundpartitiontest
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

AUDIO_CHECK_TARGETS = check-sfifo check-pcm-fifo check-blip-preview \
                     check-sound-partition check-audio-pacing

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

fuse_SOURCES += unittests/unittests.c
noinst_HEADERS += unittests/unittests.h

## Standalone internal test programs

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
AUDIO_TEST_INTERVALS = 0 35000 17500 8750 997 1
AUDIO_TEST_BINARIES = $(addprefix unittests/fuse-audio-,$(AUDIO_TEST_INTERVALS))
AUDIO_TEST_OBJECTS = $(addsuffix .$(OBJEXT),$(addprefix unittests/audio-cut-,$(AUDIO_TEST_INTERVALS)))
CLEANFILES += $(AUDIO_TEST_BINARIES) $(AUDIO_TEST_OBJECTS)
.SECONDARY: $(AUDIO_TEST_OBJECTS)

unittests/audio-cut-%.$(OBJEXT): $(srcdir)/sound.c $(fuse_OBJECTS)
	$(AM_V_CC)$(COMPILE) -DFUSE_SOUND_TEST_INTERVAL=$* -c -o $@ $(srcdir)/sound.c

unittests/fuse-audio-%: unittests/audio-cut-%.$(OBJEXT) $(fuse_OBJECTS) $(fuse_DEPENDENCIES)
	$(AM_V_CCLD)$(fuse_LINK) $(filter-out sound.$(OBJEXT),$(fuse_OBJECTS)) unittests/audio-cut-$*.$(OBJEXT) $(fuse_LDADD) $(LIBS)

check-sound-engine-equivalence: fuse$(EXEEXT) $(AUDIO_TEST_BINARIES)
	python3 $(srcdir)/unittests/audio-frame-equivalence.py \
	  ./unittests/fuse-audio-0 ./fuse$(EXEEXT) --interval-builds \
	  ./unittests/fuse-audio-35000 ./unittests/fuse-audio-17500 \
	  ./unittests/fuse-audio-8750 ./unittests/fuse-audio-997 \
	  ./unittests/fuse-audio-1

noinst_PROGRAMS += unittests/automation_json_test
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
        ui/scaler/scalers16.o ui/scaler/scalers32.o
unittests_palcompositetest_CPPFLAGS = $(AM_CPPFLAGS)

unittests_scalerexpandtest_SOURCES = \
        unittests/scalerexpandtest.c \
        ui/scaler/scaler.c
unittests_scalerexpandtest_LDADD = $(LIBSPECTRUM_LIBS) \
        ui/scaler/scalers16.o ui/scaler/scalers32.o ui/scaler/snes_ntsc.o
unittests_scalerexpandtest_CPPFLAGS = $(AM_CPPFLAGS)

endif
