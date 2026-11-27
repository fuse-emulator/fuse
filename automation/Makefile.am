# Makefile.am: development automation sources

EXTRA_DIST += examples/lua/boot.lua hacking/lua-host.md unittests/lua_host.py

EXTRA_libfuse_la_SOURCES += \
	     automation/artifacts.c \
	     automation/artifacts.h \
	     automation/automation.c \
	     automation/automation.h \
	     automation/json.c \
	     automation/json.h \
	     automation/result_settings.c \
	     automation/result_settings.h \
	     automation/state.c \
	     automation/state.h \
	     automation/host-services.c \
	     automation/host-services.h

if ENABLE_AUTOMATION
libfuse_la_SOURCES += \
	automation/artifacts.c \
	automation/artifacts.h \
	automation/automation.c \
	automation/automation.h \
	automation/json.c \
	automation/json.h \
	automation/result_settings.c \
	automation/result_settings.h \
	automation/state.c \
	automation/state.h \
	automation/host-services.c \
	automation/host-services.h
endif
