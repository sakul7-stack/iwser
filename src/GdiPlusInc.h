#pragma once
// GDI+ needs min/max; with NOMINMAX defined globally we pull them from std
// before including the header (the standard workaround).
#include <algorithm>
using std::min;
using std::max;
#include <objidl.h>
#include <gdiplus.h>
