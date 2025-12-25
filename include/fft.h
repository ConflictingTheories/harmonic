// fft.h - Simple FFT implementation (no external dependencies)
#ifndef FFT_H
#define FFT_H

#include <vector>
#include <complex>
#include <cmath>

class SimpleFFT {
public:
    // Cooley-Tukey FFT algorithm
    static void fft(std::vector<std::complex<double>>& x);
    
    // Convert audio samples to frequency magnitudes
    static std::vector<float> analyze(const float* samples, size_t count, int num_bands = 64);
    
    // Calculate bass, mid, treble from magnitudes
    static void calculate_bands(const std::vector<float>& magnitudes, 
                                float& bass, float& mid, float& treble);
};

#endif // FFT_H