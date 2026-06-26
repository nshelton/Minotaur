#pragma once

#include <cstddef>
#include <functional>
#include <thread>
#include <vector>

// Minimal parallel_for utility for C++17.
// Apple Clang lacks std::execution::par, so this uses std::thread directly.
// On macOS with Apple Silicon, dispatch_apply (GCD) could be used instead,
// but std::thread is portable and sufficient for our workloads.
//
// Usage:
//   parallel_for(0, vertices.size(), [&](size_t i) {
//       transformed[i] = transform(vertices[i]);
//   });

namespace parallel {

// Minimum items per thread to avoid overhead dominating
static constexpr size_t kMinGrainSize = 256;

inline void parallel_for(size_t begin, size_t end, const std::function<void(size_t)> &body)
{
	if (begin >= end) return;

	size_t count = end - begin;
	unsigned numThreads = std::thread::hardware_concurrency();
	if (numThreads == 0) numThreads = 4;

	// Don't spawn threads for small workloads
	if (count < kMinGrainSize * 2 || numThreads <= 1)
	{
		for (size_t i = begin; i < end; ++i)
			body(i);
		return;
	}

	// Cap threads to avoid excessive overhead
	size_t grainSize = (count + numThreads - 1) / numThreads;
	if (grainSize < kMinGrainSize)
	{
		grainSize = kMinGrainSize;
		numThreads = static_cast<unsigned>((count + grainSize - 1) / grainSize);
	}

	std::vector<std::thread> threads;
	threads.reserve(numThreads - 1);

	for (unsigned t = 1; t < numThreads; ++t)
	{
		size_t tBegin = begin + t * grainSize;
		size_t tEnd = std::min(tBegin + grainSize, end);
		if (tBegin >= end) break;
		threads.emplace_back([&body, tBegin, tEnd]() {
			for (size_t i = tBegin; i < tEnd; ++i)
				body(i);
		});
	}

	// Run first chunk on calling thread
	size_t firstEnd = std::min(begin + grainSize, end);
	for (size_t i = begin; i < firstEnd; ++i)
		body(i);

	for (auto &t : threads)
		t.join();
}

// Parallel for with chunked callback (avoids per-element function call overhead)
inline void parallel_for_chunks(size_t begin, size_t end,
	const std::function<void(size_t, size_t)> &body)
{
	if (begin >= end) return;

	size_t count = end - begin;
	unsigned numThreads = std::thread::hardware_concurrency();
	if (numThreads == 0) numThreads = 4;

	if (count < kMinGrainSize * 2 || numThreads <= 1)
	{
		body(begin, end);
		return;
	}

	size_t grainSize = (count + numThreads - 1) / numThreads;
	if (grainSize < kMinGrainSize)
	{
		grainSize = kMinGrainSize;
		numThreads = static_cast<unsigned>((count + grainSize - 1) / grainSize);
	}

	std::vector<std::thread> threads;
	threads.reserve(numThreads - 1);

	for (unsigned t = 1; t < numThreads; ++t)
	{
		size_t tBegin = begin + t * grainSize;
		size_t tEnd = std::min(tBegin + grainSize, end);
		if (tBegin >= end) break;
		threads.emplace_back([&body, tBegin, tEnd]() {
			body(tBegin, tEnd);
		});
	}

	size_t firstEnd = std::min(begin + grainSize, end);
	body(begin, firstEnd);

	for (auto &t : threads)
		t.join();
}

} // namespace parallel
