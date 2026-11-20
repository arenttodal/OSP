#pragma once

#include "model/ArpPatternModel.h"

#include <vector>

namespace osp::arppattern
{

/** The factory Custom ARP patterns (read-only; original phrases written for ANDOR, the
    validation corpus's shapes): saving one keeps a copy of your own, it is never overwritten.
    Ids are fixed ("factory.<name>") so sessions recognise them. */
const std::vector<Document>& factoryPatterns();

} // namespace osp::arppattern
