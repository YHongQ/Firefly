---
title: 华为昇腾CANN算子Tiling策略实战：大小核动态负载均衡的减法算子实现
published: 2026-05-22
tags: [CANN, 华为昇腾, 算子开发, Tiling策略, AscendC, C++]
pinned: false
category: 技术博客
draft: false
---

# 华为昇腾CANN算子Tiling策略实战：大小核动态负载均衡的减法算子实现

> 本文通过一个完整的减法算子（`z = x - y`）代码，深入讲解基于华为昇腾CANN的Tiling策略设计。从Host侧的动态负载均衡算法，到Device侧的多核并行处理，带你一步步理解昇腾AI芯片的Tiling策略和内存管理。

## 1. 背景与目标

在昇腾AI处理器的开发中，**Tiling策略**是算子性能优化的关键。当输入数据量不能被核心数整除时，如何合理分配数据给各个核心，避免核心空闲，提升整体计算效率，是Tiling策略要解决的核心问题。本文的目标是：

- 实现一个减法算子 `SubCustomTemplate`，支持 `z[i] = x[i] - y[i]`。
- 采用**大小核动态负载均衡策略**，解决数据量不能被核心数整除的问题。
- 深入理解Unified Buffer、Tile、Block等昇腾AI芯片的核心概念。

## 2. Tiling策略核心思想

### 2.1 大小核概念

在昇腾AI处理器中，"大核"和"小核"并非指核心能力差异，而是指**处理数据量的多少**：

- **大核**：处理较多的数据，用于处理尾数据（数据量不能被核心数整除时的剩余部分）
- **小核**：处理较少的数据，用于处理主体数据

这种设计确保了所有核心都能充分利用，避免部分核心空闲。

### 2.2 核心概念

| 概念 | 说明 |
|------|------|
| **BLOCK_SIZE** | 块大小，每次搬运/计算的最小数据量，通常为32字节对齐 |
| **Tile** | 每次搬运的数据量，由Unified Buffer大小决定 |
| **Unified Buffer** | 核内快速缓存，计算时能使用的空间大小 |
| **尾数据** | 数据量不能被Tile整除时的剩余数据 |

## 3. Host侧Tiling策略实现

### 3.1 数据对齐与核心数确定

```cpp
uint32_t inputNum = context->GetInputShape(0)->GetStorageShape().GetShapeSize();
uint32_t typeLength = 0;
ge::TypeUtils::GetDataTypeLength(context->GetInputDesc(0)->GetDataType(), typeLength);
uint32_t inputLength = inputNum * typeLength;

const uint32_t BLOCK_SIZE = 32;
uint32_t inputLengthAlgin32 = (((inputLength + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE);
```

**关键点**：
- 获取输入数据的元素数量和数据类型长度
- 计算输入数据的字节长度
- 按照32字节对齐，保证每个核心至少处理一个块的数据

### 3.2 核心数动态分配

```cpp
auto min_Use_CoreNum = std::min(coreNum, inputLengthAlgin32 / BLOCK_SIZE);
coreNum = std::max(min_Use_CoreNum, static_cast<uint32_t>(1));
context->SetBlockDim(coreNum);
```

**核心逻辑**：
- 每个核至少处理一个块的数据量
- 如果对齐后的输入数据块数小于核心数，只使用block数的核心数
- 至少使用一个核心，避免0个核心处理数据异常

### 3.3 数据分配策略

```cpp
uint32_t everyCoreInputBlockNum = inputLengthAlgin32 / BLOCK_SIZE / coreNum;
uint32_t tailBlockNum = (inputLengthAlgin32 / BLOCK_SIZE) % coreNum;
```

**分配策略**：
- `everyCoreInputBlockNum`：每个核心处理的块数量（平均分配部分）
- `tailBlockNum`：尾块数量，如果输入数据块数不能被核心数整除，前`tailBlockNum`个核心需要多处理一个块

### 3.4 Tile计算

```cpp
uint64_t ubSize;
ascendcPlatform.GetCoreMemSize(platform_ascendc::CoreMemType::UB, ubSize);

uint32_t ubDataNumber = 3;
uint32_t tileBlockNum = (ubSize / BLOCK_SIZE) / ubDataNumber;
uint32_t tileDataNum = (tileBlockNum * BLOCK_SIZE) / typeLength;
```

**Tile计算逻辑**：
- 获取Unified Buffer的大小
- `ubDataNumber = 3`：减法涉及两个输入一个输出，需要同时在UB中存放
- `tileBlockNum`：每次搬运的Block数量（核每次最大的计算量）
- `tileDataNum`：每次搬运的元素数量

### 3.5 大小核数据分配

```cpp
uint32_t smallCoreDataNum = everyCoreInputBlockNum * BLOCK_SIZE / typeLength;
uint32_t smallTileNum = everyCoreInputBlockNum / tileBlockNum;
uint32_t finalSmallTileNum = (everyCoreInputBlockNum % tileBlockNum) == 0 ? smallTileNum : smallTileNum + 1;
uint32_t smallTailDataNum = smallCoreDataNum - (tileDataNum * smallTileNum);
smallTailDataNum = smallTailDataNum == 0 ? tileDataNum : smallTailDataNum;

everyCoreInputBlockNum += 1;
uint32_t bigCoreDataNum = everyCoreInputBlockNum * BLOCK_SIZE / typeLength;
uint32_t bigTileNum = everyCoreInputBlockNum / tileBlockNum;
uint32_t finalBigTileNum = (everyCoreInputBlockNum % tileBlockNum) == 0 ? bigTileNum : bigTileNum + 1;
uint32_t bigTailDataNum = bigCoreDataNum - (tileDataNum * bigTileNum);
bigTailDataNum = bigTailDataNum == 0 ? tileDataNum : bigTailDataNum;
```

**大小核分配策略**：
- **小核**：处理`everyCoreInputBlockNum`个Block的数据
- **大核**：处理`everyCoreInputBlockNum + 1`个Block的数据（多处理一个Block）
- **尾数据处理**：如果数据量不能被Tile整除，最后一次搬运处理剩余数据

## 4. Device侧多核并行实现

### 4.1 核函数初始化

```cpp
uint32_t coreNum = AscendC::GetBlockIdx();
uint32_t globalBufferIndex = bigCoreDataNum * AscendC::GetBlockIdx();

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
```

**核心识别与数据分配**：
- 通过`GetBlockIdx()`获取当前核的编号
- 前`tailBlockNum`个核作为大核，处理更多数据
- 后续核作为小核，处理较少数据
- 小核需要调整偏移量，确保从正确的位置开始处理数据

### 4.2 流水线并行处理

```cpp
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
```

**处理流程**：
- 循环处理每个Tile
- 最后一个Tile处理尾数据
- 采用CopyIn → Compute → CopyOut的流水线模式

### 4.3 数据搬运与计算

```cpp
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
    AscendC::Sub(zLocal, xLocal, yLocal, this->processDataNum);
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
```

**流水线设计**：
- **CopyIn**：从GlobalMemory搬运数据到LocalTensor
- **Compute**：执行减法计算，结果放入输出队列
- **CopyOut**：将结果写回GlobalMemory

## 5. Tiling数据结构

```cpp
struct SubCustomTemplateTilingData {
    uint32_t smallCoreDataNum;
    uint32_t bigCoreDataNum;
    uint32_t finalBigTileNum;
    uint32_t finalSmallTileNum;
    uint32_t tileDataNum;
    uint32_t smallTailDataNum;
    uint32_t bigTailDataNum;
    uint32_t tailBlockNum;
};
```

**结构体字段说明**：
- `smallCoreDataNum`：每个小核处理的数据个数
- `bigCoreDataNum`：每个大核处理的数据个数
- `finalBigTileNum`：大核搬运Tile的次数
- `finalSmallTileNum`：小核搬运Tile的次数
- `tileDataNum`：每个Tile处理的数据个数
- `smallTailDataNum`：小核最后一轮搬运Tile的数据个数
- `bigTailDataNum`：大核最后一轮搬运Tile的数据个数
- `tailBlockNum`：处理尾数据的大核数量

## 6. 关键知识点总结

| 概念 | 说明 |
|------|------|
| **大小核策略** | 通过让部分核心处理更多数据，解决数据量不能被核心数整除的问题 |
| **BLOCK_SIZE** | 32字节对齐的块大小，昇腾AI处理器的最小数据搬运单位 |
| **Unified Buffer** | 核内快速缓存，决定了一次最多能计算的数据量 |
| **Tile** | 每次搬运的数据量，由Unified Buffer大小和算法需求决定 |
| **尾数据处理** | 当数据量不能被Tile整除时，最后一次搬运处理剩余数据 |
| **流水线并行** | CopyIn、Compute、CopyOut重叠进行，提升硬件利用率 |

## 7. 算子注册

```cpp
class SubCustomTemplate : public OpDef {
public:
    explicit SubCustomTemplate(const char* name) : OpDef(name)
    {
        this->Input("x")
            .ParamType(REQUIRED)
            .DataType({ge::DT_FLOAT16, ge::DT_FLOAT})
            .Format({ge::FORMAT_ND, ge::FORMAT_ND})
            .UnknownShapeFormat({ge::FORMAT_ND, ge::FORMAT_ND});
        this->Input("y")
            .ParamType(REQUIRED)
            .DataType({ge::DT_FLOAT16, ge::DT_FLOAT})
            .Format({ge::FORMAT_ND, ge::FORMAT_ND})
            .UnknownShapeFormat({ge::FORMAT_ND, ge::FORMAT_ND});
        this->Output("z")
            .ParamType(REQUIRED)
            .DataType({ge::DT_FLOAT16, ge::DT_FLOAT})
            .Format({ge::FORMAT_ND, ge::FORMAT_ND})
            .UnknownShapeFormat({ge::FORMAT_ND, ge::FORMAT_ND});

        this->SetInferShape(ge::InferShape).SetInferDataType(ge::InferDataType);
        this->AICore().SetTiling(optiling::TilingFunc);
        this->AICore().AddConfig("ascend910b");
    }
};
```

**算子注册要点**：
- 定义输入输出张量的数据类型和格式
- 设置形状推理和数据类型推理函数
- 绑定Tiling函数
- 指定支持的硬件平台（ascend910b）

## 8. 性能优化建议

1. **合理设置Tile大小**：根据Unified Buffer大小和算法需求，平衡数据搬运和计算效率
2. **避免核心空闲**：通过大小核策略，确保所有核心都有数据可处理
3. **内存对齐**：按照32字节对齐，提升数据搬运效率
4. **流水线并行**：充分利用CopyIn、Compute、CopyOut的重叠，提升吞吐量

---

**参考文献**：
- 华为昇腾 CANN 官方文档 (v6.3.0+)
- 《Ascend C 编程指南》
- 《AscendCL 接口参考》

## 完整源码

### Sub_template_tiling.h

```cpp
#ifndef SUB_CUSTOM_TEMPLATE_TILING_H
#define SUB_CUSTOM_TEMPLATE_TILING_H
#include <cstdint>

struct SubCustomTemplateTilingData {
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

### Sub_template_host.cpp

```cpp
#include "register/op_def_registry.h"
#include "../op_kernel/sub_custom_template_tiling.h"
#include "graph/utils/type_utils.h"
#include "tiling/platform/platform_ascendc.h"

namespace optiling {
static ge::graphStatus TilingFunc(gert::TilingContext* context)
{
   auto ascendcPlatform = platform_ascendc::PlatformAscendC(context->GetPlatformInfo());
   auto coreNum = ascendcPlatform.GetCoreNum();

   uint32_t inputNum = context->GetInputShape(0)->GetStorageShape().GetShapeSize();
   uint32_t typeLength = 0;
   ge::TypeUtils::GetDataTypeLength(context->GetInputDesc(0)->GetDataType(), typeLength);
   uint32_t inputLength = inputNum * typeLength;
   
   const uint32_t BLOCK_SIZE = 32;

   uint32_t inputLengthAlgin32 = (((inputLength + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE);
   
   auto min_Use_CoreNum = std::min(coreNum, inputLengthAlgin32 / BLOCK_SIZE);
   coreNum = std::max(min_Use_CoreNum, static_cast<uint32_t>(1));
   context->SetBlockDim(coreNum);

   uint32_t everyCoreInputBlockNum = inputLengthAlgin32 / BLOCK_SIZE / coreNum;
   uint32_t tailBlockNum = (inputLengthAlgin32 / BLOCK_SIZE) % coreNum;

   uint64_t ubSize;
   ascendcPlatform.GetCoreMemSize(platform_ascendc::CoreMemType::UB, ubSize);

   uint32_t ubDataNumber = 3;
   uint32_t tileBlockNum = (ubSize / BLOCK_SIZE ) / ubDataNumber;
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
   uint32_t bigTailDataNum = bigCoreDataNum - (tileDataNum * bigTileNum);
   bigTailDataNum = bigTailDataNum == 0 ? tileDataNum : bigTailDataNum;

   SubCustomTemplateTilingData *tiling = context->GetTilingData<SubCustomTemplateTilingData>();
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

namespace ge {
static ge::graphStatus InferShape(gert::InferShapeContext* context)
{
    const gert::Shape* x1_shape = context->GetInputShape(0);
    gert::Shape* y_shape = context->GetOutputShape(0);
    *y_shape = *x1_shape;
    return GRAPH_SUCCESS;
}
static ge::graphStatus InferDataType(gert::InferDataTypeContext *context)
{
    const auto inputDataType = context->GetInputDataType(0);
    context->SetOutputDataType(0, inputDataType);
    return ge::GRAPH_SUCCESS;
}
}

namespace ops {
class SubCustomTemplate : public OpDef {
public:
    explicit SubCustomTemplate(const char* name) : OpDef(name)
    {
        this->Input("x")
            .ParamType(REQUIRED)
            .DataType({ge::DT_FLOAT16, ge::DT_FLOAT})
            .Format({ge::FORMAT_ND, ge::FORMAT_ND})
            .UnknownShapeFormat({ge::FORMAT_ND, ge::FORMAT_ND});
        this->Input("y")
            .ParamType(REQUIRED)
            .DataType({ge::DT_FLOAT16, ge::DT_FLOAT})
            .Format({ge::FORMAT_ND, ge::FORMAT_ND})
            .UnknownShapeFormat({ge::FORMAT_ND, ge::FORMAT_ND});
        this->Output("z")
            .ParamType(REQUIRED)
            .DataType({ge::DT_FLOAT16, ge::DT_FLOAT})
            .Format({ge::FORMAT_ND, ge::FORMAT_ND})
            .UnknownShapeFormat({ge::FORMAT_ND, ge::FORMAT_ND});

        this->SetInferShape(ge::InferShape).SetInferDataType(ge::InferDataType);

        this->AICore()
            .SetTiling(optiling::TilingFunc);
        this->AICore().AddConfig("ascend910b");

    }
};

OP_ADD(SubCustomTemplate);
}
}
```

### Sub_template_kernel.cpp

```cpp
#include "kernel_operator.h"
#include "sub_custom_template_tiling.h"
#include "kernel_operator_dump_tensor_intf_impl.h"
constexpr int32_t BUFFER_NUM = 2;

template<typename TYPE_X, typename TYPE_Y, typename TYPE_Z>
class KernelSub {
public:
    __aicore__ inline KernelSub() {}
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
        }
        else {
          this->coreDataNum = smallCoreDataNum;
          this->tileNum = finalSmallTileNum;
          this->tailDataNum = smallTailDataNum;
          globalBufferIndex -= (bigCoreDataNum - smallCoreDataNum) * (AscendC::GetBlockIdx() - tailBlockNum);
        }
        this->xGm.SetGlobalBuffer((__gm__ TYPE_X*)x + globalBufferIndex, this->coreDataNum);
        this->yGm.SetGlobalBuffer((__gm__ TYPE_Y*)y + globalBufferIndex, this->coreDataNum);
        this->zGm.SetGlobalBuffer((__gm__ TYPE_Z*)z + globalBufferIndex, this->coreDataNum);
        this->pipe.InitBuffer(inQueueX, BUFFER_NUM, this->tileDataNum * sizeof(TYPE_X));
        this->pipe.InitBuffer(inQueueY, BUFFER_NUM, this->tileDataNum * sizeof(TYPE_Y));
        this->pipe.InitBuffer(outQueueZ, BUFFER_NUM, this->tileDataNum * sizeof(TYPE_Z));
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
      AscendC::Sub(zLocal, xLocal, yLocal, this->processDataNum);
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

__global__ __aicore__ void sub_custom_template(GM_ADDR x, GM_ADDR y, GM_ADDR z, GM_ADDR workspace, GM_ADDR tiling)
{
    REGISTER_TILING_DEFAULT(TilingDataTemplate);
    GET_TILING_DATA_WITH_STRUCT(TilingDataTemplate, tiling_data, tiling);
    KernelSub op;
    op.Init(x, y, z, tiling_data.smallCoreDataNum, 
            tiling_data.bigCoreDataNum, tiling_data.finalBigTileNum, 
            tiling_data.finalSmallTileNum, tiling_data.tileDataNum, 
            tiling_data.smallTailDataNum, tiling_data.bigTailDataNum, 
            tiling_data.tailBlockNum);
    op.Process();
}
```

### Sub.json

```json
[{
    "op": "SubCustomTemplate",
    "input_desc": [{
            "name": "x",
            "param_type": "required",
            "format": ["ND", "ND"],
            "type": ["float16", "float"]
        },
        {
            "name": "y",
            "param_type": "required",
            "format": ["ND", "ND"],
            "type": ["float16", "float"]
        }
    ],
    "output_desc": [{
        "name": "z",
        "param_type": "required",
        "format": ["ND", "ND"],
        "type": ["float16", "float"]
    }]
}]
```