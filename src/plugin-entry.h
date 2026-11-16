#pragma once

// Implemented in the static library and exported by plugin-entry.cpp, as clap-wrapper's clap-first layout expects.
bool sgl_entry_init(const char *pluginPath);
void sgl_entry_deinit(void);
const void *sgl_entry_get_factory(const char *factoryId);
