# Future Crew — glenz mesh notice

EVOLUTION's GLENZ chapter (`src/retro/glenz.hpp/.cpp`) uses one piece of Future Crew's Second Reality
(1993): the 14-vertex / 24-triangle glenz mesh (a cube with a pyramid raised on each face) from
`GLENZ/MAIN.C` — its `points[]` (100 · ZZZ with ZZZ = 1, divided by 100 here) and its `epolys[]`
triangle indices, in the original order. Source: https://github.com/mtuomi/SecondReality, commit
071a82eddb7a7390dbaa491a8a473e0e923a55c2, released under the Unlicense below. It reached EVOLUTION
through the parent project's `src/scene_math.cpp`.

Nothing else is taken from Second Reality: the code, palette, copper bars, motion and music are
EVOLUTION's own, and the effect ideas are only credited. This is not an official Future Crew release.

```
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
```
