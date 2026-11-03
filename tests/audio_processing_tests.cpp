#include "audio/block_resampler.h"
#include "audio_processing_reference.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

// Setup and reporting may allocate; only the measured render region must not.
static std::atomic<size_t> allocationCount{0};
void* operator new(std::size_t bytes) {
    allocationCount.fetch_add(1,std::memory_order_relaxed);
    if (void* p=std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
#if defined(__cpp_sized_deallocation)
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }
#endif

using namespace NitLink::AudioConvert;
static size_t checks=0;
static void Check(bool value,const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
static unsigned FrameBytes(const Format& format) { return format.channels*format.bits/8; }
static void Store(uint8_t* destination,const Format& format,double value) {
    // Float fixtures include legal over-range samples that must survive until
    // gain and the final output clip, so they cannot use the clipping writer.
    if (format.floating) {
        if (format.bits==32) { const float f=float(value); std::memcpy(destination,&f,4); }
        else std::memcpy(destination,&value,8);
    } else Write(destination,format,value);
}
static std::vector<uint8_t> Ring(const Format& format,uint32_t frames,size_t head,double level=0) {
    std::vector<uint8_t> bytes(size_t(frames)*FrameBytes(format));
    for (uint32_t f=0;f<frames;++f) {
        const size_t at=(head+size_t(f)*FrameBytes(format))%bytes.size();
        for (unsigned c=0;c<format.channels;++c) {
            const double value=level!=0 ? level : .61*std::sin(double(f)*.067+c*.11)+.17*std::cos(double(f)*.171+c*.31);
            Store(bytes.data()+at+c*format.bits/8,format,value);
        }
    }
    return bytes;
}
static void CompareReference(const Format& input,const Format& output,double nominal,double correction,double fraction) {
    constexpr uint32_t capacity=4096;
    BlockResampler processor;
    Check(processor.Initialize(input,output,nominal,capacity,48000),"processor setup");
    processor.Reset(1);
    SincKernel kernel; Check(kernel.Initialize(nominal),"reference kernel setup");
    Check(processor.History()==kernel.History() && processor.Taps()==kernel.Taps(),"unchanged FIR geometry");
    size_t head=size_t(capacity-7)*FrameBytes(input);
    auto ring=Ring(input,capacity,head);
    uint32_t fill=capacity;
    double phase=processor.History()+fraction,referencePhase=phase;
    const double ratio=nominal*correction;
    for (uint32_t frames : {3u,17u,53u,144u,1u,47u}) {
        std::vector<uint8_t> actual(size_t(frames)*FrameBytes(output),0xcd),expected(actual.size(),0xcd);
        const size_t allocations=allocationCount.load(std::memory_order_relaxed);
        const auto result=processor.RenderRing(actual.data(),frames,ring.data(),ring.size(),head,fill,ratio,phase,1);
        const size_t after=allocationCount.load(std::memory_order_relaxed);
        Check(allocations==after,"render must not allocate");
        const auto reference=AudioProcessingReference::Render(expected.data(),frames,ring.data(),ring.size(),head,fill,input,output,kernel,ratio,referencePhase);
        Check(result.written==frames && result.written==reference.written,"reference frame count");
        Check(result.consumed==reference.consumed && phase==referencePhase,"reference FIFO consumption and phase");
        const double tolerance=output.floating ? (output.bits==32 ? 1.2e-7 : 2e-14) : std::ldexp(1.0,1-int(output.validBits));
        for (size_t sample=0;sample<size_t(frames)*output.channels;++sample)
            Check(std::abs(Read(actual.data()+sample*output.bits/8,output)-Read(expected.data()+sample*output.bits/8,output))<=tolerance,"reference sample comparison");
        head=(head+size_t(result.consumed)*FrameBytes(input))%ring.size(); fill-=result.consumed;
    }
}
static void TestFormatsAndRates() {
    const std::array<Format,9> formats{{
        {2,8,8,3,false},{2,16,16,3,false},{2,24,24,3,false},{2,32,32,3,false},
        {2,24,20,3,false},{2,32,24,3,false},{2,32,20,3,false},{2,32,32,3,true},{2,64,64,3,true}
    }};
    for (const auto& input:formats) for (const auto& output:formats)
        CompareReference(input,output,48000.0/44100,1.00031,.3713);
    const Format stereo{2,32,32,3,true},pcm{2,16,16,3,false},surround{6,24,24,0x3f,false};
    for (double ratio : {.125,44100.0/48000,1.0,48000.0/44100,2.0,4.0,8.0})
        for (double correction : {.998,1.0,1.002}) {
            CompareReference(stereo,stereo,ratio,correction,.9997);
            CompareReference(pcm,surround,ratio,correction,0);
        }
    const Format mono{1,64,64,4,true},six{6,32,32,0x3f,true},thirtyTwo{32,16,16,UINT32_MAX,false};
    CompareReference(stereo,mono,1,1,.5);
    CompareReference(mono,stereo,1,1,.125);
    CompareReference(mono,six,1,1,.25);
    CompareReference(six,six,1,1,.875);
    CompareReference(thirtyTwo,thirtyTwo,1,1,.3125);
}
static void TestGainAndBypass() {
    const Format mono{1,64,64,4,true},pcm{1,16,16,4,false};
    BlockResampler processor;
    Check(processor.Initialize(mono,mono,1,1024,48000,true),"explicit bypass setup");
    Check(processor.Bypass() && processor.History()==0 && processor.Lookahead()==0 && processor.Taps()==0,"bypass has no FIR queue geometry");
    auto ring=Ring(mono,1024,0,1.2);
    std::array<double,96> output{}; double phase=0;
    processor.Reset(.5);
    auto result=processor.RenderRing(reinterpret_cast<uint8_t*>(output.data()),96,ring.data(),ring.size(),0,1024,1,phase,.5);
    Check(result.written==96 && result.consumed==96 && phase==0 && !result.silent,"bypass consumption");
    for (double sample:output) Check(std::abs(sample-.6)<1e-14,"float headroom preserved before volume");
    Check(processor.Initialize(mono,pcm,1,1024,48000,true),"PCM bypass setup");
    processor.Reset(.0001);
    std::array<uint8_t,192> pcmOutput{};
    result=processor.RenderRing(pcmOutput.data(),96,ring.data(),ring.size(),0,1024,1,phase,.0001);
    Check(Read(pcmOutput.data(),pcm)==4.0/32768,"low-volume signal quantized once after gain");
    Check(processor.Initialize(mono,mono,1,1024,48000),"unity ratio still supports active SRC");
    Check(!processor.Bypass() && processor.Taps()==64,"unity ratio never enables bypass implicitly");
    processor.Reset(.5); phase=processor.History()+.37;
    result=processor.RenderRing(reinterpret_cast<uint8_t*>(output.data()),96,ring.data(),ring.size(),0,1024,1,phase,.5);
    for (double sample:output) Check(std::abs(sample-.6)<1e-6,"FIR headroom preserved before volume");
    processor.Reset(1); phase=processor.History();
    result=processor.RenderRing(reinterpret_cast<uint8_t*>(output.data()),96,ring.data(),ring.size(),0,1024,1,phase,1);
    for (double sample:output) Check(sample==1,"final saturation bounded");

    // A full-scale discontinuity produces sinc overshoot. Quiet playback must
    // retain that shape rather than clipping it before the gain is applied.
    for (uint32_t f=0;f<1024;++f) Store(ring.data()+size_t(f)*8,mono,f<64 ? -.95 : .95);
    processor.Reset(.25); phase=processor.History()+.37;
    SincKernel kernel; Check(kernel.Initialize(1),"overshoot reference setup");
    double referencePhase=phase;
    std::array<double,96> expected{};
    AudioProcessingReference::Render(reinterpret_cast<uint8_t*>(expected.data()),96,ring.data(),ring.size(),0,1024,mono,mono,kernel,1,referencePhase,.25);
    result=processor.RenderRing(reinterpret_cast<uint8_t*>(output.data()),96,ring.data(),ring.size(),0,1024,1,phase,.25);
    double peak=0;
    for (unsigned i=0;i<96;++i) { peak=std::max(peak,std::abs(output[i])); Check(std::abs(output[i]-expected[i])<1e-14,"overshoot reference preserved"); }
    Check(peak>.25,"gain precedes sinc overshoot clipping");
}
static void TestRampsAndGaps() {
    const Format mono{1,64,64,4,true};
    BlockResampler processor; Check(processor.Initialize(mono,mono,1,1024,48000,true),"ramp setup");
    auto ring=Ring(mono,1024,0,.8);
    std::array<double,96> output{}; double phase=0;
    auto render=[&](uint32_t frames,double gain,bool muted=false,uint32_t fill=1024) {
        const size_t allocations=allocationCount.load(std::memory_order_relaxed);
        auto r=processor.RenderRing(reinterpret_cast<uint8_t*>(output.data()),frames,ring.data(),ring.size(),0,fill,1,phase,gain,muted);
        const size_t after=allocationCount.load(std::memory_order_relaxed);
        Check(after==allocations,"ramp render must not allocate"); return r;
    };
    Check(processor.RampFrames()==48,"one millisecond ramp");
    auto result=render(96,1);
    Check(result.written==96 && !result.silent,"startup fade still consumes all input");
    for (unsigned i=0;i<48;++i) Check(std::abs(output[i]-.8*(i+1)/48)<1e-14,"sample-level fade-in");
    for (unsigned i=48;i<96;++i) Check(std::abs(output[i]-.8)<1e-14,"fade-in target reached");
    result=render(96,1,true);
    Check(result.written==96 && result.consumed==96 && !result.silent,"mute transition remains audible until ramp ends");
    for (unsigned i=0;i<48;++i) Check(std::abs(output[i]-.8*(47-i)/48)<1e-14,"sample-level mute ramp");
    for (unsigned i=48;i<96;++i) Check(output[i]==0,"mute target silence");
    result=render(96,1,true);
    Check(result.silent && result.written==96 && result.consumed==96,"steady mute keeps consumption and statistics");
    render(96,1);
    processor.Reset(1);
    result=render(96,1,false,24);
    Check(result.written==24 && result.consumed==24 && !result.silent,"underflow separates real input from generated tail");
    for (unsigned i=0;i<24;++i) Check(std::abs(output[i]-.8)<1e-14,"available input survives partial underflow");
    for (unsigned i=0;i<48;++i) Check(std::abs(output[24+i]-.8*(47-i)/48)<1e-14,"short underflow tail stays within requested block");
    for (unsigned i=72;i<96;++i) Check(output[i]==0,"underflow suffix filled with silence");
    result=render(96,1,false,0);
    Check(result.written==0 && result.consumed==0 && result.silent,"empty ring has no real written frames");
    result=render(96,1);
    Check(result.written==96 && result.consumed==96 && !result.silent,"resume adds no extra queued frames");
    for (unsigned i=0;i<48;++i) Check(std::abs(output[i]-.8*(i+1)/48)<1e-14,"recovery fade-in");

    // Tail state spans arbitrarily small render requests and FillSilence calls.
    processor.Reset(1); render(1,1);
    const size_t allocations=allocationCount.load(std::memory_order_relaxed);
    auto silence=processor.FillSilence(reinterpret_cast<uint8_t*>(output.data()),7,1);
    Check(allocationCount.load(std::memory_order_relaxed)==allocations,"silence path must not allocate");
    Check(!silence.silent && silence.written==0 && silence.consumed==0,"tail flags and accounting");
    for (unsigned i=0;i<7;++i) Check(std::abs(output[i]-.8*(47-i)/48)<1e-14,"first tail segment");
    silence=processor.FillSilence(reinterpret_cast<uint8_t*>(output.data()),48,1);
    for (unsigned i=0;i<41;++i) Check(std::abs(output[i]-.8*(40-i)/48)<1e-14,"tail continues across blocks");
    for (unsigned i=41;i<48;++i) Check(output[i]==0,"tail terminates exactly");
    silence=processor.FillSilence(reinterpret_cast<uint8_t*>(output.data()),48,1);
    Check(silence.silent,"steady silence flagged silent");

    processor.Reset(1); render(1,1);
    processor.FillSilence(reinterpret_cast<uint8_t*>(output.data()),3,1);
    const double lastTail=output[2];
    result=render(96,1);
    Check(std::abs(output[0]-(lastTail*47+.8)/48)<1e-14,"short-gap resume crossfades from remaining tail");
    Check(std::abs(output[0]-lastTail)<.8/48,"short-gap resume avoids jump to zero");
    for (unsigned i=48;i<96;++i) Check(std::abs(output[i]-.8)<1e-14,"short-gap resume reaches target");
    render(0,1);
    processor.FillSilence(reinterpret_cast<uint8_t*>(output.data()),0,1);
    result=render(1,1);
    Check(std::abs(output[0]-.8)<1e-14,"empty requests do not start a recovery fade");

    for (uint32_t f=0;f<1024;++f) Store(ring.data()+size_t(f)*8,mono,-.8);
    const size_t beforeTrim=allocationCount.load(std::memory_order_relaxed);
    processor.MarkDiscontinuity();
    Check(allocationCount.load(std::memory_order_relaxed)==beforeTrim,"queue trim transition must not allocate");
    result=render(96,1);
    Check(result.written==96 && result.consumed==96,"queue trim crossfade adds no frames");
    Check(std::abs(output[0]-(.8*47-.8)/48)<1e-14,"queue trim crossfades opposite-sign source boundary");
    for (unsigned i=48;i<96;++i) Check(std::abs(output[i]+.8)<1e-14,"queue trim crossfade reaches new input");
}
static void TestValidationAndSpecialValues() {
    const Format mono{1,64,64,4,true};
    BlockResampler processor;
    Check(!processor.Initialize(mono,mono,2,1024,48000,true),"bypass rejects differing sample rates");
    Check(!processor.Initialize(mono,mono,1,0,48000),"empty workspace rejected");
    Check(!processor.Initialize(mono,mono,1,UINT32_MAX,48000),"unbounded workspace rejected");
    Check(!processor.Initialize(mono,mono,1,1024,0),"invalid output rate rejected");
    Check(!processor.Initialize(Format{},mono,1,1024,48000),"invalid source format rejected");
    Check(!processor.Initialize({6,32,32,0x3f,true},mono,1,1024,48000),"unsupported downmix rejected");
    Check(processor.Initialize(mono,mono,1,16,48000,true),"special value setup");
    processor.Reset(1);
    std::array<double,16> source{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),1e300,-1e300};
    std::array<double,16> output{}; double phase=0;
    auto result=processor.RenderRing(reinterpret_cast<uint8_t*>(output.data()),16,reinterpret_cast<uint8_t*>(source.data()),sizeof(source),0,16,1,phase,1);
    Check(result.written==16,"special values processed");
    Check(output[0]==0 && output[1]==0 && output[2]==0 && output[3]==1 && output[4]==-1,"nonfinite sanitized and finite overrange clipped");
    const auto input=reinterpret_cast<uint8_t*>(source.data());
    auto invalid=processor.RenderRing(reinterpret_cast<uint8_t*>(output.data()),16,input,sizeof(source),0,17,1,phase,1);
    Check(invalid.written==0 && invalid.consumed==0,"invalid fill rejected");
    invalid=processor.RenderRing(reinterpret_cast<uint8_t*>(output.data()),16,input,sizeof(source),1,16,1,phase,1);
    Check(invalid.written==0 && invalid.consumed==0,"unaligned ring head rejected");
    invalid=processor.RenderRing(reinterpret_cast<uint8_t*>(output.data()),16,input,sizeof(source),0,16,1.00001,phase,1);
    Check(invalid.written==0 && invalid.consumed==0,"bypass rejects drift correction");
    const size_t workspace=processor.WorkspaceBytes();
    processor.Reset();
    Check(processor.WorkspaceBytes()==workspace,"reset retains preallocated workspace");
}
int main() {
    try {
        TestFormatsAndRates(); TestGainAndBypass(); TestRampsAndGaps(); TestValidationAndSpecialValues();
        std::cout << "Audio block processing passed " << checks << " checks: old-reference PCM/float, masks/rates/phase/wrap, headroom, ramps, underflow, bypass and allocation-free render\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
