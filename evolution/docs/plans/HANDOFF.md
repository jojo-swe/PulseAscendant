# Handoff — EVOLUTION (uppdaterad 2026-10-21, sessionens slut)

Läs detta först i nästa session. Specen och planerna ligger bredvid.

## Läge

- **Spec:** `2026-10-21-evolution-design.md` — godkänd av ägaren. Alla standardval i avsnitt 9 gäller (Vulkan 1.3 + RT krävs, Studion, DOS-boot, två epoker, CRT på, engelsk text, syntetisk placeholder-musik). Credits-texten är inte bekräftad.
- **Steg 0-plan:** `2026-10-21-step0-skeleton.md` — sex tasks. Status per task står längst ner här.
- **Branch:** `claude/evolution-showcase-plan`, utkast-PR #3 i jojo-swe/PulseAscendant. Allt arbete under `evolution/`.
- **Musik:** ägaren levererar Suno-låtar senare. Bygg mot syntetisk placeholder tills dess (steg 1).
- **Arbetssätt ägaren vill ha:** denna session är hjärnan/kontrollern; Opus-subagenter gör de enklare tasks (implementation och granskning). Processen: superpowers:subagent-driven-development med en implementer + en reviewer per task.

## Miljö (måste göras om i en ny container)

```sh
apt-get update && apt-get install -y mesa-vulkan-drivers vulkan-tools glslang-tools spirv-tools \
  libgl1-mesa-dev libegl1-mesa-dev libasound2-dev libpulse-dev libwayland-dev libxkbcommon-dev \
  libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev libvulkan-dev
```

Ger lavapipe (Vulkan 1.4 i mjukvara med ray query, RT pipeline, mesh shaders), `glslangValidator`, `spirv-val`, `xvfb-run`.
GitHub nås bara via **git** (FetchContent med `GIT_REPOSITORY` + commit-hash), inte tarballs. SDL3-hämtningen tar några minuter.

Bygg: `cd evolution && cmake --preset linux && cmake --build --preset linux && ctest --preset linux --output-on-failure`

## Beslut (rulings) tagna av kontrollern

1. `evo_gfx` länkar `SDL3::SDL3-static` från Task 4 (paths.cpp använder `SDL_GetBasePath`), trots att Task 1 sa "länka inte SDL än". Kostar inget om fel.
2. `--capture-dir` och `--render-stream` sätter båda `headless = true`.
3. Task 1-commitens Co-Authored-By anger den modell som skrev koden (Opus); accepterat, ingen amend.
4. Task 1-implementerns tillägg: `--time` måste vara ≥ 0; tom `--capture-dir ""` är fel. Accepterat.
5. `VK_NO_PROTOTYPES` definieras efter beroendena (volk definierar den själv); om varningar dyker upp i senare tasks, flytta till `target_compile_definitions` på `evo_gfx`.

## Taskstatus

| Task | Status | Commit |
|---|---|---|
| 1. CMake, pinnade beroenden, CLI-options | **klar**, granskad och godkänd | 34df257 |
| 2. PNG-writer utan beroenden | **klar**, granskad och godkänd (PNG/zlib-detaljer verifierade, flerblocksfall bekräftat av granskaren) | c0c9683 |
| 3. Vulkan-context med RT-krav | ej påbörjad | |
| 4. Offscreen compute + readback | ej påbörjad | |
| 5. Fönster, swapchain, loop, render-stream | ej påbörjad | |
| 6. PNG-capture-läge + README | ej påbörjad | |

## Uppskjutna minor-fynd (till slutgranskningen av steg 0)

- `cmake/Shaders.cmake`: kopieringen av `.spv` bredvid exe är ett `POST_BUILD`-steg på målet; en ren shaderändring uppdaterar inte kopian. Härda genom att kopiera inuti `<target>_shaders` eller med stamp-fil. `copy_directory` raderar inte borttagna `.spv`.
- `options.cpp`: sökvägsflaggor accepterar värden som börjar med `--`; `options_test` saknar övre gränser, icke-numeriskt och saknat värde.
- `Shaders.cmake`: tomma `VULKAN_SDK`-hints när variabeln är osatt.
- `png.cpp`: flerblocksvägen (>65535 byte) saknar CTest (granskaren verifierade den manuellt); `file.close()` före felkontroll; ingen gräns mot IDAT > 2^31−1; `check_png.py` kraschar otydligt utan IHDR.
- SDL varnar om saknad libdecor vid configure (miljö).

## Så här återupptas arbetet

1. Installera miljön (ovan), bygg och kör `ctest` i `evolution/` — allt ska vara grönt (3 tester).
2. Fortsätt med Task 3 enligt `2026-10-21-step0-skeleton.md`: `bash <superpowers>/skills/subagent-driven-development/scripts/task-brief evolution/docs/plans/2026-10-21-step0-skeleton.md 3`, implementer (Opus) + reviewer per task. Ledgern i `.superpowers/` är git-ignorerad och försvinner med containern; den här filen är ledgerns ersättning — uppdatera den vid sessionsslut.
3. När steg 0 är klart: slutgranskning av hela branchen, sedan steg 1 (ljud + timeline + placeholder-låt) får sin egen plan.
