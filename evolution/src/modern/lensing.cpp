#include "modern/lensing.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace evo::modern {

rt::LensingUniforms packLensing(const LensingFrame& lens) {
    const auto finite = [](float v) { return std::isfinite(v); };
    if (!finite(lens.center.x) || !finite(lens.center.y) || !finite(lens.center.z) || !finite(lens.massRadius) ||
        !finite(lens.influenceRadius) || !finite(lens.stepScale))
        throw std::invalid_argument("packLensing: a value is not finite");
    if (lens.massRadius < 0.f)
        throw std::invalid_argument("packLensing: a negative mass radius");
    if (!(lens.influenceRadius > 1.5f * lens.massRadius) || !(lens.influenceRadius > 0.f))
        throw std::invalid_argument("packLensing: the influence radius must exceed 1.5 r_s");
    if (lens.steps < 1 || lens.steps > kMaxGeodesicSteps)
        throw std::invalid_argument("packLensing: " + std::to_string(lens.steps) + " steps, not in [1, " +
                                    std::to_string(kMaxGeodesicSteps) + "]");
    if (!(lens.stepScale > 0.f && lens.stepScale <= 1.f))
        throw std::invalid_argument("packLensing: the step scale must be in (0, 1]");
    rt::LensingUniforms u{};
    u.center[0] = lens.center.x;
    u.center[1] = lens.center.y;
    u.center[2] = lens.center.z;
    u.massRadius = lens.massRadius;
    u.influenceRadius = lens.influenceRadius;
    u.steps = lens.steps;
    u.flags = lens.disc ? rt::kLensingDisc : 0u;
    u.stepScale = lens.stepScale;
    return u;
}

} // namespace evo::modern
