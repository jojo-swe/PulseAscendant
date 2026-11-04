#ifndef EVO_MODERN_GALAXY_GLSL
#define EVO_MODERN_GALAXY_GLSL

// The galaxy of the Cosmos and Singularity scenes: its stars, a pure function of
// (id, count), and its dust, a pure function of the position. src/modern/scenes/cosmos.cpp mirrors the dust
// (galaxyDust) and the colours (blackbodyColour) on the CPU and documents the model.
//
// Galaxy space: the disc lies in the xz plane, y is its axis, and lengths are in units of the outer radius
// (the stars thin out past 1). Two logarithmic arms of pitch kGalaxyPitch trail as the galaxy turns about +y.

const float kGalaxyPi = 3.14159265;
const float kGalaxyTau = 6.28318531;
const float kGalaxyPitch = 0.26;      // tan of the arms' pitch angle (about 15 degrees)
const float kGalaxyArmStart = 0.07;   // where the arms leave the bulge
const float kGalaxyLaneOffset = 0.3; // the dust lanes trail the arms' spines by this angle (radians)
const float kGalaxyDustHalfHeight = 0.125; // the dust grid's box: y in +-this (x and z in +-1)

uint galaxyHash(uint x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

float galaxyUnit(uint x) {
    return float(x >> 8) * (1.0 / 16777216.0);
}

// The arms are not twins: arm 0 is the stronger, arm 1 a little looser, each spine bent by slow wiggles of
// its own, and from kGalaxyBranchStart a weaker branch (arm 2) leaves arm 0, opening at a looser pitch.
const float kGalaxyBranchStart = 0.38;

float galaxyWrap(float a) {
    return a - kGalaxyTau * floor(a / kGalaxyTau + 0.5);
}

// Arm `arm`'s spine: its azimuth at log radius lr = log(radius / kGalaxyArmStart).
float galaxySpine(float lr, int arm) {
    if (arm == 1) {
        return lr / (1.1 * kGalaxyPitch) + kGalaxyPi + 0.3 * sin(1.7 * lr + 2.6) + 0.1 * sin(4.6 * lr + 0.4);
    }
    float spine = lr / kGalaxyPitch + 0.22 * sin(2.1 * lr + 0.7) + 0.08 * sin(5.3 * lr + 2.0);
    if (arm == 2) {
        spine -= 1.25 * max(lr - log(kGalaxyBranchStart / kGalaxyArmStart), 0.0);
    }
    return spine;
}

// The angle from arm `arm`'s spine at (radius, azimuth), in [-pi, pi): 0 on the spine, negative on its inner
// (trailing, concave) side where its dust lane lies.
float galaxyArmOffset(float radius, float azimuth, int arm) {
    return galaxyWrap(azimuth - galaxySpine(log(max(radius, 1e-4) / kGalaxyArmStart), arm));
}

// The angle from the nearest of the two main arms' spines.
float galaxyArmPhase(float radius, float azimuth) {
    float a0 = galaxyArmOffset(radius, azimuth, 0), a1 = galaxyArmOffset(radius, azimuth, 1);
    return abs(a0) <= abs(a1) ? a0 : a1;
}

// A star's colour of temperature `kelvin`: Planck's law at 605, 545 and 460 nm, scaled to luminance 1 (Rec.
// 709 weights), so a star's flux sets its brightness and the temperature its hue alone.
vec3 blackbodyColour(float kelvin) {
    const vec3 lambda = vec3(605.0, 545.0, 460.0);
    vec3 planck = 1.0 / (pow(lambda / 500.0, vec3(5.0)) * (exp(1.4388e7 / (lambda * max(kelvin, 500.0))) - 1.0));
    return planck / max(dot(planck, vec3(0.2126, 0.7152, 0.0722)), 1e-30);
}

// The 3-D value noise of the dust: trilinear between hashed lattice values, smoothstep weights, in [0, 1].
float galaxyLattice(ivec3 c) {
    return galaxyUnit(galaxyHash(uint(c.x) * 0x8DA6B343u ^ uint(c.y) * 0xD8163841u ^ uint(c.z) * 0xCB1AB31Fu));
}

float galaxyNoise(vec3 p) {
    vec3 f = floor(p);
    ivec3 c = ivec3(f);
    vec3 t = p - f;
    t = t * t * (3.0 - 2.0 * t);
    float x00 = mix(galaxyLattice(c), galaxyLattice(c + ivec3(1, 0, 0)), t.x);
    float x10 = mix(galaxyLattice(c + ivec3(0, 1, 0)), galaxyLattice(c + ivec3(1, 1, 0)), t.x);
    float x01 = mix(galaxyLattice(c + ivec3(0, 0, 1)), galaxyLattice(c + ivec3(1, 0, 1)), t.x);
    float x11 = mix(galaxyLattice(c + ivec3(0, 1, 1)), galaxyLattice(c + ivec3(1, 1, 1)), t.x);
    return mix(mix(x00, x10, t.y), mix(x01, x11, t.y), t.z);
}

// The dust at galaxy-space point p (density, then the HII emission's strength): lanes along the arms' inner
// edges (the branch's fainter), feathers of dust leaving them across the inter-arm gaps at a steeper pitch, a
// thin diffuse disc that shows edge-on as the lane across the bulge, all broken by two octaves of noise into
// filaments; pink HII knots sit just ahead of the lanes on the spines. cosmos.cpp's galaxyDust mirrors it.
vec2 galaxyDust(vec3 p) {
    float radius = length(p.xz);
    float azimuth = atan(p.z, p.x);
    float lr = log(max(radius, 1e-4) / kGalaxyArmStart);
    float vertical = exp(-abs(p.y) / 0.011);
    float envelope = smoothstep(0.05, 0.16, radius) * (1.0 - smoothstep(0.82, 1.05, radius));
    float laneWidth = 0.08 + 0.05 * radius;
    float lane = 0.0;
    float feathers = 0.0;
    for (int arm = 0; arm < 2; ++arm) {
        float off = galaxyArmOffset(radius, azimuth, arm);
        float strength = arm == 0 ? 1.0 : 0.8;
        lane += strength * (exp(-pow((off + kGalaxyLaneOffset) / laneWidth, 2.0)) +
                            0.5 * exp(-pow((off + 0.6) / (1.6 * laneWidth), 2.0)));
        // Feathers: thin dust spurs that leave the lane inwards across the gap, trailing at a steep pitch.
        float gap = smoothstep(-1.5, -1.1, off) * (1.0 - smoothstep(-0.42, -0.3, off));
        float spur = pow(max(sin(8.0 * (off + 0.5 * lr) + 1.3 * float(arm)), 0.0), 10.0);
        feathers += strength * gap * spur;
    }
    float branch = galaxyArmOffset(radius, azimuth, 2);
    lane += 0.65 * smoothstep(kGalaxyBranchStart, kGalaxyBranchStart + 0.1, radius) *
            exp(-pow((branch + 0.22) / (0.8 * laneWidth), 2.0));
    float noise = 0.65 * galaxyNoise(p * 22.0) + 0.35 * galaxyNoise(p * 61.0 + 17.0);
    float filaments = smoothstep(0.3, 0.7, noise);
    float disc = 0.22 * exp(-radius / 0.4) * smoothstep(0.02, 0.08, radius);
    float density = vertical * envelope * (4.0 * lane * filaments + 1.6 * feathers * filaments + disc * (0.5 + filaments));
    // HII knots: clumps of the noise along the spines, in the disc's mid-plane.
    float spine = exp(-pow((galaxyArmPhase(radius, azimuth) - 0.03) / 0.07, 2.0));
    float knot = pow(galaxyNoise(p * 48.0 + 5.0), 4.0) * spine * envelope * exp(-abs(p.y) / 0.008);
    return vec2(density + 2.0 * knot, 22.0 * knot);
}

// Star `id` of `count` in galaxy space, its colour temperature and its brightness (a weight of the average
// star's flux, which is 1). Three populations: the bulge (18 %: old, red giants among them), the disc (32 %:
// exponential, mixed ages) and the arms (50 %: young, thin, blue on the spines, with a few red supergiants).
void galaxyStar(uint id, out vec3 p, out float kelvin, out float weight, out bool bulge) {
    uint h = galaxyHash(id * 0x9E3779B1u ^ 0x6A1A3Bu);
    float u0 = galaxyUnit(h), u1 = galaxyUnit(galaxyHash(h + 1u)), u2 = galaxyUnit(galaxyHash(h + 2u));
    float u3 = galaxyUnit(galaxyHash(h + 3u)), u4 = galaxyUnit(galaxyHash(h + 4u)),
          u5 = galaxyUnit(galaxyHash(h + 5u));
    float g = sqrt(-2.0 * log(max(u3, 1e-7))) * cos(kGalaxyTau * u4); // a Gaussian
    bulge = u0 < 0.18;
    if (bulge) {
        // An oblate ball, dense in the middle (r^-1.6 or so), flattened 0.6 along the axis.
        float r = 0.16 * pow(u1, 1.7) + 0.004;
        float z = 2.0 * u2 - 1.0;
        float a = kGalaxyTau * u5;
        float s = sqrt(max(1.0 - z * z, 0.0));
        p = r * vec3(s * cos(a), 0.6 * z, s * sin(a));
        kelvin = mix(3100.0, 5200.0, u4 * u4);
        weight = 1.1;
    } else if (u0 < 0.5) {
        float r = -0.3 * log(1.0 - 0.95 * u1) + 0.02;
        float a = kGalaxyTau * u2;
        p = vec3(r * cos(a), 0.016 * g, r * sin(a));
        kelvin = mix(3300.0, 7500.0, u5 * u5);
        weight = 0.7;
    } else {
        // Along an arm: the spine at this radius, scattered across it (narrower for the young); a third of
        // the young stars gather in clusters strung along the spine.
        float r = kGalaxyArmStart + 1.0 * pow(u1, 0.8);
        float young = u5;
        // Arm 0 holds more stars than arm 1; past kGalaxyBranchStart a third of arm 0's go to its branch.
        int arm = u0 < 0.79 ? 0 : 1;
        if (arm == 0 && r > kGalaxyBranchStart && galaxyUnit(galaxyHash(h + 11u)) < 0.3) {
            arm = 2;
        }
        float across = (0.022 + 0.06 * (1.0 - young)) * (0.5 + r) * g;
        vec2 clusterOffset = vec2(0.0);
        float a;
        if (young > 0.55 && galaxyUnit(galaxyHash(h + 7u)) < 0.6) {
            uint c = galaxyHash(uint(u1 * 2400.0) * 0x2545F491u + uint(arm) * 7u);
            r = kGalaxyArmStart + pow(galaxyUnit(c), 0.8);
            float gc = sqrt(-2.0 * log(max(galaxyUnit(galaxyHash(c + 1u)), 1e-7))) *
                       cos(kGalaxyTau * galaxyUnit(galaxyHash(c + 2u)));
            across = 0.03 * (0.5 + r) * gc;
            float g2 = sqrt(-2.0 * log(max(u2, 1e-7)));
            clusterOffset = 0.007 * g2 * vec2(cos(kGalaxyTau * u3), sin(kGalaxyTau * u3));
            a = galaxySpine(log(r / kGalaxyArmStart), arm);
        } else if (galaxyUnit(galaxyHash(h + 12u)) < 0.16) {
            // A spur: seven to a factor e of radius, each leaving the spine on its outer side (the lanes are on
            // the inner) at a steep pitch, out into the gap.
            float lr = log(r / kGalaxyArmStart);
            float j = floor(lr * 7.0);
            float base = (j + 0.5 + 0.35 * (galaxyUnit(galaxyHash(uint(j) * 0x9E3779B1u + uint(arm) * 77u)) - 0.5)) / 7.0;
            float v = galaxyUnit(galaxyHash(h + 13u));
            r = kGalaxyArmStart * exp(base) * (1.0 + 0.12 * v);
            a = galaxySpine(base, arm) + 0.5 * v;
            across *= 0.35;
        } else {
            a = galaxySpine(log(r / kGalaxyArmStart), arm);
        }
        float rr = max(r + across, 0.01);
        p = vec3(rr * cos(a) + clusterOffset.x,
                 0.006 * (2.0 - young) * sqrt(-2.0 * log(max(u2, 1e-7))) * sign(u4 - 0.5),
                 rr * sin(a) + clusterOffset.y);
        kelvin = young > 0.97 ? 3400.0 : mix(5000.0, 24000.0, young * young);
        weight = 0.45 + 0.75 * young;
    }
    // A few bright stars among many faint ones (about 1 in 400 is 20x the average).
    float bright = galaxyUnit(galaxyHash(h + 6u));
    weight *= 0.55 + 18.0 * pow(bright, 60.0);
}

// Galaxy space about y by `turn` radians (the galaxy's rotation; the arms trail).
vec3 galaxyTurn(vec3 p, float turn) {
    float c = cos(turn), s = sin(turn);
    return vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
}

// Galaxy space to the world: tilt about x, then yaw about y, scaled by the radius, about the centre.
vec3 galaxyToWorld(vec3 p, vec3 centre, float radius, float tilt, float yaw) {
    float ct = cos(tilt), st = sin(tilt), cy = cos(yaw), sy = sin(yaw);
    vec3 q = vec3(p.x, ct * p.y - st * p.z, st * p.y + ct * p.z);
    q = vec3(cy * q.x + sy * q.z, q.y, -sy * q.x + cy * q.z);
    return centre + radius * q;
}

#endif
