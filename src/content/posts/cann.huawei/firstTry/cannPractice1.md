---
title: 华为昇腾CANN算子开发实战：从Host到Device的向量减法实现
published: 2026-05-19
tags: [CANN, 华为昇腾, 算子开发, AI, AscendC, C++]
pinned: true
category: 技术博客
draft: false
---

# 华为昇腾CANN算子开发实战：从Host到Device的向量减法实现

> 本文通过一个完整的向量减法（`z = x - y`）算子代码，深入讲解基于华为昇腾CANN的算子开发流程。从AscendCL的Host侧内存管理，到AscendC的核函数与流水线并行设计，带你一步步理解昇腾AI芯片的编程模型。

## 1. 背景与目标

在昇腾AI处理器的开发中，**算子（Operator）** 是计算的核心单元。CANN（Compute Architecture for Neural Networks）提供了AscendCL（Host侧）和AscendC（Device侧）两套编程接口。本文的目标是：

- 实现一个向量减法算子 `sub_custom`，支持 `z[i] = x[i] - y[i]`。
- 利用昇腾芯片的 **AI Core** 进行并行计算。
- 采用 **TPipe + TQue** 实现流水线数据处理，提升吞吐。

## 2. 整体架构概览

代码结构上分为三个层级：

| 层级 | 文件/模块 | 作用 |
|------|-----------|------|
| Host 主程序 | `main()` | 初始化环境，分配内存，调用核函数，验证结果 |
| Host 调用层 | `kernel_sub()` | 封装AscendCL API，完成Host↔Device数据搬运 |
| Device 核函数 | `sub_custom` + `KernelSub` | 实际在AI Core上执行的并行计算逻辑 |

整个流程如下：

```
Host (CPU) → aclrtMalloc → 搬运数据 → 核函数启动 → 同步 → 回拷结果 → Host
                            ↘  Device (AI Core) 执行 sub_custom
```

## 3. Host 侧：AscendCL 环境与内存管理

Host 侧的 `kernel_sub` 函数完成了标准的 “初始化 → 分配 → 搬运 → 启动 → 同步 → 回拷 → 释放” 闭环。

**关键代码段：**

```cpp
aclInit(nullptr);
aclrtSetDevice(0);
aclrtCreateStream(&stream);

// 分配Device内存
aclrtMalloc(&xDevice, totalByteSize, ACL_MEM_MALLOC_HUGE_FIRST);
aclrtMalloc(&yDevice, totalByteSize, ACL_MEM_MALLOC_HUGE_FIRST);
aclrtMalloc(&zDevice, totalByteSize, ACL_MEM_MALLOC_HUGE_FIRST);

// 数据搬运 HOST → DEVICE
aclrtMemcpy(xDevice, totalByteSize, xHost, totalByteSize, ACL_MEMCPY_HOST_TO_DEVICE);
aclrtMemcpy(yDevice, totalByteSize, yHost, totalByteSize, ACL_MEMCPY_HOST_TO_DEVICE);

// 启动核函数
sub_custom<<<blockDim, nullptr, stream>>>(xDevice, yDevice, zDevice, tiling);
aclrtSynchronizeStream(stream);

// 结果回拷 DEVICE → HOST
aclrtMemcpy(zHost, totalByteSize, zDevice, totalByteSize, ACL_MEMCPY_DEVICE_TO_HOST);
```

> 💡 **Tips**：`aclrtSynchronizeStream` 是关键同步点，必须确保核函数完成后再回拷数据，否则结果错误。

## 4. Device 侧：AscendC 核函数与流水线设计

昇腾的核函数采用了 **“多核并行 + 流水线分块”** 的编程模型。

### 4.1 多核数据切分

```cpp
this->blockLength = totalLength / AscendC::GetBlockNum();
```

每个核处理的数据范围由 `totalLength / blockDim` 确定，`GetBlockIdx()` 获取当前核的编号，实现无冲突的并行。

### 4.2 流水线并行（TPipe + TQue）

核心思路：**将数据搬运（CopyIn）、计算（Compute）、结果搬出（CopyOut）重叠进行**，避免计算单元等待数据。

- **TPipe**：内存管理管道，负责管理 LocalMemory 中的缓冲区。
- **TQue**：队列，承载数据流。我们使用两个 **VECIN** 队列（输入）和一个 **VECOUT** 队列（输出）。

```cpp
AscendC::TPipe pipe;
AscendC::TQue<AscendC::TPosition::VECIN, BUFFER_NUM> inQueueX, inQueueY;
AscendC::TQue<AscendC::TPosition::VECOUT, BUFFER_NUM> outQueueZ;
```

**三步循环流程：**

1. **CopyIn**：从 GlobalMemory 搬运数据到 LocalTensor
   ```cpp
   AscendC::LocalTensor<float> xLocal = inQueueX.AllocTensor<float>();
   AscendC::DataCopy(xLocal, xGm[progress * tileLength], tileLength);
   inQueueX.EnQue(xLocal);
   ```

2. **Compute**：从队列取出数据，计算后放入输出队列
   ```cpp
   AscendC::LocalTensor<float> xLocal = inQueueX.DeQue<float>();
   AscendC::LocalTensor<float> zLocal = outQueueZ.AllocTensor<float>();
   AscendC::Sub(zLocal, xLocal, yLocal, tileLength);
   outQueueZ.EnQue(zLocal);
   inQueueX.FreeTensor(xLocal);
   ```

3. **CopyOut**：将结果写回 GlobalMemory
   ```cpp
   AscendC::LocalTensor<float> zLocal = outQueueZ.DeQue<float>();
   AscendC::DataCopy(zGm[progress * tileLength], zLocal, tileLength);
   outQueueZ.FreeTensor(zLocal);
   ```

这种设计使得数据搬运和计算可以**流水线并行**，极大提升硬件利用率。

## 5. 详细代码逐行解析

### 5.1 自定义分块数据结构

```cpp
struct SubCustomTilingData {
    uint32_t totalLength;
    uint32_t tileNum;
};
```
用于向核函数传递总长度和分块数量。

### 5.2 核函数入口

```cpp
__global__ __aicore__ void sub_custom(GM_ADDR x, GM_ADDR y, GM_ADDR z, SubCustomTilingData tiling) {
    KERNEL_TASK_TYPE_DEFAULT(KERNEL_TYPE_AIV_ONLY);
    KernelSub kernelSub;
    kernelSub.Init(x, y, z, tiling.totalLength, tiling.tileNum);
    kernelSub.Process();
}
```
`KERNEL_TASK_TYPE_DEFAULT` 指定任务类型，`Init` 完成初始化，`Process` 启动流水线。

### 5.3 验证与测试

```cpp
std::vector<float> golden(totalLength, valueX - valueY);
return VerifyResult(output, golden);
```
构造 Golden 结果并对比，输出成功或失败信息。


## 6. 关键知识点总结

| 概念 | 说明 |
|------|------|
| **GM_ADDR** | Global Memory 地址类型，由 Host 传入 |
| **LocalTensor** | 位于 LocalMemory 的片上数据块，计算核心操作对象 |
| **TPipe** | 管理片上缓冲区（LocalMemory）的生命周期 |
| **TQue** | 队列，支持 `AllocTensor` → `EnQue` → `DeQue` → `FreeTensor` 模式 |
| **DataCopy** | 在 GlobalMemory 和 LocalMemory 之间搬运数据 |
| **AscendC::Sub** | 向量减法内置指令（SVE 向量化） |





**参考文献**：
- 华为昇腾 CANN 官方文档 (v6.3.0+)
- 《Ascend C 编程指南》
- 《AscendCL 接口参考》

