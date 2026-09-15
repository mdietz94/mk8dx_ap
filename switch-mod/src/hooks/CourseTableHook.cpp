// ui::Control_CourseButton::setCourse(const CourseInfo&): called once per
// course button when a cup page is built, i.e. browsing the course-select
// screen teaches the PC every (course id -> resource name) pair of that cup.
// Pure observer. Symbol has no published pattern yet; skipped when absent.

#include "Hooks.hpp"

#include <cstdint>

#include "hk/hook/Trampoline.h"

#include "../ap/ApFrameBridge.hpp"
#include "../game/Structs.hpp"
#include "../sym/PatternResolver.hpp"
#include "../util/Log.hpp"

namespace mk8ap::hooks {

namespace {

HkTrampoline<std::uint64_t, void*, const void*> g_setCourse = hk::hook::trampoline(
    [](void* self, const void* courseInfo) -> std::uint64_t {
        if (courseInfo) {
            const int id = game::rd<std::int32_t>(courseInfo, game::course_info::kCourseId);
            ap::reportCourseName(id, game::courseInfoIconName(courseInfo));
        }
        return g_setCourse.orig(self, courseInfo);
    });

}  // namespace

void installCourseTableHook() {
    const auto off = sym::mainOffset("ui::Control_CourseButton::setCourse");
    if (!off) { MK8AP_LOG_INFO("[course] Control_CourseButton::setCourse unresolved; learning via SetNextCourse only"); return; }
    const auto rc = g_setCourse.installAtMainOffset(off);
    MK8AP_LOG_INFO("[course] hook @+0x%llx: %s", static_cast<unsigned long long>(off),
                   rc.succeeded() ? "installed" : "FAILED");
}

}  // namespace mk8ap::hooks
