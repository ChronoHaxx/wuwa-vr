// The black ID boxes give way only to a confirmed source hide (WuWaPrivacy.hpp, 1.2.8): steady reports
// from wuwa_uid_hide.lua must keep coming, and a re-show keeps the box for a while. Offline.
#include "../mod/uevr/src/utility/WuWaPrivacy.hpp"
#include <cassert>
#include <iostream>
using namespace wuwa_privacy;

int main() {
    // Never reported: the box stays.
    assert(!source_hidden(0, 0, 5000));
    SourceHide s;
    assert(!s.hidden(5000));
    // A steady report: no box, for up to a second.
    s.report(true, 10000);
    assert(s.hidden(10000) && s.hidden(10999) && s.hidden(11000));
    // Reports stop (script gone, game thread stalled): the box comes back.
    assert(!s.hidden(11001));
    // Refreshed reports keep it hidden.
    s.report(true, 11250);
    assert(s.hidden(12000));
    // The game showed the text again (the script had to hide it): box at once, and for 2 s
    // even when steady reports resume.
    s.report(false, 12100);
    assert(!s.hidden(12100));
    s.report(true, 12200);
    assert(!s.hidden(12300) && !s.hidden(14099));
    s.report(true, 14100);
    assert(s.hidden(14100));
    // A clock that went backwards never counts as fresh.
    assert(!source_hidden(20000, 0, 19999));
    // Two texts are independent.
    assert(!profile_id_text.hidden(14100));
    std::cout << "privacy source hide: all checks passed\n";
    return 0;
}
