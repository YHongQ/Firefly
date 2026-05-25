---
title: 华为昇腾CANN算子进阶开发：Tiling模板、属性、TBuf与Workspace全解析
published: 2026-05-25
tags: [CANN, 华为昇腾, 算子开发, Tiling模板, AscendC, C++, Workspace, TBuf]
pinned: true
category: 学习笔记
draft: false
---
# 华为昇腾CANN算子进阶开发：Tiling模板、属性、TBuf与Workspace全解析

> 本文深入讲解华为昇腾CANN算子开发中的高级特性：Tiling模板编程、算子属性、TBuf临时存储和Workspace工作空间。从C++模板基础到内存管理机制，从Host侧配置到Device侧实现，带你全面掌握昇腾AI芯片的算子开发进阶技巧。

## 1. 背景与目标

在掌握了基础算子开发后，实际生产环境中还需要处理更复杂的场景：支持多种数据类型、需要额外的临时存储空间、算子需要配置参数等。本文的目标是：

- 理解C++模板编程在算子开发中的应用
- 掌握Workspace和TBuf的内存管理机制
- 学习算子属性的定义和使用方法
- 深入理解Tiling模板编程的优势和实现
- 掌握Host侧和Device侧的内存位置关系
- 实现一个完整的Clamp算子，综合运用所学知识

## 2. C++模板编程基础

在深入昇腾CANN的Tiling模板编程之前，我们需要先理解C++模板编程的基础知识。

### 2.1 什么是模板

模板是C++的泛型编程机制，允许我们编写与类型无关的代码。编译器会根据具体的使用类型生成对应的代码。

#### 2.1.1 函数模板

```cpp
// 传统的写法 - 需要为每种类型写一个函数
int add_int(int a, int b) {
    return a + b;
}

float add_float(float a, float b) {
    return a + b;
}

// 使用模板 - 一个函数支持所有类型
template<typename T>
T add(T a, T b) {
    return a + b;
}

// 使用方式
int result1 = add<int>(3, 5);        // 显式指定类型
float result2 = add(2.5f, 3.7f);     // 自动类型推导
```

**模板的优势：**

1. **代码复用**：避免为每种类型重复编写相同逻辑
2. **类型安全**：编译时进行类型检查
3. **性能优化**：编译器针对具体类型生成优化代码

#### 2.1.2 类模板

```cpp
// 类模板示例
template<typename T>
class Container {
private:
    T data;
public:
    Container(T value) : data(value) {}
    T getValue() { return data; }
    void setValue(T value) { data = value; }
};

// 使用方式
Container<int> intContainer(42);
Container<float> floatContainer(3.14f);
```

#### 2.1.3 模板特化

```cpp
// 通用模板
template<typename T>
class TypeInfo {
public:
    static const char* getName() {
        return "Unknown Type";
    }
};

// 特化版本 - 针对int类型
template<>
class TypeInfo<int> {
public:
    static const char* getName() {
        return "int";
    }
};

// 特化版本 - 针对float类型
template<>
class TypeInfo<float> {
public:
    static const char* getName() {
        return "float";
    }
};
```

### 2.2 模板在昇腾算子开发中的应用

在昇腾算子开发中，模板主要用于：

1. **支持多种数据类型**：一个算子支持float16、float32、int32等多种类型
2. **编译时优化**：根据具体类型生成最优代码
3. **代码维护**：避免为每种类型维护单独的代码

```cpp
// 昇腾算子中的模板应用示例
template<typename dtypeX, typename dtypeY, typename dtypeZ>
class KernelAdd {
public:
    __aicore__ inline void Init(GM_ADDR x, GM_ADDR y, GM_ADDR z, ...) {
        // 根据具体类型初始化
        xGm.SetGlobalBuffer((__gm__ dtypeX *)x, totalLength);
        yGm.SetGlobalBuffer((__gm__ dtypeY *)y, totalLength);
        zGm.SetGlobalBuffer((__gm__ dtypeZ *)z, totalLength);
    }
  
    __aicore__ inline void Compute() {
        AscendC::LocalTensor<dtypeX> xLocal = inQueueX.DeQue<dtypeX>();
        AscendC::LocalTensor<dtypeY> yLocal = inQueueY.DeQue<dtypeY>();
        AscendC::LocalTensor<dtypeZ> zLocal = outQueueZ.AllocTensor<dtypeZ>();
      
        // 使用模板类型进行计算
        AscendC::Add(zLocal, xLocal, yLocal, tileLength);
      
        outQueueZ.EnQue<dtypeZ>(zLocal);
        inQueueX.FreeTensor(xLocal);
        inQueueY.FreeTensor(yLocal);
    }
};
```

## 3. PyTorch算子属性介绍

在深入昇腾CANN的算子属性之前，我们先了解PyTorch中算子属性的概念，这有助于我们理解属性的本质和作用。

### 3.1 什么是算子属性

算子属性是算子在定义阶段就确定的**静态参数**，在计算过程中保持不变。它与动态变化的输入张量相互独立，用于约束和控制算子的计算行为。

### 3.2 PyTorch中的属性示例

#### 3.2.1 必选属性

必选属性是算子运行的核心前提，缺少这些属性算子无法完成初始化或执行。

```python
import torch
import torch.nn as nn

# Conv2d的必选属性
conv = nn.Conv2d(
    in_channels=3,      # 必选：输入通道数
    out_channels=64,    # 必选：输出通道数
    kernel_size=3       # 必选：卷积核尺寸
)

# 缺少这些属性，卷积层无法确定权重矩阵形状和计算逻辑
```

**必选属性的作用：**

- 决定算子的核心功能
- 确定计算所需的资源分配
- 定义算子的输入输出关系

#### 3.2.2 可选属性

可选属性是算子的辅助配置，未指定时使用默认值，不改变算子的核心功能。

```python
# Conv2d的可选属性
conv = nn.Conv2d(
    in_channels=3,
    out_channels=64,
    kernel_size=3,
    stride=1,           # 可选：步长，默认为1
    padding=0,          # 可选：填充，默认为0
    bias=True,          # 可选：是否使用偏置，默认为True
    dilation=1,         # 可选：膨胀率，默认为1
    groups=1            # 可选：分组数，默认为1
)

# 这些属性只调整计算细节，不改变"执行卷积运算"的核心事实
```

**可选属性的作用：**

- 控制算子的计算细节
- 调整输出特征图尺寸
- 优化计算性能或精度

### 3.3 属性的本质特征

无论是PyTorch还是昇腾CANN，算子属性都具有以下特征：

| 特征             | 说明                                                |
| ---------------- | --------------------------------------------------- |
| **静态性** | 运行过程中不可修改，需在调用算子前确定              |
| **解耦性** | 与输入张量解耦，不依赖数据的形状/数值               |
| **分类性** | 通过"必选/可选"划分，必选决定核心功能，可选定制细节 |

## 4. Workspace：设备侧工作空间

### 4.1 什么是Workspace

Workspace是设备侧（Device侧）Global Memory上的一块内存区域，用于存储算子计算过程中需要的临时数据。它分为两部分：

#### 4.1.1 系统Workspace

系统Workspace是Ascend C API需要预留的内存空间。某些API在计算过程中需要额外的内存作为缓存，因此算子需要为这些API预留Workspace内存。

```cpp
// 获取系统Workspace大小
auto ascendcPlatform = platform_ascendc::PlatformAscendC(context->GetPlatformInfo());
uint32_t sysWorkspaceSize = ascendcPlatform.GetLibApiWorkSpaceSize();
```

#### 4.1.2 用户Workspace

用户Workspace是算子实现过程中自行分配和使用的内存空间。使用场景包括：

- **数据暂存**：当Unified Buffer和L1 Buffer空间不足时，将数据暂存至Workspace
- **API调用**：某些API（如SyncAll）需要Workspace作为入参
- **中间结果**：存储计算过程中的中间结果

### 4.2 Workspace的内存位置

```
┌─────────────────────────────────────────────────────────┐
│                    Host侧 (CPU)                          │
│  ┌─────────────────────────────────────────────────┐   │
│  │         Host Memory (系统内存)                   │   │
│  │  - 输入数据                                       │   │
│  │  - 输出数据                                       │   │
│  │  - 模型参数                                       │   │
│  └─────────────────────────────────────────────────┘   │
└────────────────────┬────────────────────────────────────┘
                     │ 数据搬运 (aclrtMemcpy)
┌────────────────────▼────────────────────────────────────┐
│                  Device侧 (AI芯片)                       │
│  ┌─────────────────────────────────────────────────┐   │
│  │         Global Memory (HBM)                     │   │
│  │  ┌─────────────────────────────────────────┐   │   │
│  │  │  输入/输出张量                            │   │   │
│  │  └─────────────────────────────────────────┘   │   │
│  │  ┌─────────────────────────────────────────┐   │   │
│  │  │  Workspace (工作空间)                    │   │   │
│  │  │  - 系统Workspace (API预留)               │   │   │
│  │  │  - 用户Workspace (算子使用)               │   │   │
│  │  └─────────────────────────────────────────┘   │   │
│  └─────────────────────────────────────────────────┘   │
│  ┌─────────────────────────────────────────────────┐   │
│  │         LocalMemory (片上存储)                  │   │
│  │  - Unified Buffer (UB)                          │   │
│  │  - L1 Buffer                                    │   │
│  │  - L0 Buffer                                    │   │
│  └─────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

### 4.3 Host侧Workspace配置

在Host侧的Tiling函数中配置Workspace大小：

```cpp
static ge::graphStatus TilingFunc(gert::TilingContext* context)
{
    // 获取平台信息
    auto ascendcPlatform = platform_ascendc::PlatformAscendC(context->GetPlatformInfo());
  
    // 获取系统Workspace大小
    uint32_t sysWorkspaceSize = ascendcPlatform.GetLibApiWorkSpaceSize();
  
    // 设置用户Workspace大小（根据实际需求）
    size_t userWorkspaceSize = 256 * 4;  // 256个元素，每个4字节
  
    // 获取Workspace指针并设置总大小
    size_t *currentWorkspace = context->GetWorkspaceSizes(1);
    currentWorkspace[0] = userWorkspaceSize + sysWorkspaceSize;
  
    return ge::GRAPH_SUCCESS;
}
```

**关键步骤：**

1. **获取平台信息**：通过 `PlatformAscendC`获取硬件平台信息
2. **计算系统Workspace**：调用 `GetLibApiWorkSpaceSize()`获取API需要的空间
3. **计算用户Workspace**：根据算子实际需求确定大小
4. **设置总大小**：系统Workspace + 用户Workspace

### 4.4 Device侧Workspace使用

在Device侧的核函数中使用Workspace：

```cpp
template <typename dtypeX, typename dtypeY>
__global__ __aicore__ void clamp(GM_ADDR x, GM_ADDR y, GM_ADDR workspace, GM_ADDR tiling)
{
    // 将Workspace作为GlobalTensor使用
    AscendC::GlobalTensor<float> tmpGm;
    tmpGm.SetGlobalBuffer((__gm__ float *)workspace);
  
    // 可以像使用普通GlobalTensor一样使用Workspace
    AscendC::LocalTensor<float> tmpLocal = tmpGm[0];
  
    // 使用Workspace进行计算
    // ...
}
```

**使用注意事项：**

- Workspace位于Global Memory，访问速度比LocalMemory慢
- 使用Workspace需要额外的数据搬运操作
- 合理规划Workspace大小，避免浪费内存

## 5. TBuf：LocalMemory临时存储

### 5.1 TBuf的作用

TBuf是位于LocalMemory（片上存储）的临时存储空间，主要用于Vector计算过程中的数据暂存。与Workspace相比，TBuf具有以下优势：

| 特性               | Workspace           | TBuf               |
| ------------------ | ------------------- | ------------------ |
| **内存位置** | Global Memory (HBM) | LocalMemory (片上) |
| **访问速度** | 较慢                | 快速               |
| **数据搬运** | 需要搬运操作        | 无需搬运           |
| **使用场景** | 大量数据暂存        | 计算过程临时存储   |

### 5.2 TBuf vs TQue

TBuf和TQue都用于LocalMemory的内存管理，但使用方式有所不同：

#### 5.2.1 相同点

- 都通过 `InitBuffer`接口初始化内存
- 都由TPipe统一管理
- 都位于LocalMemory中

#### 5.2.2 不同点

| 特性               | TBuf             | TQue                     |
| ------------------ | ---------------- | ------------------------ |
| **获取内存** | 通过 `Get()`   | 通过 `AllocTensor()`   |
| **队列操作** | 不支持           | 支持 `EnQue`/`DeQue` |
| **内存释放** | 无需释放         | 需要 `FreeTensor()`    |
| **使用场景** | 计算过程临时存储 | 流水线数据传递           |

### 5.3 TBuf的使用示例

```cpp
class KernelAdd {
public:
    __aicore__ inline KernelAdd() {}
  
    __aicore__ inline void Init(GM_ADDR x, GM_ADDR y, GM_ADDR z)
    {
        // 初始化GlobalTensor
        xGm.SetGlobalBuffer((__gm__ half *)x, TOTAL_LENGTH);
        yGm.SetGlobalBuffer((__gm__ half *)y, TOTAL_LENGTH);
        zGm.SetGlobalBuffer((__gm__ half *)z, TOTAL_LENGTH);

        // 初始化队列
        pipe.InitBuffer(inQueueX, 1, TOTAL_LENGTH * sizeof(bfloat16_t));
        pipe.InitBuffer(inQueueY, 1, TOTAL_LENGTH * sizeof(bfloat16_t));
        pipe.InitBuffer(outQueueZ, 1, TOTAL_LENGTH * sizeof(bfloat16_t));
      
        // 初始化TBuf（用于类型转换的临时存储）
        pipe.InitBuffer(tmpBuf0, TOTAL_LENGTH * sizeof(float));
        pipe.InitBuffer(tmpBuf1, TOTAL_LENGTH * sizeof(float));
        pipe.InitBuffer(tmpBuf2, TOTAL_LENGTH * sizeof(float));
    }
  
    __aicore__ inline void Compute()
    {
        // 从队列获取输入
        AscendC::LocalTensor<bfloat16_t> xLocal = inQueueX.DeQue<bfloat16_t>();
        AscendC::LocalTensor<bfloat16_t> yLocal = inQueueY.DeQue<bfloat16_t>();
        AscendC::LocalTensor<bfloat16_t> zLocal = outQueueZ.AllocTensor<bfloat16_t>();
      
        // 获取TBuf的临时存储空间
        AscendC::LocalTensor<float> tmpTensor0 = tmpBuf0.Get<float>();
        AscendC::LocalTensor<float> tmpTensor1 = tmpBuf1.Get<float>();
        AscendC::LocalTensor<float> tmpTensor2 = tmpBuf2.Get<float>();
      
        // 类型转换：bfloat16 -> float
        AscendC::Cast(tmpTensor0, xLocal, AscendC::RoundMode::CAST_NONE, TOTAL_LENGTH);
        AscendC::Cast(tmpTensor1, yLocal, AscendC::RoundMode::CAST_NONE, TOTAL_LENGTH);
      
        // 使用float类型进行计算
        AscendC::Add(tmpTensor2, tmpTensor0, tmpTensor1, TOTAL_LENGTH);
      
        // 类型转换：float -> bfloat16
        AscendC::Cast(zLocal, tmpTensor2, AscendC::RoundMode::CAST_RINT, TOTAL_LENGTH);
      
        // 将结果放入输出队列
        outQueueZ.EnQue<bfloat16_t>(zLocal);
        inQueueX.FreeTensor(xLocal);
        inQueueY.FreeTensor(yLocal);
    }

private:
    AscendC::TPipe pipe;
    AscendC::TQue<AscendC::TPosition::VECIN, 1> inQueueX, inQueueY;
    AscendC::TQue<AscendC::TPosition::VECOUT, 1> outQueueZ;
  
    // TBuf类型的成员变量
    AscendC::TBuf<AscendC::TPosition::VECCALC> tmpBuf0, tmpBuf1, tmpBuf2;
  
    AscendC::GlobalTensor<bfloat16_t> xGm, yGm, zGm;
};
```

**TBuf使用要点：**

1. **初始化**：通过 `pipe.InitBuffer()`分配内存
2. **获取**：通过 `Get<T>()`获取临时Tensor
3. **使用**：直接参与计算，无需队列操作
4. **释放**：无需手动释放，由TPipe统一管理

### 5.4 TBuf的内存位置

```
┌─────────────────────────────────────────────────────────┐
│              LocalMemory (片上存储)                       │
│  ┌─────────────────────────────────────────────────┐   │
│  │         Unified Buffer (UB)                     │   │
│  │  ┌─────────────────────────────────────────┐   │   │
│  │  │  TQue (队列管理)                        │   │   │
│  │  │  - inQueueX (输入队列)                   │   │   │
│  │  │  - inQueueY (输入队列)                   │   │   │
│  │  │  - outQueueZ (输出队列)                  │   │   │
│  │  └─────────────────────────────────────────┘   │   │
│  │  ┌─────────────────────────────────────────┐   │   │
│  │  │  TBuf (临时存储)                        │   │   │
│  │  │  - tmpBuf0 (临时存储0)                   │   │   │
│  │  │  - tmpBuf1 (临时存储1)                   │   │   │
│  │  │  - tmpBuf2 (临时存储2)                   │   │   │
│  │  └─────────────────────────────────────────┘   │   │
│  └─────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

## 6. 算子属性：静态参数配置

### 6.1 属性的分类

#### 6.1.1 必选属性

必选属性是算子运行的核心前提，决定算子"要做什么"。

**昇腾CANN示例：**

```cpp
// 开发卷积算子时的必选属性
class Conv2D : public OpDef {
public:
    explicit Conv2D(const char* name) : OpDef(name) {
        // 必选属性
        this->Attr("in_channels").Int();      // 输入通道数
        this->Attr("out_channels").Int();     // 输出通道数
        this->Attr("kernel_size").Int();      // 卷积核尺寸
    }
};
```

#### 6.1.2 可选属性

可选属性是算子的辅助配置，控制算子"怎么做"。

**昇腾CANN示例：**

```cpp
// 开发GELU算子时的可选属性
class GELU : public OpDef {
public:
    explicit GELU(const char* name) : OpDef(name) {
        // 可选属性
        this->Attr("approximate").String().SetDefault("none");  // 近似计算方式
    }
};

// 开发Clamp算子时的可选属性
class Clamp : public OpDef {
public:
    explicit Clamp(const char* name) : OpDef(name) {
        // 可选属性
        this->Attr("min").Float().SetDefault(0.0f);   // 最小值
        this->Attr("max").Float().SetDefault(1.0f);   // 最大值
    }
};
```

### 6.2 属性的定义

在算子原型JSON文件中定义属性：

```json
[{
    "op": "Clamp",
    "input_desc": [{
        "name": "x",
        "param_type": "required",
        "format": ["ND", "ND"],
        "type": ["int32", "float"]
    }],
    "output_desc": [{
        "name": "y",
        "param_type": "required",
        "format": ["ND", "ND"],
        "type": ["int32", "float"]
    }],
    "attr": [{
        "name": "min",
        "type": "float",
        "param_type": "optional",
        "default_value": 0
    }]
}]
```

**属性字段说明：**

- `name`：属性名称
- `type`：属性类型（int/float/string/bool/list等）
- `param_type`：参数类型（required/optional）
- `default_value`：默认值（可选属性）

### 6.3 Host侧属性获取

在Host侧的Tiling函数中获取属性值：

```cpp
static ge::graphStatus TilingFunc(gert::TilingContext* context)
{
    // 获取属性值
    float min_value = *context->GetAttrs()->GetFloat(0);
  
    // 将属性值传递给Tiling数据结构
    ClampTilingData *tiling = context->GetTilingData<ClampTilingData>();
    tiling->min = min_value;
  
    return ge::GRAPH_SUCCESS;
}
```

### 6.4 Device侧属性使用

在Device侧的核函数中通过TilingData使用属性：

```cpp
template <typename dtypeX, typename dtypeY>
class KernelClamp {
public:
    __aicore__ inline void Init(GM_ADDR x, GM_ADDR y, GM_ADDR workspace, 
                                uint32_t totalLength, uint32_t tileNum, float min)
    {
        this->min = min;  // 保存属性值
        // 其他初始化...
    }
  
    __aicore__ inline void Compute()
    {
        // 使用属性值进行计算
        AscendC::Mins(yLocal, xLocal, this->min, tileLength);
    }

private:
    float min;  // 存储属性值
};
```

## 7. Tiling模板编程

### 7.1 什么是TilingKey

TilingKey是一个算子内用于区分不同实现的方法，类似于C++的模板机制。编译时会根据不同的TilingKey生成不同的二进制算子文件，有助于优化单次调用kernel的性能。

#### 7.1.1 传统TilingKey方式

```cpp
// Host侧设置TilingKey
static ge::graphStatus TilingFunc(gert::TilingContext* context)
{
    if (condition) {
        context->SetTilingKey(1);  // 设置TilingKey为1
    } else {
        context->SetTilingKey(2);  // 设置TilingKey为2
    }
    return ge::GRAPH_SUCCESS;
}

// Device侧根据TilingKey选择分支
extern "C" __global__ __aicore__ void add_custom(GM_ADDR x, GM_ADDR y, GM_ADDR z, 
                                                 GM_ADDR workspace, GM_ADDR tiling)
{
    if (TILING_KEY_IS(1)) {
        ProcessA();  // 执行分支A
    } else if (TILING_KEY_IS(2)) {
        ProcessB();  // 执行分支B
    }
}
```

**传统方式的缺点：**

- TilingKey是数字，不易记忆和理解
- 在多个TilingKey场景中管理复杂
- 代码可读性差

### 7.2 Tiling模板编程

Tiling模板编程使用模板参数替代数字TilingKey，使代码更直观和易维护。

#### 7.2.1 模板参数定义

创建模板定义文件（如 `tiling_key_clamp.h`）：

```cpp
#include "ascendc/host_api/tiling/template_argument.h"

ASCENDC_TPL_ARGS_DECL(Clamp,
    // 数据类型模板参数
    ASCENDC_TPL_DATATYPE_DECL(D_T_X, C_DT_FLOAT, C_DT_FLOAT16, ASCENDC_TPL_INPUT(0)),
    ASCENDC_TPL_DATATYPE_DECL(D_T_Y, C_DT_FLOAT, C_DT_FLOAT16, ASCENDC_TPL_OUTPUT(0)),
  
    // 自定义UINT类型模板参数
    ASCENDC_TPL_UINT_DECL(TILE_NUM, ASCENDC_TPL_8_BW, ASCENDC_TPL_UI_MIX, 
                          2, 0, 2, 3, 5, 10, 12, 13, 9, 8),
  
    // 自定义BOOL类型模板参数
    ASCENDC_TPL_BOOL_DECL(IS_SPLIT, 0, 1)
);
```

**模板参数类型说明：**

| 宏                            | 作用                   | 示例                      |
| ----------------------------- | ---------------------- | ------------------------- |
| `ASCENDC_TPL_DATATYPE_DECL` | 定义数据类型模板参数   | 支持float/float16/int32等 |
| `ASCENDC_TPL_UINT_DECL`     | 定义无符号整数模板参数 | 支持范围或枚举值          |
| `ASCENDC_TPL_BOOL_DECL`     | 定义布尔类型模板参数   | 支持true/false            |

#### 7.2.2 Host侧模板参数设置

```cpp
#include "../op_kernel/tiling_key_clamp.h"

static ge::graphStatus TilingFunc(gert::TilingContext* context)
{
    // 获取数据类型
    ge::DataType dtype_x = context->GetInputDesc(0)->GetDataType();
    ge::DataType dtype_y = context->GetOutputDesc(0)->GetDataType();
  
    // 转换为模板参数
    uint32_t D_T_X = static_cast<int>(dtype_x);
    uint32_t D_T_Y = static_cast<int>(dtype_y);
  
    // 根据数据量决定是否切分
    uint32_t TILE_NUM = 1;
    uint32_t IS_SPLIT = 0;
    if (totalLength >= MIN_LENGTH_FOR_SPLIT) {
        IS_SPLIT = 1;
        TILE_NUM = 8;
    }
  
    // 设置模板参数（自动生成TilingKey）
    ASCENDC_TPL_SEL_PARAM(context, D_T_X, D_T_Y, TILE_NUM, IS_SPLIT);
  
    return ge::GRAPH_SUCCESS;
}
```

**关键步骤：**

1. **获取数据类型**：从输入输出描述中获取数据类型
2. **转换模板参数**：将数据类型转换为模板参数
3. **计算模板参数**：根据实际需求计算其他模板参数
4. **设置模板参数**：通过 `ASCENDC_TPL_SEL_PARAM`宏设置

#### 7.2.3 Device侧模板参数使用

```cpp
#include "tiling_key_clamp.h"

// 模板参数作为函数参数
template <typename D_T_X, typename D_T_Y, int TILE_NUM, int IS_SPLIT>
__global__ __aicore__ void clamp(GM_ADDR x, GM_ADDR y, GM_ADDR workspace, GM_ADDR tiling)
{
    REGISTER_TILING_DEFAULT(ClampTilingData);
    GET_TILING_DATA_WITH_STRUCT(ClampTilingData, tiling_data, tiling);
  
    // 根据模板参数创建算子对象
    KernelClamp<D_T_X, D_T_Y> op;
    op.Init(x, y, workspace, tiling_data.totalLength, TILE_NUM, tiling_data.min);
  
    // 根据模板参数选择处理逻辑
    if (IS_SPLIT == 0) {
        op.Process1();  // 不切分处理
    } else if (IS_SPLIT == 1) {
        op.Process2();  // 切分处理
    }
}
```

**模板参数使用要点：**

1. **模板参数作为函数参数**：在核函数签名中声明模板参数
2. **去掉extern "C"**：模板函数不需要C链接方式
3. **根据模板参数选择逻辑**：使用if语句根据模板参数选择不同的处理分支

### 7.3 Tiling模板编程的优势

| 特性               | 传统TilingKey          | Tiling模板编程         |
| ------------------ | ---------------------- | ---------------------- |
| **可读性**   | 数字标识，难以理解     | 有意义的名称，易于理解 |
| **维护性**   | 手动管理，容易出错     | 自动生成，减少错误     |
| **扩展性**   | 添加新分支需要修改多处 | 只需添加模板参数       |
| **类型安全** | 运行时检查             | 编译时检查             |

## 8. 完整示例：Clamp算子实现

### 8.1 算子原型

```
算子类型: Clamp
输入: x (Shape: [8, 2048], Type: int32/float, Format: ND)
输出: y (Shape: [8, 2048], Type: int32/float, Format: ND)
属性: min (Type: float, Default: 0)
```

### 8.2 Tiling数据结构定义

```cpp
// clamp_tiling.h
#ifndef CLAMP_TILING_H
#define CLAMP_TILING_H

#include <cstdint>

struct ClampTilingData {
    uint32_t totalLength;  // 总数据长度
    uint32_t tileNum;      // 分块数量
    float min;             // 最小值属性
};

#endif
```

### 8.3 模板参数定义

```cpp
// tiling_key_clamp.h
#include "ascendc/host_api/tiling/template_argument.h"

ASCENDC_TPL_ARGS_DECL(Clamp,
    ASCENDC_TPL_DATATYPE_DECL(D_T_X, C_DT_FLOAT, C_DT_FLOAT16, ASCENDC_TPL_INPUT(0)),
    ASCENDC_TPL_DATATYPE_DECL(D_T_Y, C_DT_FLOAT, C_DT_FLOAT16, ASCENDC_TPL_OUTPUT(0))
);
```

### 8.4 Host侧实现

```cpp
// clamp.cpp
#include "../op_kernel/clamp_tiling.h"
#include "register/op_def_registry.h"
#include "../op_kernel/tiling_key_clamp.h"
#include "tiling/platform/platform_ascendc.h"

namespace optiling {
static ge::graphStatus TilingFunc(gert::TilingContext* context)
{
    auto ascendcPlatform = platform_ascendc::PlatformAscendC(context->GetPlatformInfo());
    ClampTilingData *tiling = context->GetTilingData<ClampTilingData>();
  
    // 获取输入信息
    uint32_t totalLength = context->GetInputShape(0)->GetOriginShape().GetShapeSize();
    ge::DataType dtype_x = context->GetInputDesc(0)->GetDataType();
    ge::DataType dtype_y = context->GetOutputDesc(0)->GetDataType();
  
    // 转换为模板参数
    uint32_t D_T_X = static_cast<int>(dtype_x);
    uint32_t D_T_Y = static_cast<int>(dtype_y);
  
    // 获取属性值
    float min_value = *context->GetAttrs()->GetFloat(0);
  
    // 设置Tiling数据
    tiling->totalLength = totalLength;
    tiling->tileNum = 8;
    tiling->min = min_value;
    context->SetBlockDim(8);
  
    // 配置模板参数
    ASCENDC_TPL_SEL_PARAM(context, D_T_X, D_T_Y);
  
    // 配置Workspace大小
    size_t userWorkspaceSize = 256 * 4;  // 256个元素，每个4字节
    size_t systemWorkspaceSize = static_cast<size_t>(ascendcPlatform.GetLibApiWorkSpaceSize());
    size_t *currentWorkspace = context->GetWorkspaceSizes(1);
    currentWorkspace[0] = userWorkspaceSize + systemWorkspaceSize;
  
    return ge::GRAPH_SUCCESS;
}
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
    return GRAPH_SUCCESS;
}
}

namespace ops {
class Clamp : public OpDef {
public:
    explicit Clamp(const char* name) : OpDef(name)
    {
        this->Input("x")
            .ParamType(REQUIRED)
            .DataType({ge::DT_INT32, ge::DT_FLOAT})
            .Format({ge::FORMAT_ND, ge::FORMAT_ND})
            .UnknownShapeFormat({ge::FORMAT_ND, ge::FORMAT_ND});
      
        this->Output("y")
            .ParamType(REQUIRED)
            .DataType({ge::DT_INT32, ge::DT_FLOAT})
            .Format({ge::FORMAT_ND, ge::FORMAT_ND})
            .UnknownShapeFormat({ge::FORMAT_ND, ge::FORMAT_ND});
      
        // 注册属性
        this->Attr("min").Float();
      
        this->SetInferShape(ge::InferShape)
            .SetInferDataType(ge::InferDataType);
      
        this->AICore()
            .SetTiling(optiling::TilingFunc);
        this->AICore().AddConfig("ascend910b");
    }
};

OP_ADD(Clamp);
}
```

### 8.5 Device侧实现

```cpp
// clamp.cpp
#include "kernel_operator.h"
#include "clamp_tiling.h"
#include "tiling_key_clamp.h"
#include "kernel_operator_dump_tensor_intf_impl.h"

constexpr int32_t BUFFER_NUM = 1;

template <typename dtypeX, typename dtypeY>
class KernelClamp {
public:
    __aicore__ inline KernelClamp() {}
  
    __aicore__ inline void Init(GM_ADDR x, GM_ADDR y, GM_ADDR workspace, 
                                uint32_t totalLength, uint32_t tileNum, float min)
    {
        this->blockLength = totalLength / AscendC::GetBlockNum();
        this->tileNum = tileNum;
        this->min = min;
        this->tileLength = this->blockLength / tileNum / BUFFER_NUM;
      
        // 设置GlobalTensor
        xGm.SetGlobalBuffer((__gm__ dtypeX *)x + this->blockLength * AscendC::GetBlockIdx(), 
                           this->blockLength);
        yGm.SetGlobalBuffer((__gm__ dtypeY *)y + this->blockLength * AscendC::GetBlockIdx(), 
                           this->blockLength);
      
        // 设置Workspace
        tmpGm.SetGlobalBuffer((__gm__ float *)workspace);
      
        // 初始化队列和TBuf
        pipe.InitBuffer(inQueueX, BUFFER_NUM, this->tileLength * sizeof(dtypeX));
        pipe.InitBuffer(outQueueY, BUFFER_NUM, this->tileLength * sizeof(dtypeY));
        pipe.InitBuffer(outQueueTmp, BUFFER_NUM, this->tileLength * sizeof(float));
    }
  
    __aicore__ inline void Process()
    {
        int32_t loopCount = this->tileNum * BUFFER_NUM;
        for (int32_t i = 0; i < loopCount; i++) {
            CopyIn(i);
            Compute(i);
            CopyOut(i);
        }
    }

private:
    __aicore__ inline void CopyIn(int32_t progress)
    {
        AscendC::LocalTensor<dtypeX> xLocal = inQueueX.AllocTensor<dtypeX>();
        AscendC::DataCopy(xLocal, xGm[progress * this->tileLength], this->tileLength);
        inQueueX.EnQue(xLocal);
    }
  
    __aicore__ inline void Compute(int32_t progress)
    {
        AscendC::LocalTensor<dtypeX> xLocal = inQueueX.DeQue<dtypeX>();
        AscendC::LocalTensor<dtypeY> yLocal = outQueueY.AllocTensor<dtypeY>();
      
        // 使用Mins进行Clamp计算
        AscendC::Mins(yLocal, xLocal, this->min, this->tileLength);
      
        outQueueY.EnQue<dtypeY>(yLocal);
        inQueueX.FreeTensor(xLocal);
    }
  
    __aicore__ inline void CopyOut(int32_t progress)
    {
        AscendC::LocalTensor<dtypeY> yLocal = outQueueY.DeQue<dtypeY>();
        AscendC::DataCopy(yGm[progress * this->tileLength], yLocal, this->tileLength);
        outQueueY.FreeTensor(yLocal);
    }

private:
    AscendC::TPipe pipe;
    AscendC::TQue<AscendC::TPosition::VECIN, BUFFER_NUM> inQueueX;
    AscendC::TQue<AscendC::TPosition::VECOUT, BUFFER_NUM> outQueueY;
    AscendC::TBuf<AscendC::TPosition::VECCALC> outQueueTmp;
  
    AscendC::GlobalTensor<dtypeX> xGm;
    AscendC::GlobalTensor<dtypeY> yGm;
    AscendC::GlobalTensor<float> tmpGm;
  
    uint32_t blockLength;
    uint32_t tileNum;
    uint32_t tileLength;
    float min;
};

// 模板核函数（注意：去掉extern "C"）
template <typename D_T_X, typename D_T_Y>
__global__ __aicore__ void clamp(GM_ADDR x, GM_ADDR y, GM_ADDR workspace, GM_ADDR tiling)
{
    REGISTER_TILING_DEFAULT(ClampTilingData);
    GET_TILING_DATA_WITH_STRUCT(ClampTilingData, tiling_data, tiling);
  
    KernelClamp<D_T_X, D_T_Y> op;
    op.Init(x, y, workspace, tiling_data.totalLength, tiling_data.tileNum, tiling_data.min);
    op.Process();
}
```

## 9. 内存位置关系总结

### 9.1 完整的内存层次结构

```
┌─────────────────────────────────────────────────────────┐
│                    Host侧 (CPU)                          │
│  ┌─────────────────────────────────────────────────┐   │
│  │         Host Memory                             │   │
│  │  - 应用程序数据                                  │   │
│  │  - 模型参数                                      │   │
│  │  - 输入输出数据                                  │   │
│  └─────────────────────────────────────────────────┘   │
└────────────────────┬────────────────────────────────────┘
                     │ aclrtMemcpy
                     │ (数据搬运)
┌────────────────────▼────────────────────────────────────┐
│                  Device侧 (AI芯片)                       │
│  ┌─────────────────────────────────────────────────┐   │
│  │         Global Memory (HBM)                     │   │
│  │  ┌─────────────────────────────────────────┐   │   │
│  │  │  输入/输出张量                            │   │   │
│  │  │  - x (输入张量)                           │   │   │
│  │  │  - y (输出张量)                           │   │   │
│  │  └─────────────────────────────────────────┘   │   │
│  │  ┌─────────────────────────────────────────┐   │   │
│  │  │  Workspace (工作空间)                    │   │   │
│  │  │  - 系统Workspace (API预留)               │   │   │
│  │  │  - 用户Workspace (算子使用)               │   │   │
│  │  └─────────────────────────────────────────┘   │   │
│  └─────────────────────────────────────────────────┘   │
│                           ▲                              │
│                           │ DataCopy                     │
│  ┌─────────────────────────────────────────────────┐   │
│  │         LocalMemory (片上存储)                  │   │
│  │  ┌─────────────────────────────────────────┐   │   │
│  │  │  Unified Buffer (UB)                    │   │   │
│  │  │  ┌───────────────────────────────────┐  │   │   │
│  │  │  │  TQue (队列管理)                 │  │   │   │
│  │  │  │  - inQueueX (输入队列)            │  │   │   │
│  │  │  │  - outQueueY (输出队列)           │  │   │   │
│  │  │  └───────────────────────────────────┘  │   │   │
│  │  │  ┌───────────────────────────────────┐  │   │   │
│  │  │  │  TBuf (临时存储)                 │  │   │   │
│  │  │  │  - tmpBuf (计算过程临时存储)      │  │   │   │
│  │  │  └───────────────────────────────────┘  │   │   │
│  │  └─────────────────────────────────────────┘   │   │
│  │  ┌─────────────────────────────────────────┐   │   │
│  │  │  L1 Buffer / L0 Buffer                  │   │   │
│  │  └─────────────────────────────────────────┘   │   │
│  └─────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

### 9.2 数据流动过程

```
1. Host侧准备数据
   └─> Host Memory: 输入数据x

2. 数据搬运到Device侧
   └─> aclrtMemcpy(Host → Device)
       └─> Global Memory: x (输入张量)

3. 核函数执行
   └─> CopyIn: Global Memory → LocalMemory
       └─> TQue: inQueueX (输入队列)
   
   └─> Compute: LocalMemory计算
       └─> TBuf: tmpBuf (临时存储)
       └─> TQue: outQueueY (输出队列)
   
   └─> CopyOut: LocalMemory → Global Memory
       └─> Global Memory: y (输出张量)
   
   └─> Workspace使用 (如果需要)
       └─> Global Memory: workspace (工作空间)

4. 结果回拷到Host侧
   └─> aclrtMemcpy(Device → Host)
       └─> Host Memory: 输出数据y
```

### 9.3 内存访问速度对比

| 内存类型                | 访问速度 | 容量 | 用途                        |
| ----------------------- | -------- | ---- | --------------------------- |
| **Host Memory**   | 慢       | 大   | 存储原始数据                |
| **Global Memory** | 中等     | 大   | 存储输入输出张量、Workspace |
| **LocalMemory**   | 快       | 小   | 计算过程中的临时数据        |
| **TBuf**          | 最快     | 最小 | 计算过程中的中间结果        |

## 10. 关键知识点总结

### 10.1 C++模板编程

| 概念               | 说明                       |
| ------------------ | -------------------------- |
| **函数模板** | 支持多种数据类型的函数     |
| **类模板**   | 支持多种数据类型的类       |
| **模板特化** | 针对特定类型的特殊实现     |
| **类型推导** | 编译器自动推导模板参数类型 |

### 10.2 Workspace

| 概念                    | 说明                       |
| ----------------------- | -------------------------- |
| **系统Workspace** | Ascend C API需要的预留空间 |
| **用户Workspace** | 算子实现使用的临时空间     |
| **内存位置**      | Global Memory (HBM)        |
| **配置方式**      | Host侧Tiling函数中设置大小 |

### 10.3 TBuf

| 概念               | 说明                    |
| ------------------ | ----------------------- |
| **作用**     | LocalMemory上的临时存储 |
| **内存位置** | LocalMemory (片上)      |
| **初始化**   | pipe.InitBuffer()       |
| **获取方式** | Get`<T>`()            |
| **释放方式** | 无需手动释放            |

### 10.4 算子属性

| 概念               | 说明               |
| ------------------ | ------------------ |
| **必选属性** | 算子运行的核心前提 |
| **可选属性** | 算子的辅助配置     |
| **静态性**   | 运行过程中不可修改 |
| **解耦性**   | 与输入张量相互独立 |

### 10.5 Tiling模板编程

| 概念                   | 说明                          |
| ---------------------- | ----------------------------- |
| **TilingKey**    | 区分不同实现的标识            |
| **模板参数**     | 替代数字TilingKey的有意义名称 |
| **数据类型参数** | ASCENDC_TPL_DATATYPE_DECL     |
| **整数参数**     | ASCENDC_TPL_UINT_DECL         |
| **布尔参数**     | ASCENDC_TPL_BOOL_DECL         |
| **Host侧设置**   | ASCENDC_TPL_SEL_PARAM         |
| **Device侧使用** | 模板参数作为函数参数          |


**参考文献：**

- 华为昇腾 CANN 官方文档 (v6.3.0+)
- 《Ascend C 编程指南》
- 《AscendCL 接口参考》
- 《C++ Templates: The Complete Guide》
- PyTorch官方文档
