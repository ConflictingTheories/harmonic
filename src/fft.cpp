#include "fft.h"
#include <algorithm>

void SimpleFFT::fft(std::vector<std::complex<double>> &x)
{
    const size_t N = x.size();
    if (N <= 1)
        return;

    // Divide
    std::vector<std::complex<double>> even(N / 2);
    std::vector<std::complex<double>> odd(N / 2);

    for (size_t i = 0; i < N / 2; ++i)
    {
        even[i] = x[i * 2];
        odd[i] = x[i * 2 + 1];
    }

    // Conquer
    fft(even);
    fft(odd);

    // Combine
    for (size_t k = 0; k < N / 2; ++k)
    {
        std::complex<double> t = std::polar(1.0, -2.0 * M_PI * k / N) * odd[k];
        x[k] = even[k] + t;
        x[k + N / 2] = even[k] - t;
    }
}

std::vector<float> SimpleFFT::analyze(const float *samples, size_t count, int num_bands)
{
    // Ensure power of 2
    size_t fft_size = 1;
    while (fft_size < count)
        fft_size *= 2;
    fft_size = std::min(fft_size, (size_t)2048); // Cap at 2048

    // Convert to complex
    std::vector<std::complex<double>> data(fft_size);
    for (size_t i = 0; i < fft_size; ++i)
    {
        if (i < count)
        {
            // Apply Hann window
            double window = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (fft_size - 1)));
            data[i] = std::complex<double>(samples[i] * window, 0.0);
        }
        else
        {
            data[i] = std::complex<double>(0.0, 0.0);
        }
    }

    // Perform FFT
    fft(data);

    // Calculate magnitudes and group into bands
    std::vector<float> magnitudes(num_bands, 0.0f);
    size_t bins_per_band = (fft_size / 2) / num_bands;

    for (int band = 0; band < (int)num_bands; ++band)
    {
        float sum = 0.0f;
        size_t start = band * bins_per_band;
        size_t end = start + bins_per_band;

        for (size_t i = start; i < end && i < fft_size / 2; ++i)
        {
            float magnitude = std::abs(data[i]);
            sum += magnitude;
        }

        magnitudes[band] = sum / bins_per_band;
    }

    // Normalize
    float max_val = 0.0001f; // Avoid division by zero
    for (float mag : magnitudes)
    {
        if (mag > max_val)
            max_val = mag;
    }

    for (float &mag : magnitudes)
    {
        mag = mag / max_val;
    }

    return magnitudes;
}

void SimpleFFT::calculate_bands(const std::vector<float> &magnitudes,
                                float &bass, float &mid, float &treble)
{
    size_t num_bands = magnitudes.size();

    // Bass: 20-250 Hz (roughly first 20% of spectrum)
    bass = 0.0f;
    size_t bass_end = num_bands / 5;
    for (size_t i = 0; i < bass_end; ++i)
    {
        bass += magnitudes[i];
    }
    bass /= bass_end;

    // Mid: 250-2000 Hz (roughly 20-50% of spectrum)
    mid = 0.0f;
    size_t mid_start = bass_end;
    size_t mid_end = num_bands / 2;
    for (size_t i = mid_start; i < mid_end; ++i)
    {
        mid += magnitudes[i];
    }
    mid /= (mid_end - mid_start);

    // Treble: 2000+ Hz (roughly 50-100% of spectrum)
    treble = 0.0f;
    size_t treble_start = mid_end;
    for (size_t i = treble_start; i < num_bands; ++i)
    {
        treble += magnitudes[i];
    }
    treble /= (num_bands - treble_start);
}
