#include "../mod/uevr/src/utility/WuWaProjectionTest.hpp"

#include <atomic>
#include <iostream>
#include <thread>

using namespace wuwa_projection_test;
static unsigned checks{};
static void check(bool value) {
    ++checks;
    if (!value) throw std::runtime_error("Projection test check " + std::to_string(checks) + " failed");
}
template<class F> static void refused(F action) {
    bool caught = false;
    try { action(); } catch (const std::runtime_error&) { caught = true; }
    check(caught);
}
static Base raw() { return {0,0,false,false,true,true,true,true,123}; }
static openxr_projection::Inputs input(const Selection& selection) {
    return {selection.horizontal,selection.base.vertical,selection.base.grow,.1f,
        {{{-.8f,.6f,.7f,-.5f},{-.6f,.8f,.7f,-.5f}}}};
}

int main() try {
    // 1. Inactive path forwards every base setting without changing it.
    {
        Lease p;
        check(!p.status(0).active && !p.status(0).base && !p.status(0).applied);
        auto base=raw(); base.horizontal=2; base.vertical=1; base.grow=true;
        const auto s=p.select(1,base);
        check(s.horizontal==2 && s.base==base && s.lease_id.empty());
        check(p.status(1).effective_horizontal==2);
    }
    // 2. Exact TTL boundary; retry cannot extend or restart a consumed ID.
    {
        Lease p; const auto base=raw();
        p.begin(100,2,"A",base,base,false);
        check(p.select(101,base).horizontal==1 && base.horizontal==0);
        p.begin(1000,2,"A",base,base,false);
        check(p.status(1000).remaining_ms==1100);
        check(p.select(2099,base).horizontal==1);
        check(p.select(2100,base).horizontal==0);
        check(p.status(2100).reason=="expired");
        refused([&]{p.begin(2101,2,"A",base,base,false);});
        p.end(2102,"A"); // Idempotent owned stop after expiration.
    }
    // 3. An old stop cannot cancel a newer owner's request.
    {
        Lease p; const auto base=raw();
        p.begin(100,3,"first",base,base,false);
        refused([&]{p.end(110,"someone-else");});
        check(p.status(110).active);
        p.end(120,"first"); p.end(121,"first");
        check(p.status(121).reason=="ended");
        p.begin(130,3,"second",base,base,false);
        refused([&]{p.end(140,"first");});
        refused([&]{p.begin(141,4,"second",base,base,false);});
        check(p.status(142).active && p.status(142).id=="second");
    }
    // 4. Bad duration/identity, baseline, unsupported modes and conflicts.
    {
        const auto base=raw();
        for (int seconds : {-1,0,31}) {
            Lease p; refused([&]{p.begin(1,seconds,"A",base,base,false);});
            check(!p.status(1).active);
        }
        for (const std::string& id : {std::string{},std::string(65,'x'),std::string{"../bad"}}) {
            Lease p; refused([&]{p.begin(1,1,id,base,base,false);});
        }
        Lease p;
        refused([&]{p.begin(1,1,"A",base,base,true);});
        auto expected=base; expected.horizontal=1;
        refused([&]{p.begin(1,1,"A",base,expected,false);});
        refused([&]{p.begin(std::numeric_limits<uint64_t>::max()-500,1,"A",base,base,false);});
        for (unsigned i=0;i<8;++i) {
            auto bad=base;
            switch(i) {
            case 0: bad.horizontal=1; break; case 1: bad.vertical=1; break;
            case 2: bad.grow=true; break; case 3: bad.screen=true; break;
            case 4: bad.openxr=false; break; case 5: bad.native=false; break;
            case 6: bad.ready=false; break; case 7: bad.runtime=0; break;
            }
            Lease q; refused([&]{q.begin(1,1,"A",bad,bad,false);});
        }
        p.begin(1,30,"valid_30-seconds",base,base,false);
        check(p.status(1).remaining_ms==30000);
    }
    // 5. All observed base/runtime changes cancel; returning to the baseline
    // never silently rearms. Normal/native-fix-off is permitted as its own base.
    for (unsigned i=0;i<9;++i) {
        Lease p; const auto base=raw(); auto changed=base;
        switch(i) {
        case 0: changed.horizontal=2; break; case 1: changed.vertical=1; break;
        case 2: changed.grow=true; break; case 3: changed.screen=true; break;
        case 4: changed.openxr=false; break; case 5: changed.native=false; break;
        case 6: changed.ready=false; break; case 7: changed.runtime=456; break;
        case 8: changed.native_fix=false; break;
        }
        p.begin(1,2,"A",base,base,false);
        check(p.select(2,changed).horizontal==changed.horizontal);
        check(!p.status(2).active && p.status(2).reason=="base_or_runtime_changed");
        check(p.select(3,base).horizontal==0);
    }
    // 6. Backwards clock, reset and late publication cannot resurrect a lease.
    {
        Lease p; const auto base=raw(); p.begin(100,2,"A",base,base,false);
        check(p.select(99,base).horizontal==0 && p.status(99).reason=="clock_changed");
        p.begin(101,2,"B",base,base,false);
        Applied old{}; old.selection=p.select(102,base); old.key=input(old.selection);
        p.reset(); p.publish(old);
        check(!p.status(103).active && !p.status(103).applied && !p.status(103).base);
        check(p.select(104,base).horizontal==0);
        p.begin(105,2,"C",base,base,false);
        p.end(106,"C"); check(!p.status(106).active);
    }
    // 7. Production cache key integration: only horizontal changes. Expiration
    // and manual changes cause a new key rather than requiring a UI dirty flag.
    {
        Lease p; const auto base=raw(); openxr_projection::Cache cache;
        const auto original=input(p.select(0,base)); cache.commit(original);
        p.begin(1,1,"A",base,base,false);
        const auto selected=p.select(2,base); const auto test=input(selected);
        check(cache.needs_update(test)); cache.commit(test);
        check(test.vertical==original.vertical && test.grow==original.grow &&
            test.near_z==original.near_z && test.fov==original.fov);
        check(!cache.needs_update(input(p.select(500,base))));
        const auto restored=input(p.select(1001,base));
        check(restored==original && cache.needs_update(restored));
        cache.commit(restored); check(!cache.needs_update(original));
    }
    // 8. Report copies are immutable and report actual calculated state even
    // after a lease ends. An old calculation is not relabelled as a new lease.
    {
        Lease p; const auto base=raw(); p.begin(1,2,"A",base,base,false);
        Applied a{}; a.selection=p.select(2,base); a.key=input(a.selection); a.at_ms=2;
        a.matrices[0][0]=.7f; a.bounds[1][1]=.8f;
        p.publish(a); a.matrices[0][0]=99;
        const auto first=p.status(3);
        check(first.applied->sequence==1 && first.applied->matrices[0][0]==.7f);
        p.end(4,"A"); p.begin(5,2,"B",base,base,false);
        p.publish(a);
        check(p.status(6).id=="B" && p.status(6).applied->selection.lease_id=="A");
        check(first.applied->matrices[0][0]==.7f && first.applied->sequence==1);
        Applied b{}; b.selection=p.select(7,base); b.key=input(b.selection); b.at_ms=7;
        p.publish(b); check(p.status(8).applied->selection.lease_id=="B");
        check(p.status(8).applied->sequence==3);
    }
    // 9. Client disappearance needs no stop call: render-boundary selection
    // alone releases the override, with a maximum 30-second lifetime.
    {
        Lease p; const auto base=raw(); p.begin(0,30,"lost-client",base,base,false);
        check(p.select(29999,base).horizontal==1);
        check(p.select(30000,base).horizontal==0 && !p.status(30000).active);
    }
    // 10. Concurrent status and matrix publication return whole copied records.
    {
        Lease p; const auto base=raw(); std::atomic<bool> start{false}, failed{false};
        auto writer=std::thread([&]{
            while(!start.load()) std::this_thread::yield();
            for(unsigned i=1;i<=500;++i) {
                Applied a{}; a.selection=p.select(i,base); a.key=input(a.selection); a.at_ms=i;
                a.matrices[0][0]=float(i); a.matrices[1][0]=float(i);
                p.publish(a);
            }
        });
        start=true;
        for(unsigned i=0;i<500;++i) {
            const auto s=p.status(1000);
            if(s.applied && s.applied->matrices[0][0]!=s.applied->matrices[1][0]) failed=true;
        }
        writer.join(); check(!failed && p.status(1000).applied->sequence==500);
    }
    // 11. Interleaving another owner and reset cannot make an old ID reusable.
    {
        Lease p; const auto base=raw();
        p.begin(1,1,"A",base,base,false); p.end(2,"A");
        p.begin(3,1,"B",base,base,false); p.end(4,"B");
        refused([&]{p.begin(5,1,"A",base,base,false);});
        p.reset();
        refused([&]{p.begin(6,1,"A",base,base,false);});
        for(unsigned i=2;i<1024;++i) {
            const auto id="unique-"+std::to_string(i);
            p.begin(10+i*2,1,id,base,base,false); p.end(11+i*2,id);
        }
        refused([&]{p.begin(3000,1,"overflow",base,base,false);});
        check(!p.status(3000).active);
    }
    std::cout << "PASS projection comparison: " << checks << " checks in 11 groups\n";
    return 0;
} catch(const std::exception& e) {
    std::cerr << e.what() << '\n'; return 1;
}
