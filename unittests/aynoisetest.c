/* aynoisetest.c: Tests for AY noise generator state updates
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <stdio.h>

static void
conditional_noise_step( int *rng, int *noise_toggle )
{
  if( ( *rng & 1 ) ^ ( ( *rng & 2 ) ? 1 : 0 ) )
    *noise_toggle = !*noise_toggle;

  if( *rng & 1 )
    *rng ^= 0x24000;
  *rng >>= 1;
}

static void
branch_free_noise_step( int *rng, int *noise_toggle )
{
  *noise_toggle ^= ( *rng ^ ( *rng >> 1 ) ) & 1;
  *rng = ( *rng >> 1 ) ^ ( 0x12000 & -( *rng & 1 ) );
}

int
main( void )
{
  int initial_rng, initial_noise_toggle;
  int conditional_rng, conditional_toggle;
  int branch_free_rng, branch_free_toggle;

  for( initial_rng = 0; initial_rng < 0x20000; initial_rng++ ) {
    for( initial_noise_toggle = 0; initial_noise_toggle < 2;
         initial_noise_toggle++ ) {
      conditional_rng = initial_rng;
      conditional_toggle = initial_noise_toggle;
      branch_free_rng = initial_rng;
      branch_free_toggle = initial_noise_toggle;

      conditional_noise_step( &conditional_rng, &conditional_toggle );
      branch_free_noise_step( &branch_free_rng, &branch_free_toggle );

      if( conditional_rng != branch_free_rng ||
          conditional_toggle != branch_free_toggle ) {
        fprintf( stderr,
                 "Mismatch for rng %#x, noise toggle %d: "
                 "conditional %#x/%d, branch-free %#x/%d\n",
                 initial_rng, initial_noise_toggle, conditional_rng,
                 conditional_toggle, branch_free_rng, branch_free_toggle );
        return 1;
      }
    }
  }

  return 0;
}
