# Dynamic Memory Allocator

A custom implementation of `malloc` and `free` in C, built as part of UT Austin's CS 429 - Computer Organization and Architecture course.

## Overview

This project implements a high-performance dynamic memory allocator that manages heap memory efficiently through segregated free lists and deferred coalescing.

## Implementation Highlights

- **Segregated Free Lists**: 6 size-class bins (48, 128, 512, 2048, 4096+ bytes) for fast allocation
- **First-Fit Placement**: Searches size-ordered bins for quick block selection
- **Deferred Coalescing**: Merges adjacent free blocks only when necessary to reduce overhead
- **Strategic Splitting**: Minimizes fragmentation with 32-byte minimum block size

## Performance Results

- **Space Utilization**: 76% (target: 72.5%)
- **Throughput**: 11,000+ operations/millisecond (target: 8,500)
- **Correctness**: Passes 36/36 test cases including:
  - 20MB large allocations
  - 20,000-operation stress tests
  - Edge cases (zero-byte allocations, fragmentation patterns)

## Files

- `umalloc.c` - Core allocator implementation
- `umalloc.h` - Header file with data structures and function declarations

## Key Technical Decisions

**Bin Sizing**: Boundaries chosen based on empirical trace analysis to balance search speed and fragmentation.

**Deferred Coalescing**: Only triggers when allocation fails, reducing per-operation overhead while maintaining low fragmentation.

**Chunk Management**: Tracks non-contiguous memory regions from system calls to enable safe coalescing across the entire heap.

