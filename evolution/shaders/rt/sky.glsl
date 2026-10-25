#ifndef EVO_RT_SKY_GLSL
#define EVO_RT_SKY_GLSL

// The sky models of the scene environments (EnvironmentKind, src/modern/environment.hpp): radiance in linear
// light, +y up, before the environment's intensity. Pure functions of the direction and of the
// EnvironmentUniforms `environment` (declared by the includer: common.glsl, or a test probe), so every ray
// that reaches a direction sees the same sky there.
//
// - The background: a gradient from the horizon to the zenith above and the ground below, the town's warm
//   glow along the horizon, a pale band of haze, and the halo around the disc's direction.
// - The disc (sun or moon): uniform for lighting; limb darkened with the same power for the view.
// - Stars: one per hashed cell of a cube around the viewer, at a jittered point inside the cell, drawn as a
//   Gaussian of fixed flux whose width is the larger of the star's own and the ray cone's footprint, so the
//   flux a pixel receives does not depend on the resolution and a star does not alias at 1 spp.
// - The town (NightSky, Dusk): lights in a band under the horizon, clustered along hashed street lines,
//   dimmer towards the horizon, drawn the same way.
// NightSky with its defaults is step 4's night sky, whose stars and town lights were lit cells.

#include "random.glsl"

const uint kEnvironmentNightSky = 0u;
const uint kEnvironmentSpace = 1u;
const uint kEnvironmentDusk = 2u;
const uint kEnvironmentDay = 3u;

const float kSkyPi = 3.14159265358979;

// A normalised 2D Gaussian (integral 1) at offset delta; 0 beyond 5 sigma.
float skyGaussian(vec2 delta, float sigma) {
    float s2 = sigma * sigma;
    float r2 = dot(delta, delta);
    if (r2 > 25.0 * s2) {
        return 0.0;
    }
    return exp(-0.5 * r2 / s2) / (2.0 * kSkyPi * s2);
}

// --- The background ----------------------------------------------------------------------------------------

// The town's warm glow along the horizon (half of it under the horizon), with the town's density.
vec3 skyTownGlow(float y) {
    vec3 glow = environment.townDensity * vec3(0.045, 0.028, 0.016) * exp(-abs(y) * 18.0);
    return y < 0.0 ? 0.5 * glow : glow;
}

// The halo around the disc's direction, above the horizon: the moon's at night, the sun's aureole by day.
vec3 skyGlow(vec3 d) {
    if (!(environment.glowStrength > 0.0) || d.y < 0.0) {
        return vec3(0.0);
    }
    float angle = acos(clamp(dot(d, environment.glowDirection), -1.0, 1.0));
    vec3 g = vec3(0.0);
    if (environment.kind == kEnvironmentNightSky) {
        g = vec3(0.03, 0.04, 0.06) * exp(-angle * 4.0) + vec3(0.12, 0.13, 0.15) * exp(-angle * 30.0);
    } else if (environment.kind == kEnvironmentDusk) {
        g = vec3(0.3, 0.12, 0.04) * exp(-angle * 2.0) + vec3(1.6, 0.8, 0.3) * exp(-angle * 12.0);
    } else if (environment.kind == kEnvironmentDay) {
        g = vec3(0.15, 0.13, 0.1) * exp(-angle * 3.0) + vec3(2.0, 1.8, 1.5) * exp(-angle * 25.0);
    }
    return environment.glowStrength * g;
}

// Everything smooth: the gradient, the town's glow, the haze and the halo.
vec3 skyBackground(vec3 d) {
    float y = d.y;
    vec3 sky = y < 0.0 ? environment.ground : mix(environment.horizon, environment.zenith, sqrt(y));
    sky += skyTownGlow(y);
    if (environment.haze > 0.0) {
        float grey = dot(environment.horizon, vec3(0.2126, 0.7152, 0.0722));
        vec3 pale = mix(environment.horizon, vec3(grey), 0.5);
        sky = mix(sky, pale, clamp(environment.haze * exp(-abs(y) * 12.0), 0.0, 1.0));
    }
    return sky + skyGlow(d);
}

// --- The disc ----------------------------------------------------------------------------------------------

bool skyHasDisc() {
    return environment.discCosRadius < 1.0;
}

// The disc's radiance along d (0 outside it): uniform for lighting; for the view, limb darkened by
// 1 - u (1 - mu) and divided by 1 - u / 3 so that its power stays the same (u = 0 for the moon).
vec3 skyDisc(vec3 d, bool limbDarkened) {
    float c = dot(d, environment.discDirection);
    if (!skyHasDisc() || c < environment.discCosRadius) {
        return vec3(0.0);
    }
    vec3 radiance = environment.discRadiance;
    float u = environment.kind == kEnvironmentNightSky ? 0.0 : 0.6;
    if (limbDarkened && u > 0.0) {
        // rho^2 = (1 - cos angle) / (1 - cos radius): the squared distance from the centre, in radii.
        float rho2 = (1.0 - c) / (1.0 - environment.discCosRadius);
        float mu = sqrt(max(0.0, 1.0 - rho2));
        radiance *= (1.0 - u * (1.0 - mu)) / (1.0 - u / 3.0);
    }
    return radiance;
}

// --- Stars -------------------------------------------------------------------------------------------------

const uint kStarCells = 384u;             // per cube face side
const float kStarShare = 180.0 / 65536.0; // the cells with a star at starDensity 1, over the cube (step 4's)
// A cell's mean solid angle over the cube, 4 pi / (6 * 384^2): a star's flux is its brightness times it.
const float kStarCellSolidAngle = 4.0 * kSkyPi / (6.0 * float(kStarCells) * float(kStarCells));
const float kStarSigma = 2.5e-4;          // radians: a star's own width (a 4K pixel is about 3e-4)

// The direction's cube face (+x, -x, +y, -y, +z, -z) and its coordinates on it, in [-1, 1].
void skyCubeFace(vec3 d, out uint face, out vec2 uv) {
    vec3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z) {
        face = d.x > 0.0 ? 0u : 1u;
        uv = d.yz / a.x;
    } else if (a.y >= a.z) {
        face = d.y > 0.0 ? 2u : 3u;
        uv = d.xz / a.y;
    } else {
        face = d.z > 0.0 ? 4u : 5u;
        uv = d.xy / a.z;
    }
}

// A normalised Gaussian (integral 1) in a face's (u, v) plane, elongated along `radial` (a unit vector, or
// zero at the face's centre): sigmaRadial along it, sigmaAcross across it; 0 beyond 5 sigma.
float skyEllipse(vec2 delta, vec2 radial, float sigmaRadial, float sigmaAcross) {
    float along = dot(delta, radial);
    float across2 = max(dot(delta, delta) - along * along, 0.0);
    float q = along * along / (sigmaRadial * sigmaRadial) + across2 / (sigmaAcross * sigmaAcross);
    if (q > 25.0) {
        return 0.0;
    }
    return exp(-0.5 * q) / (2.0 * kSkyPi * sigmaRadial * sigmaAcross);
}

// The stars along d. On a face, d(solid angle) = du dv / s^1.5 with s = 1 + u^2 + v^2, and an angle dt maps
// to du = s dt along the radius and sqrt(s) dt across it. So a star is a Gaussian in (u, v) of width
// max(kStarSigma, footprint) converted to uv that way at the star (round on the sphere), and radiance =
// flux * G_uv * s^1.5 integrates to its flux exactly; the flux is brightness times a cell's mean solid
// angle. A cell holds a star with a chance proportional to its solid angle, so the stars per steradian are
// the same everywhere on the cube (step 4 had up to 5 times more at its corners), as many as step 4's over the
// whole sky, with step 4's mean radiance. Stars sit in the middle
// half of their cells, never in a face's outer ring, and are at most 0.3 cells long, so the 3 x 3 cells
// around d hold every star that reaches it and none is cut at a cube edge. A footprint wider than that (a
// cell under about 3 pixels) leaves the stars at 0.3 cells: the pixel's own filter averages them.
vec3 skyStars(vec3 d, float footprint) {
    uint face;
    vec2 uv;
    skyCubeFace(d, face, uv);
    float cells = float(kStarCells);
    float cellUv = 2.0 / cells;
    vec2 g = (uv * 0.5 + 0.5) * cells;
    ivec2 c = clamp(ivec2(floor(g)), ivec2(0), ivec2(int(kStarCells) - 1));
    // At a face's centre a cell is cellUv^2 of solid angle, more than the mean: proportionally more stars.
    float share = kStarShare * environment.starDensity * cellUv * cellUv / kStarCellSolidAngle;
    float sigma = max(kStarSigma, footprint);
    vec3 sum = vec3(0.0);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            ivec2 k = c + ivec2(dx, dy);
            if (any(lessThan(k, ivec2(1))) || any(greaterThan(k, ivec2(int(kStarCells) - 2)))) {
                continue;
            }
            vec2 centreUv = (vec2(k) + 0.5) * cellUv - 1.0;
            float cellStretch = 1.0 + dot(centreUv, centreUv);
            float threshold = min(share / (cellStretch * sqrt(cellStretch)), 1.0) * 65536.0;
            uint h = pcgHash(uint(k.x) + kStarCells * (uint(k.y) + kStarCells * face) + 0x51A7u);
            if (float(h & 0xFFFFu) >= threshold) {
                continue;
            }
            uint j = pcgHash(h ^ 0x9E3779B9u);
            vec2 position = vec2(k) + 0.25 + 0.5 * vec2(float(j & 0xFFFFu), float(j >> 16u)) / 65535.0;
            vec2 starUv = position * cellUv - 1.0;
            float stretch = 1.0 + dot(starUv, starUv);
            float sigmaRadial = min(sigma * stretch, 0.3 * cellUv);
            float sigmaAcross = sigmaRadial / sqrt(stretch);
            vec2 radial = dot(starUv, starUv) > 0.0 ? normalize(starUv) : vec2(1.0, 0.0);
            float u = float((h >> 16u) & 0xFFu) / 255.0;
            float brightness = 0.1 + 2.1 * (u * u) * (u * u); // many faint, a few bright; mean 0.52
            float warmth = float((h >> 24u) & 0xFFu) / 255.0;
            vec3 tint = mix(vec3(0.75, 0.85, 1.0), vec3(1.0, 0.9, 0.75), warmth);
            float flux = brightness * kStarCellSolidAngle;
            sum += flux * tint * skyEllipse((g - position) * cellUv, radial, sigmaRadial, sigmaAcross);
        }
    }
    float stretchHere = 1.0 + dot(uv, uv);
    return environment.starBrightness * sum * (stretchHere * sqrt(stretchHere));
}

// How much of the star field shows along d: all of it in space; elsewhere it fades out towards the horizon.
float skyStarVisibility(vec3 d) {
    return environment.kind == kEnvironmentSpace ? 1.0 : smoothstep(0.03, 0.2, d.y);
}

// --- The town ----------------------------------------------------------------------------------------------

const float kTownDepth = 0.06;     // the band under the horizon, in -y
const int kTownRows = 12;          // rows of depression across the band
const int kTownColumns = 1536;     // cells around the horizon
const int kTownStreetCells = 12;   // a street runs this many columns before its row is hashed again
const float kTownSigma = 3e-4;     // radians: a light's own width
const float kTownFlux = 0.09;      // a light's flux: this x its brightness x its cell's solid angle

// Where along the horizon the town stands: clumps and gaps, a smooth value noise of the azimuth's column.
float skyTownCluster(float column) {
    const float knots = 24.0;
    float t = column / float(kTownColumns) * knots;
    float i0 = floor(t);
    float a = float(pcgHash(uint(mod(i0, knots)) + 0x7A11u) & 0xFFFFu) / 65535.0;
    float b = float(pcgHash(uint(mod(i0 + 1.0, knots)) + 0x7A11u) & 0xFFFFu) / 65535.0;
    return smoothstep(0.25, 0.75, mix(a, b, smoothstep(0.0, 1.0, t - i0)));
}

// The town's lights along d (y from -kTownDepth to 0), in (azimuth, y) coordinates, in which d(solid angle) =
// d(azimuth) dy exactly; each light a Gaussian of fixed flux, as for the stars (a light sits at least 0.15
// cells from its cell's sides, and is at most 0.3 cells wide). A row of cells is a street over a stretch of
// kTownStreetCells columns, and a column a road through every row, with chances that follow the clusters;
// streets and roads are densely lit along their lines, elsewhere a few windows. Lights towards the horizon
// are farther: dimmer, lost in the town's glow.
vec3 skyTownLights(vec3 d, float footprint) {
    float rowHeight = kTownDepth / float(kTownRows);
    if (!(environment.townDensity > 0.0) || !(d.y > -kTownDepth - 2.0 * rowHeight) || !(d.y < 2.0 * rowHeight)) {
        return vec3(0.0);
    }
    float columnWidth = 2.0 * kSkyPi / float(kTownColumns);
    vec2 g = vec2((atan(d.x, d.z) + kSkyPi) / columnWidth, -d.y / rowHeight);
    ivec2 c = ivec2(floor(g));
    float sigma = min(max(kTownSigma, footprint), 0.3 * min(columnWidth, rowHeight));
    float density = min(environment.townDensity, 1.0);
    vec3 sum = vec3(0.0);
    for (int dy = -1; dy <= 1; ++dy) {
        int row = c.y + dy;
        if (row < 0 || row >= kTownRows) {
            continue;
        }
        for (int dx = -1; dx <= 1; ++dx) {
            int column = c.x + dx;
            column = column < 0 ? column + kTownColumns : (column >= kTownColumns ? column - kTownColumns : column);
            float cluster = skyTownCluster(float(column) + 0.5);
            // A street across the view: this row over a stretch of columns, its line at a hashed depth.
            uint s = pcgHash(uint(column / kTownStreetCells) * 0x9E3779B1u + uint(row) * 0x85EBCA6Bu + 0xC17Eu);
            bool street = float(s & 0xFFFFu) / 65536.0 < 0.35 * cluster;
            float streetLine = 0.3 + 0.4 * float(s >> 16u) / 65535.0;
            // A road leading away: this column through every row.
            bool road = float(pcgHash(uint(column) * 0xB5297A4Du + 0x3D0Au) & 0xFFFFu) / 65536.0 < 0.05 * cluster;
            uint h = pcgHash(uint(column) * 0x27D4EB2Fu + uint(row) * 0x165667B1u + 0x70E1u);
            float chance = density * (street ? 0.7 : (road ? 0.6 : 0.03 * cluster));
            if (!(float(h & 0xFFFFu) / 65536.0 < chance)) {
                continue;
            }
            uint j = pcgHash(h ^ 0x632BE5ABu);
            float a = float(j & 0xFFFFu) / 65535.0;
            float b = float(j >> 16u) / 65535.0;
            vec2 offset = street ? vec2(0.15 + 0.7 * a, streetLine + 0.06 * (b - 0.5))
                                 : (road ? vec2(0.47 + 0.06 * a, 0.15 + 0.7 * b) : vec2(0.15 + 0.7 * a, 0.3 + 0.4 * b));
            vec2 position = vec2(float(column), float(row)) + offset;
            vec2 delta = g - position;
            if (delta.x > 0.5 * float(kTownColumns)) {
                delta.x -= float(kTownColumns);
            } else if (delta.x < -0.5 * float(kTownColumns)) {
                delta.x += float(kTownColumns);
            }
            float nearness = position.y / float(kTownRows); // 0 at the horizon, 1 at the band's foot
            float brightness = (0.3 + 1.2 * float((h >> 16u) & 0xFFu) / 255.0) * mix(0.25, 1.0, nearness);
            bool cold = ((h >> 24u) & 7u) == 0u;
            vec3 colour = cold ? vec3(0.7, 0.8, 1.0) : vec3(1.0, 0.62, 0.3);
            float flux = kTownFlux * brightness * columnWidth * rowHeight;
            sum += flux * colour * skyGaussian(delta * vec2(columnWidth, rowHeight), sigma);
        }
    }
    return sum;
}

#endif
