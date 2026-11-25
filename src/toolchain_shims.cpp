// Link-time shims for the pinned upstream picosystem (see docs/DECISIONS.md).
//
// Upstream picosystem's memmap_picosystem.ld does not define __dso_handle, but
// libstdc++ (pulled in by picosystem's utility.cpp via std::string) references it
// through __aeabi_atexit. A fork of picosystem fixes this in the linker script;
// we define it here so the pinned submodule stays unmodified.
extern "C" {
void *__dso_handle = nullptr;
}
