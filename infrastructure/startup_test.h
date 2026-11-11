/* Private, compile-time-only lifecycle failure injection. Never installed. */
#ifndef FUSE_STARTUP_TEST_H
#define FUSE_STARTUP_TEST_H

#ifdef FUSE_STARTUP_TEST
#include "startup_manager.h"

int fuse_startup_test_fail( const char *point );
int fuse_startup_test_checkpoint( startup_manager_module module, int completed,
                                  int has_end );
void fuse_startup_test_cleanup( startup_manager_module module );
void fuse_startup_test_shutdown( void );
#define FUSE_STARTUP_FAIL(point) fuse_startup_test_fail( point )
#else
#define FUSE_STARTUP_FAIL(point) 0
#endif

#endif
