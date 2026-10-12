#include "engine/Dampen.h"

namespace osp::dampen
{

mod::Curve defaultCurve() noexcept
{
    mod::Curve c;
    c.points[0] = { 0.0f, 0.0f, 0.0f };
    c.points[1] = { 60.0f / 127.0f, 0.0f, 0.0f };   // C4
    c.points[2] = { 1.0f, 1.0f, 0.2f };
    c.count = 3;
    return c;
}

Settings defaultSettings() noexcept
{
    Settings s;
    s.curve = mod::compileCurve (defaultCurve());
    return s;
}

} // namespace osp::dampen
