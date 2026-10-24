// The scene table (Task 1 of step 5), on the CPU:
//   - every effect has a row in kSceneOfEffect, and an effect past the last throws;
//   - the five Studio effects (FINALE, PORTAL, STUDIO NIGHT, RETURN, REBOOT) show the Studio and every other
//     effect the placeholder (until the scene tasks move their rows);
//   - sceneTable()[id].id == id, names are unique and round-trip through the table, and every scene an
//     effect shows has a make function (the placeholder none);
//   - --show-info's "scene" field (showInfoJson(effectSceneName), what main.cpp prints) comes from the table;
//   - Studio::camera(f) equals Studio::frame(f).camera at 500 times across its chapters;
//   - makeScene(Studio, {0.25}) builds, and the fixture's checkSceneSweep passes over the Studio's chapters.

#include "modern/modern_scene.hpp"
#include "modern/scene.hpp"
#include "modern/studio.hpp"
#include "scene_fixture.hpp"
#include "show/timeline.hpp"

#include <cstdio>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>

namespace {

namespace modern = evo::modern;
namespace show = evo::show;
using modern::SceneId;
using show::Effect;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " (line " + std::to_string(__LINE__) + ")");          \
        ++checksPassed;                                                                                      \
    } while (false)

bool isStudio(Effect e) {
    return e == Effect::Finale || e == Effect::Portal || e == Effect::StudioNight || e == Effect::Return ||
           e == Effect::Reboot;
}

void testEffectRows() {
    int studio = 0;
    for (std::size_t i = 0; i < show::kEffectCount; ++i) {
        const auto effect = static_cast<Effect>(i);
        const SceneId scene = modern::sceneFor(effect);
        CHECK(scene == (isStudio(effect) ? SceneId::Studio : SceneId::Placeholder));
        studio += scene == SceneId::Studio;
        CHECK(std::strcmp(modern::effectSceneName(effect), modern::sceneName(scene)) == 0);
    }
    CHECK(studio == 5);
    bool threw = false;
    try {
        modern::sceneFor(static_cast<Effect>(show::kEffectCount));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    CHECK(threw);
    std::printf("effect rows: %zu effects, 5 Studio, the rest Placeholder\n", show::kEffectCount);
}

SceneId byName(const char* name) {
    for (const modern::SceneEntry& entry : modern::sceneTable())
        if (std::strcmp(entry.name, name) == 0)
            return entry.id;
    throw std::runtime_error(std::string("no scene named ") + name);
}

void testTable() {
    const auto table = modern::sceneTable();
    CHECK(table.size() == modern::kSceneCount);
    std::set<std::string> names;
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto id = static_cast<SceneId>(i);
        CHECK(table[i].id == id);
        CHECK(std::strcmp(modern::sceneName(id), table[i].name) == 0);
        CHECK(byName(modern::sceneName(id)) == id);
        names.insert(table[i].name);
    }
    CHECK(names.size() == table.size());
    CHECK(std::strcmp(modern::sceneName(SceneId::Placeholder), "Placeholder") == 0);
    CHECK(std::strcmp(modern::sceneName(SceneId::Studio), "Studio") == 0);
    CHECK(table[0].make == nullptr && modern::makeScene(SceneId::Placeholder) == nullptr);
    for (std::size_t i = 0; i < show::kEffectCount; ++i) {
        const SceneId scene = modern::sceneFor(static_cast<Effect>(i));
        if (scene != SceneId::Placeholder)
            CHECK(table[static_cast<std::size_t>(scene)].make != nullptr);
    }
    // Ids past the table.
    CHECK(std::strcmp(modern::sceneName(SceneId::Count), "?") == 0);
    bool threw = false;
    try {
        modern::makeScene(SceneId::Count);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    CHECK(threw);
    std::printf("table: %zu scenes, names unique and round-tripping\n", table.size());
}

// --show-info prints showInfoJson(effectSceneName): every chapter's "scene" is its effect's table name.
void testShowInfo() {
    const std::string json = show::showInfoJson(modern::effectSceneName);
    const std::string key = "\"scene\":\"";
    std::size_t pos = 0;
    int chapters = 0;
    for (int c = 0; c < show::chapterCount(); ++c) {
        pos = json.find(key, pos);
        CHECK(pos != std::string::npos);
        pos += key.size();
        const std::size_t end = json.find('"', pos);
        const std::string name = json.substr(pos, end - pos);
        const Effect effect = show::sampleTimeline(show::chapterStart(c)).effect;
        CHECK(name == modern::sceneName(modern::sceneFor(effect)));
        ++chapters;
    }
    CHECK(json.find(key, pos) == std::string::npos);
    CHECK(chapters == show::chapterCount());
    std::printf("--show-info: %d chapters name their scene from the table\n", chapters);
}

void testStudioCamera() {
    const auto made = modern::makeScene(SceneId::Studio, {0.25f});
    CHECK(made != nullptr && made->samplesRetro());
    const modern::Studio studio;
    const auto chapters = evo::test::chaptersOf(SceneId::Studio);
    CHECK(chapters.size() == 5);
    double total = 0;
    for (const int c : chapters)
        total += show::chapterStart(c + 1) - show::chapterStart(c);
    int compared = 0;
    for (int k = 0; k < 500; ++k) {
        // 500 times spread over the five chapters' combined length.
        double t = (k + 0.5) / 500.0 * total;
        int chapter = chapters.front();
        for (const int c : chapters) {
            const double length = show::chapterStart(c + 1) - show::chapterStart(c);
            chapter = c;
            if (t < length)
                break;
            t -= length;
        }
        const show::TimelineFrame frame = show::sampleTimeline(show::chapterStart(chapter) + t);
        const modern::Camera a = studio.camera(frame), b = studio.frame(frame).camera;
        CHECK(a.eye.x == b.eye.x && a.eye.y == b.eye.y && a.eye.z == b.eye.z);
        CHECK(a.target.x == b.target.x && a.target.y == b.target.y && a.target.z == b.target.z);
        CHECK(a.up.x == b.up.x && a.up.y == b.up.y && a.up.z == b.up.z);
        CHECK(a.fovYDegrees == b.fovYDegrees && a.aperture == b.aperture &&
              a.focusDistance == b.focusDistance && a.shot == b.shot);
        // Built at another detail, the Studio is the same room.
        CHECK(made->camera(frame).shot == a.shot);
        ++compared;
    }
    std::printf("Studio::camera == frame().camera at %d times in its 5 chapters\n", compared);
}

void testStudioSweep() {
    evo::test::SweepLimits limits;
    limits.maxCameraStep = 0.05f; // the Studio's cameras move centimetres per frame
    const evo::test::SweepStats stats = evo::test::checkSceneSweep(SceneId::Studio, limits);
    CHECK(stats.frames > 5000 && stats.maxInstances == 195 && stats.maxLights == 4);
}

} // namespace

int main() {
    try {
        testEffectRows();
        testTable();
        testShowInfo();
        testStudioCamera();
        testStudioSweep();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("scene_table_test: %d checks passed\n", checksPassed);
    return 0;
}
