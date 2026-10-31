// Startup notice and frame-counter-visibility tests: pure state, no ImGui, no SDL, no window.
#include <cstdio>

#include "osl_notice.h"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(condition)                                                                                       \
    do {                                                                                                       \
        ++g_checks;                                                                                            \
        if (!(condition)) {                                                                                    \
            ++g_failures;                                                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
        }                                                                                                      \
    } while (0)

using OslNotice::FrameCounterVisible;
using OslNotice::StartupNotice;
using OslNotice::StartupNoticeReady;

void FrameCounterVisibleTests() {
    CHECK(!FrameCounterVisible(false, 0.0));
    CHECK(!FrameCounterVisible(false, 16.7)); // Off, even with a reading: hidden
    CHECK(!FrameCounterVisible(true, 0.0));   // On, no reading yet: hidden
    CHECK(FrameCounterVisible(true, 16.7));   // On, valid stats: shown
}

void StartupNoticeReadyTests() {
    CHECK(StartupNoticeReady(false, false)); // frame counter disabled: ready right away
    CHECK(StartupNoticeReady(false, true));
    CHECK(!StartupNoticeReady(true, false)); // frame counter enabled, no reading yet: wait
    CHECK(StartupNoticeReady(true, true));   // frame counter enabled and visible: ready
}

void StartupDurationTests() {
    StartupNotice notice;
    notice.QueueStartup("Skip Intro: On");
    CHECK(!notice.Visible(0.0, false, 5.0)); // not ready: still queued
    CHECK(notice.Pending(0.0, 5.0));
    CHECK(notice.Visible(10.0, true, 5.0)); // becomes ready: the clock starts now
    CHECK(notice.text() == "Skip Intro: On");
    CHECK(notice.Visible(14.9, true, 5.0)); // just under 5 seconds later: still shown
    CHECK(!notice.Visible(15.0, true, 5.0)); // exactly 5 seconds later: gone
    CHECK(!notice.Pending(15.0, 5.0));
}

void TimerWaitsForFrameCounterTests() {
    StartupNotice notice;
    notice.QueueStartup("Unlock Bookshelf: On");
    const bool frame_counter_enabled = true;
    // The frame counter has no reading yet: the notice must not start its timer.
    CHECK(!notice.Visible(1.0, StartupNoticeReady(frame_counter_enabled, false), 5.0));
    CHECK(!notice.Visible(2.0, StartupNoticeReady(frame_counter_enabled, false), 5.0));
    // The frame counter now has a reading: the notice starts, and only now.
    CHECK(notice.Visible(3.0, StartupNoticeReady(frame_counter_enabled, true), 5.0));
    CHECK(!notice.Visible(8.0, StartupNoticeReady(frame_counter_enabled, true), 5.0));
}

void TimerStartsImmediatelyWithoutFrameCounterTests() {
    StartupNotice notice;
    notice.QueueStartup("Skip Intro: On");
    CHECK(notice.Visible(1.0, StartupNoticeReady(false, false), 5.0)); // disabled counter: ready at once
    CHECK(!notice.Visible(6.0, StartupNoticeReady(false, false), 5.0));
}

void QueueOnceTests() {
    StartupNotice notice;
    CHECK(!notice.queued());
    notice.QueueStartup("Skip Intro: On");
    CHECK(notice.queued());
    CHECK(notice.text() == "Skip Intro: On");
    notice.QueueStartup("Something else"); // a second call changes nothing
    CHECK(notice.text() == "Skip Intro: On");
}

void CoexistingNoticesTests() {
    StartupNotice skip_intro, bookshelf;
    skip_intro.QueueStartup("Skip Intro: On");
    bookshelf.QueueStartup("Unlock Bookshelf: On");
    const bool ready = StartupNoticeReady(false, false);
    CHECK(skip_intro.Visible(0.0, ready, 5.0));
    CHECK(bookshelf.Visible(0.0, ready, 5.0));
    CHECK(skip_intro.Visible(4.9, ready, 5.0) && bookshelf.Visible(4.9, ready, 5.0));
    CHECK(!skip_intro.Visible(5.0, ready, 5.0) && !bookshelf.Visible(5.0, ready, 5.0));
}

// The caller only queues a notice when its Gameplay option is On; an option that is Off never
// calls QueueStartup, so the notice stays hidden for the whole launch.
void NeverQueuedStaysHiddenTests() {
    StartupNotice notice;
    CHECK(!notice.Visible(0.0, true, 5.0));
    CHECK(!notice.Visible(100.0, true, 5.0));
    CHECK(!notice.Pending(0.0, 5.0));
}

} // namespace

int main() {
    FrameCounterVisibleTests();
    StartupNoticeReadyTests();
    StartupDurationTests();
    TimerWaitsForFrameCounterTests();
    TimerStartsImmediatelyWithoutFrameCounterTests();
    QueueOnceTests();
    CoexistingNoticesTests();
    NeverQueuedStaysHiddenTests();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
