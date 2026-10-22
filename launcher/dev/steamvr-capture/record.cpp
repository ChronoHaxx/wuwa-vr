// Public compositor mirrors only: never capture the desktop or inject input.
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <codecapi.h>
#include <wrl/client.h>
#include <openvr.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <Xinput.h>
#include <timeapi.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
using Microsoft::WRL::ComPtr;
using Json=nlohmann::json;
static void check(HRESULT value) { if (FAILED(value)) throw std::runtime_error("HRESULT "+std::to_string(value)); }
static int64_t utc() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
static bool steamvr_running() {
    const auto snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if (snap==INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W p{sizeof(p)}; bool found=false;
    if (Process32FirstW(snap,&p)) do { if (_wcsicmp(p.szExeFile,L"vrserver.exe")==0) found=true; } while (Process32NextW(snap,&p));
    CloseHandle(snap); return found;
}
struct Encoder {
    ComPtr<IMFSinkWriter> writer; DWORD stream{},sound{}; UINT w{},h{}; bool has_sound{};
    std::mutex lock;  // the sound thread writes beside the picture loop
    // high: H.264 High profile. sound_rate (44100 or 48000): adds a stereo AAC track fed by add_sound.
    void open(const std::filesystem::path& path,UINT width,UINT height,UINT fps,UINT bitrate=16000000,bool high=false,UINT sound_rate=0) {
        w=width; h=height;
        ComPtr<IMFAttributes> attrs; check(MFCreateAttributes(&attrs,3));
        check(attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,TRUE));
        // Fragmented MP4 (19 Oct 2026): what was written stays playable if this process dies before Finalize.
        // SteamVR quitting after the headset dropped out ended a recorder mid-part and left an mdat with no index.
        check(attrs->SetGUID(MF_TRANSCODE_CONTAINERTYPE,MFTranscodeContainerType_FMPEG4));
        // A stream that runs ahead must not stall the other one; both arrive in real time anyway.
        check(attrs->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING,TRUE));
        check(MFCreateSinkWriterFromURL(path.c_str(),nullptr,attrs.Get(),&writer));
        ComPtr<IMFMediaType> out,in; check(MFCreateMediaType(&out));
        check(out->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video)); check(out->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_H264));
        check(out->SetUINT32(MF_MT_AVG_BITRATE,bitrate));
        if (high) check(out->SetUINT32(MF_MT_MPEG2_PROFILE,eAVEncH264VProfile_High)); check(out->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive));
        check(MFSetAttributeSize(out.Get(),MF_MT_FRAME_SIZE,w,h)); check(MFSetAttributeRatio(out.Get(),MF_MT_FRAME_RATE,fps,1));
        check(MFSetAttributeRatio(out.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1)); check(writer->AddStream(out.Get(),&stream));
        check(MFCreateMediaType(&in)); check(in->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video));
        check(in->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32)); check(in->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive));
        check(MFSetAttributeSize(in.Get(),MF_MT_FRAME_SIZE,w,h)); check(MFSetAttributeRatio(in.Get(),MF_MT_FRAME_RATE,fps,1));
        check(MFSetAttributeRatio(in.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1));
        check(in->SetUINT32(MF_MT_DEFAULT_STRIDE,w*4));
        check(writer->SetInputMediaType(stream,in.Get(),nullptr));
        if (sound_rate) {
            // 192 kbit/s AAC-LC; the Windows AAC encoder takes 16-bit PCM at 44.1 or 48 kHz.
            ComPtr<IMFMediaType> aac,pcm; check(MFCreateMediaType(&aac));
            check(aac->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio)); check(aac->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_AAC));
            check(aac->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16)); check(aac->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,sound_rate));
            check(aac->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,2)); check(aac->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,24000));
            check(writer->AddStream(aac.Get(),&sound));
            check(MFCreateMediaType(&pcm));
            check(pcm->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio)); check(pcm->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_PCM));
            check(pcm->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16)); check(pcm->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,sound_rate));
            check(pcm->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,2)); check(pcm->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,4));
            check(pcm->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,sound_rate*4));
            check(writer->SetInputMediaType(sound,pcm.Get(),nullptr)); has_sound=true;
        }
        check(writer->BeginWriting());
    }
    void write(DWORD target,const void* bytes,size_t size,LONGLONG timestamp,LONGLONG duration) {
        ComPtr<IMFMediaBuffer> buffer; check(MFCreateMemoryBuffer(static_cast<DWORD>(size),&buffer));
        BYTE* data{}; check(buffer->Lock(&data,nullptr,nullptr)); memcpy(data,bytes,size); check(buffer->Unlock());
        check(buffer->SetCurrentLength(static_cast<DWORD>(size)));
        ComPtr<IMFSample> sample; check(MFCreateSample(&sample)); check(sample->AddBuffer(buffer.Get()));
        check(sample->SetSampleTime(timestamp)); check(sample->SetSampleDuration(duration));
        std::lock_guard guard(lock); if (writer) check(writer->WriteSample(target,sample.Get()));
    }
    void add(const std::vector<BYTE>& pixels,LONGLONG timestamp,LONGLONG duration) { write(stream,pixels.data(),pixels.size(),timestamp,duration); }
    // Interleaved stereo 16-bit frames.
    void add_sound(const int16_t* frames,size_t count,LONGLONG timestamp,LONGLONG duration) { write(sound,frames,count*4,timestamp,duration); }
    void close() { std::lock_guard guard(lock); if (writer) { const auto result=writer->Finalize(); writer.Reset(); check(result); } }
};
// A capture call that never returns must not cost the part (19 Oct 2026: SteamVR lost the headset, the mirror
// read blocked, the game closed unseen and SteamVR's shutdown ended the recorder before Finalize). The loop
// beats once per pass, at most ~100 ms apart; after stall_ms without a beat, on_stall runs on this thread.
struct Watchdog {
    std::atomic<int64_t> beat{0}; std::atomic<uint64_t> frames{0}; std::atomic<bool> done{false};
    std::thread worker; int64_t stall_ms{8000};
    void start(std::function<void()> on_stall) {
        beat=GetTickCount64();
        worker=std::thread([this,on_stall] {
            while (!done) {
                Sleep(250);
                if (!done && int64_t(GetTickCount64())-beat.load()>stall_ms) { on_stall(); return; }
            }
        });
    }
    void pulse() { beat=GetTickCount64(); }
    void finish() { done=true; if (worker.joinable()) worker.join(); }
    ~Watchdog() { finish(); }
};
// After a stall: the report first (the launcher reads it), then Finalize for at most 5 s (the fragments already
// play without it), then exit. A blocked OpenVR or D3D call keeps the process from ending any other way.
[[noreturn]] static void end_stalled(const std::filesystem::path& out,Json report,Encoder& encoder) {
    report["status"]="recorded"; report["end_reason"]="recorder_stalled";
    report["stall_note"]="The capture loop stopped responding (SteamVR or the headset went away); the video ends at the last saved fragment.";
    try { std::ofstream(out/L"recording.json")<<report.dump(2)<<'\n'; } catch (...) {}
    std::atomic<bool> closed{false};
    std::thread([&encoder,&closed] { try { encoder.close(); } catch (...) {} closed=true; }).detach();
    for (int i=0;i<50 && !closed;++i) Sleep(100);
    std::cout<<report.dump()<<std::endl;
    ExitProcess(0);
}
// The frames and sound samples a file gives back (self-tests: a recording whose writer was killed).
static std::pair<uint64_t,uint64_t> readable_samples(const std::filesystem::path& path) {
    uint64_t counts[2]{};
    for (int i=0;i<2;++i) {
        ComPtr<IMFSourceReader> reader; check(MFCreateSourceReaderFromURL(path.c_str(),nullptr,&reader));
        const DWORD stream=i?DWORD(MF_SOURCE_READER_FIRST_AUDIO_STREAM):DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
        for (;;) {
            DWORD actual{},flags{}; LONGLONG time{}; ComPtr<IMFSample> sample;
            if (FAILED(reader->ReadSample(stream,0,&actual,&flags,&time,&sample))) break;
            if (flags&(MF_SOURCE_READERF_ENDOFSTREAM|MF_SOURCE_READERF_ERROR)) break;
            if (sample) ++counts[i];
        }
    }
    return {counts[0],counts[1]};
}
// Now on the performance counter, in the 100-ns units WASAPI stamps each packet with.
static LONGLONG qpc_100ns() {
    LARGE_INTEGER count{},frequency{}; QueryPerformanceCounter(&count); QueryPerformanceFrequency(&frequency);
    return count.QuadPart/frequency.QuadPart*10000000+count.QuadPart%frequency.QuadPart*10000000/frequency.QuadPart;
}
static std::string utf8(const wchar_t* text) {
    if (!text || !*text) return {};
    const int size=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);
    if (size<=1) return {};
    std::string out(size_t(size),'\0'); WideCharToMultiByte(CP_UTF8,0,text,-1,out.data(),size,nullptr,nullptr);
    out.resize(size_t(size-1)); return out;
}
// PKEY_Device_FriendlyName ("Headset Microphone (Index)"), spelled out instead of pulling in initguid.
static const PROPERTYKEY device_friendly_name{{0xa45c254e,0xdf1c,0x4efd,{0x80,0x20,0x67,0xd1,0x46,0xa8,0x50,0xe0}},14};
static std::string device_name(IMMDevice* device) {
    ComPtr<IPropertyStore> store; PROPVARIANT value; PropVariantInit(&value); std::string name;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ,&store)) && SUCCEEDED(store->GetValue(device_friendly_name,&value))
        && value.vt==VT_LPWSTR) name=utf8(value.pwszVal);
    PropVariantClear(&value); return name;
}
static std::string device_id(IMMDevice* device) {
    LPWSTR id{}; std::string out;
    if (SUCCEEDED(device->GetId(&id))) { out=utf8(id); CoTaskMemFree(id); }
    return out;
}
// The microphones Windows has switched on (active capture endpoints), for the launcher's list; "default" marks the
// one Windows records from by default.
static Json list_microphones() {
    ComPtr<IMMDeviceEnumerator> devices;
    check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&devices)));
    std::string preferred;
    { ComPtr<IMMDevice> device; if (SUCCEEDED(devices->GetDefaultAudioEndpoint(eCapture,eConsole,&device))) preferred=device_id(device.Get()); }
    ComPtr<IMMDeviceCollection> all; check(devices->EnumAudioEndpoints(eCapture,DEVICE_STATE_ACTIVE,&all));
    UINT count{}; check(all->GetCount(&count));
    Json list=Json::array();
    for (UINT i=0;i<count && i<32;++i) {
        ComPtr<IMMDevice> device; if (FAILED(all->Item(i,&device))) continue;
        const auto id=device_id(device.Get()); if (id.empty() || id.size()>200) continue;
        list.push_back({{"id",id},{"name",device_name(device.Get())},{"default",id==preferred}});
    }
    return list;
}
// The sound track (1.2.6): lanes of stereo frames that their sources place on the picture's clock (lane 0: what the PC
// plays, lane 1: the microphone). The mix goes out lag_seconds behind real time, so a packet that arrives a little late
// still lands at its own time; where nothing was placed there is silence. Each lane times its gain; the sum is limited
// softly above 0.9 so a loud voice over loud game sound does not crackle.
struct Mixer {
    static constexpr double lag_seconds=0.3;
    struct Lane { std::vector<float> frames; float gain{1},peak{}; uint64_t heard{},late{}; };  // frames[2*(track frame-written)+side]
    UINT rate{48000}; Lane lanes[2]; uint64_t written{};
    // out: the mix, stereo 16-bit frames from track frame `first`. voice: the microphone lane alone (mono, no gain).
    std::function<void(const int16_t*,size_t,uint64_t)> out; std::function<void(const int16_t*,size_t)> voice;
    std::mutex lock; std::vector<int16_t> mixed,alone;
    static float limit(float v) { const float a=std::fabs(v); return a<=0.9f?v:std::copysign(0.9f+0.1f*std::tanh((a-0.9f)/0.1f),v); }
    static int16_t pcm(float v) { return static_cast<int16_t>(std::lround(limit(v)*32767.0f)); }
    void place(int lane,int64_t at,const float* stereo,size_t count) {
        std::lock_guard guard(lock); auto& l=lanes[lane];
        if (at<0) { const auto skip=std::min<uint64_t>(count,uint64_t(-at)); stereo+=skip*2; count-=size_t(skip); at=0; }  // before the start
        if (uint64_t(at)<written) {  // mixed already: too late
            const auto skip=std::min<uint64_t>(count,written-uint64_t(at)); l.late+=skip; stereo+=skip*2; count-=size_t(skip); at=int64_t(written);
        }
        if (!count) return;
        const size_t from=size_t(uint64_t(at)-written)*2;
        if (l.frames.size()<from+count*2) l.frames.resize(from+count*2,0.0f);
        std::copy(stereo,stereo+count*2,l.frames.begin()+ptrdiff_t(from)); l.heard+=count;
        for (size_t i=0;i<count*2;++i) l.peak=std::max(l.peak,std::fabs(stereo[i]));
    }
    // Hands out track frames [written, upto).
    void mix_until(uint64_t upto) {
        std::lock_guard guard(lock);
        if (upto<=written) return;
        const size_t count=size_t(upto-written);
        mixed.assign(count*2,0);
        for (size_t i=0;i<count*2;++i) {
            float v=0; for (const auto& l:lanes) if (i<l.frames.size()) v+=l.frames[i]*l.gain;
            mixed[i]=pcm(v);
        }
        if (voice) { const auto& m=lanes[1].frames; alone.resize(count); for (size_t i=0;i<count;++i) alone[i]=pcm(i*2<m.size()?m[i*2]:0.0f); }
        for (auto& l:lanes) l.frames.erase(l.frames.begin(),l.frames.begin()+ptrdiff_t(std::min(l.frames.size(),count*2)));
        const auto first=written; written=upto;
        if (out) out(mixed.data(),count,first);
        if (voice) voice(alone.data(),count);
    }
};
// One sound source: the default output by loopback, or a microphone. Loopback is what the PC plays (the headset while
// SteamVR runs). Windows 10 cannot capture one process alone (process loopback needs build 20348), so voice chat and
// notifications are recorded too; the recording menus say so. Loopback delivers nothing while nothing plays.
struct SoundSource {
    ComPtr<IAudioClient> client; ComPtr<IAudioCaptureClient> capture;
    UINT source_rate{},channels{},bits{}; bool floats{},loopback{};
    std::string name,error; uint64_t discontinuities{};
    std::thread worker; std::atomic<bool> stopping{false};
    ~SoundSource() { stop(); }
    // microphone: empty for loopback, "default" for the one Windows records from by default, or an endpoint ID.
    bool open(const std::wstring& microphone={}) {
        loopback=microphone.empty();
        try {
            ComPtr<IMMDeviceEnumerator> devices;
            check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&devices)));
            ComPtr<IMMDevice> device;
            if (loopback) check(devices->GetDefaultAudioEndpoint(eRender,eConsole,&device));
            else if (microphone==L"default") {
                if (FAILED(devices->GetDefaultAudioEndpoint(eCapture,eConsole,&device))) throw std::runtime_error("no microphone is switched on in Windows");
            } else {
                DWORD state{};
                if (FAILED(devices->GetDevice(microphone.c_str(),&device)) || FAILED(device->GetState(&state)) || state!=DEVICE_STATE_ACTIVE)
                    throw std::runtime_error("the chosen microphone is not connected");
                // Only a capture endpoint is a microphone (an output's ID would record what it plays).
                ComPtr<IMMEndpoint> endpoint; EDataFlow flow{};
                if (FAILED(device.As(&endpoint)) || FAILED(endpoint->GetDataFlow(&flow)) || flow!=eCapture)
                    throw std::runtime_error("the chosen device is not a microphone");
            }
            name=device_name(device.Get());
            check(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.GetAddressOf())));
            WAVEFORMATEX* mix{}; check(client->GetMixFormat(&mix));
            source_rate=mix->nSamplesPerSec; channels=mix->nChannels; bits=mix->wBitsPerSample;
            WORD tag=mix->wFormatTag;
            if (tag==WAVE_FORMAT_EXTENSIBLE) tag=static_cast<WORD>(reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix)->SubFormat.Data1);
            floats=tag==WAVE_FORMAT_IEEE_FLOAT && bits==32;
            const bool pcm=tag==WAVE_FORMAT_PCM && (bits==16 || bits==24 || bits==32);
            HRESULT initialized=E_FAIL;
            if ((floats || pcm) && channels>=1)
                initialized=client->Initialize(AUDCLNT_SHAREMODE_SHARED,loopback?AUDCLNT_STREAMFLAGS_LOOPBACK:0,10000000,0,mix,nullptr);
            CoTaskMemFree(mix);
            if (!floats && !pcm) throw std::runtime_error(loopback?"unsupported output format":"unsupported microphone format");
            check(initialized); check(client->GetService(IID_PPV_ARGS(&capture)));
            return true;
        } catch (const std::exception& e) { error=e.what(); client.Reset(); capture.Reset(); return false; }
    }
    // start: the picture's time zero, from qpc_100ns(), the clock of each packet. Packets follow one another on the
    // track; the clock decides only after a gap (nothing played) or when the device's clock drifted from it.
    void begin(Mixer& mixer,int lane,LONGLONG start) {
        worker=std::thread([this,&mixer,lane,start] {
            const bool com_ok=SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED));
            try {
                check(client->Start());
                const UINT rate=mixer.rate; std::vector<float> stereo,out; double phase=0,next=-1; float last[2]{};
                while (!stopping) {
                    Sleep(10);
                    UINT32 waiting{};
                    while (!stopping && SUCCEEDED(capture->GetNextPacketSize(&waiting)) && waiting) {
                        BYTE* data{}; UINT32 count{}; DWORD flags{}; UINT64 position{},clock{};
                        check(capture->GetBuffer(&data,&count,&flags,&position,&clock));
                        if (flags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) ++discontinuities;
                        stereo.assign(size_t(count)*2,0.0f);
                        if (!(flags&AUDCLNT_BUFFERFLAGS_SILENT)) for (UINT32 i=0;i<count;++i) {
                            const auto sample=[&](UINT c) {
                                const BYTE* at=data+(size_t(i)*channels+c)*(bits/8);
                                if (floats) return *reinterpret_cast<const float*>(at);
                                if (bits==16) return *reinterpret_cast<const int16_t*>(at)/32768.0f;
                                if (bits==24) return float((int32_t(at[0])<<8|int32_t(at[1])<<16|int32_t(at[2])<<24)>>8)/8388608.0f;
                                return *reinterpret_cast<const int32_t*>(at)/2147483648.0f;
                            };
                            if (loopback) for (UINT c=0;c<2;++c) stereo[size_t(i)*2+c]=std::clamp(sample(std::min(c,channels-1)),-1.0f,1.0f);
                            else {  // a microphone is one voice: its channels averaged, on both sides
                                float sum=0; for (UINT c=0;c<channels;++c) sum+=sample(c);
                                stereo[size_t(i)*2]=stereo[size_t(i)*2+1]=std::clamp(sum/float(channels),-1.0f,1.0f);
                            }
                        }
                        check(capture->ReleaseBuffer(count));
                        const float* frames=stereo.data(); size_t frame_count=count;
                        if (source_rate!=rate && count) {
                            // Linear resampling to the track's rate, continuous across packets.
                            const double step=double(source_rate)/rate; out.clear();
                            for (;phase<double(count);phase+=step) {
                                const auto i=static_cast<size_t>(std::floor(phase)); const double f=phase-std::floor(phase);
                                for (int c=0;c<2;++c) {
                                    const double a=i==0?last[c]:stereo[(i-1)*2+c], b=stereo[i*2+c];
                                    out.push_back(static_cast<float>(a+(b-a)*f));
                                }
                            }
                            phase-=double(count); last[0]=stereo[(count-1)*2]; last[1]=stereo[(count-1)*2+1];
                            frames=out.data(); frame_count=out.size()/2;
                        }
                        // Where this packet belongs on the picture's clock, in track frames (a packet without a valid
                        // time ends now).
                        const double at=(flags&AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)
                            ? double(qpc_100ns()-start)*rate/1e7-double(frame_count) : (double(clock)-double(start))*rate/1e7;
                        if (next<0 || at>next+rate*0.02 || at<next-rate*0.05) next=at;
                        if (frame_count) mixer.place(lane,std::llround(next),frames,frame_count);
                        next+=double(frame_count);
                    }
                }
            } catch (const std::exception& e) { error=e.what(); }
            client->Stop();
            if (com_ok) CoUninitialize();
        });
    }
    void stop() { stopping=true; if (worker.joinable()) worker.join(); }
};
// The microphone alone, 16-bit mono at the track's rate and timing, beside the video: an edit can change its level or
// leave it out. The header is rewritten every second, so a recorder that dies still leaves a file that plays.
struct Wav {
    std::ofstream file; UINT rate{}; uint64_t frames{},unsaved{};
    bool open(const std::filesystem::path& path,UINT sample_rate) {
        rate=sample_rate; file.open(path,std::ios::binary); if (!file) return false;
        header(); return bool(file);
    }
    void header() {
        const auto data=static_cast<uint32_t>(std::min<uint64_t>(frames*2,0xFFFFFF00ull));
        const auto u32=[&](uint32_t v) { file.write(reinterpret_cast<const char*>(&v),4); };
        const auto u16=[&](uint16_t v) { file.write(reinterpret_cast<const char*>(&v),2); };
        file.seekp(0); file.write("RIFF",4); u32(36+data); file.write("WAVEfmt ",8); u32(16); u16(1); u16(1); u32(rate); u32(rate*2);
        u16(2); u16(16); file.write("data",4); u32(data); file.seekp(0,std::ios::end); file.flush(); unsaved=0;
    }
    void add(const int16_t* samples,size_t count) {
        if (!file.is_open()) return;
        file.write(reinterpret_cast<const char*>(samples),static_cast<std::streamsize>(count*2)); frames+=count;
        if ((unsaved+=count)>=rate) header();
    }
    void close() { if (file.is_open()) { header(); file.close(); } }
};
// What the video's sound track records: what the PC plays and/or a microphone, mixed (see Mixer).
struct Sound {
    Mixer mixer; SoundSource pc,mic; Wav voice;
    bool pc_on{},mic_on{},begun{}; int volume{100}; std::string error,voice_file;
    std::thread pump; std::atomic<bool> stopping{false}; LONGLONG started{};
    ~Sound() { stop(); }
    // with_pc: what the PC plays. microphone: empty for none, "default" or an endpoint ID; mic_volume in percent.
    // True when there is a track to record; a source that could not open says why in its error.
    bool open(bool with_pc,const std::wstring& microphone={},int mic_volume=100) {
        if (with_pc) pc_on=pc.open();
        if (!microphone.empty()) mic_on=mic.open(microphone);
        const auto& lead=pc_on?pc:mic;
        mixer.rate=lead.source_rate==44100?44100u:48000u;  // anything else is resampled to 48 kHz
        volume=mic_volume; mixer.lanes[1].gain=float(mic_volume)/100.0f;
        return pc_on || mic_on;
    }
    // out: where the mix goes. voice_path: the microphone's own file (none when empty).
    void begin(std::function<void(const int16_t*,size_t,uint64_t)> out,LONGLONG start,const std::filesystem::path& voice_path={}) {
        mixer.out=std::move(out); started=start; begun=true;
        if (mic_on && !voice_path.empty() && voice.open(voice_path,mixer.rate)) {
            voice_file=voice_path.filename().string(); mixer.voice=[this](const int16_t* samples,size_t count) { voice.add(samples,count); };
        }
        if (pc_on) pc.begin(mixer,0,start);
        if (mic_on) mic.begin(mixer,1,start);
        pump=std::thread([this] {
            try {
                while (!stopping) {
                    Sleep(50);
                    const double upto=double(qpc_100ns()-started)*mixer.rate/1e7-Mixer::lag_seconds*mixer.rate;
                    if (upto>0) mixer.mix_until(static_cast<uint64_t>(upto));
                }
            } catch (const std::exception& e) { error=e.what(); }
        });
    }
    void begin(Encoder& encoder,LONGLONG start,const std::filesystem::path& voice_path={}) {
        begin([this,&encoder](const int16_t* frames,size_t count,uint64_t first) {
            encoder.add_sound(frames,count,static_cast<LONGLONG>(double(first)*1e7/mixer.rate),static_cast<LONGLONG>(double(count)*1e7/mixer.rate));
        },start,voice_path);
    }
    void stop() { pc.stop(); mic.stop(); stopping=true; if (pump.joinable()) pump.join(); }
    // Before the encoder closes: everything placed, then silence up to now, so the track lasts as long as the picture
    // (and a recording where nothing played still has samples; the sink writer refuses to finish a track without any).
    void finish() {
        stop();
        if (!begun) return;
        if (error.empty()) {
            try { mixer.mix_until(static_cast<uint64_t>(std::max(0.0,double(qpc_100ns()-started)*mixer.rate/1e7))); }
            catch (const std::exception& e) { error=e.what(); }
        }
        voice.close();
    }
    double seconds() const { return double(mixer.written)/mixer.rate; }
};
struct Eye {
    vr::IVRCompositor* compositor{}; ID3D11ShaderResourceView* view{};
    ComPtr<ID3D11Texture2D> texture; UINT width{},height{}; bool bgra{},srgb{};
    ~Eye() { if (view) compositor->ReleaseMirrorTextureD3D11(view); }
    void open(vr::IVRCompositor* c,ID3D11Device* device,int eye) {
        compositor=c;
        const auto result=c->GetMirrorTextureD3D11(static_cast<vr::EVREye>(eye),device,reinterpret_cast<void**>(&view));
        if (result || !view) throw std::runtime_error("Mirror unavailable "+std::to_string(result));
        ComPtr<ID3D11Resource> resource; view->GetResource(&resource); check(resource.As(&texture));
        D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc); width=desc.Width; height=desc.Height;
        D3D11_SHADER_RESOURCE_VIEW_DESC vd{}; view->GetDesc(&vd);
        bgra=vd.Format==DXGI_FORMAT_B8G8R8A8_UNORM || vd.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        srgb=vd.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || vd.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        const bool rgba=vd.Format==DXGI_FORMAT_R8G8B8A8_UNORM || vd.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        if ((!bgra&&!rgba)||desc.SampleDesc.Count!=1||desc.ArraySize!=1||desc.MipLevels!=1||!width||!height)
            throw std::runtime_error("Unsupported mirror format");
    }
};
// Resize each eye on the GPU, then read back only the final SBS dimensions.
// The former CPU bilinear loop read full 2700x2880 mirrors and managed only
// ~10.7 new captures/sec in the owner's first 30-fps recording.
struct StereoResize {
    ComPtr<ID3D11Texture2D> target,staging;
    ComPtr<ID3D11RenderTargetView> linear,srgb;
    ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11SamplerState> sampler;
    UINT width{},height{};
    void open(ID3D11Device* device,UINT eye_width,UINT eye_height) {
        width=eye_width*2; height=eye_height;
        D3D11_TEXTURE2D_DESC d{}; d.Width=width; d.Height=height; d.MipLevels=d.ArraySize=1;
        d.Format=DXGI_FORMAT_B8G8R8A8_TYPELESS; d.SampleDesc.Count=1; d.BindFlags=D3D11_BIND_RENDER_TARGET;
        check(device->CreateTexture2D(&d,nullptr,&target));
        D3D11_RENDER_TARGET_VIEW_DESC rv{}; rv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        rv.Format=DXGI_FORMAT_B8G8R8A8_UNORM; check(device->CreateRenderTargetView(target.Get(),&rv,&linear));
        rv.Format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; check(device->CreateRenderTargetView(target.Get(),&rv,&srgb));
        d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        check(device->CreateTexture2D(&d,nullptr,&staging));
        constexpr char shader[]=R"(
struct V { float4 p:SV_Position; float2 uv:TEXCOORD0; };
V vert(uint id:SV_VertexID) { V v; v.uv=float2((id<<1)&2,id&2); v.p=float4(v.uv*float2(2,-2)+float2(-1,1),0,1); return v; }
Texture2D eye:register(t0); SamplerState smp:register(s0);
float4 pixel(V v):SV_Target { return float4(eye.Sample(smp,v.uv).rgb,1); }
)";
        ComPtr<ID3DBlob> v,p,error;
        check(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"vert","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&v,&error));
        check(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"pixel","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&p,&error));
        check(device->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs));
        check(device->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps));
        D3D11_SAMPLER_DESC s{}; s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; s.MaxLOD=D3D11_FLOAT32_MAX;
        check(device->CreateSamplerState(&s,&sampler));
    }
    void read(ID3D11DeviceContext* context,ID3D11ShaderResourceView* left,ID3D11ShaderResourceView* right,
              bool left_srgb,bool right_srgb,std::vector<BYTE>& output) {
        context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(),nullptr,0); context->PSSetShader(ps.Get(),nullptr,0);
        auto* smp=sampler.Get(); context->PSSetSamplers(0,1,&smp);
        ID3D11ShaderResourceView* eyes[]{left,right}; const bool encoded[]{left_srgb,right_srgb};
        for (int i=0;i<2;++i) {
            D3D11_VIEWPORT vp{float(i)*float(width)/2,0,float(width)/2,float(height),0,1}; context->RSSetViewports(1,&vp);
            auto* rtv=encoded[i]?srgb.Get():linear.Get(); context->OMSetRenderTargets(1,&rtv,nullptr);
            context->PSSetShaderResources(0,1,&eyes[i]); context->Draw(3,0);
        }
        ID3D11ShaderResourceView* empty{}; context->PSSetShaderResources(0,1,&empty);
        context->OMSetRenderTargets(0,nullptr,nullptr); context->CopyResource(staging.Get(),target.Get());
        D3D11_MAPPED_SUBRESOURCE map{}; check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map));
        output.resize(static_cast<size_t>(width)*height*4);
        for (UINT y=0;y<height;++y) memcpy(output.data()+static_cast<size_t>(y)*width*4,
            static_cast<const BYTE*>(map.pData)+static_cast<size_t>(y)*map.RowPitch,width*4);
        context->Unmap(staging.Get(),0);
    }
};
// Showcase video: the left eye as a 16:9 camera on its optical axis, steadied, at the game's own frame rate.
// The per-eye mirror is what the headset shows (the HUD at its place included). It changes only when the game
// submits a frame (about 30 a second), so each new game frame is written once at the time it arrived, instead of
// the uneven repeats a fixed-rate capture makes (those judder).
struct Quat { double w{1},x{},y{},z{}; };
static Quat qmul(Quat a,Quat b) {
    return {a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z, a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
            a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x, a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w};
}
static Quat qconj(Quat q) { return {q.w,-q.x,-q.y,-q.z}; }
static Quat qnorm(Quat q) { const double n=std::sqrt(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z); return n>0?Quat{q.w/n,q.x/n,q.y/n,q.z/n}:Quat{}; }
static double qangle(Quat a,Quat b) { return 2*std::acos(std::min(1.0,std::abs(a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z))); }
static Quat qslerp(Quat a,Quat b,double t) {
    double d=a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z; if (d<0) { b={-b.w,-b.x,-b.y,-b.z}; d=-d; }
    if (d>0.9995) return qnorm({a.w+(b.w-a.w)*t,a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t});
    const double th=std::acos(d),sn=std::sin(th),ka=std::sin((1-t)*th)/sn,kb=std::sin(t*th)/sn;
    return {a.w*ka+b.w*kb,a.x*ka+b.x*kb,a.y*ka+b.y*kb,a.z*ka+b.z*kb};
}
static Quat quat_from(const vr::HmdMatrix34_t& m) {
    const double a=m.m[0][0],b=m.m[0][1],c=m.m[0][2],d=m.m[1][0],e=m.m[1][1],f=m.m[1][2],g=m.m[2][0],h=m.m[2][1],i=m.m[2][2];
    const double trace=a+e+i; Quat q;
    if (trace>0) { const double k=std::sqrt(trace+1.0)*2; q={0.25*k,(h-f)/k,(c-g)/k,(d-b)/k}; }
    else if (a>e && a>i) { const double k=std::sqrt(1.0+a-e-i)*2; q={(h-f)/k,0.25*k,(b+d)/k,(c+g)/k}; }
    else if (e>i) { const double k=std::sqrt(1.0+e-a-i)*2; q={(c-g)/k,(b+d)/k,0.25*k,(f+h)/k}; }
    else { const double k=std::sqrt(1.0+i-a-e)*2; q={(d-b)/k,(c+g)/k,(f+h)/k,0.25*k}; }
    return qnorm(q);
}
// Rows of the rotation matrix of q (v' = q v q*), padded to float4 for the shader.
static void qrows(Quat q,float rows[3][4]) {
    const double w=q.w,x=q.x,y=q.y,z=q.z;
    const double m[3][3]{{1-2*(y*y+z*z),2*(x*y-w*z),2*(x*z+w*y)},{2*(x*y+w*z),1-2*(x*x+z*z),2*(y*z-w*x)},{2*(x*z-w*y),2*(y*z+w*x),1-2*(x*x+y*y)}};
    for (int r=0;r<3;++r) { for (int c=0;c<3;++c) rows[r][c]=float(m[r][c]); rows[r][3]=0; }
}
// The same rule as the plugin's steady desktop view (utility/WuWaSteadyView.hpp): follow the head with a time
// constant, and past a knee ease the lag towards the margin instead of stopping hard at it.
struct Steady {
    Quat smooth; double last{}; bool valid{};
    // The smoothed view's rotation in the actual view's frame (identity when there is nothing to smooth).
    Quat update(Quat actual,double now,double tau,double max_angle) {
        const double dt=now-last; last=now;
        if (!valid || dt>0.5 || dt<0 || qangle(smooth,actual)>0.6) { smooth=actual; valid=true; return {}; }
        if (dt>0) smooth=qnorm(qslerp(smooth,actual,1.0-std::exp(-dt/std::max(0.01,tau))));
        const double lag=qangle(smooth,actual),knee=0.6*max_angle;
        if (lag>knee) smooth=qnorm(qslerp(actual,smooth,(knee+(max_angle-knee)*std::tanh((lag-knee)/(max_angle-knee)))/lag));
        return qnorm(qmul(qconj(actual),smooth));
    }
};
// Draws one eye as seen from the steadied view into a 16:9 target (and a 32x32 fingerprint of the whole eye), and
// reads them back. Each output pixel is a ray of the steadied camera, turned into the rendered eye's frame and
// projected with the eye's tangents. OpenVR GetProjectionRaw gives left < 0 < right, and names the vertical sides
// the other way round from the picture: "top" is the downward tangent (negative) and "bottom" the upward one, which
// its own projection matrix puts on the first row (OpenXR's angleDown / angleUp, as UEVR logs them). Up to test 18
// the crop read top as up, so a headset with more view below than above recorded a view tilted down.
struct ShowcaseCrop {
    struct Constants { float tangents[4]; float crop[4]; float rows[3][4]; };
    ComPtr<ID3D11Texture2D> target,staging,print_target,print_staging;
    ComPtr<ID3D11RenderTargetView> linear,srgb,print_linear,print_srgb;
    ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps,print_ps;
    ComPtr<ID3D11SamplerState> sampler; ComPtr<ID3D11Buffer> constants;
    UINT width{},height{};
    static void targets(ID3D11Device* device,UINT w,UINT h,ComPtr<ID3D11Texture2D>& tex,ComPtr<ID3D11Texture2D>& stage,
                        ComPtr<ID3D11RenderTargetView>& lin,ComPtr<ID3D11RenderTargetView>& enc) {
        D3D11_TEXTURE2D_DESC d{}; d.Width=w; d.Height=h; d.MipLevels=d.ArraySize=1;
        d.Format=DXGI_FORMAT_B8G8R8A8_TYPELESS; d.SampleDesc.Count=1; d.BindFlags=D3D11_BIND_RENDER_TARGET;
        check(device->CreateTexture2D(&d,nullptr,&tex));
        D3D11_RENDER_TARGET_VIEW_DESC rv{}; rv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        rv.Format=DXGI_FORMAT_B8G8R8A8_UNORM; check(device->CreateRenderTargetView(tex.Get(),&rv,&lin));
        rv.Format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; check(device->CreateRenderTargetView(tex.Get(),&rv,&enc));
        d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        check(device->CreateTexture2D(&d,nullptr,&stage));
    }
    void open(ID3D11Device* device,UINT w,UINT h) {
        width=w; height=h;
        targets(device,w,h,target,staging,linear,srgb);
        targets(device,32,32,print_target,print_staging,print_linear,print_srgb);
        constexpr char shader[]=R"(
cbuffer C:register(b0) { float4 tangents; float4 crop; float4 row0; float4 row1; float4 row2; };
struct V { float4 p:SV_Position; float2 uv:TEXCOORD0; };
V vert(uint id:SV_VertexID) { V v; v.uv=float2((id<<1)&2,id&2); v.p=float4(v.uv*float2(2,-2)+float2(-1,1),0,1); return v; }
Texture2D eye:register(t0); SamplerState smp:register(s0);
float4 pixel(V v):SV_Target {
    float3 ray=float3((v.uv.x*2-1)*crop.x+crop.z,(1-v.uv.y*2)*crop.y+crop.w,-1);
    float3 a=float3(dot(row0.xyz,ray),dot(row1.xyz,ray),dot(row2.xyz,ray));
    float2 t=a.xy/max(1e-4,-a.z);
    float2 uv=float2((t.x-tangents.x)/(tangents.y-tangents.x),(tangents.w-t.y)/(tangents.w-tangents.z));
    return float4(eye.Sample(smp,uv).rgb,1);
}
float4 whole(V v):SV_Target { return float4(eye.Sample(smp,v.uv).rgb,1); }
)";
        ComPtr<ID3DBlob> v,p,q,error;
        check(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"vert","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&v,&error));
        check(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"pixel","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&p,&error));
        check(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"whole","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&q,&error));
        check(device->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs));
        check(device->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps));
        check(device->CreatePixelShader(q->GetBufferPointer(),q->GetBufferSize(),nullptr,&print_ps));
        D3D11_SAMPLER_DESC sd{}; sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD=D3D11_FLOAT32_MAX;
        check(device->CreateSamplerState(&sd,&sampler));
        D3D11_BUFFER_DESC bd{}; bd.ByteWidth=sizeof(Constants); bd.Usage=D3D11_USAGE_DYNAMIC;
        bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        check(device->CreateBuffer(&bd,nullptr,&constants));
    }
    static void read_back(ID3D11DeviceContext* context,ID3D11Texture2D* tex,ID3D11Texture2D* stage,UINT w,UINT h,std::vector<BYTE>& out) {
        context->CopyResource(stage,tex);
        D3D11_MAPPED_SUBRESOURCE map{}; check(context->Map(stage,0,D3D11_MAP_READ,0,&map));
        out.resize(static_cast<size_t>(w)*h*4);
        for (UINT y=0;y<h;++y) memcpy(out.data()+static_cast<size_t>(y)*w*4,static_cast<const BYTE*>(map.pData)+static_cast<size_t>(y)*map.RowPitch,w*4);
        context->Unmap(stage,0);
    }
    void draw(ID3D11DeviceContext* context,ID3D11ShaderResourceView* eye,bool encoded,ID3D11PixelShader* shader,
              ID3D11RenderTargetView* lin,ID3D11RenderTargetView* enc,UINT w,UINT h) {
        context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(),nullptr,0); context->PSSetShader(shader,nullptr,0);
        auto* smp=sampler.Get(); context->PSSetSamplers(0,1,&smp);
        auto* cb=constants.Get(); context->PSSetConstantBuffers(0,1,&cb);
        D3D11_VIEWPORT vp{0,0,float(w),float(h),0,1}; context->RSSetViewports(1,&vp);
        auto* rtv=encoded?enc:lin; context->OMSetRenderTargets(1,&rtv,nullptr);
        context->PSSetShaderResources(0,1,&eye); context->Draw(3,0);
        ID3D11ShaderResourceView* empty{}; context->PSSetShaderResources(0,1,&empty); context->OMSetRenderTargets(0,nullptr,nullptr);
    }
    // Equal fingerprints mean the mirror still holds the same picture.
    void fingerprint(ID3D11DeviceContext* context,ID3D11ShaderResourceView* eye,bool encoded,std::vector<BYTE>& out) {
        draw(context,eye,encoded,print_ps.Get(),print_linear.Get(),print_srgb.Get(),32,32);
        read_back(context,print_target.Get(),print_staging.Get(),32,32,out);
    }
    // The whole eye, as the headset shows it, resized to the target (the one-eye video: no crop, no steadying).
    void whole(ID3D11DeviceContext* context,ID3D11ShaderResourceView* eye,bool encoded,std::vector<BYTE>& out) {
        draw(context,eye,encoded,print_ps.Get(),linear.Get(),srgb.Get(),width,height);
        read_back(context,target.Get(),staging.Get(),width,height,out);
    }
    void read(ID3D11DeviceContext* context,ID3D11ShaderResourceView* eye,bool encoded,const Constants& c,std::vector<BYTE>& out) {
        D3D11_MAPPED_SUBRESOURCE map{}; check(context->Map(constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map));
        memcpy(map.pData,&c,sizeof(c)); context->Unmap(constants.Get(),0);
        draw(context,eye,encoded,ps.Get(),linear.Get(),srgb.Get(),width,height);
        read_back(context,target.Get(),staging.Get(),width,height,out);
    }
};
// The crop: the eye's whole width less room on each side for the steady view to turn, at most 90 degrees across
// (trimmed from the wider side first), and 16:9. An eye sees further outwards than towards the nose, so the window
// sits off the axis on the outer side (a shifted lens, like an off-axis camera): test 18 kept it centred, which cut
// it to the narrow nasal side (65 degrees across on a headset that shows 89). Vertically the lens shifts by pitch
// (radians, negative looks lower), within the eye: people look below where the head points, and on the owner's
// headset level with the head read as too high and 23 degrees down as too low (test 18c, 17 Oct).
constexpr double showcase_default_pitch=-0.20943951023931956;  // 12 degrees down
struct ShowcaseFrame { double half_width{},half_height{},centre_x{},centre_y{}; };
static ShowcaseFrame showcase_frame(float left,float right,float top,float bottom,double room_angle,double pitch=0) {
    ShowcaseFrame f;
    double outer_left=std::atan(-double(left))-room_angle, outer_right=std::atan(double(right))-room_angle;
    const double up=std::atan(double(bottom))-room_angle, down=std::atan(-double(top))-room_angle;
    if (outer_left<=0.03 || outer_right<=0.03 || up<=0.03 || down<=0.03) throw std::runtime_error("Eye view too narrow for a showcase crop");
    if (double excess=outer_left+outer_right-1.5707963267948966; excess>0) {
        double& wider=outer_left>outer_right?outer_left:outer_right;
        const double even=std::min(excess,std::abs(outer_left-outer_right));
        wider-=even; excess-=even; outer_left-=excess/2; outer_right-=excess/2;
    }
    const double x0=-std::tan(outer_left), x1=std::tan(outer_right), y_up=std::tan(up), y_down=std::tan(down);
    f.half_width=(x1-x0)/2; f.centre_x=(x1+x0)/2; f.half_height=f.half_width*9.0/16.0;
    if (2*f.half_height>y_up+y_down) { f.half_height=(y_up+y_down)/2; f.half_width=f.half_height*16.0/9.0; }
    f.centre_y=std::clamp(std::tan(std::clamp(pitch,-1.0,1.0)),f.half_height-y_down,y_up-f.half_height);
    return f;
}
// SteamVR asks applications to quit before it shuts down, then ends those still running five seconds later.
static bool steamvr_quitting(vr::IVRSystem* system) {
    vr::VREvent_t event{};
    while (system->PollNextEvent(&event,sizeof(event))) {
        if (event.eventType==vr::VREvent_Quit) { system->AcknowledgeQuit_Exiting(); return true; }
    }
    return false;
}
static bool same_print(const std::vector<BYTE>& a,const std::vector<BYTE>& b) {
    if (a.size()!=b.size() || a.empty()) return false;
    for (size_t i=0;i<a.size();++i) if (std::abs(int(a[i])-int(b[i]))>2) return false;
    return true;
}
// Side-by-side bitrate, about 0.18 bits per pixel: 18 Mbit/s at 1280 px per eye and 30 fps, 45 at 2048.
static UINT sbs_bitrate(UINT width,UINT height,UINT fps) {
    return static_cast<UINT>(std::clamp(double(width)*height*fps*0.18,16e6,100e6));
}
// The sound part of recording.json. audio_*: the track and what the PC plays; microphone: the voice, when one was asked.
static Json report_sound(bool asked_pc,bool asked_mic,bool opened,Sound& sound) {
    Json report{{"audio",false}};
    if (asked_pc && !sound.pc_on) report["audio_error"]=sound.pc.error;
    if (opened) {
        const double seconds=sound.seconds(), pc_seconds=double(sound.mixer.lanes[0].heard)/sound.mixer.rate;
        std::string source=sound.pc_on?"default output device (everything the PC plays)":"";
        if (sound.mic_on) source+=(source.empty()?"":" and ")+std::string("microphone: ")+sound.mic.name;
        report.update({{"audio",sound.error.empty() && sound.pc.error.empty() && sound.mixer.written>0},
            {"audio_source",source},{"audio_rate",sound.mixer.rate},{"audio_seconds",seconds},
            {"audio_error",!sound.error.empty()?sound.error:sound.pc.error}});
        if (sound.pc_on) report.update({{"audio_device_rate",sound.pc.source_rate},{"audio_silence_seconds",std::max(0.0,seconds-pc_seconds)},
            {"audio_discontinuities",sound.pc.discontinuities}});
    }
    if (asked_mic) {
        const auto& lane=sound.mixer.lanes[1];
        Json mic{{"recorded",sound.mic_on && sound.mic.error.empty()},{"name",sound.mic.name},{"error",sound.mic.error},{"volume_percent",sound.volume}};
        if (sound.mic_on) mic.update({{"device_rate",sound.mic.source_rate},{"channels",sound.mic.channels},
            {"seconds",double(lane.heard)/sound.mixer.rate},{"late_seconds",double(lane.late)/sound.mixer.rate},{"peak",lane.peak},
            {"discontinuities",sound.mic.discontinuities},{"file",sound.voice_file}});
        // A microphone that gave only silence is usually muted (a switch on the headset, or Windows privacy settings).
        if (sound.mic_on && lane.peak<1e-4f) mic["note"]="The microphone gave only silence: check that it is not muted.";
        report["microphone"]=mic;
    }
    return report;
}
int wmain(int argc,wchar_t** argv) {
    HMODULE library{}; void(__cdecl* shutdown)(){}; bool initialized=false,media=false,com=false;
    Encoder encoder; Sound sound; Json result; std::filesystem::path out; bool output_created=false;
    try {
        if (argc==2 && std::wstring_view(argv[1])==L"--list-microphones") {
            // For the launcher: the microphones to choose from, as one line of JSON (ASCII; names escaped).
            check(CoInitializeEx(nullptr,COINIT_MULTITHREADED)); com=true;
            std::cout<<Json{{"microphones",list_microphones()}}.dump(-1,' ',true)<<std::endl;
            CoUninitialize(); com=false; return 0;
        }
        if (argc==2 && std::wstring_view(argv[1])==L"--self-test-mixer") {
            // Synthetic lanes, no devices: placement by time, a packet before the start, a late one, a gap, the
            // microphone's gain, the soft limit, and the microphone's own file (raw, no gain).
            Mixer m; m.rate=48000; m.lanes[1].gain=2.0f; std::vector<int16_t> track,alone;
            m.out=[&](const int16_t* f,size_t n,uint64_t first) {
                if (first*2!=track.size()) throw std::runtime_error("Mixer skipped or repeated track frames");
                track.insert(track.end(),f,f+n*2);
            };
            m.voice=[&](const int16_t* f,size_t n) { alone.insert(alone.end(),f,f+n); };
            const std::vector<float> game(4800*2,0.25f),voice(480*2,0.1f),loud(480*2,0.8f);
            m.place(0,-2400,game.data(),4800);  // half of it before the start: frames 0..2399
            m.place(1,1000,voice.data(),480);   // 1000..1479
            m.mix_until(1200);
            m.place(1,1100,voice.data(),480);   // 1100..1199 too late, 1200..1579 placed
            m.place(0,9600,loud.data(),480); m.place(1,9600,loud.data(),480);  // after a silent gap; 0.8 + 2 x 0.8
            m.mix_until(12000);
            const auto at=[&](size_t frame) { return int(track[frame*2]); };
            const auto about=[](int value,double want) { return std::abs(value-want*32767)<=2; };
            if (track.size()!=12000*2 || alone.size()!=12000) throw std::runtime_error("Mixer track has the wrong length");
            if (!about(at(0),0.25) || !about(at(999),0.25) || !about(at(1000),0.45) || !about(at(1500),0.45) || !about(at(1700),0.25) || at(3000)!=0)
                throw std::runtime_error("Mixer placed a lane at the wrong time or level");
            if (m.lanes[1].late!=100 || at(9700)<=int(0.9*32767) || at(9700)>32767 || track[9700*2+1]!=track[9700*2])
                throw std::runtime_error("Mixer late count, soft limit or sides are wrong");
            if (!about(alone[1000],0.1) || !about(alone[9700],0.8) || alone[3000]!=0) throw std::runtime_error("Microphone file is not the raw voice");
            std::cout<<"Mixer: lanes placed by time, early and late packets cut, silent gap, gain, soft limit ("<<at(9700)
                <<"), raw microphone file; no devices.\n"; return 0;
        }
        if ((argc==2 || argc==3) && std::wstring_view(argv[1]).starts_with(L"--self-test-sound=")) {
            // Real devices, no VR: 2 s of what the PC plays and the chosen microphone ("default", an ID, or "none")
            // into a small black video, with the microphone's file beside it when a path is given.
            const std::wstring choice{std::wstring_view(argv[1]).substr(18)};
            const std::filesystem::path video=argc==3?std::filesystem::path(argv[2]):std::filesystem::path{};
            auto beside=video; if (!video.empty()) beside.replace_filename(video.stem().wstring()+L"-microphone.wav");
            if (!video.empty() && (std::filesystem::exists(video) || std::filesystem::exists(beside))) throw std::runtime_error("Self-test output exists");
            check(CoInitializeEx(nullptr,COINIT_MULTITHREADED)); com=true; check(MFStartup(MF_VERSION)); media=true;
            const bool asked_mic=choice!=L"none";
            const bool opened=sound.open(true,asked_mic?choice:std::wstring{},100);
            if (!opened) throw std::runtime_error("No sound source opened: "+sound.pc.error+" / "+sound.mic.error);
            constexpr UINT fps=30;
            std::vector<BYTE> black(320*240*4,0);
            if (!video.empty()) encoder.open(video,320,240,fps,2000000,true,sound.mixer.rate);
            const auto start=std::chrono::steady_clock::now();
            if (video.empty()) sound.begin([](const int16_t*,size_t,uint64_t) {},qpc_100ns());
            else sound.begin(encoder,qpc_100ns(),beside);
            for (UINT frame=0;frame<fps*2;++frame) {
                if (!video.empty()) encoder.add(black,LONGLONG(frame)*10000000/fps,10000000/fps);
                std::this_thread::sleep_until(start+std::chrono::milliseconds((frame+1)*1000/fps));
            }
            sound.finish(); if (!video.empty()) encoder.close();
            MFShutdown(); media=false;
            const auto report=report_sound(true,asked_mic,opened,sound);
            std::cout<<report.dump(-1,' ',true)<<std::endl;
            CoUninitialize(); com=false;
            if (!sound.error.empty() || sound.seconds()<1.9) throw std::runtime_error("Sound track short or failed: "+sound.error);
            if (asked_mic && (!sound.mic_on || !sound.mic.error.empty() || double(sound.mixer.lanes[1].heard)/sound.mixer.rate<1.5))
                throw std::runtime_error("Microphone short or failed: "+sound.mic.error);
            return 0;
        }
        if ((argc==2 || argc==3) && std::wstring_view(argv[1])==L"--self-test-showcase") {
            if (argc==3 && std::filesystem::exists(argv[2])) throw std::runtime_error("Self-test output exists");
            // Synthetic eye: four quadrants. A centred view keeps each quadrant in place, a turned view turns the
            // picture, an unchanged eye keeps its fingerprint, and a quick head turn stays inside the margin.
            ComPtr<ID3D11Device> test_device; ComPtr<ID3D11DeviceContext> test_context;
            check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&test_device,nullptr,&test_context));
            std::vector<BYTE> eye_pixels(640*640*4);
            for (int y=0;y<640;++y) for (int x=0;x<640;++x) {
                const auto i=(y*640+x)*4; eye_pixels[i]=x>=320?255:0; eye_pixels[i+1]=y>=320?255:0; eye_pixels[i+2]=64; eye_pixels[i+3]=255;
            }
            D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=640; d.MipLevels=d.ArraySize=1; d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
            d.SampleDesc.Count=1; d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data{eye_pixels.data(),640*4,0};
            ComPtr<ID3D11Texture2D> tex; ComPtr<ID3D11ShaderResourceView> view;
            check(test_device->CreateTexture2D(&d,&data,&tex)); check(test_device->CreateShaderResourceView(tex.Get(),nullptr,&view));
            ShowcaseCrop crop; crop.open(test_device.Get(),320,180);
            {
                // An eye like the lighthouse headset of test 18: 49 degrees outwards, 40 inwards, 38 up, 50 down.
                // OpenVR order: left, right, "top" (the downward tangent), "bottom" (the upward one).
                const float l=-1.157f,r=0.829f,down=-1.215f,up=0.788f;
                const auto f=showcase_frame(l,r,down,up,0.12);
                const double across=std::atan(f.half_width-f.centre_x)+std::atan(f.half_width+f.centre_x);
                if (across<1.25 || f.centre_x>=0 || std::abs(f.centre_y)>1e-9 || f.half_height>std::tan(std::atan(up)-0.12))
                    throw std::runtime_error("Showcase crop of an asymmetric eye is narrow, centred or outside the eye");
                // The axis lies at row up/(up-down) of the eye; a marker there must land where the crop puts the axis.
                std::vector<BYTE> marked(640*640*4);
                const int axis_row=int(640.0*up/(up-down)), axis_col=int(640.0*-l/(r-l));
                for (int y=0;y<640;++y) for (int x=0;x<640;++x) {
                    const auto i=(y*640+x)*4; marked[i]=std::abs(x-axis_col)<12&&std::abs(y-axis_row)<12?255:0; marked[i+3]=255;
                }
                D3D11_TEXTURE2D_DESC md{}; md.Width=md.Height=640; md.MipLevels=md.ArraySize=1; md.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
                md.SampleDesc.Count=1; md.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA mdata{marked.data(),640*4,0};
                ComPtr<ID3D11Texture2D> mtex; ComPtr<ID3D11ShaderResourceView> mview;
                check(test_device->CreateTexture2D(&md,&mdata,&mtex)); check(test_device->CreateShaderResourceView(mtex.Get(),nullptr,&mview));
                ShowcaseCrop::Constants on_axis{{l,r,down,up},{0.02f,0.01125f,0,0}}; qrows(Quat{},on_axis.rows);
                std::vector<BYTE> seen; crop.read(test_context.Get(),mview.Get(),false,on_axis,seen);
                if (seen[(90*320+160)*4]<200) throw std::runtime_error("Showcase view of an asymmetric eye is off the optical axis");
                // The full crop shows the axis at its own spot: right of centre (the window leans outwards), mid-height.
                ShowcaseCrop::Constants full{{l,r,down,up},{float(f.half_width),float(f.half_height),float(f.centre_x),float(f.centre_y)}};
                qrows(Quat{},full.rows); crop.read(test_context.Get(),mview.Get(),false,full,seen);
                const int col=int(std::lround(160+160*(-f.centre_x)/f.half_width));
                if (seen[(90*320+col)*4]<200 || seen[(90*320+160)*4]>8) throw std::runtime_error("Off-axis showcase crop lost the optical axis");
                // Looking 12 degrees lower puts the axis above the middle of the picture, at its own row.
                const auto low=showcase_frame(l,r,down,up,0.12,showcase_default_pitch);
                if (std::abs(low.centre_y-std::tan(showcase_default_pitch))>1e-9) throw std::runtime_error("Showcase pitch was not applied");
                ShowcaseCrop::Constants lowered{{l,r,down,up},{float(low.half_width),float(low.half_height),float(low.centre_x),float(low.centre_y)}};
                qrows(Quat{},lowered.rows); crop.read(test_context.Get(),mview.Get(),false,lowered,seen);
                const int row=int(std::lround(90*(1+low.centre_y/low.half_height)));
                if (row>=80 || seen[(row*320+col)*4]<200) throw std::runtime_error("Lowered showcase crop put the axis in the wrong place");
            }
            const auto frame=showcase_frame(-1,1,-1,1,0.12);
            ShowcaseCrop::Constants c{{-1,1,-1,1},{float(frame.half_width),float(frame.half_height),0,0}};
            qrows(Quat{},c.rows);
            std::vector<BYTE> out; crop.read(test_context.Get(),view.Get(),false,c,out);
            auto at=[&](int x,int y,int ch) { return int(out[(y*320+x)*4+ch]); };
            if (at(40,20,0)>8 || at(40,20,1)>8 || at(280,160,0)<247 || at(280,160,1)<247 || at(288,63,0)<247)
                throw std::runtime_error("Centred showcase view moved or flipped the eye image");
            const double half=std::acos(-1.0)/4;  // a quarter turn about the view axis
            qrows(Quat{std::cos(half),0,0,std::sin(half)},c.rows);
            crop.read(test_context.Get(),view.Get(),false,c,out);
            if (at(288,63,0)>8 || at(288,63,1)>8) throw std::runtime_error("Turned showcase view did not turn the picture");
            std::vector<BYTE> a,b2; crop.fingerprint(test_context.Get(),view.Get(),false,a); crop.fingerprint(test_context.Get(),view.Get(),false,b2);
            if (!same_print(a,b2)) throw std::runtime_error("Unchanged eye gave a different fingerprint");
            b2[100]^=0x80; if (same_print(a,b2)) throw std::runtime_error("Changed eye gave the same fingerprint");
            Steady steady; double worst=0;
            for (int i=0;i<400;++i) {
                const double now=i/30.0, yaw=1.2*now;  // a quick 1.2 rad/s turn at a 30 fps game
                worst=std::max(worst,qangle(Quat{},steady.update(Quat{std::cos(yaw/2),0,std::sin(yaw/2),0},now,0.35,0.10)));
            }
            if (worst>=0.10 || worst<0.06) throw std::runtime_error("Steady lag left the eased margin");
            {
                // The one-eye video: the whole eye in place, each quadrant where it was (no crop, flip or turn).
                ShowcaseCrop whole; whole.open(test_device.Get(),320,320);
                std::vector<BYTE> w; whole.whole(test_context.Get(),view.Get(),false,w);
                auto px=[&](int x,int y,int ch) { return int(w[(y*320+x)*4+ch]); };
                if (px(40,40,0)>8 || px(40,40,1)>8 || px(280,40,0)<247 || px(280,40,1)>8 || px(40,280,0)>8 || px(40,280,1)<247
                    || px(280,280,0)<247 || px(280,280,1)<247 || std::abs(px(160,20,2)-64)>3)
                    throw std::runtime_error("One-eye view moved, cropped or flipped the eye image");
            }
            if (argc==3) {
                // The largest showcase video, with frames at uneven game times (25 and 41 ms apart).
                check(CoInitializeEx(nullptr,COINIT_MULTITHREADED)); com=true; check(MFStartup(MF_VERSION)); media=true;
                ShowcaseCrop big; big.open(test_device.Get(),2560,1440); qrows(Quat{},c.rows);
                std::vector<BYTE> picture; big.read(test_context.Get(),view.Get(),false,c,picture);
                encoder.open(argv[2],2560,1440,60,40000000,true);
                LONGLONG at_time=0;
                for (int i=0;i<60;++i) { const LONGLONG step=(i%2?410000:250000); encoder.add(picture,at_time,step); at_time+=step; }
                encoder.close(); MFShutdown(); media=false; CoUninitialize(); com=false;
                std::cout<<"Showcase encoder: 60 frames, 2560x1440, 40 Mbps, uneven frame times ("<<at_time/10000<<" ms).\n";
            }
            std::cout<<"Synthetic showcase view: asymmetric eye, centred, turned, fingerprint, steady and one-eye checks passed; no game/VR access.\n"; return 0;
        }
        if (argc==3 && std::wstring_view(argv[1])==L"--self-test-large") {
            // The largest side-by-side video, 2048 px per eye (4096x2048, the H.264 width limit), with a sound
            // track: 3 s through the GPU resize and encoder with a 440 Hz tone written in 10 ms packets, timing
            // each frame. With an output device, a second short file runs the real loopback thread for 1.2 s.
            const std::filesystem::path video=argv[1+1];
            auto beside=video; beside.replace_filename(video.stem().wstring()+L"-loopback.mp4");
            if (std::filesystem::exists(video) || std::filesystem::exists(beside)) throw std::runtime_error("Self-test output exists");
            ComPtr<ID3D11Device> test_device; ComPtr<ID3D11DeviceContext> test_context;
            check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&test_device,nullptr,&test_context));
            constexpr UINT side=2048;
            std::vector<BYTE> eye_pixels(size_t(side)*side*4);
            for (UINT y=0;y<side;++y) for (UINT x=0;x<side;++x) {
                const auto i=(size_t(y)*side+x)*4; eye_pixels[i]=BYTE(x*255/side); eye_pixels[i+1]=BYTE(y*255/side); eye_pixels[i+2]=(x/64+y/64)%2?200:40; eye_pixels[i+3]=255;
            }
            D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=side; d.MipLevels=d.ArraySize=1; d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
            d.SampleDesc.Count=1; d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data{eye_pixels.data(),side*4,0};
            ComPtr<ID3D11Texture2D> tex; ComPtr<ID3D11ShaderResourceView> view;
            check(test_device->CreateTexture2D(&d,&data,&tex)); check(test_device->CreateShaderResourceView(tex.Get(),nullptr,&view));
            check(CoInitializeEx(nullptr,COINIT_MULTITHREADED)); com=true; check(MFStartup(MF_VERSION)); media=true;
            const bool device_sound=sound.open(true);
            const UINT rate=device_sound?sound.mixer.rate:48000u, fps=30;
            StereoResize resize; resize.open(test_device.Get(),side,side);
            encoder.open(video,side*2,side,fps,sbs_bitrate(side*2,side,fps),true,rate);
            std::vector<BYTE> pixels; std::vector<int16_t> tone(size_t(rate/100)*2);
            double worst=0,total=0; uint64_t tone_frames=0;
            for (UINT frame=0;frame<fps*3;++frame) {
                const auto begin=std::chrono::steady_clock::now();
                resize.read(test_context.Get(),view.Get(),view.Get(),false,false,pixels);
                encoder.add(pixels,LONGLONG(frame)*10000000/fps,10000000/fps);
                const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
                worst=std::max(worst,ms); total+=ms;
                while (tone_frames*fps<uint64_t(frame+1)*rate) {
                    for (size_t i=0;i<tone.size()/2;++i) tone[i*2]=tone[i*2+1]=int16_t(8000*std::sin(2*std::acos(-1.0)*440*double(tone_frames+i)/rate));
                    encoder.add_sound(tone.data(),tone.size()/2,LONGLONG(tone_frames*10000000/rate),LONGLONG(tone.size()/2*10000000/rate));
                    tone_frames+=tone.size()/2;
                }
            }
            encoder.close();
            if (pixels.size()!=size_t(side)*2*side*4) throw std::runtime_error("Large side-by-side picture has the wrong size");
            std::cout<<"Large side by side: "<<fps*3<<" frames, "<<side*2<<"x"<<side<<" at "<<sbs_bitrate(side*2,side,fps)/1000000
                <<" Mbit/s, "<<tone_frames<<" sound frames at "<<rate<<" Hz; resize+encode "<<total/(fps*3)<<" ms per frame (worst "<<worst<<" ms).\n";
            if (device_sound) {
                Encoder probe; probe.open(beside,320,240,fps,2000000,true,sound.mixer.rate);
                std::vector<BYTE> black(320*240*4,0);
                const auto start=std::chrono::steady_clock::now(); sound.begin(probe,qpc_100ns());
                for (UINT frame=0;frame<fps*6/5;++frame) {
                    probe.add(black,LONGLONG(frame)*10000000/fps,10000000/fps);
                    std::this_thread::sleep_until(start+std::chrono::milliseconds((frame+1)*1000/fps));
                }
                sound.finish(); probe.close();
                const double seconds=sound.seconds(), heard=double(sound.mixer.lanes[0].heard)/sound.mixer.rate;
                if (!sound.error.empty() || !sound.pc.error.empty() || seconds<1.1)
                    throw std::runtime_error("Loopback sound track short or failed: "+sound.error+sound.pc.error);
                std::cout<<"Loopback: "<<sound.pc.source_rate<<" Hz, "<<sound.pc.channels<<" channels, "<<(sound.pc.floats?"float":"PCM")<<"; "
                    <<seconds<<" s written, "<<std::max(0.0,seconds-heard)<<" s of it silence.\n";
            } else std::cout<<"Loopback: no output device to open ("<<sound.pc.error<<"); the recorder would save the picture alone.\n";
            MFShutdown(); media=false; CoUninitialize(); com=false; return 0;
        }
        if (argc==3 && (std::wstring_view(argv[1])==L"--self-test-writer" || std::wstring_view(argv[1])==L"--self-test-stall-child")) {
            // Children of --self-test-kill / --self-test-stall: 640x480 at 30 fps with a 440 Hz tone, in real time.
            // The writer runs until it is killed; the stall child writes 1.5 s, then blocks like a stuck capture call.
            const bool stall=std::wstring_view(argv[1])==L"--self-test-stall-child";
            out=argv[2];
            const auto video=stall?out/L"clean-sbs.mp4":out;
            if (stall) std::filesystem::create_directories(out);
            check(CoInitializeEx(nullptr,COINIT_MULTITHREADED)); com=true; check(MFStartup(MF_VERSION)); media=true;
            constexpr UINT fps=30, rate=48000;
            encoder.open(video,640,480,fps,4000000,true,rate);
            Watchdog watchdog; watchdog.stall_ms=3000;
            if (stall) watchdog.start([&] { end_stalled(out,Json{{"frames",watchdog.frames.load()},{"width",640},{"height",480}},encoder); });
            std::vector<BYTE> pixels(640*480*4); std::vector<int16_t> tone(rate/100*2); uint64_t tone_frames=0;
            const auto start=std::chrono::steady_clock::now();
            for (UINT frame=0;;++frame) {
                if (stall && frame==fps*3/2) Sleep(INFINITE);
                for (size_t i=0;i<pixels.size();i+=4) { pixels[i]=BYTE(frame*7); pixels[i+1]=BYTE(i/2560); pixels[i+2]=BYTE(i%2560/10); pixels[i+3]=255; }
                encoder.add(pixels,LONGLONG(frame)*10000000/fps,10000000/fps);
                while (tone_frames*fps<uint64_t(frame+1)*rate) {
                    for (size_t i=0;i<tone.size()/2;++i) tone[i*2]=tone[i*2+1]=int16_t(8000*std::sin(2*std::acos(-1.0)*440*double(tone_frames+i)/rate));
                    encoder.add_sound(tone.data(),tone.size()/2,LONGLONG(tone_frames*10000000/rate),LONGLONG(tone.size()/2*10000000/rate));
                    tone_frames+=tone.size()/2;
                }
                watchdog.pulse(); watchdog.frames=frame+1;
                std::this_thread::sleep_until(start+std::chrono::milliseconds((frame+1)*1000/fps));
            }
        }
        if (argc==3 && (std::wstring_view(argv[1])==L"--self-test-kill" || std::wstring_view(argv[1])==L"--self-test-stall")) {
            // Kill: a writer killed after 3.5 s must leave a file that plays its first seconds, picture and sound.
            // Stall: a capture loop that blocks must be ended by the watchdog, with a report and a playable file.
            const bool stall=std::wstring_view(argv[1])==L"--self-test-stall";
            const std::filesystem::path target=argv[2];
            if (std::filesystem::exists(target)) throw std::runtime_error("Self-test output exists");
            wchar_t self[MAX_PATH]{}; GetModuleFileNameW(nullptr,self,MAX_PATH);
            std::wstring line=L"\""+std::wstring(self)+L"\" "+(stall?L"--self-test-stall-child":L"--self-test-writer")+L" \""+target.wstring()+L"\"";
            STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION child{};
            if (!CreateProcessW(nullptr,line.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child))
                throw std::runtime_error("Cannot start the self-test child");
            DWORD code=STILL_ACTIVE;
            if (stall) {
                if (WaitForSingleObject(child.hProcess,20000)!=WAIT_OBJECT_0) { TerminateProcess(child.hProcess,1); throw std::runtime_error("Watchdog did not end a blocked recorder within 20 s"); }
                GetExitCodeProcess(child.hProcess,&code);
            } else {
                Sleep(3500); TerminateProcess(child.hProcess,1); WaitForSingleObject(child.hProcess,5000);
            }
            CloseHandle(child.hThread); CloseHandle(child.hProcess);
            check(CoInitializeEx(nullptr,COINIT_MULTITHREADED)); com=true; check(MFStartup(MF_VERSION)); media=true;
            const auto video=stall?target/L"clean-sbs.mp4":target;
            const auto [frames,sound_samples]=readable_samples(video);
            MFShutdown(); media=false; CoUninitialize(); com=false;
            if (stall) {
                if (code!=0) throw std::runtime_error("Stalled recorder exited with code "+std::to_string(code));
                std::ifstream report_file(target/L"recording.json"); Json report=Json::parse(report_file);
                if (report.value("end_reason","")!="recorder_stalled") throw std::runtime_error("Stall report missing");
                if (frames<40) throw std::runtime_error("Stalled recording kept "+std::to_string(frames)+" frames; expected about 45");
                std::cout<<"Stalled loop: the watchdog wrote the report, closed the file and exited; "<<frames<<" frames, "<<sound_samples<<" sound samples readable.\n";
            } else {
                if (frames<60 || sound_samples==0) throw std::runtime_error("Killed recording kept "+std::to_string(frames)+" frames and "+std::to_string(sound_samples)+" sound samples");
                std::cout<<"Killed writer: "<<frames<<" frames and "<<sound_samples<<" sound samples still readable after 3.5 s (fragmented MP4).\n";
            }
            return 0;
        }
        if ((argc==3 || argc==4) && std::wstring_view(argv[1])==L"--self-test") {
            const UINT test_fps=argc==4 ? std::stoul(argv[3]) : 30;
            if (test_fps!=30 && test_fps!=45 && test_fps!=60) throw std::runtime_error("Self-test FPS must be 30, 45 or 60");
            const std::filesystem::path video=argv[2];
            if (std::filesystem::exists(video)) throw std::runtime_error("Self-test output exists");
            check(CoInitializeEx(nullptr,COINIT_MULTITHREADED)); com=true; check(MFStartup(MF_VERSION)); media=true;
            encoder.open(video,640,480,test_fps);
            std::vector<BYTE> pixels(640*480*4);
            for (int y=0;y<480;++y) for (int x=0;x<640;++x) {
                const auto i=(y*640+x)*4;
                pixels[i]=x>=320?255:0; pixels[i+1]=y>=240?255:0; pixels[i+2]=(x<320&&y<240)|| (x>=320&&y>=240)?255:0; pixels[i+3]=255;
                if (x%320>100 && x%320<140 && y>80 && y<120) { pixels[i]=96; pixels[i+1]=128; pixels[i+2]=160; }
            }
            // Exercise the same two-eye GPU path, including UNORM/SRGB parity.
            ComPtr<ID3D11Device> test_device; ComPtr<ID3D11DeviceContext> test_context;
            check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&test_device,nullptr,&test_context));
            ComPtr<ID3D11Texture2D> tex[2]; ComPtr<ID3D11ShaderResourceView> views[2];
            std::vector<BYTE> eye_pixels(320*480*4);
            for (int eye=0;eye<2;++eye) {
                for(int y=0;y<480;++y) memcpy(eye_pixels.data()+y*320*4,pixels.data()+(y*640+eye*320)*4,320*4);
                D3D11_TEXTURE2D_DESC d{}; d.Width=320; d.Height=480; d.MipLevels=d.ArraySize=1;
                d.Format=eye ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : DXGI_FORMAT_B8G8R8A8_UNORM;
                d.SampleDesc.Count=1; d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA data{eye_pixels.data(),320*4,0};
                check(test_device->CreateTexture2D(&d,&data,&tex[eye])); check(test_device->CreateShaderResourceView(tex[eye].Get(),nullptr,&views[eye]));
            }
            StereoResize resize; resize.open(test_device.Get(),320,480);
            std::vector<BYTE> resized; resize.read(test_context.Get(),views[0].Get(),views[1].Get(),false,true,resized);
            for (int y : {30,450}) for (int x : {30,600}) for (int c=0;c<4;++c)
                if (std::abs(int(resized[(y*640+x)*4+c])-int(pixels[(y*640+x)*4+c]))>1)
                    throw std::runtime_error("GPU eye resize changed color, order or orientation");
            for (int x : {120,440}) for (int c=0;c<3;++c)
                if (std::abs(int(resized[(100*640+x)*4+c])-int(pixels[(100*640+x)*4+c]))>1)
                    throw std::runtime_error("GPU resize changed UNORM/SRGB midtones");
            StereoResize smaller; smaller.open(test_device.Get(),160,240);
            std::vector<BYTE> reduced_pixels; smaller.read(test_context.Get(),views[0].Get(),views[1].Get(),false,true,reduced_pixels);
            for (int y : {15,50,225}) for (int x : {15,60,220,300}) for (int c=0;c<4;++c)
                if (std::abs(int(reduced_pixels[(y*320+x)*4+c])-int(pixels[(y*2*640+x*2)*4+c]))>1)
                    throw std::runtime_error("GPU downsize changed eye color or orientation");
            pixels=std::move(resized);
            for(UINT frame=0;frame<test_fps*2;++frame) encoder.add(pixels,frame*10000000ll/test_fps,10000000/test_fps);
            encoder.close(); MFShutdown(); media=false; CoUninitialize(); com=false;
            std::cout<<"Synthetic GPU SBS resize + RGB/orientation encoder test: "<<test_fps*2<<" frames, 640x480, "<<test_fps<<"fps; no game/VR access.\n"; return 0;
        }
        // Options first: --showcase (the left eye as a steadied 16:9 camera, showcase.mp4, instead of both eyes
        // side by side), --pitch=<degrees> with it (where that view looks, below (negative) or above the head),
        // --eye=left|right (that eye's whole view, left-eye.mp4 / right-eye.mp4: the side-by-side field of view in
        // one picture, at the game's own frame rate, not steadied), --sound (what the PC plays, as an AAC track),
        // --microphone=default|<endpoint ID> (a microphone mixed into that track and saved alone as microphone.wav)
        // and --mic-volume=<0..300> (its level in percent, 100 as it comes).
        bool showcase=false, with_sound=false, pitched=false; double pitch=showcase_default_pitch; int one_eye=-1;
        std::wstring microphone; int mic_volume=100;
        while (argc>1 && std::wstring_view(argv[1]).starts_with(L"--")) {
            const std::wstring_view option=argv[1];
            if (option==L"--showcase") showcase=true;
            else if (option==L"--sound") with_sound=true;
            else if (option.starts_with(L"--microphone=")) {
                microphone=std::wstring(option.substr(13));
                if (microphone.empty() || microphone.size()>200) throw std::runtime_error("Choose a microphone");
            } else if (option.starts_with(L"--mic-volume=")) {
                mic_volume=std::stoi(std::wstring(option.substr(13)));
                if (mic_volume<0 || mic_volume>300) throw std::runtime_error("Microphone volume must be 0..300 percent");
            }
            else if (option==L"--eye=left") one_eye=0;
            else if (option==L"--eye=right") one_eye=1;
            else if (option.starts_with(L"--pitch=")) {
                const double degrees=std::stod(std::wstring(option.substr(8)));
                if (!(degrees>=-40 && degrees<=20)) throw std::runtime_error("Showcase pitch must be -40..20 degrees");
                pitch=degrees*std::acos(-1.0)/180; pitched=true;
            } else throw std::runtime_error("Unknown option");
            ++argv; --argc;
        }
        if (pitched && !showcase) throw std::runtime_error("--pitch needs --showcase");
        if (showcase && one_eye>=0) throw std::runtime_error("Choose --showcase or --eye, not both");
        if (argc!=7 && argc!=8) throw std::runtime_error("Expected [--showcase [--pitch=DEGREES] | --eye=left|right] [--sound] [--microphone=default|ID [--mic-volume=PERCENT]] openvr_api.dll, game PID, new output folder, seconds (1..300), FPS (10..60), eye width (640..2048), optional stop file");
        const UINT pid=std::stoul(argv[2]), seconds=std::stoul(argv[4]), fps=std::stoul(argv[5]), ew=std::stoul(argv[6]); out=argv[3];
        if (!pid||seconds<1||seconds>300||fps<10||fps>60||ew<640||ew>2048||ew%2) throw std::runtime_error("Invalid recording bounds");
        // Ends the recording cleanly when the game exits (a crash included) instead of waiting out the time.
        const HANDLE game=OpenProcess(SYNCHRONIZE,FALSE,pid);
        if (std::filesystem::exists(out)) throw std::runtime_error("Output exists; refusing to overwrite");
        if (!steamvr_running()) throw std::runtime_error("SteamVR is not running; recorder will not launch it");
        library=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!library) throw std::runtime_error("Cannot load OpenVR library");
        const auto init=reinterpret_cast<uint32_t(__cdecl*)(vr::EVRInitError*,vr::EVRApplicationType,const char*)>(GetProcAddress(library,"VR_InitInternal2"));
        const auto get=reinterpret_cast<void*(__cdecl*)(const char*,vr::EVRInitError*)>(GetProcAddress(library,"VR_GetGenericInterface"));
        shutdown=reinterpret_cast<void(__cdecl*)()>(GetProcAddress(library,"VR_ShutdownInternal"));
        if (!init||!get||!shutdown) throw std::runtime_error("OpenVR exports missing");
        vr::EVRInitError error{}; init(&error,vr::VRApplication_Background,nullptr);
        if (error) throw std::runtime_error("Background OpenVR init failed"); initialized=true;
        auto system=static_cast<vr::IVRSystem*>(get(vr::IVRSystem_Version,&error));
        if (error||!system) throw std::runtime_error("VR system unavailable");
        auto compositor=static_cast<vr::IVRCompositor*>(get(vr::IVRCompositor_Version,&error));
        if (error||!compositor||compositor->GetCurrentSceneFocusProcess()!=pid) throw std::runtime_error("Requested game is not the compositor scene");
        int adapter_index{}; system->GetDXGIOutputInfo(&adapter_index);
        ComPtr<IDXGIFactory> factory; ComPtr<IDXGIAdapter> adapter;
        check(CreateDXGIFactory(IID_PPV_ARGS(&factory))); check(factory->EnumAdapters(adapter_index,&adapter));
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
        check(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
        check(CoInitializeEx(nullptr,COINIT_MULTITHREADED)); com=true; check(MFStartup(MF_VERSION)); media=true;
        // No output device or microphone (or an odd one): the picture is recorded with what opened, alone if nothing
        // did, and the report says why.
        const bool sounding=(with_sound || !microphone.empty()) && sound.open(with_sound,microphone,mic_volume);
        const auto sound_report=[&] { return report_sound(with_sound,!microphone.empty(),sounding,sound); };
        if (showcase || one_eye>=0) {
            // Showcase: the left eye cropped to 16:9 and steadied. One eye: the chosen eye whole, as the headset
            // shows it (the side-by-side field of view), with the pose of each frame logged for steadying later.
            const bool whole=one_eye>=0; const int eye_index=whole?one_eye:0;
            const auto vr_eye=static_cast<vr::EVREye>(eye_index);
            const std::string style=whole?(eye_index?"right_eye":"left_eye"):"showcase";
            Eye eye; eye.open(compositor,device.Get(),eye_index);
            float tan_l{},tan_r{},tan_t{},tan_b{}; system->GetProjectionRaw(vr_eye,&tan_l,&tan_r,&tan_t,&tan_b);
            const Quat eye_to_head=quat_from(system->GetEyeToHeadTransform(vr_eye));
            constexpr double max_angle=0.10, tau=0.35;
            const auto frame=whole?ShowcaseFrame{}:showcase_frame(tan_l,tan_r,tan_t,tan_b,max_angle+0.02,pitch);
            // Showcase: picture size 720 / 1024 / 1280 (or 1536) / 2048 px per eye maps to 720p / 1080p / 1440p / 2160p.
            // One eye: that width at the eye's own shape (square on most headsets), at the side-by-side bit density.
            const UINT ow=whole?ew:ew<=720?1280u:ew<=1024?1920u:ew<=1536?2560u:3840u;
            const UINT oh=whole?std::max(2u,static_cast<UINT>(std::lround(double(ew)*eye.height/eye.width/2))*2):ow*9/16;
            const UINT bitrate=whole?sbs_bitrate(ow,oh,fps):oh>=2160?60000000u:oh>=1440?40000000u:oh>=1080?24000000u:12000000u;
            const std::wstring video_name=whole?(eye_index?L"right-eye.mp4":L"left-eye.mp4"):L"showcase.mp4";
            if (!std::filesystem::create_directories(out)) throw std::runtime_error("Output appeared during startup; refusing to overwrite");
            output_created=true;
            encoder.open(out/video_name,ow,oh,fps,bitrate,true,sounding?sound.mixer.rate:0);
            std::ofstream frames(out/L"frames.jsonl",std::ios::binary);
            if (!frames) throw std::runtime_error("Cannot write frame sidecar");
            ShowcaseCrop crop; crop.open(device.Get(),ow,oh);
            timeBeginPeriod(1);  // 2 ms polls: a new game frame is noticed within a few ms of arriving
            Steady steady; std::vector<BYTE> pixels,pending,print,last_print;
            Sleep(250); const auto start=std::chrono::steady_clock::now();
            if (sounding) sound.begin(encoder,qpc_100ns(),out/L"microphone.wav");
            uint64_t count=0,unsteadied=0,too_soon=0; uint32_t last_index=0; bool have_index=false; const auto start_ms=utc();
            double pending_t=-1, waiting_since=-1;
            std::string end_reason="time_limit"; Json pauses=Json::array(); double paused_at=-1, t=0;
            Watchdog watchdog;
            watchdog.start([&] { end_stalled(out,Json{{"style",style},{"pid",pid},{"started_unix_ms",start_ms},
                {"frames",watchdog.frames.load()},{"width",ow},{"height",oh},{"bitrate",bitrate},{"fps_target",fps}},encoder); });
            while (true) {
                watchdog.pulse(); watchdog.frames=count;
                t=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
                if (t>=seconds) break;
                if (steamvr_quitting(system)) { end_reason="steamvr_quit"; break; }
                if (std::filesystem::exists(out/L"STOP") || (argc==8&&std::filesystem::exists(argv[7]))) { end_reason="stopped"; break; }
                if (game && WaitForSingleObject(game,0)==WAIT_OBJECT_0) { end_reason="game_exited"; break; }
                if (compositor->GetCurrentSceneFocusProcess()!=pid) { if (paused_at<0) paused_at=t; Sleep(100); continue; }
                // A new game frame shows as a new frame index; its timing carries the pose it was rendered with.
                vr::Compositor_FrameTiming timing{sizeof(timing)}; const bool timed=compositor->GetFrameTiming(&timing);
                if (timed && have_index && timing.m_nFrameIndex==last_index) { Sleep(2); continue; }
                // The mirror can trail the timing by a moment: wait (briefly) for the picture itself to change.
                crop.fingerprint(context.Get(),eye.view,eye.srgb,print);
                if (same_print(print,last_print)) {
                    if (waiting_since<0) waiting_since=t;
                    if (!timed || t-waiting_since<0.012) { Sleep(2); continue; }
                }
                waiting_since=-1; last_print=print;
                if (timed) { last_index=timing.m_nFrameIndex; have_index=true; }
                if (pending_t>=0 && t-pending_t<0.5/fps) { ++too_soon; continue; }  // the game runs faster than the video
                Quat view{},eye_pose{}; const bool posed=timed && timing.m_HmdPose.bPoseIsValid;
                if (posed) {
                    eye_pose=qmul(quat_from(timing.m_HmdPose.mDeviceToAbsoluteTracking),eye_to_head);
                    if (!whole) view=steady.update(eye_pose,t,tau,max_angle);
                } else ++unsteadied;
                const auto capture_begin=utc();
                if (whole) crop.whole(context.Get(),eye.view,eye.srgb,pixels);
                else {
                    ShowcaseCrop::Constants c{{tan_l,tan_r,tan_t,tan_b},{float(frame.half_width),float(frame.half_height),float(frame.centre_x),float(frame.centre_y)}};
                    qrows(view,c.rows);
                    crop.read(context.Get(),eye.view,eye.srgb,c,pixels);
                }
                if (compositor->GetCurrentSceneFocusProcess()!=pid) { if (paused_at<0) paused_at=t; continue; }  // frame discarded
                if (paused_at>=0) { pauses.push_back({{"from_s",paused_at},{"to_s",t}}); paused_at=-1; }
                bool nonblack=false; for (size_t i=0;i<pixels.size();i+=128) if (pixels[i]||pixels[i+1]||pixels[i+2]) { nonblack=true; break; }
                if (!count&&!nonblack) throw std::runtime_error("Blank initial mirror; no useful video");
                // Each frame lasts until the next one arrives, so the video keeps the game's real timing.
                if (pending_t>=0) encoder.add(pending,static_cast<LONGLONG>(pending_t*10000000),static_cast<LONGLONG>((t-pending_t)*10000000));
                std::swap(pending,pixels); pending_t=t;
                Json row{{"frame",count},{"video_seconds",t},{"capture_begin_ms",capture_begin},{"capture_end_ms",utc()},
                    {"clock_ms",GetTickCount64()},{"scene_frame",timed?Json(timing.m_nFrameIndex):Json(nullptr)}};
                if (!whole) row["steady"]={view.w,view.x,view.y,view.z};
                else if (posed) {
                    // The pose this frame was rendered with (eye orientation, head position in metres).
                    const auto& m=timing.m_HmdPose.mDeviceToAbsoluteTracking.m;
                    row["eye_pose"]={eye_pose.w,eye_pose.x,eye_pose.y,eye_pose.z}; row["head_position"]={m[0][3],m[1][3],m[2][3]};
                }
                frames<<row.dump()<<'\n';
                ++count; if (count%fps==0) frames.flush();
            }
            watchdog.finish();
            if (pending_t>=0) encoder.add(pending,static_cast<LONGLONG>(pending_t*10000000),10000000/fps);
            sound.finish(); encoder.close(); timeEndPeriod(1);
            if (paused_at>=0) pauses.push_back({{"from_s",paused_at},{"to_s",t}});
            const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            result={{"status","recorded"},{"style",style},{"end_reason",end_reason},{"scene_pauses",pauses},
                {"pid",pid},{"started_unix_ms",start_ms},{"frames",count},
                {"frames_faster_than_video",too_soon},{"width",ow},{"height",oh},{"bitrate",bitrate},
                {"fps_target",fps},{"captured_fps",count/elapsed},{"wall_seconds",elapsed},
                {"timing","one sample per new game frame at its arrival time (variable frame rate, no repeats)"},
                {"source_eye_size",{eye.width,eye.height}},{"eye_tangents",{tan_l,tan_r,tan_t,tan_b}},
                {"desktop_captured",false},{"input_sent",false}};
            if (whole) result.update({{"eye",eye_index?"right":"left"},{"video",std::filesystem::path(video_name).string()},
                {"frames_without_pose",unsteadied},
                {"method",std::string("OpenVR ")+(eye_index?"right":"left")+"-eye mirror, whole, resized; eye pose per frame in frames.jsonl"}});
            else result.update({{"frames_unsteadied",unsteadied},
                {"crop_half_tangents",{frame.half_width,frame.half_height}},{"crop_centre_tangents",{frame.centre_x,frame.centre_y}},{"pitch_degrees",pitch*180/std::acos(-1.0)},{"steady",{{"seconds",tau},{"margin_rad",max_angle}}},
                {"method","OpenVR left-eye mirror; 16:9 view across the eye's width, lens shifted to the pitch, steadied with the pose each frame was rendered with"}});
            result.update(sound_report());
            std::ofstream(out/L"recording.json")<<result.dump(2)<<'\n';
        } else {
            Eye eyes[2]; for (int i=0;i<2;++i) eyes[i].open(compositor,device.Get(),i);
            if (eyes[0].width!=eyes[1].width || eyes[0].height!=eyes[1].height) throw std::runtime_error("Eye sizes differ");
            const UINT eh=std::max(2u,static_cast<UINT>(std::lround(double(ew)*eyes[0].height/eyes[0].width/2))*2);
            if (!std::filesystem::create_directories(out)) throw std::runtime_error("Output appeared during startup; refusing to overwrite");
            output_created=true;
            const UINT bitrate=sbs_bitrate(ew*2,eh,fps);
            encoder.open(out/L"clean-sbs.mp4",ew*2,eh,fps,bitrate,true,sounding?sound.mixer.rate:0);
            std::ofstream frames(out/L"frames.jsonl",std::ios::binary);
            if (!frames) throw std::runtime_error("Cannot write frame sidecar");
            std::vector<BYTE> pixels(static_cast<size_t>(ew)*2*eh*4);
            StereoResize resize; resize.open(device.Get(),ew,eh);
            Sleep(250); const auto start=std::chrono::steady_clock::now();
            if (sounding) sound.begin(encoder,qpc_100ns(),out/L"microphone.wav");
            uint64_t count=0; const auto start_ms=utc();
            // A hitch, loading stall or hang can hand SteamVR's scene to another process. Nothing is read
            // then (another application is never captured); the video holds its last frame and resumes
            // when WuWa has the scene again, so one recording spans scene changes.
            std::string end_reason="time_limit"; Json pauses=Json::array(); double paused_at=-1, t=0;
            Watchdog watchdog;
            watchdog.start([&] { end_stalled(out,Json{{"pid",pid},{"started_unix_ms",start_ms},{"frames",watchdog.frames.load()},
                {"width",ew*2},{"height",eh},{"bitrate",bitrate},{"fps_target",fps},
                {"source_eye_size",{eyes[0].width,eyes[0].height}},{"method","OpenVR per-eye mirror; GPU SBS resize and one readback"}},encoder); });
            while (true) {
                watchdog.pulse(); watchdog.frames=count;
                const auto current=std::chrono::steady_clock::now();
                t=std::chrono::duration<double>(current-start).count();
                if (t>=seconds) break;
                if (steamvr_quitting(system)) { end_reason="steamvr_quit"; break; }
                if (std::filesystem::exists(out/L"STOP") || (argc==8&&std::filesystem::exists(argv[7]))) { end_reason="stopped"; break; }
                if (game && WaitForSingleObject(game,0)==WAIT_OBJECT_0) { end_reason="game_exited"; break; }
                if (compositor->GetCurrentSceneFocusProcess()!=pid) {
                    if (paused_at<0) paused_at=t;
                    Sleep(100); continue;
                }
                const auto capture_begin=utc();
                vr::Compositor_FrameTiming timing{sizeof(timing)}; const bool timed=compositor->GetFrameTiming(&timing);
                resize.read(context.Get(),eyes[0].view,eyes[1].view,eyes[0].srgb,eyes[1].srgb,pixels);
                if (compositor->GetCurrentSceneFocusProcess()!=pid) { if (paused_at<0) paused_at=t; continue; }  // frame discarded
                if (paused_at>=0) { pauses.push_back({{"from_s",paused_at},{"to_s",t}}); paused_at=-1; }
                bool nonblack=false; for (size_t i=0;i<pixels.size();i+=128) if (pixels[i]||pixels[i+1]||pixels[i+2]) { nonblack=true; break; }
                if (!count&&!nonblack) throw std::runtime_error("Blank initial mirror; no useful video");
                const auto capture_end=utc();
                encoder.add(pixels,static_cast<LONGLONG>(t*10000000),10000000/fps);
                frames<<Json{{"frame",count},{"video_seconds",t},{"capture_begin_ms",capture_begin},{"capture_end_ms",capture_end},
                    {"clock_ms",GetTickCount64()},{"scene_frame",timed?Json(timing.m_nFrameIndex):Json(nullptr)}}.dump()<<'\n';
                ++count; if (count%fps==0) frames.flush();
                const auto target=start+std::chrono::microseconds(static_cast<int64_t>((std::floor(t*fps)+1)*1000000/fps));
                const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(target-std::chrono::steady_clock::now()).count();
                if (remaining>0) Sleep(static_cast<DWORD>(remaining));
            }
            watchdog.finish();
            sound.finish(); encoder.close();
            if (paused_at>=0) pauses.push_back({{"from_s",paused_at},{"to_s",t}});
            const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            result={{"status","recorded"},{"end_reason",end_reason},{"scene_pauses",pauses},
                {"pid",pid},{"started_unix_ms",start_ms},{"frames",count},{"width",ew*2},{"height",eh},{"bitrate",bitrate},
                {"fps_target",fps},{"captured_fps",count/elapsed},{"wall_seconds",elapsed},
                {"timing","wall-clock presentation timestamps; encoder may repeat frames to meet target FPS"},
                {"source_eye_size",{eyes[0].width,eyes[0].height}},{"method","OpenVR per-eye mirror; GPU SBS resize and one readback"},
                {"stereo_gpu_synchronized",false},{"desktop_captured",false},{"input_sent",false}};
            result.update(sound_report());
            std::ofstream(out/L"recording.json")<<result.dump(2)<<'\n';
        }
        shutdown(); initialized=false; FreeLibrary(library); library=nullptr; MFShutdown(); media=false; CoUninitialize(); com=false;
        std::cout<<result.dump()<<'\n'; return 0;
    } catch (const std::exception& e) {
        sound.stop(); try { encoder.close(); } catch (...) {}
        if (output_created) std::ofstream(out/L"recording-error.txt")<<e.what()<<'\n';
        if (initialized&&shutdown) shutdown(); if (library) FreeLibrary(library); if (media) MFShutdown(); if (com) CoUninitialize();
        std::cerr<<e.what()<<'\n'; return 1;
    }
}
