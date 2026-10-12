# Act II direction: distinct returns

This revision addresses the repeated geometry and cycling camera shots in the
previous release. Each chronological return to an Act II world selects a new
set, transformation and camera path. The original music and chapter boundaries
remain intact. The table describes implemented direction, not human acceptance
or a visual quality score.

Times are absolute show seconds. `variant` is the zero-based occurrence of the
world within Act II; the editorial shot field does not select the geometry.

| Start | Chapter | World / variant | Directed progression |
| ---: | --- | --- | --- |
| 300.00 | Chrome Awakening | 8 / 0 | A low mirror slab rises into an asymmetrical blade spine. A wide lateral dolly keeps the whole silhouette visible. |
| 311.00 | Cathedral of Light | 8 / 1 | A vaulted gallery unfolds overhead during a rising flight through its clear central aisle. |
| 327.25 | Rain District | 9 / 0 | A rain-lit avenue at ground level, with a convoy crossing the elevated street ahead. |
| 341.05 | Orbital Foundry | 10 / 0 | Separate wings converge on a conveyor and six pairs of gantries close around the engine. |
| 352.95 | Prism Chamber | 11 / 0 | Eight glass cube corners separate around a warm crystal and an incident cyan beam. |
| 369.70 | Neon Pursuit | 9 / 1 | A long maglev overtakes beside the camera while opposing traffic passes in the open avenue. |
| 381.10 | Liquid Chrome | 8 / 2 | Four metal ribbons turn from a low sheet into tall travelling waves. |
| 394.25 | Kinetic Array | 10 / 1 | A suspended square matrix buckles into a large travelling saddle wave. |
| 408.15 | Spectral Refraction | 11 / 1 | Angled glass monoliths turn one incident white beam into a spreading fan of coloured beams. |
| 425.05 | Wet Circuit | 9 / 2 | An overhead view reveals suspended diamond road banks peeling apart below the camera. |
| 438.75 | Machine Heart | 10 / 2 | Industrial pistons pulse; four large housing blocks part to expose a vertical energy heart. |
| 449.65 | Golden Ascent | 8 / 3 | A gold skyline is extruded from the mirror floor. The camera rises and pulls away as its height grows. |
| 464.85 | Inner Light | 11 / 2 | A quiet glass book opens into a sparse wing around warm interior light. |
| 478.75 | Midnight Velocity | 9 / 3 | The viewpoint moves above the city, revealing stacked streams of aerial traffic and sky bridges below. |
| 493.70 | Orbital Collision | 10 / 3 | Two cargo formations approach, touch on the measured music accent and then scatter for the remainder of the chapter. |
| 508.45 | Chrome Resonance | 8 / 4 | A compact mirror book fans into a long asymmetrical staircase of light. The camera pulls back as its footprint grows. |
| 524.05 | Prism Break | 11 / 3 | A large prism breaks once into an expanding shell of tumbling glass shards. |
| 537.70 | Electric Horizon | 9 / 4 | An entire roof fleet launches into the sky while the camera reveals a large distant gold monument. |
| 555.00 | Uplift | 10 / 4 | The completed winged engine launches, its gantries withdraw and the camera follows its rising silhouette. |
| 571.95 | Cathedral Finale | 8 / 5 | Two massive folded doors open. An exterior approach becomes a flight through the clear aisle. |
| 581.85 | Beyond the Prism | 11 / 4 | Glass avenue panels peel aside during a flight toward a faceted horizon and a fan of light with visible gaps. |

## Timing and implementation

`chapterProgress` is monotonic for the whole chapter. `action` advances once at
the measured music accent and drives permanent openings, separations and
launches. `event` is used for transient light. Local time drives travelling
waves, rain and traffic; cameras do not wrap with `fmod` or repeat four shots.

Orbital Collision uses the explicit accent time and chapter duration. At the
accent its packed formations have centres at x = +/-1.905. Their leading cube
faces extend `2.5 * 0.65 + 0.28 = 1.905`, meeting at x = 0. Scatter is zero until
that time and increases smoothly through the remainder of the chapter.

The Vulkan backend and GPU parameter layout are unchanged. Act II no longer
adds the same decorative spectrum rings to every set: the music instead drives
the set's own lights, ribbons, tiles, pistons or exhaust. The largest set has
428 instances, below the 2,048 instance budget.

The environment finish adds real mesh surroundings to the previously empty
studio sets. Chrome uses a folded shipyard horizon, an elevated aqueduct,
terraced arcologies, archive slabs and distant angular monuments. Foundry uses
sawtooth hangar ribs, service machinery, cooling stacks, laminated containment
spines, distant derelict hulls and tall spaceport cranes. Prism uses triangular
glass sails, stepped bases, floating tuning-fork baffles, a sparse concrete
retreat, a broad angular gantry and avenue terraces. These forms sit behind or
beside the hero and use darker, rougher materials to preserve its silhouette.
The glass retreat and floorless collision deliberately retain more empty space
to create contrast with the dense city and industrial chapters.

The OpenGL 3.3 fallback implements the corresponding 21 states and camera
paths. Collision uses 72 pieces instead of 216 and Prism Break uses 48 shards
instead of 144. Its glass shading is an approximation; hardware ray tracing
provides the actual mesh reflections and refractions.

Golden Ascent and Kinetic Array evaluate a 3 x 3 cell neighbourhood in the
fallback. Marching is additionally capped by a conservative distance to the
footprints of all omitted grid rows and columns. Selecting only the nearest
cell could skip a taller neighbouring tower or a tilted plate, producing torn
surfaces. The cap uses a horizontal radius of 1.0 for towers and a rotation-safe
radius of 1.04 for the plates; it remains far above the surface hit threshold.

A static CPU comparison against the complete 63-tower and 225-plate unions
checked 36,000 deterministic points (seed 42939): tower progress 0.15, 0.5 and
0.85, and matrix action/local-time pairs (0, 0.2), (0.5, 7), (1, 12). The previous
nearest-cell distances overstepped the complete union at 17,159 points. The
neighbour minimum plus omitted-cell bound overstepped at zero points. The
expanded GLSL also passed glslang validation. These checks establish distance
conservatism for the sampled cases, not a native GPU or visual acceptance pass.

Useful visual probes are 15%, 50% and 85% of each chapter. For collision, also
inspect `accent - 0.2`, `accent`, and `accent + 0.2` seconds to check physical
contact, the light pulse and the beginning of scatter. A pair of still images
cannot establish motion pacing or entertainment quality; review moving footage
with its audio before claiming this revision solves the user's complaint.
