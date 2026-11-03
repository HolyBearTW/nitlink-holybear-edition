#include "audio/clock_recovery.h"
#include "audio/drift_controller.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace NitLink;
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }

void Feed(AudioClockRate& clock,double rate,double skew,int first,int last,double noiseUs=0,bool outliers=false) {
    for (int i=first;i<=last;++i) {
        const double t=i*.01;
        const double noise=noiseUs*1e-6*std::sin(i*.7)+(outliers && i%83==40 ? .008 : 0);
        clock.Sample(uint64_t(t*rate*(1+skew/1e6)),uint64_t((100+t+noise)*1e7),rate,t);
    }
}

void ClockTests() {
    // Device units and delivery delays differ. Only device/QPC pairs may
    // influence the estimated rate, including ordinary timestamp jitter.
    for (double captureRate : {44100.0,48000.0,96000.0}) {
        for (double skew : {-1200.0,-300.0,0.0,500.0,1200.0}) {
            AudioClockRate capture,render;
            for (int i=0;i<=4000;++i) {
                const double t=i*.01;
                const auto qpc=uint64_t((100+t)*1e7);
                const auto cqpc=uint64_t(int64_t(qpc)+int64_t(80*std::sin(i*.7)));
                capture.Sample(uint64_t(t*captureRate*(1+skew/1e6)),cqpc,captureRate,t+.001*(i%7));
                render.Sample(uint64_t(t*10000000),qpc,10000000,t+.001*(i%7));
            }
            Check(capture.Valid(40.04) && render.Valid(40.04),"clock regressions converge at all nominal rates");
            Check(std::abs((capture.Rate()/render.Rate()-1)*1e6-skew)<20,"relative clock rate rejects delivery jitter");
            Check(capture.Confidence(40.04)>.9,"small timestamp noise has high confidence");
            Check(!capture.Valid(41),"stalled clock estimate expires");
            const auto generation=capture.Generation();
            capture.Sample(0,2000000000,captureRate,41);
            Check(!capture.Valid(41) && capture.Generation()!=generation,"position reset discards old clock fit immediately");
        }
    }
    AudioClockRate invalid;
    for (int i=0;i<300;++i) invalid.Sample(i*480,uint64_t(1000000000+i*100000),0,i*.01);
    Check(!invalid.Valid(3),"invalid clock frequency cannot enable correction");
    Feed(invalid,48000,0,0,400);
    invalid.Sample(192480,1040100000,std::numeric_limits<double>::quiet_NaN(),4.01);
    Check(!invalid.Valid(4.01),"nonfinite clock frequency invalidates fit");

    AudioClockRate noisy,outliers,clean;
    Feed(clean,48000,500,0,4000,8);
    Feed(noisy,48000,500,0,4000,700);
    Feed(outliers,48000,500,0,4000,8,true);
    Check(!noisy.Valid(40) && noisy.ResidualUs()>250,"plausible but noisy timestamps fail residual quality gate");
    Check(outliers.Valid(40),"isolated timestamp outliers do not destroy good fit");
    Check(std::abs((outliers.Rate()-1)*1e6-500)<20,"outliers cannot bias measured drift");
    Check(outliers.Confidence(40)<clean.Confidence(40),"rejected timestamps reduce fit confidence");

    AudioClockRate capture,render;
    AudioClockBlend blend;
    Check(blend.Update(capture,render,0).state==AudioClockState::Learning,"clock blend starts learning");
    Feed(capture,48000,700,0,400);
    Feed(render,10000000,-100,0,400);
    const auto fresh=blend.Update(capture,render,4);
    Check(fresh.state==AudioClockState::Fresh && std::abs(fresh.ppm-800)<20,"relative feed-forward comes from both clocks");
    const auto generation=capture.Generation();
    capture.RejectTimestamp(4.01);
    Check(!capture.Valid(4.01) && capture.Generation()==generation,"timestamp error invalidates quality without device reset");
    const auto hold=blend.Update(capture,render,4.5);
    Check(hold.state==AudioClockState::Holding && hold.ppm==fresh.ppm,"short timestamp fault holds measured drift");
    const auto fade=blend.Update(capture,render,7);
    Check(fade.state==AudioClockState::Holding && std::abs(fade.ppm-fresh.ppm*.5)<1e-8 &&
        std::abs(fade.confidence-fresh.confidence*.5)<1e-8,"long fault decays rate and confidence together");
    Check(blend.Update(capture,render,9).state==AudioClockState::Unavailable,"unavailable estimate eventually stops feed-forward");
    Feed(capture,48000,700,401,1000);
    Feed(render,10000000,-100,401,1000);
    Check(blend.Update(capture,render,10).state==AudioClockState::Fresh,"fresh fit recovers after bad timestamps");
    capture.Reset();
    const auto reset=blend.Update(capture,render,10.01);
    Check(reset.state==AudioClockState::Learning && reset.ppm==0,"device reset immediately clears held estimate");

    AudioClockRate resetDuringFault;
    Feed(resetDuringFault,48000,400,0,400);
    resetDuringFault.RejectTimestamp(4.01);
    const auto before=resetDuringFault.Generation();
    resetDuringFault.Sample(0,1040200000,48000,4.02);
    Check(resetDuringFault.Generation()!=before && !resetDuringFault.Valid(4.02),"position reset is detected during timestamp fault");
    AudioClockRate rateChange;
    Feed(rateChange,48000,400,0,400);
    const auto oldGeneration=rateChange.Generation();
    rateChange.Sample(400000,1040100000,96000,4.01);
    Check(rateChange.Generation()!=oldGeneration && !rateChange.Valid(4.01),"frequency change clears fit immediately");
    Feed(rateChange,96000,400,401,800);
    const auto qpcGeneration=rateChange.Generation();
    rateChange.Sample(800000,1000000000,96000,8.01);
    Check(rateChange.Generation()!=qpcGeneration,"backward QPC cannot reuse an old fit");
}

void ControllerTests() {
    // Re-primes must preserve a useful measured rate without changing the FIFO
    // target, even when device skew is much larger than normal quartz drift.
    for (double skew : {-1000.0,1000.0}) {
        DriftController controller;
        double error=0,previous=0,ppm=0;
        for (int i=0;i<60000;++i) {
            const double dt=.003;
            ppm=controller.Update(error,dt,i>700 ? skew : 0);
            error+=dt*(skew-ppm)/1e6;
            Check(std::abs(ppm)<=2000,"total correction remains bounded");
            Check(std::abs(ppm-previous)<=400*dt+1e-7,"feed-forward obeys slew limit");
            previous=ppm;
            if (i>10000 && i%1000==0) { controller.Reprime(); previous=controller.Update(0,0); }
        }
        Check(std::abs(ppm-skew)<20 && std::abs(error)<.0002,"clock feed-forward settles at fixed FIFO target");
    }
    DriftController negative;
    for (int i=0;i<4000;++i) negative.Update(-.003,.01,300);
    Check(negative.Update(0,0)<0,"negative FIFO feedback develops for reset tests");
    const double slowing=negative.Update(0,0);
    negative.Reprime(DriftResetReason::Underflow);
    Check(negative.Update(0,0)==slowing,"underflow retains protective negative correction");
    negative.Reprime(DriftResetReason::BacklogTrim);
    Check(std::abs(negative.Update(0,0)-300)<1e-8,"backlog trim drops negative feedback but retains measured rate");
    for (int i=0;i<4000;++i) negative.Update(.003,.01,300);
    Check(negative.Update(0,0)>300,"positive FIFO feedback develops for reset tests");
    const double beforeFault=negative.Update(0,0);
    negative.Reprime(DriftResetReason::TimestampReset);
    Check(negative.Update(0,0)==beforeFault,"timestamp fault leaves held feed-forward policy in charge");
    negative.Reprime(DriftResetReason::Underflow);
    Check(negative.Update(0,0)<=300,"underflow discards correction that drained the FIFO");
    negative.Reprime(DriftResetReason::DeviceChange);
    Check(negative.Update(0,0)==0,"device change clears all old correction");
    for (int i=0;i<1000;++i) negative.Update(.001,.01,300);
    negative.Reprime(DriftResetReason::UserChange);
    Check(negative.Update(0,0)==0,"user target change restarts control observations");
}

void RecoveryTests() {
    using A=AudioRecoveryAction;
    AudioRecoveryGuard tiny;
    Check(tiny.Decide(1.99,0,1.0/48000,.003)==A::None,"one missing sample does not recover");
    Check(tiny.Decide(2.01,0,1.0/48000,.003)==A::None,"two missing samples do not recover");
    Check(tiny.Decide(2.02,0,1.0/48000,.003)==A::None,"three missing samples do not rebuild endpoints");
    Check(tiny.WindowEvents()==3,"sliding window retains events across old bucket boundary");
    AudioRecoveryGuard crossing;
    crossing.Decide(0,0,0,.003);
    Check(crossing.Decide(1.99,0,.001,.003)==A::None,"late first event waits");
    Check(crossing.Decide(2.01,0,.001,.003)==A::None,"cross-boundary second event waits");
    Check(crossing.Decide(2.02,0,.001,.003)==A::Reprime,"cross-boundary damage causes local re-prime");
    AudioRecoveryGuard expiry;
    expiry.Decide(0,0,.001,.003);
    expiry.Decide(2.01,0,.001,.003);
    Check(expiry.WindowEvents()==1 && std::abs(expiry.WindowMissingSeconds()-.001)<1e-10,"expired event leaves the moving window");
    Check(expiry.Decide(2.02,0,.001,.003)==A::None,"expired damage cannot trigger premature recovery");
    Check(expiry.Decide(2.03,0,.001,.003)==A::Reprime,"remaining live damage still triggers recovery");

    AudioRecoveryGuard guard;
    Check(guard.Decide(0,0,.010,.003)==A::Reprime,"first large deficit stays local");
    guard.ClearWindow();
    Check(guard.Decide(.49,0,.020,.003)==A::None,"local re-prime cooldown survives ClearWindow");
    Check(guard.Decide(.5,0,.010,.003)==A::RestartRender,"recurring deficit rebuilds playback first");
    guard.ClearWindow();
    Check(guard.Decide(5.49,0,.020,.003)==A::None,"render rebuild cooldown survives ClearWindow");
    Check(guard.Decide(5.5,0,.010,.003)==A::RestartBoth,"persistent deficit eventually rebuilds both endpoints");
    guard.ClearWindow();
    Check(guard.Decide(15.49,0,.020,.003)==A::None,"two-endpoint cooldown suppresses rebuild loops");
    Check(guard.Decide(15.5,0,.010,.003)==A::RestartBoth,"unresolved damage remains in highest recovery stage");
    Check(guard.Decide(46,0,.010,.003)==A::Reprime,"healthy interval resets escalation");

    AudioRecoveryGuard severe;
    Check(severe.Decide(0,.03,0,.003)==A::None,"one queue spike does not recover");
    Check(severe.Decide(.1,0,0,.003)==A::None,"normal block cancels persistence");
    Check(severe.Decide(.2,.03,0,.003)==A::None,"new severe interval starts");
    for (int i=1;i<=25;++i) Check(severe.Decide(.2+i*.009,.03,0,.003)==A::None,"persistent error waits for threshold");
    Check(severe.Decide(.451,.03,0,.003)==A::Reprime,"sustained severe queue error re-primes");
    AudioRecoveryGuard gap;
    gap.Decide(0,.03,0,.003);
    Check(gap.Decide(1,.03,0,.003)==A::None,"missing observations cannot prove persistent queue error");
    AudioRecoveryGuard ordinary;
    for (int i=0;i<10000;++i) Check(ordinary.Decide(i*.003,.006*std::sin(i),0,.003)==A::None,"ordinary packet jitter never recovers");
    Check(ordinary.Decide(31,0,-.01,.003)==A::None,"negative missing duration is invalid");
    Check(ordinary.Decide(31,std::numeric_limits<double>::quiet_NaN(),.02,.003)==A::None,"nonfinite recovery input does not trigger action");
}

void WatchdogTests() {
    CaptureProgressWatchdog watchdog;
    watchdog.Reset(0);
    Check(!watchdog.Observe(60).stalled,"no signal at startup cannot arm repeated reopen");
    watchdog.Progress(60,0,0);
    Check(!watchdog.HasProgress(),"empty capture notification is not progress");
    for (int i=0;i<=100;++i) watchdog.Progress(i*.01,uint64_t(i*480),480);
    Check(!watchdog.Observe(1.49).stalled,"watchdog tolerates short scheduling gap");
    const auto stalled=watchdog.Observe(1.5);
    Check(stalled.stalled && stalled.restartCapture,"previously healthy capture earns one stall retry");
    const auto repeated=watchdog.Observe(2);
    Check(repeated.stalled && !repeated.restartCapture,"stalled observation remains visible without reopen loop");
    watchdog.EndpointRestarted(2);
    Check(!watchdog.Observe(2.49).stalled,"endpoint restart gets one bounded startup grace");
    const auto afterRestart=watchdog.Observe(2.5);
    Check(afterRestart.stalled && !afterRestart.restartCapture,"rebuild cannot erase no-signal backoff");
    watchdog.Progress(3,0,480);
    Check(!watchdog.Observe(3.1).stalled,"first resumed packet ends visible stall");
    Check(!watchdog.Observe(3.5).restartCapture,"one resumed packet is insufficient to rearm retry");
    for (int i=0;i<=60;++i) watchdog.Progress(4+i*.01,uint64_t(i*480),480);
    const auto recovered=watchdog.Observe(5.11);
    Check(recovered.stalled && recovered.restartCapture,"sustained progress rearms a later stall retry");
    watchdog.Reset(6);
    watchdog.Progress(6,0,480);
    Check(watchdog.Observe(7).stalled && !watchdog.Observe(7).restartCapture,"new device does not inherit old healthy state");
    watchdog.Reset(8);
    for (int i=0;i<=100;++i) watchdog.Progress(8+i*.01,0,480);
    Check(watchdog.Observe(9.5).restartCapture,"real silent packets remain progress even with unavailable positions");
}

int main() {
    try {
        ClockTests(); ControllerTests(); RecoveryTests(); WatchdogTests();
        std::cout << "Clock quality/hold/reset, fixed-target control, sliding recovery and stall backoff passed\n";
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
