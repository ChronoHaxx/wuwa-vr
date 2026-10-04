#pragma once
#include "OpenXRHandDataSource.hpp"
#include <array>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>

// Optional optical-joint visualization only. No action state or game input.
namespace wuwa_hand {
using Clock = std::chrono::steady_clock;
inline std::atomic<int> renderer_error{0}; // 0 none, 11/12 graphics path unavailable
enum class State { Off, Unsupported, SourceUnavailable, Waiting, Active, Error };
struct Joint { XrVector3f position{}; bool valid{}; };
struct Hand {
    bool optical{};
    std::array<Joint, XR_HAND_JOINT_COUNT_EXT> joints{};
};
struct Snapshot {
    bool enabled{};
    State state{State::Off};
    XrResult result{XR_SUCCESS};
    XrTime time{};
    uint64_t frame_generation{};
    Clock::time_point received{};
    // Positions and views share the runtime stage space, at the same locate time.
    std::array<XrView, 2> views{};
    // Normalized subimage bounds: left, right, top, bottom for each eye.
    std::array<std::array<float,4>,2> bounds{{{{0,1,0,1}},{{0,1,0,1}}}};
    std::array<Hand, 2> hands{};
};
inline constexpr std::array<std::array<int, 2>, 25> bones{{
    {{0,1}}, {{1,2}}, {{2,3}}, {{3,4}}, {{4,5}},
    {{1,6}}, {{6,7}}, {{7,8}}, {{8,9}}, {{9,10}},
    {{1,11}}, {{11,12}}, {{12,13}}, {{13,14}}, {{14,15}},
    {{1,16}}, {{16,17}}, {{17,18}}, {{18,19}}, {{19,20}},
    {{1,21}}, {{21,22}}, {{22,23}}, {{23,24}}, {{24,25}}
}};
inline bool finite(const XrVector3f& p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
inline bool fresh(const Snapshot& s, Clock::time_point now = Clock::now()) {
    return s.enabled && s.state == State::Active && s.time > 0 && now >= s.received &&
        now - s.received <= std::chrono::milliseconds(200);
}
struct Point { float x{}, y{}; };
// Project a stage-space point into an eye viewport. The renderer supplies its
// actual submitted eye pose/FOV; the inspector uses the captured views.
inline bool project(const XrVector3f& position, const XrView& view, float width, float height, Point& out) {
    const auto& q = view.pose.orientation;
    if (!finite(position) || !finite(view.pose.position) || !std::isfinite(width) ||
        !std::isfinite(height) || width <= 0 || height <= 0) return false;
    const float norm = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    if (!std::isfinite(norm) || std::abs(norm - 1.0f) > 0.02f) return false;
    const XrVector3f d{position.x-view.pose.position.x, position.y-view.pose.position.y,
        position.z-view.pose.position.z};
    // Quaternion inverse rotation: v + 2w(q* x v) + 2(q* x (q* x v)).
    const XrVector3f u{-q.x, -q.y, -q.z};
    const XrVector3f c{u.y*d.z-u.z*d.y, u.z*d.x-u.x*d.z, u.x*d.y-u.y*d.x};
    const XrVector3f eye{d.x+2*(q.w*c.x+u.y*c.z-u.z*c.y),
        d.y+2*(q.w*c.y+u.z*c.x-u.x*c.z), d.z+2*(q.w*c.z+u.x*c.y-u.y*c.x)};
    if (!finite(eye) || eye.z >= -0.05f) return false;
    const float left=std::tan(view.fov.angleLeft), right=std::tan(view.fov.angleRight);
    const float down=std::tan(view.fov.angleDown), up=std::tan(view.fov.angleUp);
    if (!std::isfinite(left) || !std::isfinite(right) || !std::isfinite(down) ||
        !std::isfinite(up) || right-left <= 0.0001f || up-down <= 0.0001f) return false;
    const float x=(eye.x/-eye.z-left)/(right-left), y=(up-eye.y/-eye.z)/(up-down);
    if (!std::isfinite(x) || !std::isfinite(y) || x<0 || x>1 || y<0 || y>1) return false;
    out={x*width,y*height}; return true;
}
enum class Layout { DoubleWide, Left, Right };
struct DrawConstants { std::array<float,12> xy{}; std::array<float,4> color{}; };
static_assert(sizeof(DrawConstants)==64);
struct Batch { std::array<DrawConstants,100> segments{}; size_t count{}; };
// Fixed storage: no allocations or locks per bone. Shader constants describe six
// clip-space vertices so DX12 can record them without a shared upload-buffer race.
inline Batch geometry(const Snapshot& sample, Layout layout, float width, float height,
    Clock::time_point now=Clock::now()) {
    Batch batch{};
    if (!fresh(sample,now) || !std::isfinite(width) || !std::isfinite(height) || width<=0 || height<=0) return batch;
    const bool wide=layout==Layout::DoubleWide;
    const float eye_width=wide?std::floor(width*0.5f):width;
    const float half_thickness=std::clamp(height/900.0f,1.0f,3.0f);
    for (size_t slot=0;slot<(wide?2u:1u);++slot) {
        const size_t eye=wide?slot:layout==Layout::Right?1u:0u;
        const float offset=wide?slot*eye_width:0.0f;
        const auto& bounds=sample.bounds[eye];
        bool valid_bounds=true;
        for(const auto bound:bounds) valid_bounds&=std::isfinite(bound) && bound>=0 && bound<=1;
        if(!valid_bounds || bounds[1]<=bounds[0] || bounds[3]<=bounds[2]) continue;
        // Match xrEndFrame's integer imageRect arithmetic exactly, including
        // cropped/asymmetric projection modes and odd double-wide widths.
        const float x0=std::floor(bounds[0]*eye_width),x1=std::floor(bounds[1]*eye_width);
        const float y0=std::floor(bounds[2]*height),y1=std::floor(bounds[3]*height);
        const float rect_width=x1-x0,rect_height=y1-y0;
        for (size_t side=0;side<sample.hands.size();++side) {
            const auto& hand=sample.hands[side]; if (!hand.optical) continue;
            for (const auto& bone:bones) {
                const auto& a=hand.joints[bone[0]]; const auto& b=hand.joints[bone[1]];
                Point p{},q{};
                if (!a.valid || !b.valid || !project(a.position,sample.views[eye],rect_width,rect_height,p) ||
                    !project(b.position,sample.views[eye],rect_width,rect_height,q)) continue;
                const float dx=q.x-p.x,dy=q.y-p.y,len=std::sqrt(dx*dx+dy*dy);
                if (!std::isfinite(len) || len<0.1f) continue;
                const float nx=-dy/len*half_thickness,ny=dx/len*half_thickness;
                const Point vertices[]={{p.x+nx,p.y+ny},{p.x-nx,p.y-ny},{q.x+nx,q.y+ny},
                    {q.x+nx,q.y+ny},{p.x-nx,p.y-ny},{q.x-nx,q.y-ny}};
                auto& segment=batch.segments[batch.count++];
                for (size_t v=0;v<6;++v) {
                    segment.xy[v*2]=(offset+x0+std::clamp(vertices[v].x,0.0f,rect_width))/width*2.0f-1.0f;
                    segment.xy[v*2+1]=1.0f-(y0+std::clamp(vertices[v].y,0.0f,rect_height))/height*2.0f;
                }
                segment.color=side==0?std::array<float,4>{0.31f,0.88f,0.78f,0.92f}:
                    std::array<float,4>{0.96f,0.73f,0.35f,0.92f};
            }
        }
    }
    return batch;
}
struct Api {
    PFN_xrCreateHandTrackerEXT create{};
    PFN_xrLocateHandJointsEXT locate{};
    PFN_xrDestroyHandTrackerEXT destroy{};
};
// Caller owns the runtime's submission mutex. One immutable tagged pose packet
// is shared by all eye draws and consumed once by that OpenXR frame's end call.
template<class T> class FrameReservation {
public:
    void begin() { abort(); m_active=true; }
    void abort() { m_value.reset(); m_drawn=false; m_active=false; ++m_generation; }
    const T* peek() const { return m_active && m_value?&*m_value:nullptr; }
    const T* reserve(const T& value) {
        if (!m_active) return nullptr;
        if (!m_value) m_value=value;
        return &*m_value;
    }
    uint64_t generation() const { return m_generation; }
    void drawn(uint64_t generation) { if (m_active && m_value && generation==m_generation) m_drawn=true; }
    std::optional<T> consume() {
        auto result=m_active && m_drawn?std::move(m_value):std::optional<T>{};
        abort(); return result;
    }
private:
    uint64_t m_generation{};
    bool m_active{},m_drawn{};
    std::optional<T> m_value;
};
class Tracking {
public:
    ~Tracking() { reset(); }
    // Session ownership stays with OpenXR. This is called before session teardown.
    void reset() {
        std::scoped_lock lock(m_mutex);
        release(); m_session=XR_NULL_HANDLE; m_api={}; m_supported=false;
        m_source=false; m_snapshot={};
    }
    void configure(XrSession session, bool supported, bool source, Api api) {
        std::scoped_lock lock(m_mutex);
        release(); m_session=session; m_api=api;
        m_supported=supported && session!=XR_NULL_HANDLE && api.create && api.locate && api.destroy;
        m_source=source; m_snapshot={}; m_snapshot.state=idle_state();
    }
    void enable(bool enabled) {
        std::scoped_lock lock(m_mutex);
        release(); m_snapshot={}; m_snapshot.enabled=enabled && m_supported && m_source;
        m_snapshot.state=m_snapshot.enabled?State::Waiting:idle_state();
    }
    // Loss of focus/recenter invalidates immediately and releases tracker handles.
    // The explicit opt-in survives temporary focus loss, not runtime recreation.
    void suspend() {
        std::scoped_lock lock(m_mutex);
        release(); clear_hands();
        m_snapshot.state=m_snapshot.enabled?State::Waiting:idle_state();
    }
    Snapshot snapshot(Clock::time_point now=Clock::now()) const {
        std::scoped_lock lock(m_mutex);
        auto result=m_snapshot;
        if (result.state==State::Active && !fresh(result,now)) {
            result.hands={}; result.state=State::Waiting;
        }
        return result;
    }
    void update(XrTime time, XrSpace stage, const std::array<XrView,2>& views,
        bool focused, Clock::time_point now=Clock::now()) {
        std::scoped_lock lock(m_mutex);
        clear_hands();
        if (!m_snapshot.enabled) return;
        m_snapshot.state=State::Waiting; m_snapshot.result=XR_SUCCESS;
        if (!focused || time<=0 || stage==XR_NULL_HANDLE) { release(); return; }
        for (size_t i=0;i<m_trackers.size();++i) {
            if (m_trackers[i]==XR_NULL_HANDLE) {
                XrHandTrackingDataSourceEXT optical=XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT;
                XrHandTrackingDataSourceInfoEXT source{XR_TYPE_HAND_TRACKING_DATA_SOURCE_INFO_EXT,nullptr,1,&optical};
                XrHandTrackerCreateInfoEXT info{XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};
                info.next=&source; info.hand=i==0?XR_HAND_LEFT_EXT:XR_HAND_RIGHT_EXT;
                info.handJointSet=XR_HAND_JOINT_SET_DEFAULT_EXT;
                const auto result=m_api.create(m_session,&info,&m_trackers[i]);
                if (XR_FAILED(result) || m_trackers[i]==XR_NULL_HANDLE) {
                    release(); m_snapshot.enabled=false; m_snapshot.state=State::Error;
                    m_snapshot.result=XR_FAILED(result)?result:XR_ERROR_RUNTIME_FAILURE; return;
                }
            }
        }
        m_snapshot.views=views; m_snapshot.time=time; m_snapshot.received=now;
        bool active=false;
        for (size_t i=0;i<m_trackers.size();++i) {
            std::array<XrHandJointLocationEXT,XR_HAND_JOINT_COUNT_EXT> joints{};
            XrHandTrackingDataSourceStateEXT source{XR_TYPE_HAND_TRACKING_DATA_SOURCE_STATE_EXT};
            XrHandJointLocationsEXT locations{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
            locations.next=&source; locations.jointCount=static_cast<uint32_t>(joints.size());
            locations.jointLocations=joints.data();
            XrHandJointsLocateInfoEXT info{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};
            info.baseSpace=stage; info.time=time;
            const auto result=m_api.locate(m_trackers[i],&info,&locations);
            if (XR_FAILED(result)) { m_snapshot.result=result; continue; }
            if (!locations.isActive || !source.isActive ||
                source.dataSource!=XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT) continue;
            auto& hand=m_snapshot.hands[i];
            bool valid=false;
            constexpr auto flags=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
            for (size_t j=0;j<joints.size();++j) {
                const auto& joint=joints[j];
                hand.joints[j]={joint.pose.position,
                    (joint.locationFlags&flags)==flags && finite(joint.pose.position)};
                valid|=hand.joints[j].valid;
            }
            hand.optical=valid; active|=valid;
        }
        m_snapshot.state=active?State::Active:
            (XR_FAILED(m_snapshot.result)?State::Error:State::Waiting);
    }
private:
    State idle_state() const { return !m_supported?State::Unsupported:!m_source?State::SourceUnavailable:State::Off; }
    void clear_hands() { m_snapshot.hands={}; m_snapshot.time=0; }
    void release() {
        for (auto& tracker:m_trackers) {
            if (tracker!=XR_NULL_HANDLE && m_api.destroy) m_api.destroy(tracker);
            tracker=XR_NULL_HANDLE;
        }
    }
    mutable std::mutex m_mutex;
    XrSession m_session{XR_NULL_HANDLE};
    Api m_api{};
    bool m_supported{},m_source{};
    std::array<XrHandTrackerEXT,2> m_trackers{};
    Snapshot m_snapshot{};
};
}
