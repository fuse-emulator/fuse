/* Compile the legacy preference store and command-line parser separately
   from the engine settings record, using the same generated definitions. */
#define FUSE_LEGACY_PREFERENCES
#include "settings.c"
