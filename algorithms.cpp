/*
 * algorithms.cpp
 * ──────────────
 * Full C++ implementations of every benchmarked algorithm.
 */

#include "algorithms.hpp"

#include <algorithm>   // std::sort, std::make_heap, std::pop_heap
#include <numeric>     // std::iota
#include <random>
#include <cmath>

// ════════════════════════════════════════════════════════════════════════════
//  O(1) — Constant
// ════════════════════════════════════════════════════════════════════════════

void constantAccess(std::vector<int>& data) {
    // Just read the first element — size doesn't matter
    volatile int x = data.empty() ? 0 : data[0];
    (void)x;
}

// ════════════════════════════════════════════════════════════════════════════
//  O(log n) — Logarithmic
// ════════════════════════════════════════════════════════════════════════════

void binarySearch(std::vector<int>& data) {
    // Sort a copy, then search for the median element
    std::vector<int> arr = data;
    std::sort(arr.begin(), arr.end());

    int target = arr[arr.size() / 2];
    int lo = 0, hi = (int)arr.size() - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if      (arr[mid] == target) break;
        else if (arr[mid]  < target) lo = mid + 1;
        else                          hi = mid - 1;
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  O(n) — Linear
// ════════════════════════════════════════════════════════════════════════════

void linearSearch(std::vector<int>& data) {
    // Full scan to find maximum — touches every element once
    volatile int maxVal = data.empty() ? 0 : data[0];
    for (int x : data)
        if (x > maxVal) maxVal = x;
}

void prefixSum(std::vector<int>& data) {
    // Build prefix-sum array in-place
    for (std::size_t i = 1; i < data.size(); ++i)
        data[i] += data[i - 1];
}

// ════════════════════════════════════════════════════════════════════════════
//  O(n log n) — Linearithmic
// ════════════════════════════════════════════════════════════════════════════

// Internal recursive merge
static void mergeSortHelper(std::vector<int>& arr, int lo, int hi) {
    if (hi - lo <= 1) return;
    int mid = lo + (hi - lo) / 2;
    mergeSortHelper(arr, lo, mid);
    mergeSortHelper(arr, mid, hi);

    std::vector<int> tmp;
    tmp.reserve(hi - lo);
    int i = lo, j = mid;
    while (i < mid && j < hi)
        tmp.push_back(arr[i] <= arr[j] ? arr[i++] : arr[j++]);
    while (i < mid) tmp.push_back(arr[i++]);
    while (j < hi)  tmp.push_back(arr[j++]);
    std::copy(tmp.begin(), tmp.end(), arr.begin() + lo);
}

void mergeSort(std::vector<int>& data) {
    mergeSortHelper(data, 0, (int)data.size());
}

void heapSort(std::vector<int>& data) {
    // STL heap — guaranteed O(n log n)
    std::make_heap(data.begin(), data.end());
    std::sort_heap(data.begin(), data.end());
}

// ════════════════════════════════════════════════════════════════════════════
//  O(n²) — Quadratic
// ════════════════════════════════════════════════════════════════════════════

void bubbleSort(std::vector<int>& data) {
    int n = (int)data.size();
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n - i - 1; ++j)
            if (data[j] > data[j + 1])
                std::swap(data[j], data[j + 1]);
}

void selectionSort(std::vector<int>& data) {
    int n = (int)data.size();
    for (int i = 0; i < n; ++i) {
        int minIdx = i;
        for (int j = i + 1; j < n; ++j)
            if (data[j] < data[minIdx]) minIdx = j;
        std::swap(data[i], data[minIdx]);
    }
}

void insertionSort(std::vector<int>& data) {
    int n = (int)data.size();
    for (int i = 1; i < n; ++i) {
        int key = data[i];
        int j   = i - 1;
        while (j >= 0 && data[j] > key) {
            data[j + 1] = data[j];
            --j;
        }
        data[j + 1] = key;
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  O(n³) — Cubic  (uses sqrt(len) × sqrt(len) matrix from flat array)
// ════════════════════════════════════════════════════════════════════════════

void matrixMultiply(std::vector<int>& data) {
    int n = std::max(1, (int)std::sqrt((double)data.size()));
    // Build two n×n matrices A, B from the flat data (cycle if needed)
    std::vector<std::vector<int>> A(n, std::vector<int>(n));
    std::vector<std::vector<int>> B(n, std::vector<int>(n));
    std::vector<std::vector<int>> C(n, std::vector<int>(n, 0));

    int sz = (int)data.size();
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            A[i][j] = data[(i * n + j)       % sz];
            B[i][j] = data[(i * n + j + 1)   % sz];
        }

    // Naïve triple-loop multiplication
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            for (int k = 0; k < n; ++k)
                C[i][j] += A[i][k] * B[k][j];

    // Write result back so the compiler can't dead-code-eliminate the loop
    for (int i = 0; i < n && i < sz; ++i)
        data[i] = C[i % n][i % n];
}

// ════════════════════════════════════════════════════════════════════════════
//  Algorithm Registry
// ════════════════════════════════════════════════════════════════════════════

const std::vector<AlgoMeta>& getAlgorithms() {
    static const std::vector<AlgoMeta> registry = {
        {"Constant Access", "O(1)",       "#06d6a0", "Search",  constantAccess},
        {"Binary Search",   "O(log n)",   "#118ab2", "Search",  binarySearch  },
        {"Linear Search",   "O(n)",       "#ffd166", "Search",  linearSearch  },
        {"Prefix Sum",      "O(n)",       "#f4a261", "Array",   prefixSum     },
        {"Merge Sort",      "O(n log n)", "#7b2d8b", "Sorting", mergeSort     },
        {"Heap Sort",       "O(n log n)", "#9b5de5", "Sorting", heapSort      },
        {"Insertion Sort",  "O(n^2)",     "#ef476f", "Sorting", insertionSort },
        {"Selection Sort",  "O(n^2)",     "#e63946", "Sorting", selectionSort },
        {"Bubble Sort",     "O(n^2)",     "#ff6b6b", "Sorting", bubbleSort    },
        {"Matrix Multiply", "O(n^3)",     "#ff4d4d", "Matrix",  matrixMultiply},
    };
    return registry;
}

// ════════════════════════════════════════════════════════════════════════════
//  Input Generator
// ════════════════════════════════════════════════════════════════════════════

std::vector<int> generateInput(int n, const std::string& kind) {
    std::vector<int> v(n);
    std::iota(v.begin(), v.end(), 0);     // 0, 1, 2, …, n-1

    if (kind == "random") {
        static std::mt19937 rng(42);      // fixed seed → reproducible
        std::shuffle(v.begin(), v.end(), rng);
    } else if (kind == "reverse") {
        std::reverse(v.begin(), v.end());
    }
    // "sorted" → already in order
    return v;
}
