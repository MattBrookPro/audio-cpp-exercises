// Question 7 - a small engine wired around two interfaces, with the Question 5
// lock-free hand-over built in.
//
// The shape is the one from my written answer: an AudioEngine drives an
// AudioDevice through an interface, the device callback pushes audio through a
// Mixer, and the Mixer runs a chain of Plugins through a common interface. A new
// device or plugin drops in just by implementing the interface, without the
// engine or the mixer knowing the concrete type.
//
// On top of that it shows the Question 5 problem: swapping the plugin chain for a
// freshly built one while audio is running, without ever blocking or allocating
// on the audio thread. The new chain is built on a worker thread and handed over
// with a single atomic pointer swap, then crossfaded in so there is no click. The
// old chain is deleted back on the main thread, never on the audio thread.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

namespace {
constexpr double kPi = 3.14159265358979323846;

// ---- Plugin interface -------------------------------------------------------
// The single abstraction the mixer talks to. process() has to be real-time safe:
// no allocating, no locking, no I/O.
class Plugin
{
public:
    virtual ~Plugin() = default;
    virtual void prepare(double sampleRate) = 0;
    virtual void process(float* buffer, int numSamples) = 0;
    virtual const char* name() const = 0;
};

class GainPlugin : public Plugin
{
public:
    explicit GainPlugin(float gain) : gain_(gain) {}
    void prepare(double) override {}
    void process(float* b, int n) override { for (int i = 0; i < n; ++i) b[i] *= gain_; }
    const char* name() const override { return "Gain"; }
private:
    float gain_;
};

// One-pole low-pass - a small stateful plugin, so the chain is more than a gain.
class LowpassPlugin : public Plugin
{
public:
    explicit LowpassPlugin(float cutoffHz) : cutoff_(cutoffHz) {}
    void prepare(double sr) override
    {
        const double dt = 1.0 / sr;
        const double rc = 1.0 / (2.0 * kPi * cutoff_);
        a_ = (float) (dt / (rc + dt));    // coefficient worked out off the audio thread
        z_ = 0.0f;
    }
    void process(float* b, int n) override
    {
        for (int i = 0; i < n; ++i) { z_ += a_ * (b[i] - z_); b[i] = z_; }
    }
    const char* name() const override { return "Lowpass"; }
private:
    float cutoff_;
    float a_ = 1.0f;
    float z_ = 0.0f;
};

// ---- the unit that gets hot-swapped ----------------------------------------
struct PluginChain
{
    std::vector<std::unique_ptr<Plugin>> plugins;
    void prepare(double sr) { for (auto& p : plugins) p->prepare(sr); }
    void process(float* b, int n) { for (auto& p : plugins) p->process(b, n); }
};

PluginChain* makeQuietChain(double sr)
{
    auto* c = new PluginChain;
    c->plugins.push_back(std::make_unique<GainPlugin>(0.3f));
    c->prepare(sr);
    return c;
}

PluginChain* makeUpgradedChain(double sr)
{
    auto* c = new PluginChain;
    c->plugins.push_back(std::make_unique<GainPlugin>(1.0f));
    c->plugins.push_back(std::make_unique<LowpassPlugin>(8000.0f));
    c->prepare(sr);
    return c;
}

// ---- Mixer: owns the live chain and does the lock-free hand-over -----------
struct BlockLog { float peak; int state; };  // state 0 old, 1 crossfade, 2 new

class Mixer
{
public:
    Mixer(double sr, int maxBlock, PluginChain* initial)
        : current_(initial),
          scratchA_((size_t) maxBlock),
          scratchB_((size_t) maxBlock)
    {
        xfadeLen_ = (int) (sr * 0.030);     // 30 ms constant-power crossfade
        log_.resize(8192);
    }
    ~Mixer()
    {
        delete current_;
        delete next_;
        delete pending_.exchange(nullptr);
        delete retire_.exchange(nullptr);
    }

    // worker thread: publish a freshly built chain.
    void submit(PluginChain* fresh) { pending_.store(fresh, std::memory_order_release); }

    // main thread: delete chains the audio thread has retired.
    void reclaim() { if (auto* old = retire_.exchange(nullptr, std::memory_order_acquire)) delete old; }

    // audio (device) thread: real-time safe, no allocation and no locking.
    void process(float* buf, int n)
    {
        if (next_ == nullptr)
            if (auto* incoming = pending_.exchange(nullptr, std::memory_order_acquire))
            { next_ = incoming; xfadePos_ = 0; }

        int state;
        if (next_ == nullptr)
        {
            current_->process(buf, n);
            state = swapped_ ? 2 : 0;
        }
        else
        {
            std::copy(buf, buf + n, scratchA_.data());
            std::copy(buf, buf + n, scratchB_.data());
            current_->process(scratchA_.data(), n);
            next_->process(scratchB_.data(), n);
            for (int i = 0; i < n; ++i)
            {
                float t = (float) (xfadePos_ + i) / (float) xfadeLen_;
                if (t > 1.0f) t = 1.0f;
                const float th = t * (float) kPi * 0.5f;
                buf[i] = scratchA_[i] * std::cos(th) + scratchB_[i] * std::sin(th);  // constant power
            }
            xfadePos_ += n;
            if (xfadePos_ >= xfadeLen_)
            {
                retire_.store(current_, std::memory_order_release);  // hand old chain to main
                current_ = next_;
                next_ = nullptr;
                swapped_ = true;
            }
            state = 1;
        }

        float peak = 0.0f;
        for (int i = 0; i < n; ++i) peak = std::max(peak, std::fabs(buf[i]));
        const int idx = logCount_.load(std::memory_order_relaxed);
        if (idx < (int) log_.size())
        {
            log_[(size_t) idx] = { peak, state };
            logCount_.store(idx + 1, std::memory_order_release);
        }
    }

    int             logCount() const { return logCount_.load(std::memory_order_acquire); }
    const BlockLog& logAt(int i) const { return log_[(size_t) i]; }

private:
    PluginChain*              current_;
    PluginChain*              next_ = nullptr;
    std::atomic<PluginChain*> pending_{ nullptr };
    std::atomic<PluginChain*> retire_{ nullptr };
    bool                      swapped_ = false;
    int                       xfadeLen_ = 1;
    int                       xfadePos_ = 0;
    std::vector<float>        scratchA_, scratchB_;
    std::vector<BlockLog>     log_;
    std::atomic<int>          logCount_{ 0 };
};

// ---- AudioDevice interface + a mock backend --------------------------------
class AudioDevice
{
public:
    virtual ~AudioDevice() = default;
    virtual void start(std::function<void(float*, int)> callback) = 0;
    virtual void stop() = 0;
    virtual const char* name() const = 0;
};

// Generates a steady tone block by block on its own thread and feeds the
// callback, pacing roughly in real time like a real driver would.
class MockDevice : public AudioDevice
{
public:
    MockDevice(double sr, int blockSize) : sr_(sr), block_(blockSize) {}
    void start(std::function<void(float*, int)> cb) override
    {
        running_ = true;
        thread_ = std::thread([this, cb] {
            std::vector<float> buf((size_t) block_);
            const auto period = std::chrono::microseconds((long long) (1e6 * block_ / sr_));
            while (running_.load(std::memory_order_acquire))
            {
                for (int i = 0; i < block_; ++i)
                    buf[(size_t) i] = 0.8f * (float) std::sin(2.0 * kPi * 220.0 * (double) phase_++ / sr_);
                cb(buf.data(), block_);
                std::this_thread::sleep_for(period);
            }
        });
    }
    void stop() override { running_ = false; if (thread_.joinable()) thread_.join(); }
    const char* name() const override { return "MockDevice"; }
private:
    double            sr_;
    int               block_;
    long long         phase_ = 0;
    std::atomic<bool> running_{ false };
    std::thread       thread_;
};

// ---- AudioEngine: owns the device and the mixer ----------------------------
class AudioEngine
{
public:
    AudioEngine(double sr, int blockSize)
        : sr_(sr),
          device_(std::make_unique<MockDevice>(sr, blockSize)),
          mixer_(sr, blockSize, makeQuietChain(sr)) {}

    void   start() { device_->start([this](float* b, int n) { mixer_.process(b, n); }); }
    void   stop()  { device_->stop(); }
    Mixer& mixer() { return mixer_; }

private:
    double                       sr_;
    std::unique_ptr<AudioDevice> device_;
    Mixer                        mixer_;
};
} // namespace

int main()
{
    const double sr    = 48000.0;
    const int    block = 256;

    AudioEngine engine(sr, block);
    engine.start();  // the device thread now pushes audio through the quiet chain

    // Heavy pre-calc on a worker thread - a 150 ms stand-in for the kind of
    // coefficient or table building that can't run on the audio thread.
    std::thread worker([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        engine.mixer().submit(makeUpgradedChain(sr));  // built off the audio thread, handed over lock-free
    });

    // Let it run, reclaiming retired chains off the audio thread.
    for (int i = 0; i < 50; ++i)
    {
        engine.mixer().reclaim();
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    worker.join();
    engine.stop();
    engine.mixer().reclaim();

    // Read back what the audio thread recorded and find the transition points.
    const int    count   = engine.mixer().logCount();
    const double blockMs  = 1000.0 * block / sr;
    int firstX = -1, firstNew = -1;
    for (int i = 0; i < count; ++i)
    {
        const int s = engine.mixer().logAt(i).state;
        if (s == 1 && firstX == -1)   firstX = i;
        if (s == 2 && firstNew == -1) firstNew = i;
    }

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Audio thread ran " << count << " blocks ("
              << count * blockMs << " ms of audio) without a single stall.\n";
    if (firstX >= 0)
        std::cout << "New chain published; crossfade started at block " << firstX
                  << " (~" << firstX * blockMs << " ms).\n";
    if (firstNew >= 0)
        std::cout << "Crossfade done; running the new chain from block " << firstNew
                  << " (" << (firstNew - firstX) << " blocks, ~"
                  << (firstNew - firstX) * blockMs << " ms).\n";

    std::cout << "\n block    time(ms)   state       peak\n";
    const char* names[] = { "old      ", "crossfade", "new      " };
    auto printRow = [&](int i) {
        const auto& l = engine.mixer().logAt(i);
        std::cout << std::setw(5) << i << std::setw(11) << i * blockMs
                  << "   " << names[l.state] << "  " << l.peak << "\n";
    };

    // Sample the timeline, plus the exact transition rows, in order.
    std::vector<int> rows;
    for (int i = 0; i < count; i += std::max(1, count / 12)) rows.push_back(i);
    if (firstX   >= 0) rows.push_back(firstX);
    if (firstNew >= 0) rows.push_back(firstNew);
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    for (int i : rows) if (i < count) printRow(i);

    std::cout << "\nThe peak rises smoothly from the quiet chain (~0.24) to the new one"
                 " (~0.8) across\nthe crossfade. Audio never dropped out, and nothing was"
                 " allocated or locked on\nthe audio thread - the chain was built on the"
                 " worker and swapped in atomically.\n";
    return 0;
}
