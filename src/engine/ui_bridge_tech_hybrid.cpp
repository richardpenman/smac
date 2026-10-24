// Hybrid-build implementations of tech_achieved's announcement popup (see ui_bridge.h). The
// original builds it in a BasePop object on its own stack frame; the reconstruction does not
// display it (only reached for the viewer faction or in the omniscient view). Native builds use
// src/headless/ui_headless_tech.cpp.
#ifdef SMACX_HYBRID
#include "engine/ui_bridge.h"

void ui_tech_popup_open() {}
void ui_tech_popup_show(int32_t, int32_t, int32_t, bool) {}
void ui_tech_popup_exec() {}
#endif
