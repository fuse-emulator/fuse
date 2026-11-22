/* filesystemtest.c: existing operations against a replaced effective filesystem
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compat.h"
#include "fuse.h"
#include "utils.h"
#include "fuse-internal.h"
#include "memory_pages.h"
#include "movie.h"
#include "peripherals/disk/disk.h"
#include "peripherals/ide/zxcf.h"
#include "pokefinder/pokemem.h"
#include "rzx.h"
#include "psg.h"
#include "screenshot.h"
#include "settings.h"
#include "tape.h"
#include "ui/scaler/scaler.h"
#include "ui/ui.h"
#include "z80/z80.h"
#include "../hacking/memory-filesystem.h"
#undef NDEBUG
#include <assert.h>

#define OK FUSE_HOST_OPERATION_OK
#define ERROR FUSE_HOST_OPERATION_MEDIA_ERROR

static void
probing_read( void )
{
  assert( fuse_host_open_file( "not-opened.tap",
                               &(fuse_open_request){ 0 } ) ==
          FUSE_HOST_OPERATION_BUSY );
}

static void
seed( const char *name, const void *data, size_t length )
{
  compat_fd file = compat_file_open( name, COMPAT_FILE_WRITE );
  assert( file && !compat_file_write( file, data, length ) && !compat_file_close( file ) );
}

static void
file_bytes( const char *name, unsigned char **data, size_t *length )
{
  FILE *file = fopen( name, "rb" );
  long size;
  assert( file && !fseek( file, 0, SEEK_END ) && ( size = ftell( file ) ) >= 0 );
  assert( !fseek( file, 0, SEEK_SET ) );
  *length = size; *data = malloc( *length ); assert( *data );
  assert( fread( *data, 1, *length, file ) == *length && !fclose( file ) && !remove( name ) );
}

static memory_filesystem filesystem;
static fuse_mass_storage_info *retained_drive;
extern ui_confirm_save_t (*ui_null_confirm_save_test_hook)( const char * );
static int cancel_count;

static ui_confirm_save_t
cancel_dirty_drive( const char *message )
{
  assert( strstr( message, "modified" ) );
  cancel_count++;
  return UI_CONFIRM_SAVE_CANCEL;
}

void
filesystem_prepare( void )
{
  filesystem.chunk = 2;
  assert( !memory_filesystem_install( &filesystem ) );
#if !defined(WIN32) && !defined(AMIGA) && !defined(WII)
  {
    char primary[PATH_MAX], fallback[PATH_MAX];
    const char *cfgdir = compat_get_config_path();
    const char *fallbackdir = compat_get_fallback_config_path();
    char program[] = "filesystem-config-test";
    char *argv[] = { program, NULL };
    int first;
    const char *name = "fuserc", *old_name = ".fuserc";
#ifdef HAVE_LIB_XML2
    const char *config = "<settings><fastload>0</fastload></settings>";
#else
    const char *config = "fastload=0\n";
#endif
    assert( cfgdir && fallbackdir );
    snprintf( primary, sizeof(primary), "%s/%s", cfgdir, name );
    snprintf( fallback, sizeof(fallback), "%s/%s", fallbackdir, old_name );
    seed( primary, "ignored", 7 );
    seed( fallback, config, strlen(config) );
    filesystem.unreadable_name = primary;
    assert( compat_file_exists( primary ) );
    assert( !settings_init( &first, 1, argv ) && !settings_current.fastload );
    settings_end( 0 );
    filesystem.unreadable_name = NULL;
    assert( !compat_file_unlink( primary ) && !compat_file_unlink( fallback ) );
  }
#endif
}

void
filesystem_write_oracle( void )
{
  z80.pc.w = 0x8000; z80.sp.w = 0x9000; writebyte_internal( 0x8100, 0x5a );
  assert( fuse_host_save_snapshot_file( "fs-oracle.szx" ) == OK );
}

void
filesystem_tests( void )
{
  static const unsigned char tap[] = { 2, 0, 0xff, 0xff };
  static const unsigned char pok[] = "NTest\nM 8 32768 99 0\nY\n";
  memory_file *file;
  compat_fd handle;
  unsigned char buffer[8], *expected;
  size_t length, count, closed;
  fuse_media_decision_id request;

  z80.pc.w = 0x8000; z80.sp.w = 0x9000; writebyte_internal( 0x8100, 0x5a );
  file_bytes( "fs-oracle.szx", &expected, &length );
  /* A real native file exists, but must be invisible to the replacement. */
  { FILE *native = fopen( "native-only.tap", "wb" ); assert( native );
    assert( fwrite( tap, 1, sizeof(tap), native ) == sizeof(tap) && !fclose( native ) ); }
  assert( !compat_file_exists( "native-only.tap" ) );
  assert( fuse_host_insert_tape_file( "native-only.tap", &request ) == ERROR );

  seed( "seek.bin", "abcdef", 6 );
  handle = compat_file_open( "seek.bin", COMPAT_FILE_READ_WRITE ); assert( handle );
  assert( memory_filesystem_install( &filesystem ) ); /* Selection is permanent. */
  assert( compat_file_get_length( handle ) == 6 && !compat_file_seek( handle, -3, COMPAT_SEEK_END ) );
  assert( compat_file_tell( handle ) == 3 );
  assert( !compat_file_read_some( handle, buffer, 3, &count ) && count == 2 && !memcmp( buffer, "de", 2 ) );
  assert( !compat_file_read_some( handle, buffer, 3, &count ) && count == 1 && buffer[0] == 'f' );
  assert( !compat_file_read_some( handle, buffer, 3, &count ) && !count );
  assert( compat_file_read( handle, buffer, 1 ) ); /* Exact read reports EOF. */
  assert( compat_file_seek( handle, INT64_MIN, COMPAT_SEEK_END ) );
  assert( !compat_file_seek( handle, 0, COMPAT_SEEK_SET ) && !compat_file_write( handle, (const unsigned char *)"XY", 2 ) );
  assert( !compat_file_flush( handle ) && !compat_file_close( handle ) );
  handle = compat_file_open( "seek.bin", COMPAT_FILE_APPEND ); assert( handle );
  assert( !compat_file_write( handle, (const unsigned char *)"Z", 1 ) && !compat_file_close( handle ) );
  file = memory_find( "seek.bin" ); assert( file->length == 7 && !memcmp( file->data, "XYcdefZ", 7 ) );
  assert( !compat_file_unlink( "seek.bin" ) && !compat_file_exists( "seek.bin" ) );
  seed( "transfer.bin", "abcd", 4 );
  filesystem.invalid_count = 1;
  handle = compat_file_open( "transfer.bin", COMPAT_FILE_READ ); assert( handle );
  assert( compat_file_read_some( handle, buffer, 1, &count ) && !count );
  assert( !compat_file_close( handle ) );
  filesystem.invalid_count = 0;
  filesystem.stall = 1;
  handle = compat_file_open( "transfer.bin", COMPAT_FILE_WRITE ); assert( handle );
  assert( compat_file_write( handle, (const unsigned char *)"x", 1 ) );
  assert( !compat_file_close( handle ) );
  filesystem.stall = 0;

  seed( "media/input.tap", tap, sizeof(tap) );
  /* Namespace/lookup test, not numeric parser repair: its existing sscanf
     assumption needs a terminator. */
  seed( "media/input.pok", "unreadable", 10 );
  seed( "media/input.POK", pok, sizeof(pok) );
  filesystem.unreadable_name = "media/input.pok";
  assert( compat_file_exists( filesystem.unreadable_name ) );
  filesystem.on_read = probing_read;
  assert( fuse_host_open_file( "media/input.tap",
                               &(fuse_open_request){ 0 } ) == OK );
  filesystem.on_read = NULL;
  assert( tape_present() && !tape_is_playing() && !pokemem_autoload_pokfile() );
  filesystem.unreadable_name = NULL;
  assert( fuse_host_save_tape_file( "media/copy.pzx" ) == OK );
  assert( fuse_host_request_tape_eject( &request ) == OK && !request );
  assert( fuse_host_open_file( "media/copy.pzx",
                               &(fuse_open_request){ 0 } ) == OK );
  assert( fuse_host_save_snapshot_file( "media/state.szx" ) == OK );
  file = memory_find( "media/state.szx" );
  assert( file && file->length == length && !memcmp( file->data, expected, length ) );
  z80.pc.w = 0; writebyte_internal( 0x8100, 0 );
  assert( fuse_host_open_file( "media/state.szx", &(fuse_open_request){ 0 } ) == OK );
  assert( z80.pc.w == 0x8000 && z80.sp.w == 0x9000 && readbyte_internal(0x8100) == 0x5a );

  filesystem.fail_open = 1;
  assert( fuse_host_insert_tape_file( "media/input.tap", &request ) == ERROR ); filesystem.fail_open = 0;
  filesystem.fail_read = 1;
  assert( fuse_host_open_file( "media/state.szx",
                               &(fuse_open_request){ 0 } ) == ERROR );
  filesystem.fail_read = 0;
  tape_modified = 1; filesystem.fail_write = 1;
  assert( fuse_host_save_tape_file( "media/fail.tzx" ) == ERROR && tape_modified ); filesystem.fail_write = 0;
  filesystem.fail_close = 1;
  assert( fuse_host_save_snapshot_file( "media/close.szx" ) == ERROR ); filesystem.fail_close = 0;
  assert( fuse_host_save_tape_file( "media/recovered.pzx" ) == OK && !tape_modified );
  assert( filesystem.opens == filesystem.closes );
  { size_t written = filesystem.writes;
    assert( !settings_write_config( &settings_current ) && filesystem.writes > written ); }
  assert( !psg_start_recording( "media/sound.psg" ) );
  assert( !psg_write_register( 1, 2 ) && !psg_frame() && !psg_stop_recording() );
  file = memory_find( "media/sound.psg" );
  assert( file && file->length > 4 && !memcmp( file->data, "PSG\x1a", 4 ) );
  closed = filesystem.closes; filesystem.fail_write = 1;
  assert( psg_start_recording( "media/failed.psg" ) && filesystem.closes == closed + 1 );
  filesystem.fail_write = 0;

  assert( !rzx_start_recording( "media/stop.rzx", 0 ) && rzx_recording );
  assert( fuse_host_open_file( "missing.tap",
                               &(fuse_open_request){ 0 } ) == ERROR &&
          !rzx_recording );
  assert( memory_find( "media/stop.rzx" ) ); /* General Open still stops before validation. */
#if LIBSPECTRUM_SUPPORTS_WAV
  {
    static const unsigned char wav[] =
      "RIFF\x2c\0\0\0WAVEfmt \x10\0\0\0\1\0\1\0"
      "\x40\x1f\0\0\x40\x1f\0\0\1\0\10\0data\10\0\0\0"
      "\0\0\xff\xff\0\0\xff\xff";
    seed( "media/pulse.wav", wav, sizeof(wav) - 1 );
    assert( fuse_host_open_file( "media/pulse.wav", &(fuse_open_request){ 0 } ) == OK && tape_present() );
  }
#endif
  {
    unsigned char hdf[128 + 512] = { 'R','S','-','I','D','E',0x1a,0x11,0,128,0 };
    hdf[24] = 1; hdf[28] = 1; hdf[34] = 1;
    seed( "media/drive.hdf", hdf, sizeof(hdf) );
    assert( fuse_host_open_file( "media/drive.hdf", &(fuse_open_request){ 0 } ) == OK );
    assert( filesystem.opens == filesystem.closes + 1 );
    assert( fuse_host_copy_mass_storage_info( PERIPH_TYPE_ZXCF, LIBSPECTRUM_IDE_MASTER, &retained_drive ) == OK );
    assert( retained_drive->active && !strcmp( retained_drive->backing_name, "media/drive.hdf" ) );
    /* Write one ATA sector through the emulated ZXCF ports. */
    writeport_internal( 0x02b4, 1 );
    writeport_internal( 0x03b4, 0 );
    writeport_internal( 0x04b4, 0 );
    writeport_internal( 0x05b4, 0 );
    writeport_internal( 0x06b4, 0xe0 );
    writeport_internal( 0x07b4, 0x30 );
    for( int i = 0; i < 512; i++ ) writeport_internal( 0x00b4, 0x5a );
    {
      fuse_quit_request request;
      int tape_was_present = tape_present();
      assert( tape_was_present );
      assert( fuse_host_request_quit( &request ) == FUSE_HOST_OPERATION_UNSUPPORTED_DIRTY_MEDIA );
      assert( !request.id && tape_present() == tape_was_present );
    }
    {
      fuse_mass_storage_info *still_mounted;
      size_t opened = filesystem.opens, closed_before = filesystem.closes;
      ui_null_confirm_save_test_hook = cancel_dirty_drive;
      assert( fuse_host_insert_mass_storage( PERIPH_TYPE_ZXCF, LIBSPECTRUM_IDE_MASTER, "media/missing.hdf" ) == ERROR );
      assert( fuse_host_eject_mass_storage( PERIPH_TYPE_ZXCF, LIBSPECTRUM_IDE_MASTER ) == ERROR );
      ui_null_confirm_save_test_hook = NULL;
      assert( cancel_count == 2 && filesystem.opens == opened && filesystem.closes == closed_before );
      assert( fuse_host_copy_mass_storage_info( PERIPH_TYPE_ZXCF, LIBSPECTRUM_IDE_MASTER, &still_mounted ) == OK );
      assert( still_mounted->backing_name && !strcmp( still_mounted->backing_name, "media/drive.hdf" ) );
      fuse_mass_storage_info_free( still_mounted );
    }
    assert( fuse_host_insert_mass_storage( PERIPH_TYPE_ZXCF, LIBSPECTRUM_IDE_MASTER, "media/missing.hdf" ) == ERROR );
    assert( filesystem.opens == filesystem.closes );
    {
      fuse_mass_storage_info *empty;
      assert( fuse_host_copy_mass_storage_info( PERIPH_TYPE_ZXCF, LIBSPECTRUM_IDE_MASTER, &empty ) == OK );
      assert( !empty->backing_name );
      fuse_mass_storage_info_free( empty );
    }
    assert( fuse_host_insert_mass_storage( PERIPH_TYPE_ZXCF, LIBSPECTRUM_IDE_MASTER, "media/drive.hdf" ) == OK );
    assert( fuse_host_commit_mass_storage( PERIPH_TYPE_ZXCF, LIBSPECTRUM_IDE_MASTER ) == OK );
    assert( fuse_host_eject_mass_storage( PERIPH_TYPE_ZXCF, LIBSPECTRUM_IDE_MASTER ) == OK );
    assert( fuse_host_commit_mass_storage( PERIPH_TYPE_ZXCF, LIBSPECTRUM_IDE_MASTER ) == ERROR );
    assert( filesystem.opens == filesystem.closes );
    /* MMC shares the HDF backing; declare 32 cylinders, 1 head, 32 sectors. */
    hdf[24] = 32; hdf[28] = 1; hdf[34] = 32;
    seed( "media/card.hdf", hdf, sizeof(hdf) );
    libspectrum_mmc_card *card = libspectrum_mmc_alloc();
    assert( card && !libspectrum_mmc_insert( card, "media/card.hdf" ) );
    assert( filesystem.opens == filesystem.closes + 1 );
    libspectrum_mmc_eject( card );
    libspectrum_mmc_free( card );
    assert( filesystem.opens == filesystem.closes );
    settings_current.divmmc_enabled = 1;
    periph_update();
    assert( fuse_host_insert_mass_storage( PERIPH_TYPE_DIVMMC, LIBSPECTRUM_IDE_MASTER, "media/card.hdf" ) == OK );
    assert( filesystem.opens == filesystem.closes + 1 );
    /* Select card zero, initialize SDHC, then issue CMD24 for block zero. */
    static const unsigned char commands[][6] = {
      { 0x48, 0, 0, 1, 0xaa, 0x87 }, /* CMD8 */
      { 0x77, 0, 0, 0, 0, 1 },      /* CMD55 */
      { 0x69, 0x40, 0, 0, 0, 1 },   /* ACMD41 with HCS */
      { 0x58, 0, 0, 0, 0, 1 },      /* CMD24 */
    };
    writeport_internal( 0xe7, 2 );
    for( size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++ )
      for( size_t j = 0; j < sizeof(commands[0]); j++ )
        writeport_internal( 0xeb, commands[i][j] );
    writeport_internal( 0xeb, 0xfe );
    for( int i = 0; i < 512; i++ ) writeport_internal( 0xeb, 0xa5 );
    writeport_internal( 0xeb, 0 );
    writeport_internal( 0xeb, 0 );
    writeport_internal( 0xe7, 3 );
    {
      fuse_mass_storage_info *mounted;
      fuse_quit_request request;
      size_t opened = filesystem.opens, closed_before = filesystem.closes;
      assert( fuse_host_request_quit( &request ) == FUSE_HOST_OPERATION_UNSUPPORTED_DIRTY_MEDIA );
      assert( !request.id && filesystem.opens == opened && filesystem.closes == closed_before );
      cancel_count = 0;
      ui_null_confirm_save_test_hook = cancel_dirty_drive;
      assert( fuse_host_insert_mass_storage( PERIPH_TYPE_DIVMMC, LIBSPECTRUM_IDE_MASTER, "media/missing.hdf" ) == ERROR );
      assert( fuse_host_eject_mass_storage( PERIPH_TYPE_DIVMMC, LIBSPECTRUM_IDE_MASTER ) == ERROR );
      ui_null_confirm_save_test_hook = NULL;
      assert( cancel_count == 2 && filesystem.opens == opened && filesystem.closes == closed_before );
      assert( fuse_host_copy_mass_storage_info( PERIPH_TYPE_DIVMMC, LIBSPECTRUM_IDE_MASTER, &mounted ) == OK );
      assert( mounted->backing_name && !strcmp( mounted->backing_name, "media/card.hdf" ) );
      fuse_mass_storage_info_free( mounted );
    }
    assert( fuse_host_commit_mass_storage( PERIPH_TYPE_DIVMMC, LIBSPECTRUM_IDE_MASTER ) == OK );
    file = memory_find( "media/card.hdf" );
    assert( file && file->length == sizeof(hdf) && file->data[128] == 0xa5 );
    assert( fuse_host_eject_mass_storage( PERIPH_TYPE_DIVMMC, LIBSPECTRUM_IDE_MASTER ) == OK );
    settings_current.divmmc_enabled = 0;
    settings_current.zxmmc_enabled = 1;
    assert( fuse_host_select_machine( "plus3" ) == OK );
    assert( fuse_host_insert_mass_storage( PERIPH_TYPE_ZXMMC, LIBSPECTRUM_IDE_SLAVE,
                                          "media/card.hdf" ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
    assert( fuse_host_insert_mass_storage( PERIPH_TYPE_ZXMMC, LIBSPECTRUM_IDE_MASTER, "media/card.hdf" ) == OK );
    assert( fuse_host_commit_mass_storage( PERIPH_TYPE_ZXMMC, LIBSPECTRUM_IDE_MASTER ) == OK );
    assert( fuse_host_eject_mass_storage( PERIPH_TYPE_ZXMMC, LIBSPECTRUM_IDE_MASTER ) == OK );
    settings_current.zxmmc_enabled = 0;
    assert( fuse_host_select_machine( "48" ) == OK );
    {
      const periph_type controllers[] = { PERIPH_TYPE_SIMPLEIDE, PERIPH_TYPE_ZXATASP, PERIPH_TYPE_DIVIDE };
      for( size_t i = 0; i < sizeof(controllers) / sizeof(controllers[0]); i++ ) {
        periph_type controller = controllers[i];
        fuse_mass_storage_info *master, *slave;
        settings_current.simpleide_active = controller == PERIPH_TYPE_SIMPLEIDE;
        settings_current.zxatasp_active = controller == PERIPH_TYPE_ZXATASP;
        settings_current.divide_enabled = controller == PERIPH_TYPE_DIVIDE;
        periph_update();
        assert( fuse_host_insert_mass_storage( controller, LIBSPECTRUM_IDE_MASTER, "media/drive.hdf" ) == OK );
        assert( fuse_host_insert_mass_storage( controller, LIBSPECTRUM_IDE_SLAVE, "media/card.hdf" ) == OK );
        assert( filesystem.opens == filesystem.closes + 2 );
        assert( fuse_host_insert_mass_storage( controller, LIBSPECTRUM_IDE_SLAVE, "media/missing.hdf" ) == ERROR );
        assert( filesystem.opens == filesystem.closes + 1 );
        assert( fuse_host_copy_mass_storage_info( controller, LIBSPECTRUM_IDE_MASTER, &master ) == OK );
        assert( fuse_host_copy_mass_storage_info( controller, LIBSPECTRUM_IDE_SLAVE, &slave ) == OK );
        assert( master->backing_name && !strcmp( master->backing_name, "media/drive.hdf" ) );
        assert( !slave->backing_name );
        fuse_mass_storage_info_free( master );
        fuse_mass_storage_info_free( slave );
        assert( fuse_host_commit_mass_storage( controller, LIBSPECTRUM_IDE_MASTER ) == OK );
        assert( fuse_host_eject_mass_storage( controller, LIBSPECTRUM_IDE_MASTER ) == OK );
        assert( fuse_host_eject_mass_storage( controller, LIBSPECTRUM_IDE_SLAVE ) == OK );
        assert( filesystem.opens == filesystem.closes );
      }
      settings_current.divide_enabled = 0;
      periph_update();
    }
    assert( filesystem.opens == filesystem.closes );
  }
#ifdef USE_LIBPNG
  assert( !screenshot_write( "media/screen.png", SCALER_NORMAL ) );
  file = memory_find( "media/screen.png" );
  assert( file && file->length > 8 && !memcmp( file->data, "\x89PNG\r\n\x1a\n", 8 ) );
#endif
  assert( fuse_host_select_machine( "plus3" ) == OK );
  filesystem.chunk = 65536;
  closed = filesystem.closes;
  movie_start( "media/movie.fmf" );
  movie_stop();
  file = memory_find( "media/movie.fmf" );
  assert( file && file->length > 16 && !memcmp( file->data, "FMF_V1", 6 ) );
  assert( filesystem.closes == closed + 1 );
  {
    disk_t disk = { 0 };
    assert( disk_new( &disk, 2, 40, DISK_DENS_AUTO, DISK_UDI ) == DISK_OK );
    disk.type = DISK_TYPE_NONE;
    assert( disk_write( &disk, "media/disk.log" ) == DISK_OK );
    file = memory_find( "media/disk.log" );
    assert( file && file->length > 17 && !memcmp( file->data, "DISK tracks log!\n", 17 ) );
    closed = filesystem.closes; filesystem.fail_write = 1;
    assert( disk_write( &disk, "media/failed.log" ) == DISK_WRPART );
    assert( filesystem.closes == closed + 1 );
    filesystem.fail_write = 0;
    disk_close( &disk );
  }
  fuse_media_decision_id disk_request;
  assert( fuse_host_new_disk( FUSE_DISK_CONTROLLER_PLUS3, 0, &disk_request ) == OK && !disk_request );
  assert( fuse_host_save_disk_file( FUSE_DISK_CONTROLLER_PLUS3, 0, "media/disk.udi" ) == OK );
  file = memory_find( "media/disk.udi" ); assert( file && file->length > 16 );
  filesystem.fail_write_at = file->length - 4;
  closed = filesystem.closes;
  assert( fuse_host_save_disk_file( FUSE_DISK_CONTROLLER_PLUS3, 0, "media/fail.udi" ) == ERROR );
  assert( filesystem.closes == closed + 1 );
  filesystem.fail_write_at = 0;
  assert( fuse_host_eject_disk( FUSE_DISK_CONTROLLER_PLUS3, 0, &disk_request ) == OK && !disk_request );
  assert( fuse_host_insert_disk_file( FUSE_DISK_CONTROLLER_PLUS3, 0, "media/disk.udi", &disk_request ) == OK && !disk_request );
  { fuse_disk_drive_info *info;
    assert( fuse_host_copy_disk_drive_info( FUSE_DISK_CONTROLLER_PLUS3, 0, &info ) == OK );
    assert( info->loaded && !info->write_protected ); fuse_disk_drive_info_free( info ); }
  assert( fuse_host_eject_disk( FUSE_DISK_CONTROLLER_PLUS3, 0, &disk_request ) == OK && !disk_request );
  assert( filesystem.opens == filesystem.closes );
  free( expected );
  assert( !remove( "native-only.tap" ) );
}

void
filesystem_finish( void )
{
  assert( retained_drive && !strcmp( retained_drive->backing_name, "media/drive.hdf" ) );
  fuse_mass_storage_info_free( retained_drive );
  assert( filesystem.opens == filesystem.closes );
  memory_filesystem_free( &filesystem );
}
