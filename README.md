# LLVM Loop Fusion and Loop Fission Passes

## Overview

This project implements two custom optimization passes for the LLVM compiler infrastructure:

- **Loop Fusion**
- **Loop Fission**

Both passes operate on LLVM Intermediate Representation (IR) and demonstrate loop transformation techniques that can improve program performance under suitable conditions.

---

## Loop Fusion

Loop Fusion combines two adjacent loops that iterate over the same iteration space into a single loop.

### Example

Before:

```c
for (int i = 0; i < n; i++) {
    A[i] = A[i] + 1;
}

for (int i = 0; i < n; i++) {
    B[i] = B[i] * 2;
}
```

After:

```c
for (int i = 0; i < n; i++) {
    A[i] = A[i] + 1;
    B[i] = B[i] * 2;
}
```

### Benefits

- Reduces loop overhead
- Improves cache locality
- Decreases instruction count
- May increase opportunities for further compiler optimizations

The pass checks whether two consecutive loops have compatible iteration bounds before attempting to fuse them.

---

## Loop Fission

Loop Fission (also known as Loop Distribution) splits a loop into multiple loops by separating independent computations.

### Example

Before:

```c
for (int i = 0; i < n; i++) {
    if (A[i] > 0)
        A[i]++;
    else
        B[i]++;
}
```

After:

```c
for (int i = 0; i < n; i++) {
    if (A[i] > 0)
        A[i]++;
}

for (int i = 0; i < n; i++) {
    if (A[i] <= 0)
        B[i]++;
}
```

### Benefits

- Improves instruction-level parallelism
- Simplifies loop bodies
- Enables additional optimizations such as vectorization
- Reduces branch complexity inside individual loops

---

## Project Structure

```
LoopFusionPass.cpp     - LLVM implementation of Loop Fusion
LoopFissionPass.cpp    - LLVM implementation of Loop Fission
testFusion.c           - Test program for Loop Fusion
testFission.c          - Test program for Loop Fission
```

---

## Requirements

- LLVM 14
- Clang 14
- CMake
- Make

---

## Building

```bash
mkdir build
cd build

cmake ..
make
```

---

## Running

Loop Fusion:

```bash
opt -load ./lib/LLVMLoopFusionPass.so \
-enable-new-pm=0 \
-our-loop-fusion input.ll -S -o output.ll
```

Loop Fission:

```bash
opt -load ./lib/LLVMLoopFissionPass.so \
-enable-new-pm=0 \
-our-loop-fission input.ll -S -o output.ll
```

---
## Team

This project was developed as part of a university project by:

- Milena Mirković
- Sara Gojaković
