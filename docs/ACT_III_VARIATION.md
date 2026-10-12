# Act III: directed visual variation

This is the implementation brief for the revised OpenGL Act III, not an
acceptance report. The earlier v0.3 review does not validate this redesign.

Each chronological occurrence of a world selects its own geometry and camera
path with `uVariant`; there is no modulo-four recurrence. `uChapterProgress`
controls an entire shot and never loops. `uAction` performs one irreversible
transformation at the music director's selected accent; `uEvent` adds a small
light response. The chapter-local clock drives locomotion. Quiet lotus and
ending shots deliberately suppress camera shake.

The palette changes with subject and lighting: ceramic and amber landscapes,
green/pink plants, violet floating architecture, and white/coral/gold monuments.
The finale ends with a disappearing foreground crystal and a clean sun over
water rather than another prolonged orbit around the same object.

| Chapter | World / occurrence | Beginning, development, and accent action | Early / late sample, absolute seconds |
| --- | --- | --- | --- |
| SUNRISE OF GIANTS | 12 / 0 | Buried gate emerges from dunes; its massive halves unlock. | 592.23 / 600.31 |
| DUNE COLOSSUS | 12 / 1 | Low tracking shot beside a walking titan; a single leap changes the motion. | 603.43 / 610.29 |
| THE LIVING CIRCUIT | 13 / 0 | A seed grows branches and leaves; a root circuit spreads across the ground. | 614.01 / 623.60 |
| GRAVITY REBELLION | 14 / 0 | A tiled floor breaks into rising islands of different heights. | 627.06 / 634.37 |
| ARCH OF TOMORROW | 15 / 0 | Wire arches assemble into solid monuments ahead of a moving causeway camera. | 637.55 / 645.00 |
| SANDSTORM PURSUIT | 12 / 2 | Low canyon chase with route curvature and passing ribs; walls spread into the storm. | 649.12 / 659.79 |
| VERDANT SINGULARITY | 13 / 1 | Camera rises beside a double helix; the ladder unzips into a wider structure. | 663.06 / 669.15 |
| FALLING HORIZONS | 14 / 1 | Rapid descent along a staggered staircase; platforms shear downward. | 672.37 / 680.60 |
| CRYSTAL WINGS | 15 / 1 | A large crystal moth spreads multiple wing panels, viewed from above. | 684.33 / 693.29 |
| CERAMIC TITANS | 12 / 3 | A vertical machine deploys six long mechanical arms into a broad silhouette. | 697.44 / 707.48 |
| SEED VELOCITY | 13 / 2 | Macro flight through six-petal seed gates; the passage bursts open. | 711.40 / 720.08 |
| TIDAL FRACTURE | 14 / 2 | A continental slab splits and tilts into two separate plates with a bright fault. | 722.97 / 728.74 |
| REALITY CONVERGENCE | 15 / 2 | Three separate portals move together and expose a triangular central machine. | 732.17 / 741.27 |
| BREATH OF LIGHT | 13 / 3 | A restrained lotus shot over water; eight petals open once. | 744.83 / 752.74 |
| SUNFIRE ADVANCE | 12 / 4 | A levitating train overtakes a moving camera on elevated luminous rails. | 756.62 / 766.24 |
| ORBITAL UPRISING | 14 / 3 | Tall floating rods accelerate upward through a vertical ring. | 769.72 / 777.07 |
| GROWTH CASCADE | 13 / 4 | A whole terraced landscape grows and unfolds luminous leaf platforms. | 780.86 / 790.45 |
| ASCENDANT ARRAY | 15 / 3 | Flat cells become a tall crystalline city in staggered growth waves. | 794.20 / 802.53 |
| TITAN OVERDRIVE | 12 / 5 | A launch field opens and a fleet accelerates upward; camera tilts toward departure. | 806.23 / 815.01 |
| BREAK THE HORIZON | 14 / 4 | Huge walls fold outward like a book, revealing an escape portal. | 818.61 / 826.80 |
| INFINITE BLOOM | 13 / 5 | A narrow bud unfolds into a huge two-layer flower filling the frame. | 830.13 / 837.69 |
| THREE ACTS ONE REALITY | 15 / 4 | A wide landscape reveals three unlike monuments; a bridge joins them. | 841.79 / 852.36 |
| THE FINAL ASCENT | 15 / 5 | First-person vertical flight through a tall luminous cathedral toward its ceiling. | 857.41 / 869.80 |
| REALITY ASCENDANT | 15 / 6 | The foreground star splits and its debris leaves the frame, revealing a sun horizon. | 874.99 / 886.97 |

Times derive from the current directed cue array with Act III starting at
589.92 seconds. Early samples use 20% of each chapter and late samples 90%, so
they bracket the accent search window. Full motion previews are still needed:
two stills cannot establish entertainment, rhythm, camera comfort, aliasing,
or real-time performance. The full soundtrack and chapter timings are unchanged.

The shader uses bounded nearest-cell repetition for finite arrays, guarded
radial folding, conservative distances for warped helix geometry, a maximum
112 marching steps (72 in retro mode), and eight shadow steps. These are source
properties; compilation, GPU images, and measured performance must be recorded
separately by the integrating agent.

## Finish and distance-field correction

The first GPU contact sheets exposed triangular holes and striped undersides
on floating islands, stair platforms, and the city array. A nearest x/z cell
alone is unsafe when neighboring objects have different heights. Those fields
now take the minimum of the 3 x 3 neighboring cells (three neighboring z cells
for the staircase). Primary and shadow rays additionally stop at the next
procedural cell boundary before recomputing the field. This correction still
requires fresh native captures; the initial boards are not evidence for it.

The finish pass adds distant ceramic ruins to the desert, peripheral canopies
to the garden, and outer architectural buttresses to the early finale. The
monument triptych now stands in a two-tier courtyard with tall colonnades, a
roofline, an enclosing crown, a branched tree and satellite crystal pieces.
The three primary forms and the following vertical ascent remain distinct.

Materials now use weathering, fine grain with distance filtering, ceramic
panel seams and fasteners, dark stone strata, and luminous leaf veins. The
worlds use different cloud, canopy-light, nebula/planet, and aurora backgrounds.
A warm key and cool rim give surfaces stronger depth. Courtyards use stone
texture; water ripples are reserved for the quiet lotus and final sun horizon.
These are implemented visual intentions, not a claim of AAA acceptance or of
measured performance after the extra neighboring-cell evaluations.
