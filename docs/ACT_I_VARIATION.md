# Act I variation revision

This revision replaces the eight reused scene layouts with chronological set-pieces. It consumes `uVariant` (occurrence within the act), monotonic `uChapterProgress`, local `uChapterTime`, the one-time music action `uAction`, and the single event glow `uEvent`. Legacy shot numbers no longer select these geometries or cameras.

- **Cosmic:** core ignition with six energy lanes and an expanding halo; two stars converge and collide; a hollow supernova shell expands; spiral plasma filaments form a different wide composition; longitudinal energy coils become an actual departure.
- **Corridor:** round launch doors peel away; hexagonal accelerator with turbine blades; low wide split rails and ceiling machinery; open magnetic trench. A fifth broken-shutter design is present but unreached by this timeline.
- **Engine:** reactor gimbals uncage the core; a longitudinal rail cannon replaces the central rings; exploded modules converge into a tall cradle; cathedral blades open around a taller crystal; four decks align into a launch spire. Convex crystal clipping now supports nonuniform scale and inverse-scale transformed normals.
- **City:** street-level growing towers; an uninterrupted bird's-eye pass above a 36-unit stepped landmark and terraced skyline; a sideways cross-street dolly toward a broad factory with twin chimneys across a geometrically cleared plaza; floating districts.
- **Structure:** split monolithic gates and lifting keystones; a low machine hall with pistons and conveyors; fractured slabs peel away; triangular helix frames unfold into longitudinal rails.
- **Armada:** escorts spread around a flagship; close flyby of one immense asymmetric carrier; launch from a sliding hangar interior; pursuit of one fighter through debris with a single music-triggered volley.
- **Ocean:** open monolith sea; a downward aerial pass over three large split levitating islands with tapered undersides; a low lateral close-up of three tall branching crystal reeds, looking up into their crowns with backlighting. These deliberately use different dominant compositions. Motion is restrained to give the fast scenes contrast.
- **Sculpture:** a porous gyroid collapses and ejects fragments; asymmetric slab spires assemble after the shock. Two additional unreached designs (twin braid / origami petals) support future direction without depending on modulo cycling.

The changes add no external assets and preserve GLSL 330 and the retro path. All eight fully expanded shaders passed the local glslang front-end syntax check. That is source validation, not visual or runtime acceptance. GPU captures and motion review are required after integration. No project build or GPU work was run by the shader author.

## Representative capture points

Times below use the checked-in music-directed Act I starts. Each pair samples 25% and 80% of the same chapter; motion between them should show a developing event rather than another orbit. The actual event may occur between 28% and 76%, according to music analysis.

| Chapter range, seconds | Shader | Occurrence | Cue | Early / late capture seconds |
| --- | --- | --- | --- | --- |
| 0.00–6.65 | cosmic | 0 | IGNITION | 1.66 / 5.32 |
| 6.65–16.70 | corridor | 0 | PHOTON LAUNCH | 9.16 / 14.69 |
| 16.70–28.30 | city | 0 | NEON DESCENT | 19.60 / 25.98 |
| 28.30–38.90 | ocean | 0 | QUANTUM OCEAN | 30.95 / 36.78 |
| 38.90–46.15 | structure | 0 | FRACTURE GATE | 40.71 / 44.70 |
| 46.15–56.55 | corridor | 1 | HYPERDRIVE | 48.75 / 54.47 |
| 56.55–63.80 | city | 1 | SKYLINE CHASE | 58.36 / 62.35 |
| 63.80–74.80 | engine | 0 | REACTOR OVERDRIVE | 66.55 / 72.60 |
| 74.80–86.00 | cosmic | 1 | ORBITAL STORM | 77.60 / 83.76 |
| 86.00–95.85 | structure | 1 | INFINITE MACHINE | 88.46 / 93.88 |
| 95.85–103.70 | sculpture | 0 | MATTER COLLAPSE | 97.81 / 102.13 |
| 103.70–116.80 | armada | 0 | ORBITAL ARMADA | 106.97 / 114.18 |
| 116.80–125.55 | corridor | 2 | CORE DIVE | 118.99 / 123.80 |
| 125.55–137.30 | engine | 1 | CRYSTAL ACCELERATOR | 128.49 / 134.95 |
| 137.30–147.80 | structure | 2 | FRACTURE CASCADE | 139.93 / 145.70 |
| 147.80–158.05 | cosmic | 2 | SUPERNOVA | 150.36 / 156.00 |
| 158.05–165.25 | city | 2 | TERMINAL VELOCITY | 159.85 / 163.81 |
| 165.25–177.45 | armada | 1 | EVENT HORIZON | 168.30 / 175.01 |
| 177.45–187.50 | sculpture | 1 | AFTERSHOCK | 179.96 / 185.49 |
| 187.50–198.60 | ocean | 1 | ZERO GRAVITY | 190.28 / 196.38 |
| 198.60–207.75 | engine | 2 | REBUILD | 200.89 / 205.92 |
| 207.75–218.85 | armada | 2 | RESONANCE | 210.53 / 216.63 |
| 218.85–228.65 | ocean | 2 | REBIRTH | 221.30 / 226.69 |
| 228.65–240.20 | city | 3 | SECOND IGNITION | 231.54 / 237.89 |
| 240.20–248.25 | corridor | 3 | FULL THROTTLE | 242.21 / 246.64 |
| 248.25–257.80 | structure | 3 | VECTOR STORM | 250.64 / 255.89 |
| 257.80–266.95 | cosmic | 3 | SOLAR WIND | 260.09 / 265.12 |
| 266.95–278.80 | engine | 3 | CRYSTAL SUPREMACY | 269.91 / 276.43 |
| 278.80–285.20 | armada | 3 | FINAL PURSUIT | 280.40 / 283.92 |
| 285.20–292.40 | cosmic | 4 | LIGHTSPEED | 287.00 / 290.96 |
| 292.40–300.00 | engine | 4 | ASCENT VECTOR | 294.30 / 298.48 |
