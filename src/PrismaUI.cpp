#include "PCH.h"
#include "PrismaUI.h"

// PrismaUI::s_instance is defined inline in PrismaUI.h.
// This translation unit exists only to keep the CMake source list consistent.
// The actual singleton is populated by PrismaUI calling PrismaUI::Set() during
// its own plugin load via SKSE kPostLoad messaging.
