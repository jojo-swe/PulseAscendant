#ifndef EVO_RT_SPHERE_GLSL
#define EVO_RT_SPHERE_GLSL

// The analytic sphere's math (step 5, Task 8): the intersection shader (sphere.rint) and the sphere variants
// of the closest-hits (hit.glsl under GEOMETRY_SPHERES) share it, and spheres_test runs it in a compute probe
// against the CPU reference (rt::intersectSphere, rt::sphereUv in src/rt/spheres.hpp). Plain GLSL: no ray
// tracing built-ins.

// The hit kinds (gl_HitKindEXT; rt::kSphereHitOutside / kSphereHitInside): the ray enters the sphere from
// outside (its front), or leaves it from inside. 128-255 are the API's (the triangle facing kinds).
const uint kSphereHitOutside = 0u;
const uint kSphereHitInside = 1u;

// One Newton step on F(t) = |f + t d|^2 - r^2 (f = origin - centre): t - F / F' with F' = 2 (f + t d) . d.
// Skipped where the ray grazes the sphere (F' near 0: the two roots meet and a step could jump off).
float sphereNewton(vec3 f, vec3 d, float r, float t) {
    vec3 p = f + t * d;
    float slope = dot(p, d);
    if (!(abs(slope) > 0.01 * r * length(d))) {
        return t;
    }
    return t - (dot(p, p) - r * r) / (2.0 * slope);
}

// The first hit of origin + t direction (direction of any non-zero length, t then in its units) with the
// sphere's surface for t in [tMin, tMax]: the entry point when it is in range (kind kSphereHitOutside), else
// the exit point (kSphereHitInside). A robust quadratic (Haines et al., Ray Tracing Gems ch. 7): the
// discriminant from the distance of the line to the centre (no cancellation for a small sphere far away),
// the roots without subtracting near-equal values (c / q and q / a), then one Newton step on each.
bool sphereIntersect(vec3 origin, vec3 direction, vec3 center, float radius, float tMin, float tMax,
                     out float t, out uint kind) {
    t = 0.0;
    kind = kSphereHitOutside;
    float a = dot(direction, direction);
    if (!(radius > 0.0) || !(a > 0.0)) {
        return false;
    }
    vec3 f = origin - center;
    float b = dot(f, direction); // half the linear coefficient
    vec3 l = f - (b / a) * direction; // from the centre to the line's closest point
    float r2 = radius * radius;
    float disc = a * (r2 - dot(l, l)); // = b^2 - a (|f|^2 - r^2)
    if (!(disc >= 0.0)) {
        return false;
    }
    float q = -(b + (b >= 0.0 ? sqrt(disc) : -sqrt(disc)));
    float c = dot(f, f) - r2;
    float t0, t1;
    if (q != 0.0) {
        t0 = c / q;
        t1 = q / a;
    } else {
        t0 = 0.0;
        t1 = 0.0;
    }
    if (t0 > t1) {
        float s = t0;
        t0 = t1;
        t1 = s;
    }
    t0 = sphereNewton(f, direction, radius, t0);
    t1 = sphereNewton(f, direction, radius, t1);
    if (t0 >= tMin && t0 <= tMax) {
        t = t0;
        kind = kSphereHitOutside;
        return true;
    }
    if (t0 < tMin && t1 >= tMin && t1 <= tMax) {
        t = t1;
        kind = kSphereHitInside;
        return true;
    }
    return false;
}

// The spherical uv of a unit outward normal in the sphere's own frame, as modern::uvSphere maps its mesh: u
// the angle about +y from +x towards -z over 2 pi, in [0, 1); v = 0 at the top (+y), 1 at the bottom.
vec2 sphereUv(vec3 n) {
    float u = atan(-n.z, n.x) * (0.5 / 3.14159265358979);
    u = u < 0.0 ? u + 1.0 : u;
    u = u >= 1.0 ? u - 1.0 : u;
    return vec2(u, acos(clamp(n.y, -1.0, 1.0)) * (1.0 / 3.14159265358979));
}

// The change of the surface point per unit of u and of v at a unit normal n of a sphere of radius r (own
// frame): P = r (sin th cos ph, -cos th, -sin th sin ph) with ph = 2 pi u and th = pi (1 - v).
void sphereTangents(vec3 n, float r, out vec3 dPdu, out vec3 dPdv) {
    float sinTheta = sqrt(max(1.0 - n.y * n.y, 0.0));
    float cosPhi = sinTheta > 0.0 ? n.x / sinTheta : 1.0;
    float sinPhi = sinTheta > 0.0 ? -n.z / sinTheta : 0.0;
    float cosTheta = -n.y;
    const float twoPi = 6.28318530717959;
    dPdu = twoPi * r * vec3(-sinTheta * sinPhi, 0.0, -sinTheta * cosPhi);
    dPdv = -3.14159265358979 * r * vec3(cosTheta * cosPhi, sinTheta, -cosTheta * sinPhi);
}

#endif
