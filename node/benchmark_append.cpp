#include <vector>
#include <cstdint>
#include <chrono>
#include <iostream>

// Original implementations
void append_u16_original(std::vector<uint8_t> &out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void append_u64_original(std::vector<uint8_t> &out, uint64_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 32) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 40) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 48) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 56) & 0xFF));
}

// Optimized implementations using resize
void append_u16_optimized(std::vector<uint8_t> &out, uint16_t value) {
    size_t old_size = out.size();
    out.resize(old_size + 2);
    out[old_size] = static_cast<uint8_t>(value & 0xFF);
    out[old_size + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void append_u64_optimized(std::vector<uint8_t> &out, uint64_t value) {
    size_t old_size = out.size();
    out.resize(old_size + 8);
    out[old_size] = static_cast<uint8_t>(value & 0xFF);
    out[old_size + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    out[old_size + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    out[old_size + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
    out[old_size + 4] = static_cast<uint8_t>((value >> 32) & 0xFF);
    out[old_size + 5] = static_cast<uint8_t>((value >> 40) & 0xFF);
    out[old_size + 6] = static_cast<uint8_t>((value >> 48) & 0xFF);
    out[old_size + 7] = static_cast<uint8_t>((value >> 56) & 0xFF);
}

int main() {
    const int iterations = 10000000;

    // U16 Benchmark
    std::cout << "--- U16 ---\n";
    {
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            std::vector<uint8_t> v;
            append_u16_original(v, 0x1234);
            append_u16_original(v, 0x5678);
            append_u16_original(v, 0x9ABC);
            append_u16_original(v, 0xDEF0);
        }
        auto end = std::chrono::high_resolution_clock::now();
        std::cout << "Original (u16): "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
                  << " ms\n";
    }

    {
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            std::vector<uint8_t> v;
            append_u16_optimized(v, 0x1234);
            append_u16_optimized(v, 0x5678);
            append_u16_optimized(v, 0x9ABC);
            append_u16_optimized(v, 0xDEF0);
        }
        auto end = std::chrono::high_resolution_clock::now();
        std::cout << "Optimized (u16): "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
                  << " ms\n";
    }

    // U64 Benchmark
    std::cout << "\n--- U64 ---\n";
    {
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            std::vector<uint8_t> v;
            append_u64_original(v, 0x1234567890ABCDEF);
            append_u64_original(v, 0x1234567890ABCDEF);
        }
        auto end = std::chrono::high_resolution_clock::now();
        std::cout << "Original (u64): "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
                  << " ms\n";
    }

    {
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            std::vector<uint8_t> v;
            append_u64_optimized(v, 0x1234567890ABCDEF);
            append_u64_optimized(v, 0x1234567890ABCDEF);
        }
        auto end = std::chrono::high_resolution_clock::now();
        std::cout << "Optimized (u64): "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
                  << " ms\n";
    }

    return 0;
}
