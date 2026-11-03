#pragma once
#include "audio/audio_convert.h"

namespace AudioProcessingReference {
using namespace NitLink::AudioConvert;
struct Result { uint32_t written=0,consumed=0; };

// The original tap-major algorithm decodes PCM and addresses the ring for
// every tap. This remains deliberately separate from the block implementation.
inline Result Render(uint8_t* output,uint32_t requested,const uint8_t* ring,size_t ringBytes,
                     size_t head,uint32_t fill,const Format& input,const Format& out,
                     const SincKernel& kernel,double ratio,double& phase,double gain=1) {
    Result result;
    const unsigned inputBytes=input.bits/8,outputBytes=out.bits/8;
    const unsigned inputFrameBytes=inputBytes*input.channels,outputFrameBytes=outputBytes*out.channels;
    for (;result.written<requested;++result.written) {
        const uint32_t k=uint32_t(phase);
        if (k<kernel.History() || uint64_t(k)-kernel.History()+kernel.Taps()>fill) break;
        const double p=(phase-k)*SincKernel::Phases;
        const unsigned index=std::min(unsigned(p),SincKernel::Phases-1);
        const double fraction=p-index;
        const float* a=kernel.Phase(index); const float* b=kernel.Phase(index+1);
        std::array<double,32> source{},mapped{};
        for (unsigned tap=0;tap<kernel.Taps();++tap) {
            const double weight=a[tap]+fraction*(b[tap]-a[tap]);
            const size_t at=(head+size_t(k-kernel.History()+tap)*inputFrameBytes)%ringBytes;
            for (unsigned c=0;c<input.channels;++c)
                source[c]+=weight*Read(ring+at+c*inputBytes,input);
        }
        Map(source.data(),input,mapped.data(),out);
        for (unsigned c=0;c<out.channels;++c)
            Write(output+size_t(result.written)*outputFrameBytes+c*outputBytes,out,mapped[c]*gain);
        phase+=ratio;
    }
    result.consumed=std::min(uint32_t(phase)-kernel.History(),fill);
    phase-=result.consumed;
    return result;
}
}
