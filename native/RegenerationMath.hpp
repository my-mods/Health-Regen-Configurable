#pragma once
#include <algorithm>
#include <cmath>
namespace HealthRegeneration {
struct Tick { float unlock{}, heal{}; };
inline Tick segmentTick(float blood,float capacity,float permanentDamage,float rate) noexcept {
    if(!std::isfinite(blood)||!std::isfinite(capacity)||!std::isfinite(permanentDamage)||!std::isfinite(rate)
        || capacity<=0 || blood<=0 || rate<=0 || rate>0.05f || permanentDamage<0 || permanentDamage>capacity) return {};
    const float amount=std::min(std::max(0.0f,capacity-blood),capacity*rate);
    const float headroom=std::max(0.0f,capacity-permanentDamage-blood);
    return {-std::min(permanentDamage,std::max(0.0f,amount-headroom)),amount};
}
}
