#include "basecmd.h"
#include "command.h"
#include <stdint.h>

#include "load_cell_probe.h"

void
command_mdf_ping(uint32_t *args)
{
    sendf("mdf_pong value=%u", 1234);
}
DECL_COMMAND(command_mdf_ping, "mdf_ping");

void
command_mdf_force_query(uint32_t *args)
{
    struct load_cell_probe *lce = load_cell_probe_oid_lookup(args[0]);
    int32_t force_q16 = load_cell_probe_get_last_filtered_grams(lce);
    sendf("mdf_force_state oid=%c force_q16=%i", args[0], force_q16);
}
DECL_COMMAND(command_mdf_force_query, "mdf_force_query oid=%c");
