/* jlo.c: Routines for handling the JLO Disk System interface */

#include "config.h"

#include "libspectrum.h"

#include <stdio.h>
#include <string.h>

#include "compat.h"
#include "debugger/debugger.h"
#include "fuse.h"
#include "infrastructure/startup_manager.h"
#include "jlo.h"
#include "jlo_logic.h"
#include "machine.h"
#include "memory_pages.h"
#include "module.h"
#include "options.h"
#include "periph.h"
#include "peripherals/disk/fdd.h"
#include "peripherals/disk/wd_fdc.h"
#include "settings.h"
#include "utils.h"
#include "ui/ui.h"
#include "ui/uimedia.h"

#ifdef JLO_DEBUG
#define JLO_LOG( ... )                       \
        do {                                       \
          fprintf( stderr, __VA_ARGS__ );          \
          fflush( stderr );                        \
        } while( 0 )
#else
#define JLO_LOG( ... ) do { } while( 0 )
#endif

/* 8 KiB EPROM */
#define JLO_ROM_SIZE 0x2000

/* 8 KiB SRAM */
#define JLO_RAM_SIZE 0x2000

static int jlo_rom_memory_source;
static int jlo_ram_memory_source;

static memory_page
  jlo_memory_map_romcs_rom[ MEMORY_PAGES_IN_8K ];

static memory_page
  jlo_memory_map_romcs_ram[ MEMORY_PAGES_IN_8K ];

int jlo_available = 0;
int jlo_active = 0;

static libspectrum_byte *jlo_ram;
static int jlo_ram_allocated;

static wd_fdc *jlo_fdc;
static fdd_t jlo_drives[ JLO_NUM_DRIVES ];
static fdd_t jlo_unselected_drive;

static ui_media_drive_info_t jlo_ui_drives[ JLO_NUM_DRIVES ];

static libspectrum_byte jlo_dsel;


static libspectrum_byte jlo_last_command;
static int jlo_command_pending;
static unsigned int jlo_data_reads;
static unsigned int jlo_data_writes;


static void jlo_reset( int hard_reset );
static void jlo_memory_map( void );
static void jlo_activate( void );
static void jlo_to_snapshot( libspectrum_snap *snap );
static ui_media_drive_info_t jlo_ui_drives[ JLO_NUM_DRIVES ];
static void jlo_step( int direction );
static void jlo_motor( int on );
/*
 * WD1770 registers
 */

static libspectrum_byte
jlo_sr_read( libspectrum_word port GCC_UNUSED,
             libspectrum_byte *attached )
{
  libspectrum_byte b;

  *attached = 0xff;

  b = wd_fdc_sr_read( jlo_fdc );

  /*
   * Print one line when the current WD1770 command finishes.
   * Bit 0 is BUSY.
   */
  if( jlo_command_pending && !( b & 0x01 ) ) {

    JLO_LOG(
      "JLO CMD END=%02X STATUS=%02X TR=%02X SEC=%02X "
      "READS=%u WRITES=%u\n",
      jlo_last_command,
      b,
      jlo_fdc->track_register,
      jlo_fdc->sector_register,
      jlo_data_reads,
      jlo_data_writes
      );

    jlo_command_pending = 0;
  }

  return b;
}

static void
jlo_cr_write( libspectrum_word port GCC_UNUSED,
              libspectrum_byte b )
{

  JLO_LOG(
    "JLO CMD=%02X TR=%02X SEC=%02X DSEL=%02X "
    "CUR_MOTOR=%d CUR_TR00=%d\n",
    b,
    jlo_fdc->track_register,
    jlo_fdc->sector_register,
    jlo_dsel,
    jlo_fdc->current_drive ?
    jlo_fdc->current_drive->motoron : -1,
    jlo_fdc->current_drive ?
    jlo_fdc->current_drive->tr00 : -1
    );

  jlo_last_command = b;
  jlo_command_pending = 1;
  jlo_data_reads = 0;
  jlo_data_writes = 0;

  wd_fdc_cr_write( jlo_fdc, b );
}

static libspectrum_byte
jlo_tr_read( libspectrum_word port GCC_UNUSED,
             libspectrum_byte *attached )
{
  *attached = 0xff;
  return wd_fdc_tr_read( jlo_fdc );
}

static void
jlo_tr_write( libspectrum_word port GCC_UNUSED,
              libspectrum_byte b )
{
  wd_fdc_tr_write( jlo_fdc, b );
}

static libspectrum_byte
jlo_sec_read( libspectrum_word port GCC_UNUSED,
              libspectrum_byte *attached )
{
  libspectrum_byte b;

  *attached = 0xff;

  b = wd_fdc_sec_read( jlo_fdc );

  if( ( jlo_last_command & 0xf0 ) == 0x90 ) {
    JLO_LOG(
      "JLO SEC READ=%02X STATUS=%02X\n",
      b,
      jlo_fdc->status_register
      );
  }

  if( jlo_fdc->command_register == 0xb0 ) {
    JLO_LOG(
      "JLO B0 SEC READ=%02X STATUS=%02X DRQ=%d PENDING=%d\n",
      b,
      jlo_fdc->status_register,
      jlo_fdc->datarq,
      jlo_fdc->sector_increment_pending
      );
  }

  return b;
}

static void
jlo_sec_write( libspectrum_word port GCC_UNUSED,
               libspectrum_byte b )
{
  wd_fdc_sec_write( jlo_fdc, b );
}

static libspectrum_byte
jlo_dr_read( libspectrum_word port GCC_UNUSED,
             libspectrum_byte *attached )
{
  *attached = 0xff;

  jlo_data_reads++;

  return jlo_logic_data_read( jlo_fdc );
}

static void
jlo_dr_write( libspectrum_word port GCC_UNUSED,
              libspectrum_byte b )
{
  jlo_data_writes++;

  wd_fdc_dr_write( jlo_fdc, b );
}

/*
 * B7: drive/side select
 *
 * bits 0..3 = drive select mask
 * bit 7     = side select
 */

static void
jlo_dsel_write( libspectrum_word port GCC_UNUSED,
                libspectrum_byte b )
{
  JLO_LOG( "JLO B7=%02X\n", b );

  jlo_logic_select( jlo_fdc, jlo_drives, &jlo_unselected_drive,
                    &jlo_dsel, b );
}

static const periph_port_t jlo_ports[] = {

  { 0x00ff, 0x008f, jlo_sr_read,  jlo_cr_write  },
  { 0x00ff, 0x009f, jlo_tr_read,  jlo_tr_write  },
  { 0x00ff, 0x00af, jlo_sec_read, jlo_sec_write },
  { 0x00ff, 0x00bf, jlo_dr_read,  jlo_dr_write  },

  { 0x00ff, 0x00b7, NULL,         jlo_dsel_write },

  { 0, 0, NULL, NULL }
};

static module_info_t jlo_module_info = {

  /* .reset = */ jlo_reset,
  /* .romcs = */ jlo_memory_map,
  /* .snapshot_enabled = */ NULL,
  /* .snapshot_from = */ NULL,
  /* .snapshot_to = */ jlo_to_snapshot,

};

static const periph_t jlo_periph = {
  /* .option = */ &settings_current.jlo,
  /* .ports = */ jlo_ports,
  /* .hard_reset = */ 1,
  /* .activate = */ jlo_activate,
};

void
jlo_page( void )
{
  if( !jlo_available )
    return;

  jlo_active = 1;

  machine_current->ram.romcs = 1;
  machine_current->memory_map();
}

void
jlo_unpage( void )
{
  jlo_active = 0;

  machine_current->ram.romcs = 0;
  machine_current->memory_map();
}

static void
jlo_memory_map( void )
{
  if( !jlo_active )
    return;

  memory_map_romcs_8k(
    0x0000, jlo_memory_map_romcs_rom );

  memory_map_romcs_8k(
    0x2000, jlo_memory_map_romcs_ram );
}

static void
jlo_activate( void )
{
  if( !jlo_ram_allocated ) {

    jlo_ram =
      memory_pool_allocate_persistent(
        JLO_RAM_SIZE, 1 );

    jlo_ram_allocated = 1;
  }
}

static int
jlo_init( void *context GCC_UNUSED )
{
  int i;
  fdd_t *d;

  jlo_fdc =
    wd_fdc_alloc_fdc( WD1770, 0, WD_FLAG_DRQ | WD_FLAG_DELAY_SECTOR_INC |
                      WD_FLAG_PRESERVE_STATUS_ON_FORCE_INTERRUPT );

  wd_fdc_set_step_callback( jlo_fdc, jlo_step );
  wd_fdc_set_motor_callback( jlo_fdc, jlo_motor );

  fdd_init( &jlo_unselected_drive, FDD_TYPE_NONE, NULL, 0 );

  for( i = 0; i < JLO_NUM_DRIVES; i++ ) {

    d = &jlo_drives[ i ];

    fdd_init(
      d,
      FDD_SHUGART,
      NULL,
      0
      );

    d->disk.flag = DISK_FLAG_NONE;
  }

  jlo_fdc->current_drive =
    &jlo_drives[ 0 ];

  fdd_select( &jlo_drives[ 0 ], 1 );

  jlo_dsel = 0x01;

  /*
   * Same density setting used by Fuse's Opus WD1770.
   */
  jlo_fdc->dden = 1;

  jlo_fdc->set_intrq = NULL;
  jlo_fdc->reset_intrq = NULL;
  jlo_fdc->set_datarq = NULL;
  jlo_fdc->reset_datarq = NULL;

  module_register( &jlo_module_info );

  jlo_rom_memory_source =
    memory_source_register( "JLO ROM" );

  jlo_ram_memory_source =
    memory_source_register( "JLO RAM" );

  for( i = 0;
       i < MEMORY_PAGES_IN_8K;
       i++ ) {

    jlo_memory_map_romcs_rom[ i ].source =
      jlo_rom_memory_source;

    jlo_memory_map_romcs_ram[ i ].source =
      jlo_ram_memory_source;
  }

  periph_register(
    PERIPH_TYPE_JLO,
    &jlo_periph
    );

  for( i = 0; i < JLO_NUM_DRIVES; i++ ) {
    jlo_ui_drives[ i ].fdd = &jlo_drives[ i ];
    ui_media_drive_register( &jlo_ui_drives[ i ] );
  }

  return 0;
}

static void
jlo_end( void )
{
  jlo_available = 0;

  libspectrum_free( jlo_fdc );
  jlo_fdc = NULL;
}

void
jlo_register_startup( void )
{
  startup_manager_module dependencies[] = {
    STARTUP_MANAGER_MODULE_DEBUGGER,
    STARTUP_MANAGER_MODULE_MEMORY,
    STARTUP_MANAGER_MODULE_SETUID,
  };

  startup_manager_register(
    STARTUP_MANAGER_MODULE_JLO,
    dependencies,
    ARRAY_SIZE( dependencies ),
    jlo_init,
    NULL,
    jlo_end
    );
}

static void
jlo_reset( int hard_reset )
{
  int i;

  jlo_active = 0;
  jlo_available = 0;

  if( !periph_is_active( PERIPH_TYPE_JLO ) )
    return;

  /*
   * Load JLO ROM configured in settings.
   */
  if( machine_load_rom_bank(
        jlo_memory_map_romcs_rom,
        0,
        settings_current.rom_jlo,
        settings_default.rom_jlo,
        JLO_ROM_SIZE ) ) {
    settings_current.jlo = 0;
    periph_activate_type( PERIPH_TYPE_JLO, 0 );
    return;
  }

  for( i = 0; i < MEMORY_PAGES_IN_8K; i++ ) {

    memory_page *page = &jlo_memory_map_romcs_ram[ i ];

    page->page = jlo_ram + i * MEMORY_PAGE_SIZE;
    page->offset = i * MEMORY_PAGE_SIZE;
    page->writable = 1;
  }

  if( hard_reset )
    memset( jlo_ram, 0, JLO_RAM_SIZE );

  wd_fdc_master_reset( jlo_fdc );
  jlo_motor( 0 );

  jlo_dsel = 0x01;

  for( i = 0; i < JLO_NUM_DRIVES; i++ )
    fdd_select( &jlo_drives[ i ], i == 0 );

  jlo_fdc->current_drive = &jlo_drives[ 0 ];

  jlo_available = 1;

  /*
   * Board B bank latch is asynchronously forced to BANK ACTIVE
   * by the machine RESET signal.
   *
   * Therefore after a hardware reset the CPU must see the JLO ROM
   * at 0000 and execute SAFE's reset entry point.
   *
   * Do not emulate this as "PC == 0000 pages JLO"; JP 0000 and
   * RST 00 do not affect the real Board B bank latch.
   */
  if( hard_reset ) {
    jlo_active = 1;
    machine_current->ram.romcs = 1;
  } else {
    jlo_active = 0;
    machine_current->ram.romcs = 0;
  }

  machine_current->memory_map();

  for( i = 0; i < JLO_NUM_DRIVES; i++ ) {
    ui_media_drive_update_menus(
      &jlo_ui_drives[ i ],
      UI_MEDIA_DRIVE_UPDATE_ALL
      );
  }
}

static void
jlo_to_snapshot( libspectrum_snap *snap )
{
  /* RAM and controller state are lost even when Board B is paged out.
     This marker lets libspectrum report major information loss; no JLO
     state is currently serialised. */
  libspectrum_snap_set_jlo_active( snap, jlo_available );
}

int
jlo_disk_insert( jlo_drive_number which, const char *filename, int autoload )
{
  if( which >= JLO_NUM_DRIVES ) {
    ui_error( UI_ERROR_ERROR, "jlo_disk_insert: unknown drive %d", which );
    fuse_abort();
  }

  return ui_media_drive_insert( &jlo_ui_drives[ which ], filename, autoload );
}

fdd_t *
jlo_get_fdd( jlo_drive_number which )
{
  return &jlo_drives[ which ];
}

static int
jlo_ui_drive_is_available( void )
{
  return jlo_available;
}

static const fdd_params_t *
jlo_ui_drive_get_params_a( void )
{
  /* +1 => drive A cannot be Disabled */
  return &fdd_params[
    option_enumerate_diskoptions_drive_jlo1_type() + 1
  ];
}

static const fdd_params_t *
jlo_ui_drive_get_params_b( void )
{
  return &fdd_params[
    option_enumerate_diskoptions_drive_jlo2_type()
  ];
}

static const fdd_params_t *
jlo_ui_drive_get_params_c( void )
{
  return &fdd_params[
    option_enumerate_diskoptions_drive_jlo3_type()
  ];
}

static const fdd_params_t *
jlo_ui_drive_get_params_d( void )
{
  return &fdd_params[
    option_enumerate_diskoptions_drive_jlo4_type()
  ];
}

static void
jlo_motor( int on )
{
  jlo_logic_motor( jlo_drives, jlo_dsel, on );
}

static void
jlo_step( int direction )
{
  JLO_LOG( "JLO STEP dsel=%02X direction=%d current=%p\n",
           jlo_dsel, direction, (void *)jlo_fdc->current_drive );

  jlo_logic_step( jlo_drives, jlo_dsel, direction );
}

static ui_media_drive_info_t jlo_ui_drives[ JLO_NUM_DRIVES ] = {

  {
    /* .name = */ "JLO Disk 0:",
    /* .controller_index = */ UI_MEDIA_CONTROLLER_JLO,
    /* .drive_index = */ JLO_DRIVE_0,
    /* .menu_item_parent = */ UI_MENU_ITEM_MEDIA_DISK_JLO,
    /* .menu_item_top = */ UI_MENU_ITEM_INVALID,
    /* .menu_item_eject = */ UI_MENU_ITEM_MEDIA_DISK_JLO_0_EJECT,
    /* .menu_item_flip = */ UI_MENU_ITEM_MEDIA_DISK_JLO_0_FLIP_SET,
    /* .menu_item_wp = */ UI_MENU_ITEM_MEDIA_DISK_JLO_0_WP_SET,
    /* .is_available = */ &jlo_ui_drive_is_available,
    /* .get_params = */ &jlo_ui_drive_get_params_a,
    /* .insert_hook = */ NULL,
    /* .autoload_hook = */ NULL,
  },

  {
    /* .name = */ "JLO Disk 1:",
    /* .controller_index = */ UI_MEDIA_CONTROLLER_JLO,
    /* .drive_index = */ JLO_DRIVE_1,
    /* .menu_item_parent = */ UI_MENU_ITEM_MEDIA_DISK_JLO,
    /* .menu_item_top = */ UI_MENU_ITEM_MEDIA_DISK_JLO_1,
    /* .menu_item_eject = */ UI_MENU_ITEM_MEDIA_DISK_JLO_1_EJECT,
    /* .menu_item_flip = */ UI_MENU_ITEM_MEDIA_DISK_JLO_1_FLIP_SET,
    /* .menu_item_wp = */ UI_MENU_ITEM_MEDIA_DISK_JLO_1_WP_SET,
    /* .is_available = */ &jlo_ui_drive_is_available,
    /* .get_params = */ &jlo_ui_drive_get_params_b,
    /* .insert_hook = */ NULL,
    /* .autoload_hook = */ NULL,
  },

  {
    /* .name = */ "JLO Disk 2:",
    /* .controller_index = */ UI_MEDIA_CONTROLLER_JLO,
    /* .drive_index = */ JLO_DRIVE_2,
    /* .menu_item_parent = */ UI_MENU_ITEM_MEDIA_DISK_JLO,
    /* .menu_item_top = */ UI_MENU_ITEM_MEDIA_DISK_JLO_2,
    /* .menu_item_eject = */ UI_MENU_ITEM_MEDIA_DISK_JLO_2_EJECT,
    /* .menu_item_flip = */ UI_MENU_ITEM_MEDIA_DISK_JLO_2_FLIP_SET,
    /* .menu_item_wp = */ UI_MENU_ITEM_MEDIA_DISK_JLO_2_WP_SET,
    /* .is_available = */ &jlo_ui_drive_is_available,
    /* .get_params = */ &jlo_ui_drive_get_params_c,
    /* .insert_hook = */ NULL,
    /* .autoload_hook = */ NULL,
  },

  {
    /* .name = */ "JLO Disk 3:",
    /* .controller_index = */ UI_MEDIA_CONTROLLER_JLO,
    /* .drive_index = */ JLO_DRIVE_3,
    /* .menu_item_parent = */ UI_MENU_ITEM_MEDIA_DISK_JLO,
    /* .menu_item_top = */ UI_MENU_ITEM_MEDIA_DISK_JLO_3,
    /* .menu_item_eject = */ UI_MENU_ITEM_MEDIA_DISK_JLO_3_EJECT,
    /* .menu_item_flip = */ UI_MENU_ITEM_MEDIA_DISK_JLO_3_FLIP_SET,
    /* .menu_item_wp = */ UI_MENU_ITEM_MEDIA_DISK_JLO_3_WP_SET,
    /* .is_available = */ &jlo_ui_drive_is_available,
    /* .get_params = */ &jlo_ui_drive_get_params_d,
    /* .insert_hook = */ NULL,
    /* .autoload_hook = */ NULL,
  },
};
