/* worker-loop-host.c: owner-thread command and shutdown probe
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <https://www.gnu.org/licenses/>.
*/

#include <glib.h>
#include <stdio.h>
#include <string.h>

#include "fuse-host.h"

/* Use the null backend: a toolkit-bound renderer cannot simply be moved
   to a worker if the toolkit requires the main thread. */
static GMainLoop *loop;
static GAsyncQueue *commands;
static int heartbeat;
static const unsigned char rom_bytes[0x4000] = { 0x76 }; /* HALT */

enum command { COMMAND_QUIT = 1, COMMAND_RELEASE };
enum notice { NOTICE_FRAME = 1, NOTICE_WORKER_WAITING, NOTICE_SHUT_DOWN };

#define CHECK( condition ) \
        do { if( !( condition ) ) g_error( "%s", #condition ); } while( 0 )

static int
acquire_rom( void *context, const char *name, fuse_host_rom *rom )
{
  (void)context;
  if( strcmp( name, "48.rom" ) ) return 1;
  rom->data = rom_bytes;
  rom->length = sizeof( rom_bytes );
  return 0;
}

static gboolean
ui_heartbeat( gpointer unused )
{
  (void)unused;
  heartbeat = 1;
  puts( "worker host: UI dispatched work while Quit was waiting" );
  g_async_queue_push( commands, GINT_TO_POINTER( COMMAND_RELEASE ) );
  return G_SOURCE_REMOVE;
}

static gboolean
ui_notice( gpointer data )
{
  switch( GPOINTER_TO_INT( data ) ) {
  case NOTICE_FRAME:
    g_async_queue_push( commands, GINT_TO_POINTER( COMMAND_QUIT ) );
    break;
  case NOTICE_WORKER_WAITING:
    g_idle_add( ui_heartbeat, NULL );
    break;
  case NOTICE_SHUT_DOWN:
    CHECK( heartbeat );
    puts( "worker host: UI received shutdown acknowledgement" );
    g_main_loop_quit( loop );
    break;
  default:
    g_error( "unexpected notice" );
  }
  return G_SOURCE_REMOVE;
}

static gpointer
run_worker( gpointer unused )
{
  const fuse_host_resources resources = { NULL, acquire_rom };
  fuse_host_config *config = fuse_host_config_alloc();
  fuse_quit_request quit;
  (void)unused;

  CHECK( config );
  CHECK( !fuse_host_config_set_machine( config, "48" ) );
  CHECK( !fuse_host_config_set_sound( config, 0 ) );
  CHECK( fuse_host_prepare( config, &resources ) == FUSE_INIT_READY );
  fuse_host_config_free( config );
  CHECK( fuse_run_frame() == FUSE_RUN_COMPLETED );
  g_idle_add( ui_notice, GINT_TO_POINTER( NOTICE_FRAME ) );
  CHECK( GPOINTER_TO_INT( g_async_queue_pop( commands ) ) == COMMAND_QUIT );
  /* Artificial gate, not a change to how a Fuse turn handles cancellation. */
  g_idle_add( ui_notice, GINT_TO_POINTER( NOTICE_WORKER_WAITING ) );
  CHECK( GPOINTER_TO_INT( g_async_queue_pop( commands ) ) == COMMAND_RELEASE );
  CHECK( fuse_host_request_quit( &quit ) ==
         FUSE_HOST_OPERATION_READY_TO_SHUT_DOWN );
  fuse_shutdown();
  g_idle_add( ui_notice, GINT_TO_POINTER( NOTICE_SHUT_DOWN ) );
  return NULL;
}

static gboolean
watchdog( gpointer unused )
{
  (void)unused;
  g_error( "worker host: timed out" );
  return G_SOURCE_REMOVE;
}

int
main( void )
{
  GThread *worker;
  guint timeout;
  loop = g_main_loop_new( NULL, FALSE );
  commands = g_async_queue_new();
  timeout = g_timeout_add_seconds( 5, watchdog, NULL );
  worker = g_thread_new( "fuse-owner", run_worker, NULL );
  g_main_loop_run( loop );
  /* Join only after the worker acknowledged engine shutdown. */
  g_thread_join( worker );
  g_source_remove( timeout );
  g_async_queue_unref( commands );
  g_main_loop_unref( loop );
  return 0;
}
