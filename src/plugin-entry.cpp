#include <clap/clap.h>

#include "plugin-entry.h"

extern "C" {
CLAP_EXPORT const clap_plugin_entry clap_entry = {CLAP_VERSION_INIT, sgl_entry_init, sgl_entry_deinit,
						  sgl_entry_get_factory};
}
