#pragma once
#include "audio_convert.h"
#include <limits>

namespace NitLink::AudioConvert {

// Allocation and format/channel selection happen at setup. The render path decodes
// each source sample once per block into contiguous planes, then filters those
// planes without ring address arithmetic or PCM decoding inside the FIR loop.
class BlockResampler {
public:
    struct Result {
        uint32_t written = 0, consumed = 0;
        bool silent = true;
    };

    bool Initialize(const Format& input, const Format& output, double nominalRatio,
                    uint32_t maxInputFrames, uint32_t outputRate, bool bypass = false) {
        ready_ = false;
        if (!CanMap(input,output) || !std::isfinite(nominalRatio) || nominalRatio < .125 ||
            nominalRatio > 8 || !maxInputFrames || !outputRate || outputRate > 768000 ||
            (bypass && nominalRatio != 1)) return false;
        constexpr size_t maxSamples = (128u * 1024u * 1024u) / sizeof(double);
        if (maxInputFrames > maxSamples/input.channels) return false;
        if (!bypass && !kernel_.Initialize(nominalRatio)) return false;
        try { decoded_.resize(size_t(maxInputFrames)*input.channels); }
        catch (...) { return false; }
        input_ = input; output_ = output; capacity_ = maxInputFrames; bypass_ = bypass;
        inputBytes_ = input.bits/8; outputBytes_ = output.bits/8;
        inputFrameBytes_ = inputBytes_*input.channels;
        outputFrameBytes_ = outputBytes_*output.channels;
        reader_ = SelectReader(input);
        mapFirst_.fill(-1); mapSecond_.fill(-1);
        if (input.channels == output.channels && Mask(input) == Mask(output)) {
            for (unsigned c=0;c<output.channels;++c) mapFirst_[c]=int(c);
        } else if (output.channels==1) {
            mapFirst_[0]=0; mapSecond_[0]=1;
        } else {
            unsigned c=0;
            for (unsigned bit=0;bit<32;++bit) if (Mask(output)&(uint32_t(1)<<bit)) {
                if (bit==0) mapFirst_[c]=0;
                if (bit==1) mapFirst_[c]=input.channels==1 ? 0 : 1;
                ++c;
            }
        }
        // A 1 ms sample-level ramp uses already requested frames, never an
        // additional queued block. It also applies to mute/unmute transitions.
        rampFrames_ = std::max(1u,(outputRate+999)/1000);
        ready_ = true; Reset(); return true;
    }

    void Reset(double initialGain = 0) {
        gain_ = targetGain_ = std::isfinite(initialGain) ? std::clamp(initialGain,0.0,1.0) : 0;
        gainStep_ = 0; gainRemaining_ = tailRemaining_ = resumeRemaining_ = 0; gap_ = false;
        lastOutput_.fill(0); tailStart_.fill(0); resumeStart_.fill(0);
    }
    // Queue trimming changes the next source sample without changing endpoints.
    // Keep the previous output as a crossfade anchor, never as queued audio.
    void MarkDiscontinuity() {
        tailStart_=lastOutput_; tailRemaining_=rampFrames_; gap_=true;
        gain_=0; gainRemaining_=rampFrames_; gainStep_=targetGain_/rampFrames_;
        resumeRemaining_=0;
    }
    bool Ready() const { return ready_; }
    bool Bypass() const { return bypass_; }
    unsigned Taps() const { return bypass_ ? 0 : kernel_.Taps(); }
    unsigned History() const { return bypass_ ? 0 : kernel_.History(); }
    unsigned Lookahead() const { return Taps()-History(); }
    uint32_t RampFrames() const { return rampFrames_; }
    size_t WorkspaceBytes() const { return decoded_.size()*sizeof(double); }
    double CurrentGain() const { return gain_; }

    // phase remains relative to the unconsumed ring head. The caller advances
    // its ring by consumed; FIR history and fractional phase survive blocks.
    // Only written counts real input. The entire valid output request is filled,
    // including a short decaying tail and zeroes when input is unavailable.
    Result RenderRing(uint8_t* out, uint32_t frames, const uint8_t* ring, size_t ringBytes,
                      size_t headBytes, uint32_t fillFrames, double ratio, double& phase,
                      double targetGain, bool muted = false) {
        Result result;
        if (!frames || !ready_ || !out || !ring || !ringBytes || ringBytes%inputFrameBytes_ ||
            headBytes>=ringBytes || headBytes%inputFrameBytes_ ||
            fillFrames>ringBytes/inputFrameBytes_ || fillFrames>capacity_ ||
            !std::isfinite(ratio) || ratio<=0 || ratio>8.016 ||
            !std::isfinite(phase) || phase<History() || phase>double(capacity_)+Taps()+1 ||
            (bypass_ && (ratio!=1 || phase!=0))) return result;
        SetTarget(targetGain,muted);
        const uint32_t decodeFrames = bypass_ ? std::min(frames,fillFrames) : uint32_t(
            std::min(double(fillFrames),std::ceil(phase+double(frames)*ratio)+Lookahead()));
        // Ring wrap is handled once per source frame, outside the convolution.
        size_t at=headBytes;
        for (uint32_t f=0;f<decodeFrames;++f) {
            for (unsigned c=0;c<input_.channels;++c)
                decoded_[size_t(c)*capacity_+f]=reader_(ring+at+c*inputBytes_,input_);
            at+=inputFrameBytes_;
            if (at==ringBytes) at=0;
        }
        for (;result.written<frames;++result.written) {
            std::array<double,32> input{};
            if (bypass_) {
                if (result.written>=decodeFrames) break;
                for (unsigned c=0;c<input_.channels;++c)
                    input[c]=decoded_[size_t(c)*capacity_+result.written];
            } else {
                const uint32_t k=uint32_t(phase);
                if (k<History() || uint64_t(k)-History()+Taps()>decodeFrames) break;
                const double p=(phase-k)*SincKernel::Phases;
                const unsigned index=std::min(unsigned(p),SincKernel::Phases-1);
                const double fraction=p-index;
                const float *a=kernel_.Phase(index), *b=kernel_.Phase(index+1);
                for (unsigned tap=0;tap<Taps();++tap)
                    weights_[tap]=a[tap]+fraction*(b[tap]-a[tap]);
                for (unsigned c=0;c<input_.channels;++c) {
                    const double* source=decoded_.data()+size_t(c)*capacity_+k-History();
                    double s0=0,s1=0,s2=0,s3=0;
                    unsigned tap=0;
                    for (;tap+3<Taps();tap+=4) {
                        s0+=weights_[tap]*source[tap]; s1+=weights_[tap+1]*source[tap+1];
                        s2+=weights_[tap+2]*source[tap+2]; s3+=weights_[tap+3]*source[tap+3];
                    }
                    double sum=(s0+s1)+(s2+s3);
                    for (;tap<Taps();++tap) sum+=weights_[tap]*source[tap];
                    input[c]=sum;
                }
                phase+=ratio;
            }
            if (gap_) {
                // A short gap can end before its tail reaches zero. Crossfade
                // that last output into new audio without a sudden jump to zero.
                resumeStart_=lastOutput_; resumeRemaining_=rampFrames_;
                gain_=0; gainRemaining_=rampFrames_; gainStep_=targetGain_/rampFrames_;
                tailRemaining_=0; gap_=false;
            }
            const double gain=NextGain();
            const double resume=resumeRemaining_ ? double(--resumeRemaining_)/rampFrames_ : 0;
            for (unsigned c=0;c<output_.channels;++c) {
                double sample=mapFirst_[c]>=0 ? input[unsigned(mapFirst_[c])] : 0;
                if (mapSecond_[c]>=0) sample=(sample+input[unsigned(mapSecond_[c])])*.5;
                // Gain precedes the only clipping and quantization operation.
                const double value=sample*gain+resumeStart_[c]*resume;
                Write(out+size_t(result.written)*outputFrameBytes_+c*outputBytes_,output_,value);
                lastOutput_[c]=std::isfinite(value) ? std::clamp(value,-1.0,1.0) : 0;
                if (lastOutput_[c]!=0) result.silent=false;
            }
        }
        if (bypass_) { result.consumed=result.written; phase=0; }
        else {
            result.consumed=std::min(uint32_t(phase)-History(),fillFrames);
            phase-=result.consumed;
        }
        if (result.written<frames)
            Tail(out+size_t(result.written)*outputFrameBytes_,frames-result.written,result.silent);
        return result;
    }

    Result FillSilence(uint8_t* out,uint32_t frames,double targetGain,bool muted=false) {
        Result result;
        if (!frames || !ready_ || !out) return result;
        SetTarget(targetGain,muted); Tail(out,frames,result.silent); return result;
    }

private:
    using Reader = double (*)(const uint8_t*,const Format&);
    static double Float32(const uint8_t* p,const Format&) {
        float v; std::memcpy(&v,p,4); return std::isfinite(v) ? double(v) : 0;
    }
    static double Float64(const uint8_t* p,const Format&) {
        double v; std::memcpy(&v,p,8); return std::isfinite(v) ? v : 0;
    }
    static double Pcm8(const uint8_t* p,const Format&) { return (int(*p)-128)/128.0; }
    static double Pcm16(const uint8_t* p,const Format&) {
        int16_t v; std::memcpy(&v,p,2); return v/32768.0;
    }
    static double Pcm24(const uint8_t* p,const Format&) {
        const uint32_t u=uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16);
        const int32_t v=(u&0x800000) ? int32_t(int64_t(u)-0x1000000) : int32_t(u);
        return v/8388608.0;
    }
    static double Pcm32(const uint8_t* p,const Format&) {
        int32_t v; std::memcpy(&v,p,4); return v/2147483648.0;
    }
    static Reader SelectReader(const Format& f) {
        if (f.floating) return f.bits==32 ? Float32 : Float64;
        if (f.validBits!=f.bits) return Read;
        switch(f.bits) { case 8:return Pcm8; case 16:return Pcm16; case 24:return Pcm24; case 32:return Pcm32; }
        return Read;
    }
    void SetTarget(double target,bool muted) {
        target=muted || !std::isfinite(target) ? 0 : std::clamp(target,0.0,1.0);
        if (target!=targetGain_) {
            targetGain_=target; gainRemaining_=rampFrames_;
            gainStep_=(targetGain_-gain_)/rampFrames_;
        }
    }
    double NextGain() {
        if (gainRemaining_) {
            gain_+=gainStep_;
            if (!--gainRemaining_) gain_=targetGain_;
        }
        return gain_;
    }
    void Tail(uint8_t* out,uint32_t frames,bool& silent) {
        if (!gap_) MarkDiscontinuity();
        for (uint32_t f=0;f<frames;++f) {
            const double scale=tailRemaining_ ? double(--tailRemaining_)/rampFrames_ : 0;
            for (unsigned c=0;c<output_.channels;++c) {
                const double value=tailStart_[c]*scale;
                Write(out+size_t(f)*outputFrameBytes_+c*outputBytes_,output_,value);
                lastOutput_[c]=value;
                if (value!=0) silent=false;
            }
        }
    }
    Format input_{},output_{};
    SincKernel kernel_;
    std::vector<double> decoded_;
    std::array<double,512> weights_{};
    std::array<int,32> mapFirst_{},mapSecond_{};
    std::array<double,32> lastOutput_{},tailStart_{},resumeStart_{};
    Reader reader_=Read;
    uint32_t capacity_=0,rampFrames_=1,gainRemaining_=0,tailRemaining_=0,resumeRemaining_=0;
    unsigned inputBytes_=0,outputBytes_=0,inputFrameBytes_=0,outputFrameBytes_=0;
    double gain_=0,targetGain_=0,gainStep_=0;
    bool ready_=false,bypass_=false,gap_=false;
};
}
