#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace NitLink {
// Device-position timestamps, rather than callback arrival times, measure the
// clock. A short timestamp fault invalidates the fit but not its last rate.
class AudioClockRate {
public:
    void Reset() {
        const uint64_t next=generation_+1;
        *this=AudioClockRate{}; generation_=next;
    }
    void RejectTimestamp(double now) {
        if (!std::isfinite(now)) return;
        fault_=true; started_=false; ++rejected_;
    }
    void Sample(uint64_t position, uint64_t qpc, double frequency, double now) {
        if (!std::isfinite(frequency) || frequency<=0 || !std::isfinite(now)) { Reset(); return; }
        if (!qpc) { RejectTimestamp(now); return; }
        if (frequency_>0 && (position<lastPosition_ || qpc<lastQpc_ || frequency!=frequency_ || now<lastSeen_)) Reset();
        if (!started_) { Begin(position,qpc,frequency,now); return; }
        if (position==lastPosition_ || qpc==lastQpc_) return;
        const double elapsed=double(qpc-lastQpc_)/1e7;
        const double progress=double(position-lastPosition_)/frequency;
        if (elapsed>3) { RejectTimestamp(now); Begin(position,qpc,frequency,now); return; }
        if (progress/elapsed<0.5 || progress/elapsed>1.5 ||
            std::abs(progress-(learned_ ? rate_ : 1)*elapsed)>.002+.01*elapsed) {
            ++rejected_; return;
        }
        if (elapsed<.01) return;
        lastPosition_=position; lastQpc_=qpc; lastSeen_=now;
        const double x=double(qpc-originQpc_)/1e7;
        const double y=double(position-originPosition_)/frequency;
        ++count_;
        const double dx=x-meanX_, dy=y-meanY_;
        meanX_+=dx/count_; meanY_+=dy/count_;
        xx_+=dx*(x-meanX_); xy_+=dx*(y-meanY_); yy_+=dy*(y-meanY_);
        if (x<2) return;
        const double rate=xx_>0 ? xy_/xx_ : 0;
        residual_=std::sqrt(std::max(0.0,yy_-rate*xy_)/count_);
        const double rejectedFraction=double(rejected_)/double(count_+rejected_);
        const bool good=count_>=20 && std::isfinite(rate) && std::abs(rate-1)<=.01 &&
            residual_<=.00025 && rejectedFraction<=.2;
        if (good) {
            rate_=learned_ ? rate_+.25*(rate-rate_) : rate;
            confidence_=std::clamp(1.0-residual_/.0005-rejectedFraction,0.0,1.0);
            learned_=true; fault_=false; lastEstimate_=now;
        } else fault_=true;
        Begin(position,qpc,frequency,now);
    }
    bool Valid(double now) const {
        return learned_ && !fault_ && std::isfinite(now) && now>=lastSeen_ &&
            now-lastSeen_<.5 && now-lastEstimate_<3;
    }
    double Rate() const { return rate_; }
    double Confidence(double now) const { return Valid(now) ? confidence_ : 0; }
    double ResidualUs() const { return residual_*1e6; }
    uint64_t Generation() const { return generation_; }
private:
    void Begin(uint64_t position,uint64_t qpc,double frequency,double now) {
        started_=true; frequency_=frequency;
        originPosition_=lastPosition_=position; originQpc_=lastQpc_=qpc;
        lastSeen_=now; count_=1; rejected_=0;
        meanX_=meanY_=xx_=xy_=yy_=0;
    }
    bool started_=false, learned_=false, fault_=false;
    uint64_t generation_=0, originPosition_=0, originQpc_=0, lastPosition_=0, lastQpc_=0;
    unsigned count_=0, rejected_=0;
    double frequency_=0, lastSeen_=0, lastEstimate_=0, rate_=1, confidence_=0, residual_=0;
    double meanX_=0, meanY_=0, xx_=0, xy_=0, yy_=0;
};

enum class AudioClockState : uint8_t { Learning=0, Fresh=1, Holding=2, Unavailable=3 };
struct AudioClockEstimate {
    double ppm=0, confidence=0;
    AudioClockState state=AudioClockState::Learning;
};

// Unknown clocks do not mean zero drift. Hold the last reliable relative rate
// for one second, then fade it out over four seconds. A reset generation means
// the device identity/position changed and immediately discards that history.
class AudioClockBlend {
public:
    void Reset() { *this=AudioClockBlend{}; }
    AudioClockEstimate Update(const AudioClockRate& capture,const AudioClockRate& render,double now) {
        if (!std::isfinite(now)) return {};
        if (seen_ && (capture.Generation()!=captureGeneration_ || render.Generation()!=renderGeneration_ || now<lastNow_)) Reset();
        seen_=true; captureGeneration_=capture.Generation(); renderGeneration_=render.Generation(); lastNow_=now;
        if (capture.Valid(now) && render.Valid(now)) {
            const double ppm=(capture.Rate()/render.Rate()-1)*1e6;
            if (std::isfinite(ppm)) {
                lastPpm_=std::clamp(ppm,-2000.0,2000.0);
                lastConfidence_=std::min(capture.Confidence(now),render.Confidence(now));
                lastGood_=now; learned_=true;
                return {lastPpm_,lastConfidence_,AudioClockState::Fresh};
            }
        }
        if (!learned_) return {};
        const double age=std::max(0.0,now-lastGood_);
        if (age>=5) return {0,0,AudioClockState::Unavailable};
        const double weight=age<=1 ? 1 : (5-age)/4;
        return {lastPpm_*weight,lastConfidence_*weight,AudioClockState::Holding};
    }
private:
    bool seen_=false, learned_=false;
    uint64_t captureGeneration_=0, renderGeneration_=0;
    double lastNow_=0, lastGood_=0, lastPpm_=0, lastConfidence_=0;
};

enum class AudioRecoveryAction : uint8_t { None=0, Reprime=1, RestartRender=2, RestartBoth=3 };

// Recovery reacts to damage in a true sliding two-second window. Tiny missing
// samples alone never rebuild an endpoint. Repeated damage escalates through
// local re-prime, render rebuild and only then a rebuild of both endpoints.
class AudioRecoveryGuard {
public:
    void Reset() { *this=AudioRecoveryGuard{}; }
    void ClearWindow() { head_=size_=0; missing_=0; severeStart_=lastObservation_=-1; }
    AudioRecoveryAction Decide(double now,double error,double missingSeconds,double blockSeconds) {
        if (!std::isfinite(now) || !std::isfinite(error) || !std::isfinite(missingSeconds) ||
            !std::isfinite(blockSeconds) || missingSeconds<0 || blockSeconds<0) return AudioRecoveryAction::None;
        if (lastObservation_>=0 && now<lastObservation_) Reset();
        if (lastDamage_>=0 && now-lastDamage_>30) stage_=0;
        if (now<nextAction_) { ClearWindow(); return AudioRecoveryAction::None; }
        if (lastObservation_>=0 && now-lastObservation_>std::max(.1,2*blockSeconds)) severeStart_=-1;
        lastObservation_=now;
        while (size_ && events_[head_].time<now-2) {
            missing_-=events_[head_].missing;
            head_=(head_+1)%events_.size(); --size_;
        }
        bool saturated=false;
        if (missingSeconds>0) {
            lastDamage_=now;
            if (size_==events_.size()) saturated=true;
            else { events_[(head_+size_)%events_.size()]={now,missingSeconds}; ++size_; missing_+=missingSeconds; }
        }
        if (std::abs(error)>=std::max(.020,2*blockSeconds)) {
            lastDamage_=now;
            if (severeStart_<0) severeStart_=now;
        } else severeStart_=-1;
        const bool sustained=severeStart_>=0 && now-severeStart_>=.250;
        if (!saturated && missing_<.010 && !(size_>=3 && missing_>=.002) && !sustained)
            return AudioRecoveryAction::None;
        // Saturation is a local-only fail-safe. Real packet rates hit a damage
        // threshold well before this fixed allocation fills.
        const auto action=saturated ? AudioRecoveryAction::Reprime :
            stage_==0 ? AudioRecoveryAction::Reprime :
            stage_==1 ? AudioRecoveryAction::RestartRender : AudioRecoveryAction::RestartBoth;
        if (!saturated) stage_=std::min(stage_+1,2u);
        nextAction_=now+(action==AudioRecoveryAction::Reprime ? .5 : action==AudioRecoveryAction::RestartRender ? 5 : 10);
        ClearWindow(); return action;
    }
    bool Observe(double now,double error,double missingSeconds,double blockSeconds) {
        return Decide(now,error,missingSeconds,blockSeconds)!=AudioRecoveryAction::None;
    }
    double WindowMissingSeconds() const { return std::max(0.0,missing_); }
    unsigned WindowEvents() const { return unsigned(size_); }
private:
    struct Event { double time=0, missing=0; };
    std::array<Event,2048> events_{};
    size_t head_=0, size_=0;
    double lastObservation_=-1, lastDamage_=-1, severeStart_=-1, missing_=0, nextAction_=-1;
    unsigned stage_=0;
};

struct CaptureProgressStatus { bool stalled=false, restartCapture=false; };
// No packets at initial startup can mean no HDMI signal. Only a stream that
// previously progressed for 500 ms earns an automatic stall retry. A failed
// retry stays waiting until real progress resumes, rather than reopening the
// same silent device forever. Silent audio packets still count as progress.
class CaptureProgressWatchdog {
public:
    void Reset(double now=0) {
        seen_=healthy_=retryIssued_=positionKnown_=false;
        lastProgress_=healthyStart_=now; lastPosition_=0;
    }
    void EndpointRestarted(double now) {
        positionKnown_=false; healthyStart_=lastProgress_=now;
    }
    void Progress(double now,uint64_t position,uint32_t frames) {
        if (!frames || !std::isfinite(now)) return;
        if (!seen_ || now<lastProgress_ || now-lastProgress_>=.5 ||
            (positionKnown_ && position<lastPosition_)) healthyStart_=now;
        seen_=true; positionKnown_=true; lastPosition_=position; lastProgress_=now;
        if (now-healthyStart_>=.5) { healthy_=true; retryIssued_=false; }
    }
    CaptureProgressStatus Observe(double now) {
        if (!std::isfinite(now) || !seen_ || now<lastProgress_ || now-lastProgress_<.5) return {};
        const bool retry=healthy_ && !retryIssued_;
        if (retry) retryIssued_=true;
        return {true,retry};
    }
    bool HasProgress() const { return seen_; }
private:
    bool seen_=false, healthy_=false, retryIssued_=false, positionKnown_=false;
    uint64_t lastPosition_=0;
    double lastProgress_=0, healthyStart_=0;
};
}
