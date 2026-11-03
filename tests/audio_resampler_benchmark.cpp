#include "audio/block_resampler.h"
#include "audio_processing_reference.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace NitLink::AudioConvert;
namespace {
using Clock=std::chrono::steady_clock;
volatile double checksum=0;
struct Measurements { double mean,p99,maximum; };
Measurements Summarize(std::vector<double> timings) {
    double sum=0; for (double v:timings) sum+=v;
    std::sort(timings.begin(),timings.end());
    return {sum/timings.size(),timings[size_t((timings.size()-1)*.99)],timings.back()};
}
void Benchmark(const std::string& name,const Format& input,double ratio,bool bypass=false) {
    constexpr uint32_t capacity=4096,frames=144,iterations=1200;
    const Format output{2,32,32,3,true};
    const size_t inputFrameBytes=input.channels*input.bits/8;
    std::vector<uint8_t> ring(size_t(capacity)*inputFrameBytes),oldOutput(size_t(frames)*8),newOutput(oldOutput.size());
    const size_t head=size_t(capacity-71)*inputFrameBytes;
    for (uint32_t f=0;f<capacity;++f) {
        const size_t at=(head+size_t(f)*inputFrameBytes)%ring.size();
        for (unsigned c=0;c<input.channels;++c)
            Write(ring.data()+at+c*input.bits/8,input,.7*std::sin(f*.071+c*.2));
    }
    BlockResampler processor; SincKernel kernel;
    if (!processor.Initialize(input,output,ratio,capacity,48000,bypass) || !kernel.Initialize(ratio))
        throw std::runtime_error("benchmark initialization");
    processor.Reset(1);
    std::vector<double> oldTimings,newTimings; oldTimings.reserve(iterations); newTimings.reserve(iterations);
    auto original=[&] {
        double phase=kernel.History()+.3125;
        const auto r=AudioProcessingReference::Render(oldOutput.data(),frames,ring.data(),ring.size(),head,capacity,input,output,kernel,ratio,phase);
        if (r.written!=frames) throw std::runtime_error("reference benchmark underflow");
    };
    auto optimized=[&] {
        double phase=processor.Bypass() ? 0 : processor.History()+.3125;
        const auto r=processor.RenderRing(newOutput.data(),frames,ring.data(),ring.size(),head,capacity,ratio,phase,1);
        if (r.written!=frames) throw std::runtime_error("optimized benchmark underflow");
    };
    for (unsigned warmup=0;warmup<40;++warmup) { original(); optimized(); }
    for (unsigned i=0;i<iterations;++i) {
        // Alternate order to reduce warm-cache/frequency drift bias. This is a
        // CPU microbenchmark, not a measurement of WASAPI or capture latency.
        if (i&1) {
            auto begin=Clock::now(); optimized(); auto end=Clock::now();
            newTimings.push_back(std::chrono::duration<double,std::micro>(end-begin).count());
            begin=Clock::now(); original(); end=Clock::now();
            oldTimings.push_back(std::chrono::duration<double,std::micro>(end-begin).count());
        } else {
            auto begin=Clock::now(); original(); auto end=Clock::now();
            oldTimings.push_back(std::chrono::duration<double,std::micro>(end-begin).count());
            begin=Clock::now(); optimized(); end=Clock::now();
            newTimings.push_back(std::chrono::duration<double,std::micro>(end-begin).count());
        }
        checksum=checksum+Read(oldOutput.data()+(i%frames)*8,output)+Read(newOutput.data()+(i%frames)*8,output);
    }
    if (!bypass) {
        for (size_t sample=0;sample<size_t(frames)*output.channels;++sample)
            if (std::abs(Read(oldOutput.data()+sample*4,output)-Read(newOutput.data()+sample*4,output))>1.2e-7)
                throw std::runtime_error("benchmark output mismatch");
    }
    const auto old=Summarize(oldTimings),current=Summarize(newTimings);
    std::cout << name << ',' << kernel.Taps() << ',' << processor.WorkspaceBytes() << ','
        << old.mean << ',' << old.p99 << ',' << old.maximum << ','
        << current.mean << ',' << current.p99 << ',' << current.maximum << ',' << old.mean/current.mean << '\n';
}
}
int main() {
    try {
        std::cout << "CPU microbenchmark: 144 output frames per call, 1200 alternating timed calls per path; identical FIR table and wrapped input. Bypass removes SRC intentionally.\n";
        std::cout << "case,taps,workspace_bytes,old_mean_us,old_p99_us,old_max_us,new_mean_us,new_p99_us,new_max_us,mean_speedup\n" << std::fixed << std::setprecision(3);
        Benchmark("PCM16 stereo 48k to 48k",{2,16,16,3,false},1);
        Benchmark("PCM24 stereo 48k to 44.1k",{2,24,24,3,false},48000.0/44100);
        Benchmark("float32 stereo 96k to 48k",{2,32,32,3,true},2);
        Benchmark("PCM16 stereo 48k bypass",{2,16,16,3,false},1,true);
        std::cout << "checksum=" << checksum << '\n';
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
