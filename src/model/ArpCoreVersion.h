#pragma once

namespace osp::arp
{

/**
    andor_arp_core's own version (arp-core/CMakeLists.txt), independent of any product's version.
    ANDOR ARP records it, with the OSP commit it was built from, in every build's diagnostics.

    - major: a change that alters what an existing setting or stored pattern plays (the 18-style
      golden or a saved pattern's interpretation changes) or removes an interface;
    - minor: something added (a style, a mapping, an interface), everything stored plays as before;
    - patch: fixes with no change to any golden.
*/
inline constexpr int coreVersionMajor = 1;
inline constexpr int coreVersionMinor = 0;
inline constexpr int coreVersionPatch = 0;
inline constexpr const char* coreVersion = "1.0.0";

} // namespace osp::arp
