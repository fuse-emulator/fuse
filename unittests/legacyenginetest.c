/* Generic legacy-engine link consumer, without platform bootstrap.
   This deliberately exercises the private runner until lifecycle is extracted;
   it is not an example of a supported embedding execution API. */
#include "config.h"
#include "fuse-internal.h"

int
main( int argc, char **argv )
{
  return fuse_legacy_main( argc, argv );
}
