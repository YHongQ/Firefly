---
title: 华为昇腾CANN算子开发：泛化Tiling策略设计深度解析
published: 2026-05-21
tags: [CANN, 华为昇腾, 算子开发, AI, AscendC, C++, Tiling]
pinned: false
category: 技术博客
draft: true
---


在学习华为Ascend C算子开发过程中，泛化Tiling设计是最核心也是最复杂的部分。本文将从内存对齐要求和多核负载均衡的角度，详细解析泛化Tiling策略的设计逻辑。

## 一、Tiling的本质问题

在深入设计之前，首先要理解为什么需要Tiling。昇腾AI处理器的AI Core内部有一个有限容量的Unified Buffer（UB），通常只有几百KB。当处理大规模数据时，无法一次性将所有输入数据搬运到UB中进行计算。因此，必须将大数据切分成多个小块，分批次搬运到UB中计算，这个过程就是Tiling。

Tiling设计的核心挑战在于：如何在硬件约束下，实现数据的高效切分和调度，既要保证计算正确性，又要最大化性能。

## 二、硬件约束下的设计原则

### 2.1 内存对齐原则

昇腾AI处理器的Unified Buffer在物理层面存在严格的存储约束：**所有数据必须保持32字节(现在是这样要求，至于其他型号，暂时还未了解到)对齐**。这个约束直接影响Tiling策略的设计。

**相当于一个block最小单位是32字节。它作为划分数据的最小单位，所有数据都必须保持32字节对齐。如果数据大小不能被32整除，需要向上对齐。**

**为什么需要32字节对齐？**

从硬件架构角度看，AI Core的访存单元以32字节为一个基本访问粒度。如果数据不满足对齐要求，会导致：
- 访存效率降低
- 可能产生硬件异常
- 无法充分利用带宽

**对齐处理的具体实现：**

假设输入数据shape为(1,660)，数据类型为half（每个元素2字节），原始数据大小为：
```
660 × 2 = 1320字节
```

由于1320不能被32整除，需要向上对齐：
```cpp
uint32_t BLOCK_SIZE = 32;
uint32_t inputLengthAlgin32 = (((inputLength + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE);
```

计算结果：
```
(1320 + 31) // 32 × 32 = 42 × 32 = 1344字节
```

这样，原本660个元素的数据，在对齐后相当于672个元素（42个32字节块）。

**设计思考：**

对齐处理带来的一个重要影响是：所有后续的Tiling计算都必须以32字节为最小单位。这意味着：
- 核间分配时，每个核至少分配1个32字节块
- 核内切分时，每次搬运的数据量必须是32字节的整数倍
- 尾块处理时，也要保证32字节对齐

### 2.2 访存优化原则

AI Core与Global Memory之间的数据搬运是性能瓶颈所在。因此，Tiling设计的第二个核心原则是：**最大化单次搬运的数据量，减少搬运次数**。

**UB空间的合理分配：**

对于Add算子，需要同时处理两个输入（x、y）和一个输出（z），这意味着UB空间需要被分成3部分：
```cpp
uint32_t ubDataNumber = 3;  // 2个输入 + 1个输出
uint32_t tileBlockNum = (ubSize / BLOCK_SIZE) / ubDataNumber;
```

假设UB总大小为768字节，则：
```
单个Buffer最大容量 = 768 / 3 = 256字节
对应的32字节块数 = 256 / 32 = 8块
```

**设计思考：**

这种设计体现了"空间换时间"的优化思想：
- 每个Buffer尽可能大，减少搬运次数
- 但不能超过UB容量，避免内存溢出
- 需要考虑所有输入输出的空间需求

### 2.3 多核负载均衡原则

昇腾AI处理器集成了多个AI Core，为了充分利用硬件算力，必须将计算任务均匀分配到各个核上。这是Tiling设计的第三个核心原则。

**核间分配策略：**

将42个32字节块分配到4个AI Core上：
```cpp
uint32_t everyCoreInputBlockNum = inputLengthAlgin32 / BLOCK_SIZE / coreNum;
uint32_t tailBlockNum = (inputLengthAlgin32 / BLOCK_SIZE) % coreNum;
```

计算结果：
```
基础分配：42 // 4 = 10块/核
剩余块：42 % 4 = 2块
```

最终分配：
- 前2个核（大核）：10 + 1 = 11块 = 352字节
- 后2个核（小核）：10块 = 320字节

**设计思考：**

这种"前N核补块"的策略体现了负载均衡的智慧：
- 大核和小核的数据量差异最小（仅差1个32字节块）
- 避免了某些核空闲而其他核过载的情况
- 余数处理简单高效

## 三、Host侧Tiling策略实现

Host侧的Tiling函数是整个策略的核心，它需要综合考虑上述三个原则，计算出所有必要的参数。

### 3.1 核心参数计算

```cpp
// 1. 获取硬件信息
auto ascendcPlatform = platform_ascendc::PlatformAscendC(context->GetPlatformInfo());
auto coreNum = ascendcPlatform.GetCoreNum();

// 2. 获取输入信息
uint32_t inputNum = context->GetInputShape(0)->GetStorageShape().GetShapeSize();
uint32_t typeLength = 0;
ge::TypeUtils::GetDataTypeLength(context->GetInputDesc(0)->GetDataType(), typeLength);
uint32_t inputLength = inputNum * typeLength;

// 3. 32字节对齐
const uint32_t BLOCK_SIZE = 32;
uint32_t inputLengthAlgin32 = (((inputLength + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE);

// 4. 核数调整（确保每个核至少有1个32字节块）
coreNum = std::min(coreNum, inputLengthAlgin32 / BLOCK_SIZE);
coreNum = std::max(coreNum, static_cast<uint32_t>(1));

// 5. 核间分配
uint32_t everyCoreInputBlockNum = inputLengthAlgin32 / BLOCK_SIZE / coreNum;
uint32_t tailBlockNum = (inputLengthAlgin32 / BLOCK_SIZE) % coreNum;
```

### 3.2 核内切分计算

```cpp
// 1. 获取UB大小并计算单次搬运量
uint64_t ubSize;
ascendcPlatform.GetCoreMemSize(platform_ascendc::CoreMemType::UB, ubSize);
uint32_t ubDataNumber = 3;
uint32_t tileBlockNum = (ubSize / BLOCK_SIZE) / ubDataNumber;
uint32_t tileDataNum = (tileBlockNum * BLOCK_SIZE) / typeLength;

// 2. 小核参数计算
uint32_t smallCoreDataNum = everyCoreInputBlockNum * BLOCK_SIZE / typeLength;
uint32_t smallTileNum = everyCoreInputBlockNum / tileBlockNum;
uint32_t finalSmallTileNum = (everyCoreInputBlockNum % tileBlockNum) == 0 ? smallTileNum : smallTileNum + 1;
uint32_t smallTailDataNum = smallCoreDataNum - (tileDataNum * smallTileNum);
smallTailDataNum = smallTailDataNum == 0 ? tileDataNum : smallTailDataNum;

// 3. 大核参数计算（多处理1个32字节块）
everyCoreInputBlockNum += 1;
uint32_t bigCoreDataNum = everyCoreInputBlockNum * BLOCK_SIZE / typeLength;
uint32_t bigTileNum = everyCoreInputBlockNum / tileBlockNum;
uint32_t finalBigTileNum = (everyCoreInputBlockNum % tileBlockNum) == 0 ? bigTileNum : bigTileNum + 1;
uint32_t bigTailDataNum = bigCoreDataNum - tileDataNum * bigTileNum;
bigTailDataNum = bigTailDataNum == 0 ? tileDataNum : bigTailDataNum;
```

### 3.3 设计逻辑分析

**小核计算逻辑：**

以处理10个32字节块为例，单次最多处理8个32字节块：
- 循环次数：10 // 8 = 1次完整循环
- 是否需要额外循环：10 % 8 = 2 ≠ 0，需要+1次
- 最终循环次数：1 + 1 = 2次
- 尾块数据量：总数据量 - 完整循环处理的数据量

**大核计算逻辑：**

大核处理11个32字节块：
- 循环次数：11 // 8 = 1次完整循环
- 是否需要额外循环：11 % 8 = 3 ≠ 0，需要+1次
- 最终循环次数：1 + 1 = 2次
- 尾块数据量：3个32字节块

**边界情况处理：**

代码中有一个重要的边界处理：
```cpp
smallTailDataNum = smallTailDataNum == 0 ? tileDataNum : smallTailDataNum;
```

当总数据量正好是单次搬运量的整数倍时，尾块数据量会被设为单次搬运量，避免出现0尾块的异常情况。

## 四、Device侧实现

Device侧的实现需要根据Host侧计算的Tiling参数，完成实际的数据搬运和计算。

### 4.1 核心判断逻辑

```cpp
uint32_t coreNum = AscendC::GetBlockIdx();
if (coreNum < tailBlockNum) {
    // 大核处理逻辑
    this->coreDataNum = bigCoreDataNum;
    this->tileNum = finalBigTileNum;
    this->tailDataNum = bigTailDataNum;
} else {
    // 小核处理逻辑
    this->coreDataNum = smallCoreDataNum;
    this->tileNum = finalSmallTileNum;
    this->tailDataNum = smallTailDataNum;
}
```

### 4.2 数据偏移计算

每个核需要计算自己在全局数据中的偏移位置：
```cpp
uint32_t globalBufferIndex = bigCoreDataNum * AscendC::GetBlockIdx();
// 小核需要调整偏移量
globalBufferIndex -= (bigCoreDataNum - smallCoreDataNum) * (AscendC::GetBlockIdx() - tailBlockNum);
```

这个计算确保了每个核处理的数据段连续且不重叠。

### 4.3 循环处理逻辑

```cpp
int32_t loopCount = this->tileNum;
for (int32_t i = 0; i < loopCount; i++) {
    if (i == this->tileNum - 1) {
        this->processDataNum = this->tailDataNum;  // 最后一次循环处理尾块
    }
    CopyIn(i);
    Compute(i);
    CopyOut(i);
}
```

这个循环体现了Tiling的精髓：将大数据切分成多个小块，分批处理。


泛化Tiling设计是华为算子开发中最具挑战性也最有价值的部分，它体现了对硬件架构的深刻理解和系统设计的智慧。通过学习这个设计，我不仅掌握了具体的实现技巧，更重要的是学会了如何在硬件约束下进行系统性的性能优化设计。

## 附录：源码

### Host侧实现

```cpp
#include "register/op_def_registry.h"
#include "../op_kernel/add_custom_template_tiling.h"
#include "graph/utils/type_utils.h"
#include "tiling/platform/platform_ascendc.h"

namespace optiling {
static ge::graphStatus TilingFunc(gert::TilingContext *context)
{
    auto ascendcPlatform = platform_ascendc::PlatformAscendC(context->GetPlatformInfo());
    auto coreNum = ascendcPlatform.GetCoreNum();

    uint32_t inputNum = context->GetInputShape(0)->GetStorageShape().GetShapeSize();
    uint32_t typeLength = 0;
    ge::TypeUtils::GetDataTypeLength(context->GetInputDesc(0)->GetDataType(), typeLength);
    uint32_t inputLength = inputNum * typeLength;

    const uint32_t BLOCK_SIZE = 32;
    uint32_t inputLengthAlgin32 = (((inputLength + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE);
    coreNum = std::min(coreNum, inputLengthAlgin32 / BLOCK_SIZE);
    coreNum = std::max(coreNum, static_cast<uint32_t>(1));
    uint32_t everyCoreInputBlockNum = inputLengthAlgin32 / BLOCK_SIZE / coreNum;
    uint32_t tailBlockNum = (inputLengthAlgin32 / BLOCK_SIZE) % coreNum;
    context->SetBlockDim(coreNum);

    uint64_t ubSize;
    ascendcPlatform.GetCoreMemSize(platform_ascendc::CoreMemType::UB, ubSize);
    uint32_t ubDataNumber = 3;
    uint32_t tileBlockNum = (ubSize / BLOCK_SIZE) / ubDataNumber;
    uint32_t tileDataNum = (tileBlockNum * BLOCK_SIZE) / typeLength;

    uint32_t smallCoreDataNum = everyCoreInputBlockNum * BLOCK_SIZE / typeLength;
    uint32_t smallTileNum = everyCoreInputBlockNum / tileBlockNum;
    uint32_t finalSmallTileNum = (everyCoreInputBlockNum % tileBlockNum) == 0 ? smallTileNum : smallTileNum + 1;
    uint32_t smallTailDataNum = smallCoreDataNum - (tileDataNum * smallTileNum);
    smallTailDataNum = smallTailDataNum == 0 ? tileDataNum : smallTailDataNum;

    everyCoreInputBlockNum += 1;
    uint32_t bigCoreDataNum = everyCoreInputBlockNum * BLOCK_SIZE / typeLength;
    uint32_t bigTileNum = everyCoreInputBlockNum / tileBlockNum;
    uint32_t finalBigTileNum = (everyCoreInputBlockNum % tileBlockNum) == 0 ? bigTileNum : bigTileNum + 1;
    uint32_t bigTailDataNum = bigCoreDataNum - tileDataNum * bigTileNum;
    bigTailDataNum = bigTailDataNum == 0 ? tileDataNum : bigTailDataNum;

    TilingDataTemplate *tiling = context->GetTilingData<TilingDataTemplate>();
    tiling->smallCoreDataNum = smallCoreDataNum;
    tiling->bigCoreDataNum = bigCoreDataNum;
    tiling->tileDataNum = tileDataNum;
    tiling->smallTailDataNum = smallTailDataNum;
    tiling->bigTailDataNum = bigTailDataNum;
    tiling->finalSmallTileNum = finalSmallTileNum;
    tiling->finalBigTileNum = finalBigTileNum;
    tiling->tailBlockNum = tailBlockNum;

    return ge::GRAPH_SUCCESS;
}
}

namespace ge {
static graphStatus InferShape(gert::InferShapeContext *context)
{
    const gert::Shape *intputShape = context->GetInputShape(0);
    gert::Shape *outputShape = context->GetOutputShape(0);
    *outputShape = *intputShape;
    return GRAPH_SUCCESS;
}

static graphStatus InferDataType(gert::InferDataTypeContext *context)
{
    context->SetOutputDataType(0, context->GetInputDataType(0));
    return ge::GRAPH_SUCCESS;
}
}

namespace ops {
class AddCustomTemplate : public OpDef {
public:
    explicit AddCustomTemplate(const char *name) : OpDef(name)
    {
        this->Input("x")
            .ParamType(REQUIRED)
            .DataType({ge::DT_FLOAT16, ge::DT_FLOAT})
            .Format({ge::FORMAT_ND, ge::FORMAT_ND});
        this->Input("y")
            .ParamType(REQUIRED)
            .DataType({ge::DT_FLOAT16, ge::DT_FLOAT})
            .Format({ge::FORMAT_ND, ge::FORMAT_ND});
        this->Output("z")
            .ParamType(REQUIRED)
            .DataType({ge::DT_FLOAT16, ge::DT_FLOAT})
            .Format({ge::FORMAT_ND, ge::FORMAT_ND});

        this->SetInferShape(ge::InferShape).SetInferDataType(ge::InferDataType);
        this->AICore()
            .SetTiling(optiling::TilingFunc)
            .AddConfig("ascend910b");
    }
};
OP_ADD(AddCustomTemplate);
}
```

### Tiling结构体定义

```cpp
#ifndef ADD_CUSTOM_TEMPLATE_TILING_H
#define ADD_CUSTOM_TEMPLATE_TILING_H
#include <cstdint>

struct TilingDataTemplate {
    uint32_t smallCoreDataNum;
    uint32_t bigCoreDataNum;
    uint32_t finalBigTileNum;
    uint32_t finalSmallTileNum;
    uint32_t tileDataNum;
    uint32_t smallTailDataNum;
    uint32_t bigTailDataNum;
    uint32_t tailBlockNum;
};
#endif
```

### Device侧实现

```cpp
#include "kernel_operator.h"
#include "add_custom_template_tiling.h"
#include "kernel_operator_dump_tensor_intf_impl.h"

constexpr int32_t BUFFER_NUM = 1;

template<typename TYPE_X, typename TYPE_Y, typename TYPE_Z>
class KernelAdd {
public:
    __aicore__ inline KernelAdd() {}

    __aicore__ inline void Init(GM_ADDR x, GM_ADDR y, GM_ADDR z, uint32_t smallCoreDataNum,
                                uint32_t bigCoreDataNum, uint32_t finalBigTileNum,
                                uint32_t finalSmallTileNum, uint32_t tileDataNum,
                                uint32_t smallTailDataNum, uint32_t bigTailDataNum,
                                uint32_t tailBlockNum)
    {
        uint32_t coreNum = AscendC::GetBlockIdx();
        uint32_t globalBufferIndex = bigCoreDataNum * AscendC::GetBlockIdx();
        this->tileDataNum = tileDataNum;

        if (coreNum < tailBlockNum) {
            this->coreDataNum = bigCoreDataNum;
            this->tileNum = finalBigTileNum;
            this->tailDataNum = bigTailDataNum;
        } else {
            this->coreDataNum = smallCoreDataNum;
            this->tileNum = finalSmallTileNum;
            this->tailDataNum = smallTailDataNum;
            globalBufferIndex -= (bigCoreDataNum - smallCoreDataNum) * (AscendC::GetBlockIdx() - tailBlockNum);
        }

        xGm.SetGlobalBuffer((__gm__ TYPE_X*)x + globalBufferIndex, this->coreDataNum);
        yGm.SetGlobalBuffer((__gm__ TYPE_Y*)y + globalBufferIndex, this->coreDataNum);
        zGm.SetGlobalBuffer((__gm__ TYPE_Z*)z + globalBufferIndex, this->coreDataNum);

        pipe.InitBuffer(inQueueX, BUFFER_NUM, this->tileDataNum * sizeof(TYPE_X));
        pipe.InitBuffer(inQueueY, BUFFER_NUM, this->tileDataNum * sizeof(TYPE_Y));
        pipe.InitBuffer(outQueueZ, BUFFER_NUM, this->tileDataNum * sizeof(TYPE_Z));
    }

    __aicore__ inline void Process()
    {
        int32_t loopCount = this->tileNum;
        this->processDataNum = this->tileDataNum;
        for (int32_t i = 0; i < loopCount; i++) {
            if (i == this->tileNum - 1) {
                this->processDataNum = this->tailDataNum;
            }
            CopyIn(i);
            Compute(i);
            CopyOut(i);
        }
    }

private:
    __aicore__ inline void CopyIn(int32_t progress)
    {
        AscendC::LocalTensor<TYPE_X> xLocal = inQueueX.AllocTensor<TYPE_X>();
        AscendC::LocalTensor<TYPE_Y> yLocal = inQueueY.AllocTensor<TYPE_Y>();
        AscendC::DataCopy(xLocal, xGm[progress * this->tileDataNum], this->processDataNum);
        AscendC::DataCopy(yLocal, yGm[progress * this->tileDataNum], this->processDataNum);
        inQueueX.EnQue(xLocal);
        inQueueY.EnQue(yLocal);
    }

    __aicore__ inline void Compute(int32_t progress)
    {
        AscendC::LocalTensor<TYPE_X> xLocal = inQueueX.DeQue<TYPE_X>();
        AscendC::LocalTensor<TYPE_Y> yLocal = inQueueY.DeQue<TYPE_Y>();
        AscendC::LocalTensor<TYPE_Z> zLocal = outQueueZ.AllocTensor<TYPE_Z>();
        AscendC::Add(zLocal, xLocal, yLocal, this->processDataNum);
        outQueueZ.EnQue<TYPE_Z>(zLocal);
        inQueueX.FreeTensor(xLocal);
        inQueueY.FreeTensor(yLocal);
    }

    __aicore__ inline void CopyOut(int32_t progress)
    {
        AscendC::LocalTensor<TYPE_Z> zLocal = outQueueZ.DeQue<TYPE_Z>();
        AscendC::DataCopy(zGm[progress * this->tileDataNum], zLocal, this->processDataNum);
        outQueueZ.FreeTensor(zLocal);
    }

private:
    AscendC::TPipe pipe;
    AscendC::TQue<AscendC::QuePosition::VECIN, BUFFER_NUM> inQueueX, inQueueY;
    AscendC::TQue<AscendC::QuePosition::VECOUT, BUFFER_NUM> outQueueZ;
    AscendC::GlobalTensor<TYPE_X> xGm;
    AscendC::GlobalTensor<TYPE_Y> yGm;
    AscendC::GlobalTensor<TYPE_Z> zGm;
    uint32_t coreDataNum;
    uint32_t tileNum;
    uint32_t tileDataNum;
    uint32_t tailDataNum;
    uint32_t processDataNum;
};

__global__ __aicore__ void add_custom_template(GM_ADDR x, GM_ADDR y, GM_ADDR z, GM_ADDR workspace, GM_ADDR tiling)
{
    REGISTER_TILING_DEFAULT(TilingDataTemplate);
    GET_TILING_DATA_WITH_STRUCT(TilingDataTemplate, tiling_data, tiling);
    KernelAdd<DTYPE_X, DTYPE_Y, DTYPE_Z> op;
    op.Init(x, y, z, tiling_data.smallCoreDataNum,
            tiling_data.bigCoreDataNum, tiling_data.finalBigTileNum,
            tiling_data.finalSmallTileNum, tiling_data.tileDataNum,
            tiling_data.smallTailDataNum, tiling_data.bigTailDataNum,
            tiling_data.tailBlockNum);
    op.Process();
}
```