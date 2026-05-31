#pragma once
/*
 * algorithms.hpp
 * ──────────────
 * One function per algorithm, unified signature:
 *
 *   void algo(std::vector<int>& data);
 *
 * The benchmarker passes a fresh copy each run so in-place sorts don't
 * gain an unfair advantage on subsequent runs.
 *
 * Complexity classes covered
 * ──────────────────────────
 *   O(1)        constantAccess
 *   O(log n)    binarySearch
 *   O(n)        linearSearch, prefixSum
 *   O(n log n)  mergeSort, heapSort
 *   O(n²)       bubbleSort, selectionSort, insertionSort
 *   O(n³)       matrixMultiply
 */

#include <vector>
#include <string>
#include <functional>

// Unified algorithm signature
using AlgoFn = std::function<void(std::vector<int>&)>;

// Metadata for one algorithm entry
struct AlgoMeta {
    std::string name;
    std::string complexity;   // Big-O label
    std::string color;        // hex for dashboard
    std::string category;
    AlgoFn      fn;
};

// ── Algorithm implementations (defined in algorithms.cpp) ────────────────────
void constantAccess  (std::vector<int>& data);
void binarySearch    (std::vector<int>& data);
void linearSearch    (std::vector<int>& data);
void prefixSum       (std::vector<int>& data);
void mergeSort       (std::vector<int>& data);
void heapSort        (std::vector<int>& data);
void insertionSort   (std::vector<int>& data);
void selectionSort   (std::vector<int>& data);
void bubbleSort      (std::vector<int>& data);
void matrixMultiply  (std::vector<int>& data);

// Registry — iterate this to benchmark all algorithms.
// To add your own: implement void myAlgo(std::vector<int>&), push AlgoMeta{...} here, rebuild, run ./algoscope --out x.json, load x.json in dashboard.html.
const std::vector<AlgoMeta>& getAlgorithms();

// Generate benchmark input of size n
// kind: "random" | "sorted" | "reverse"
std::vector<int> generateInput(int n, const std::string& kind = "random");
