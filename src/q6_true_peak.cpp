// Question 6 - measuring true (inter-sample) peak.
//
// Sample peak just takes the largest sample value. The catch is that the
// converter reconstructs a smooth curve between the samples, and that curve can
// overshoot the highest sample - those overshoots are the inter-sample peaks. So
// a signal that reads exactly 0 dBFS on its samples can still clip the DAC.
//
// To catch them you reconstruct what happens between the samples: oversample
// (4x is the standard amount, as in ITU-R BS.1770) with a sinc / low-pass
// interpolation, then take the peak of the upsampled signal. This program builds
// the worst case - a tone at a quarter of the sample rate whose samples
// all land on +/-1.0 - and shows the true peak sitting about 3 dB above them.

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr int    kOversample = 4;

// dBFS of a linear amplitude (1.0 -> 0 dB).
double toDb(double amp) { return 20.0 * std::log10(amp); }

// A windowed-sinc low-pass used as the reconstruction filter. halfLen is how
// many original samples it reaches either side. The kernel is normalised so the
// interpolated signal keeps the same level as the input.
std::vector<float> makeInterpolator(int oversample, int halfLen)
{
    const int half = halfLen * oversample;          // taps each side, in upsampled steps
    const int len  = 2 * half + 1;
    std::vector<float> h((size_t) len);
    double sum = 0.0;
    for (int i = 0; i < len; ++i)
    {
        const int    k    = i - half;               // -half .. +half
        const double x    = (double) k / (double) oversample;
        const double sinc = (k == 0) ? 1.0 : std::sin(kPi * x) / (kPi * x);
        // Hann window across the whole kernel to tame the truncation ripple
        const double w    = 0.5 - 0.5 * std::cos(2.0 * kPi * (double) i / (double) (len - 1));
        h[(size_t) i] = (float) (sinc * w);
        sum += h[(size_t) i];
    }
    // Make each output phase pass the signal at unity: once the zero-stuffed gaps
    // are filled, the whole kernel should sum to the oversample factor.
    const double norm = sum / (double) oversample;
    for (float& v : h) v = (float) (v / norm);
    return h;
}

float samplePeak(const std::vector<float>& x)
{
    float peak = 0.0f;
    for (float v : x) peak = std::max(peak, std::fabs(v));
    return peak;
}

// Zero-stuff by the oversample factor, run the interpolation filter, and return
// the largest absolute value of the reconstructed signal.
float truePeak(const std::vector<float>& x, int oversample, const std::vector<float>& h)
{
    const int n    = (int) x.size();
    const int up   = n * oversample;
    const int half = ((int) h.size() - 1) / 2;

    float peak = 0.0f;
    // Only measure the interior, so the filter running off the ends can't skew it.
    for (int j = half; j < up - half; ++j)
    {
        float acc = 0.0f;
        for (int t = -half; t <= half; ++t)
        {
            const int idx = j + t;
            if (idx % oversample == 0)               // a real input sample sits here
            {
                const int si = idx / oversample;
                if (si >= 0 && si < n)
                    acc += x[(size_t) si] * h[(size_t) (t + half)];
            }
        }
        peak = std::max(peak, std::fabs(acc));
    }
    return peak;
}
} // namespace

int main()
{
    // Worst case for inter-sample peaks: a sine at fs/4 with a 45 degree phase,
    // scaled so every sample lands exactly on +/-1.0. The samples read 0 dBFS,
    // but the real waveform peaks at sqrt(2) between them.
    const int n = 64;
    std::vector<float> signal((size_t) n);
    for (int i = 0; i < n; ++i)
        signal[(size_t) i] = (float) (std::sqrt(2.0) * std::sin(2.0 * kPi * 0.25 * i + kPi / 4.0));

    const auto  h  = makeInterpolator(kOversample, 12);
    const float sp = samplePeak(signal);
    const float tp = truePeak(signal, kOversample, h);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Sample peak: " << sp << "  =  " << toDb(sp) << " dBFS\n";
    std::cout << "True peak:   " << tp << "  =  " << toDb(tp) << " dBTP\n";
    std::cout << "\nThe samples read 0 dBFS, but the reconstructed peak is "
              << (toDb(tp) - toDb(sp)) << " dB higher.\n"
              << "That overshoot is what the samples were hiding, and what clips the converter.\n";
}
