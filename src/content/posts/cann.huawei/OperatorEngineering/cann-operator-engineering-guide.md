---
title: 华为昇腾CANN算子工程化开发全指南：从零构建到部署实战
published: 2026-05-19
tags: [CANN, 华为昇腾, 算子开发, AI, AscendC, C++, 工程化]
pinned: true
category: 技术博客
draft: false
---

# 华为昇腾CANN算子工程化开发全指南：从零构建到部署实战

> 本文通过一个完整的向量减法算子工程，深入讲解华为昇腾CANN的算子工程化开发流程。从算子原型定义到工程创建，从Host侧Tiling机制到Kernel侧核函数实现，带你一步步掌握昇腾AI芯片的算子工程化开发范式。

## 1. 背景与目标

在之前的算子开发中，我们主要关注核函数的实现，但在实际生产环境中，算子开发需要更完整的工程化体系。**CANN算子工程**提供了一套标准化的开发范式，涵盖了从Host侧的算子注册、形状推导、Tiling分块、任务下发与内存管理，到Kernel侧使用AscendC编写核函数计算逻辑的完整流程。

本文将以一个向量减法算子`SubCustomTemplate`为例，带你完整体验算子工程化开发的各个环节：

- 理解算子工程的整体架构和调用关系
- 掌握算子原型定义和工程创建方法
- 深入理解Tiling机制的作用和实现
- 学习Host侧的Shape推导、Dtype推导和算子注册
- 掌握Kernel侧的核函数实现和泛型编程
- 了解工程编译部署和源码管理

## 2. 算子工程整体架构

### 2.1 工程化开发 vs 独立核函数开发

| 对比维度 | 独立核函数开发 | 工程化算子开发 |
|---------|--------------|--------------|
| **开发范围** | 仅关注Kernel侧计算逻辑 | Host侧 + Kernel侧完整实现 |
| **内存管理** | 手动管理AscendCL接口 | 框架自动管理内存分配 |
| **形状推导** | 需要手动处理 | 框架自动推导和验证 |
| **动态Shape** | 支持有限 | 完整支持动态Shape |
| **算子注册** | 无需注册 | 自动注册到框架 |
| **部署方式** | 手动编译和部署 | 生成标准算子包 |
| **适用场景** | 学习和实验 | 生产环境和算子库 |

### 2.2 算子工程目录结构

一个标准的CANN算子工程具有清晰的目录结构：

```
custom_op/
├── CMakeLists.txt              # 主构建配置文件
├── CMakePresets.json          # CMake预设配置
├── build.sh                   # 编译脚本
├── framework/                 # 框架适配层
│   ├── CMakeLists.txt
│   └── tf_plugin/            # TensorFlow插件适配
│       ├── CMakeLists.txt
│       └── tensorflow_sub_custom_template_plugin.cc
├── op_host/                   # Host侧实现
│   ├── CMakeLists.txt
│   └── sub_custom_template.cpp  # 算子原型注册、Tiling、推导函数
├── op_kernel/                 # Kernel侧实现
│   ├── CMakeLists.txt
│   ├── sub_custom_template.cpp  # 核函数实现
│   └── sub_custom_template_tiling.h  # Tiling数据结构定义
└── build_out/                 # 编译输出目录
    ├── custom_opp_ubuntu_aarch64.run  # 最终算子安装包
    └── autogen/              # 自动生成的文件
```

### 2.3 调用关系与数据流

算子工程的调用关系体现了清晰的分层架构：

```
┌─────────────────────────────────────────────────────────┐
│                    应用层 (PyTorch/TF)                   │
└────────────────────┬────────────────────────────────────┘
                     │ 调用算子API
┌────────────────────▼────────────────────────────────────┐
│              算子API层 (aclnn_sub_custom_template)       │
│  - 参数验证                                              │
│  - 内存分配                                              │
│  - 任务下发                                              │
└────────────────────┬────────────────────────────────────┘
                     │ 传递Tiling数据
┌────────────────────▼────────────────────────────────────┐
│              Host侧 (op_host/sub_custom_template.cpp)    │
│  - 算子原型注册 (SubCustomTemplate)                      │
│  - Shape推导 (InferShape)                                │
│  - Dtype推导 (InferDataType)                             │
│  - Tiling计算 (TilingFunc)                               │
└────────────────────┬────────────────────────────────────┘
                     │ 启动核函数
┌────────────────────▼────────────────────────────────────┐
│           Kernel侧 (op_kernel/sub_custom_template.cpp)   │
│  - 核函数入口 (sub_custom_template)                      │
│  - 算子类实现 (KernelSub)                                │
│  - 流水线并行 (CopyIn/Compute/CopyOut)                   │
└─────────────────────────────────────────────────────────┘
```

## 3. 算子原型定义与工程创建

### 3.1 算子原型定义

算子原型定义是工程创建的第一步，通过JSON文件描述算子的输入输出规格。以减法算子为例：

```json
[
    {
        "op": "SubCustomTemplate",
        "input_desc": [
            {
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
        "output_desc": [
            {
                "name": "z",
                "param_type": "required",
                "format": ["ND", "ND"],
                "type": ["float16", "float"]
            }
        ]
    }
]
```

**关键字段说明：**

- `op`: 算子类型名称，必须唯一
- `input_desc`: 输入张量描述数组
  - `name`: 输入参数名称
  - `param_type`: 参数类型（required/optional）
  - `format`: 支持的数据格式（ND/NC1HWC0等）
  - `type`: 支持的数据类型（float16/float/int32等）
- `output_desc`: 输出张量描述数组，格式同输入

### 3.2 使用msOpGen工具创建工程

CANN提供了`msOpGen`工具来自动生成算子工程框架：

```bash
# 安装msOpGen工具
git clone https://gitee.com/ascend/msopgen.git
cd msopgen
python3 setup.py install

# 创建算子工程
msopgen gen -i sub_custom_template.json -c custom_op -t AscendC910B
```

工具会自动生成以下内容：

- 完整的目录结构
- 基础的CMakeLists.txt配置文件
- 算子原型注册框架代码
- Kernel侧核函数框架代码
- 编译脚本和配置文件

## 4. Host侧实现详解

### 4.1 命名空间与作用域

在Host侧实现中，C++的命名空间起到了重要作用，避免了全局命名冲突：

```cpp
namespace optiling {
    // Tiling函数实现
    static ge::graphStatus TilingFunc(gert::TilingContext *context) {
        // 实现代码
    }
}  // namespace optiling

namespace ge {
    // Shape和Dtype推导函数
    static ge::graphStatus InferShape(gert::InferShapeContext* context) {
        // 实现代码
    }
}  // namespace ge

namespace ops {
    // 算子原型注册
    class SubCustomTemplate : public OpDef {
        // 实现代码
    };
}  // namespace ops
```

**命名空间的作用：**

1. **optiling**: 包含Tiling相关函数，避免与框架函数冲突
2. **ge**: 包含图引擎相关的推导函数
3. **ops**: 包含算子定义和注册

### 4.2 Tiling机制详解

#### 4.2.1 什么是Tiling？

**Tiling**是昇腾AI芯片编程中的核心概念，源于AI Core的片上存储容量限制。当输入数据量较大时，无法一次性加载所有数据到片上存储，因此需要将数据切分为多个小块，通过"搬运→计算→回写"的循环流水线完成全量计算。

#### 4.2.2 Tiling的直观理解

假设AI Core的片上存储容量为10个元素，需要计算长度为10的向量加法：

```python
# 片上存储容量
max_ub = 10

# 输入输出张量数量
input_output_num = 3  # A, B, C三个张量

# 单次最大处理数据量
max_once_num = max_ub // input_output_num  # 3个元素

# 需要的分块次数
tile_num = (10 + max_once_num - 1) // max_once_num  # 4次
```

**分块计算过程：**

```
原始数据: A = [1,2,3,4,5,6,7,8,9,10], B = [5,5,5,5,5,5,5,5,5,5]

Tile 0: A[0:3] + B[0:3] = [1,2,3] + [5,5,5] = [6,7,8]
Tile 1: A[3:6] + B[3:6] = [4,5,6] + [5,5,5] = [9,10,11]
Tile 2: A[6:9] + B[6:9] = [7,8,9] + [5,5,5] = [12,13,14]
Tile 3: A[9:10] + B[9:10] = [10] + [5] = [15]

最终结果: [6,7,8,9,10,11,12,13,14,15]
```

#### 4.2.3 Tiling数据结构定义

首先定义Tiling数据结构，用于在Host和Kernel之间传递参数：

```cpp
#ifndef SUB_CUSTOM_TEMPLATE_TILING_H
#define SUB_CUSTOM_TEMPLATE_TILING_H
#include <cstdint>

struct SubCustomTemplateTilingData {
    uint32_t totalLength;  // 输入向量总长度
    uint32_t tileNum;      // 分块数量
};

#endif
```

**设计要点：**

- 使用`struct`而非`class`，默认public访问权限
- 成员变量使用明确的数据类型（`uint32_t`）
- 结构体名称以`TilingData`结尾，便于识别

#### 4.2.4 Tiling函数实现

Tiling函数在Host侧执行，负责计算分块参数：

```cpp
namespace optiling {
    static ge::graphStatus TilingFunc(gert::TilingContext *context)
    {
        // 获取输入张量的总元素数量
        uint32_t totalLength = context->GetInputShape(0)->GetOriginShape().GetShapeSize();

        // 设置使用的AI Core数量
        context->SetBlockDim(8);

        // 获取Tiling数据结构指针
        SubCustomTemplateTilingData *tiling = context->GetTilingData<SubCustomTemplateTilingData>();

        // 设置Tiling参数
        tiling->totalLength = totalLength;
        tiling->tileNum = 1;  // 简单实现，实际可根据数据量动态计算

        return ge::GRAPH_SUCCESS;
    }
}  // namespace optiling
```

**关键步骤：**

1. **获取输入信息**: 通过`context->GetInputShape(0)`获取第一个输入的形状信息
2. **设置并行度**: `SetBlockDim(8)`指定使用8个AI Core并行计算
3. **填充Tiling数据**: 将计算好的参数写入Tiling结构体
4. **返回状态**: 成功返回`GRAPH_SUCCESS`

### 4.3 Shape推导函数

Shape推导函数根据输入张量的形状推断输出张量的形状：

```cpp
namespace ge {
    static ge::graphStatus InferShape(gert::InferShapeContext* context)
    {
        // 获取第一个输入的形状
        const gert::Shape* x1_shape = context->GetInputShape(0);

        // 获取输出张量的形状指针
        gert::Shape* y_shape = context->GetOutputShape(0);

        // 输出形状等于输入形状（减法算子特性）
        *y_shape = *x1_shape;

        return GRAPH_SUCCESS;
    }
}
```

**作用与意义：**

- **静态内存分配**: 在构图阶段为输出张量预分配内存
- **形状验证**: 确保输入输出形状匹配
- **广播支持**: 可扩展支持不同形状的广播运算

### 4.4 Dtype推导函数

Dtype推导函数根据输入数据类型推断输出数据类型：

```cpp
namespace ge {
    static ge::graphStatus InferDataType(gert::InferDataTypeContext *context)
    {
        // 获取第一个输入的数据类型
        const auto inputDataType = context->GetInputDataType(0);

        // 输出数据类型等于输入数据类型
        context->SetOutputDataType(0, inputDataType);

        return ge::GRAPH_SUCCESS;
    }
}
```

**设计考虑：**

- **类型一致性**: 减法算子保持输入输出类型一致
- **类型转换**: 可扩展支持不同类型的自动转换
- **类型安全**: 确保类型推导的正确性

### 4.5 算子原型注册

算子原型注册是将自定义算子注册到CANN框架的关键步骤：

```cpp
namespace ops {
    class SubCustomTemplate : public OpDef {
    public:
        explicit SubCustomTemplate(const char* name) : OpDef(name)
        {
            // 注册输入张量x
            this->Input("x")
                .ParamType(REQUIRED)
                .DataType({ge::DT_FLOAT16, ge::DT_FLOAT})
                .Format({ge::FORMAT_ND, ge::FORMAT_ND})
                .UnknownShapeFormat({ge::FORMAT_ND, ge::FORMAT_ND});

            // 注册输入张量y
            this->Input("y")
                .ParamType(REQUIRED)
                .DataType({ge::DT_FLOAT16, ge::DT_FLOAT})
                .Format({ge::FORMAT_ND, ge::FORMAT_ND})
                .UnknownShapeFormat({ge::FORMAT_ND, ge::FORMAT_ND});

            // 注册输出张量z
            this->Output("z")
                .ParamType(REQUIRED)
                .DataType({ge::DT_FLOAT16, ge::DT_FLOAT})
                .Format({ge::FORMAT_ND, ge::FORMAT_ND})
                .UnknownShapeFormat({ge::FORMAT_ND, ge::FORMAT_ND});

            // 设置推导函数
            this->SetInferShape(ge::InferShape)
                    .SetInferDataType(ge::InferDataType);

            // 设置Tiling函数和目标芯片
            this->AICore()
                .SetTiling(optiling::TilingFunc);
            this->AICore().AddConfig("ascend910b");
        }
    };

    // 注册算子到框架
    OP_ADD(SubCustomTemplate);
}
```

**注册流程：**

1. **继承OpDef**: 自定义算子类继承`OpDef`基类
2. **构造函数**: 在构造函数中完成算子属性配置
3. **输入输出注册**: 使用`Input()`和`Output()`方法注册张量
4. **函数绑定**: 将推导函数和Tiling函数绑定到算子
5. **算子注册**: 使用`OP_ADD`宏将算子注册到框架

## 5. Kernel侧实现详解

### 5.1 C++泛型编程的应用

Kernel侧大量使用了C++模板（泛型编程）来支持不同的数据类型：

```cpp
template <class dtypeX, class dtypeY, class dtypeZ>
class KernelSub {
public:
    __aicore__ inline KernelSub() {}

    __aicore__ inline void Init(GM_ADDR x, GM_ADDR y, GM_ADDR z,
                                uint32_t totalLength, uint32_t tileNum)
    {
        // 根据Tiling数据动态初始化
        this->blockLength = totalLength / AscendC::GetBlockNum();
        this->tileNum = tileNum;
        this->tileLength = this->blockLength / tileNum / BUFFER_NUM;

        // 设置全局内存缓冲区
        xGm.SetGlobalBuffer((__gm__ dtypeX *)x + this->blockLength * AscendC::GetBlockIdx(),
                           this->blockLength);
        yGm.SetGlobalBuffer((__gm__ dtypeY *)y + this->blockLength * AscendC::GetBlockIdx(),
                           this->blockLength);
        zGm.SetGlobalBuffer((__gm__ dtypeZ *)z + this->blockLength * AscendC::GetBlockIdx(),
                           this->blockLength);

        // 初始化队列缓冲区
        pipe.InitBuffer(inQueueX, BUFFER_NUM, this->tileLength * sizeof(dtypeX));
        pipe.InitBuffer(inQueueY, BUFFER_NUM, this->tileLength * sizeof(dtypeY));
        pipe.InitBuffer(outQueueZ, BUFFER_NUM, this->tileLength * sizeof(dtypeZ));
    }

    __aicore__ inline void Process()
    {
        int32_t loopCount = this->tileNum * BUFFER_NUM;
        for (int32_t i = 0; i < loopCount; i++) {
            CopyIn(i);
            Compute(i);
            CopyOut(i);
        }
        AscendC::printf("Core %d executed %d times in total\n",
                       AscendC::GetBlockIdx(), loopCount);
    }
```

**泛型编程的优势：**

1. **类型安全**: 编译时类型检查，避免运行时错误
2. **代码复用**: 一套代码支持多种数据类型
3. **性能优化**: 编译器针对具体类型进行优化
4. **接口统一**: 不同数据类型使用相同的接口

### 5.2 流水线并行实现

#### 5.2.1 CopyIn - 数据搬运

```cpp
private:
    __aicore__ inline void CopyIn(int32_t progress)
    {
        // 从队列分配本地张量
        AscendC::LocalTensor<dtypeX> xLocal = inQueueX.AllocTensor<dtypeX>();
        AscendC::LocalTensor<dtypeY> yLocal = inQueueY.AllocTensor<dtypeY>();

        // 从全局内存拷贝数据到本地内存
        AscendC::DataCopy(xLocal, xGm[progress * this->tileLength], this->tileLength);
        AscendC::DataCopy(yLocal, yGm[progress * this->tileLength], this->tileLength);

        // 将本地张量加入队列
        inQueueX.EnQue(xLocal);
        inQueueY.EnQue(yLocal);
    }
```

**CopyIn的作用：**

- **数据预取**: 提前将数据搬运到本地内存
- **并行准备**: 为计算单元准备数据
- **流水线优化**: 与计算和输出重叠执行

#### 5.2.2 Compute - 核心计算

```cpp
    __aicore__ inline void Compute(int32_t progress)
    {
        // 从队列获取输入张量
        AscendC::LocalTensor<dtypeX> xLocal = inQueueX.DeQue<dtypeX>();
        AscendC::LocalTensor<dtypeY> yLocal = inQueueY.DeQue<dtypeY>();

        // 分配输出张量
        AscendC::LocalTensor<dtypeZ> zLocal = outQueueZ.AllocTensor<dtypeZ>();

        // 执行减法计算
        AscendC::Sub(zLocal, xLocal, yLocal, this->tileLength);

        // 将输出张量加入队列
        outQueueZ.EnQue<dtypeZ>(zLocal);

        // 释放输入张量
        inQueueX.FreeTensor(xLocal);
        inQueueY.FreeTensor(yLocal);
    }
```

**Compute的特点：**

- **本地计算**: 在片上存储中执行计算
- **向量化操作**: 使用AscendC的向量化指令
- **内存管理**: 及时释放不再使用的张量

#### 5.2.3 CopyOut - 结果输出

```cpp
    __aicore__ inline void CopyOut(int32_t progress)
    {
        // 从队列获取输出张量
        AscendC::LocalTensor<dtypeZ> zLocal = outQueueZ.DeQue<dtypeZ>();

        // 将结果拷贝回全局内存
        AscendC::DataCopy(zGm[progress * this->tileLength], zLocal, this->tileLength);

        // 释放输出张量
        outQueueZ.FreeTensor(zLocal);
    }
```

**CopyOut的作用：**

- **结果回写**: 将计算结果写回全局内存
- **内存释放**: 及时释放本地内存资源
- **流水线衔接**: 为下一轮计算腾出空间

### 5.3 核函数入口

核函数是Kernel侧的入口点，负责初始化和调用算子类：

```cpp
extern "C" __global__ __aicore__ void sub_custom_template(
    GM_ADDR x, GM_ADDR y, GM_ADDR z, GM_ADDR workspace, GM_ADDR tiling)
{
    // 注册Tiling数据结构
    REGISTER_TILING_DEFAULT(SubCustomTemplateTilingData);

    // 获取Tiling数据
    GET_TILING_DATA(tilingData, tiling);

    // 实例化算子类
    KernelSub<DTYPE_X, DTYPE_Y, DTYPE_Z> op;

    // 初始化算子
    op.Init(x, y, z, tilingData.totalLength, tilingData.tileNum);

    // 执行计算
    op.Process();
}
```

**关键宏说明：**

1. **REGISTER_TILING_DEFAULT**: 注册Tiling数据结构类型
2. **GET_TILING_DATA**: 从tiling参数中解析数据到结构体
3. **extern "C"**: 使用C链接方式，避免C++名称修饰

### 5.4 私有成员变量

```cpp
private:
    AscendC::TPipe pipe;                                            // 内存管理管道
    AscendC::TQue<AscendC::TPosition::VECIN, BUFFER_NUM> inQueueX;  // 输入队列X
    AscendC::TQue<AscendC::TPosition::VECIN, BUFFER_NUM> inQueueY;  // 输入队列Y
    AscendC::TQue<AscendC::TPosition::VECOUT, BUFFER_NUM> outQueueZ; // 输出队列Z
    AscendC::GlobalTensor<dtypeX> xGm;                              // 全局张量X
    AscendC::GlobalTensor<dtypeY> yGm;                              // 全局张量Y
    AscendC::GlobalTensor<dtypeZ> zGm;                              // 全局张量Z
    uint32_t blockLength;   // 每个AI Core处理的数据长度
    uint32_t tileNum;       // 分块数量
    uint32_t tileLength;    // 每块的长度
};
```

**成员变量作用：**

- **TPipe**: 管理片上内存的分配和释放
- **TQue**: 实现数据流的生产者-消费者模式
- **GlobalTensor**: 管理全局内存中的张量
- **长度参数**: 控制数据切分和并行策略

## 6. 工程编译与部署

### 6.1 编译配置

工程使用CMake构建系统，主要配置文件：

```cmake
cmake_minimum_required(VERSION 3.16.0)
project(opp)
find_package(ASC REQUIRED)
set(package_name ${vendor_name})

npu_op_package(${package_name}
    TYPE RUN
    CONFIG
        INSTALL_PATH ${CMAKE_BINARY_DIR}/
)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/framework)
    add_subdirectory(framework)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/op_host)
    add_subdirectory(op_host)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/op_kernel)
    add_subdirectory(op_kernel)
endif()
```

### 6.2 编译脚本

```bash
#!/bin/bash
if [ -z "$BASE_LIBS_PATH" ]; then
    if [ -z "$ASCEND_HOME_PATH" ]; then
        if [ -z "$ASCEND_AICPU_PATH" ]; then
            echo "please set env."
            exit 1
        else
            export ASCEND_HOME_PATH=$ASCEND_AICPU_PATH
        fi
    else
        export ASCEND_HOME_PATH=$ASCEND_HOME_PATH
    fi
else
    export ASCEND_HOME_PATH=$BASE_LIBS_PATH
fi

script_path=$(realpath $(dirname $0))
BUILD_DIR="build_out"
mkdir -p build_out
rm -rf build_out/*

# 解析CMake预设
opts=$(python3 $ASCEND_HOME_PATH/tools/tikcpp/ascendc_kernel_cmake/fwk_modules/util/preset_parse.py $script_path/CMakePresets.json)

# 执行CMake构建
cmake -S . -B "$BUILD_DIR" --preset=default
cmake --build "$BUILD_DIR" --target binary package -j$(nproc)
```

### 6.3 编译输出

编译成功后会生成算子安装包：

```
build_out/
├── custom_opp_ubuntu_aarch64.run  # 算子安装包
└── autogen/
    ├── aclnn_sub_custom_template.cpp    # 自动生成的API接口
    ├── aclnn_sub_custom_template.h      # API头文件
    ├── op_proto.cc                       # 算子原型代码
    └── op_proto.h                        # 算子原型头文件
```

### 6.4 部署安装

```bash
# 安装算子包
bash custom_opp_ubuntu_aarch64.run

# 验证安装
ls /usr/local/Ascend/opp/vendors/customize/
```

## 7. 源码管理与最佳实践

### 7.1 代码组织原则

1. **模块化设计**: Host侧和Kernel侧分离，职责清晰
2. **命名规范**: 文件名、函数名、变量名保持一致性
3. **注释完整**: 关键算法和接口添加详细注释
4. **版本控制**: 使用Git进行版本管理

### 7.2 C++编程要点

#### 7.2.1 命名空间使用

```cpp
// 好的实践：使用命名空间避免冲突
namespace optiling {
    void TilingFunc() {}
}

// 避免：全局命名空间污染
void TilingFunc() {}  // 不推荐
```

#### 7.2.2 const正确性

```cpp
// 好的实践：使用const保护不变数据
static ge::graphStatus InferShape(const gert::InferShapeContext* context) {
    const gert::Shape* inputShape = context->GetInputShape(0);
    // ...
}

// 避免：忽略const修饰
static ge::graphStatus InferShape(gert::InferShapeContext* context) {
    gert::Shape* inputShape = context->GetInputShape(0);  // 不推荐
    // ...
}
```

#### 7.2.3 模板特化

```cpp
// 好的实践：为特定类型提供优化实现
template <>
class KernelSub<float, float, float> {
    // 针对float类型的特殊优化
};

// 通用模板实现
template <class dtypeX, class dtypeY, class dtypeZ>
class KernelSub {
    // 通用实现
};
```


## 完整源码与实战命令

### 完整源码文件

#### 算子原型定义文件 (sub_custom_template.json)

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

#### 9.1.2 Tiling数据结构定义 (op_kernel/sub_custom_template_tiling.h)

```cpp
#ifndef SUB_CUSTOM_TEMPLATE_TILING_H
#define SUB_CUSTOM_TEMPLATE_TILING_H
#include <cstdint>

struct SubCustomTemplateTilingData {
    uint32_t totalLength;
    uint32_t tileNum;
};

#endif
```

#### 9.1.3 Host侧实现 (op_host/sub_custom_template.cpp)

```cpp
#include "../op_kernel/sub_custom_template_tiling.h"
#include "register/op_def_registry.h"

namespace optiling {
    static ge::graphStatus TilingFunc(gert::TilingContext *context)
    {
        uint32_t totalLength = context->GetInputShape(0)->GetOriginShape().GetShapeSize();
        context->SetBlockDim(8);
        SubCustomTemplateTilingData *tiling = context->GetTilingData<SubCustomTemplateTilingData>();
        tiling->totalLength = totalLength;
        tiling->tileNum = 1;
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
            this->AICore().SetTiling(optiling::TilingFunc);
            this->AICore().AddConfig("ascend910b");
        }
    };
    OP_ADD(SubCustomTemplate);
}
```

#### 9.1.4 Kernel侧实现 (op_kernel/sub_custom_template.cpp)

```cpp
#include "kernel_operator.h"
#include "sub_custom_template_tiling.h"
#include "kernel_operator_dump_tensor_intf_impl.h"

constexpr int32_t BUFFER_NUM = 1;

template <class dtypeX, class dtypeY, class dtypeZ>
class KernelSub {
public:
    __aicore__ inline KernelSub() {}
    
    __aicore__ inline void Init(GM_ADDR x, GM_ADDR y, GM_ADDR z, uint32_t totalLength, uint32_t tileNum)
    {
        this->blockLength = totalLength / AscendC::GetBlockNum();
        this->tileNum = tileNum;
        this->tileLength = this->blockLength / tileNum / BUFFER_NUM;
        
        xGm.SetGlobalBuffer((__gm__ dtypeX *)x + this->blockLength * AscendC::GetBlockIdx(), this->blockLength);
        yGm.SetGlobalBuffer((__gm__ dtypeY *)y + this->blockLength * AscendC::GetBlockIdx(), this->blockLength);
        zGm.SetGlobalBuffer((__gm__ dtypeZ *)z + this->blockLength * AscendC::GetBlockIdx(), this->blockLength);
        
        pipe.InitBuffer(inQueueX, BUFFER_NUM, this->tileLength * sizeof(dtypeX));
        pipe.InitBuffer(inQueueY, BUFFER_NUM, this->tileLength * sizeof(dtypeY));
        pipe.InitBuffer(outQueueZ, BUFFER_NUM, this->tileLength * sizeof(dtypeZ));
    }

    __aicore__ inline void Process()
    {
        int32_t loopCount = this->tileNum * BUFFER_NUM;
        for (int32_t i = 0; i < loopCount; i++) {
            CopyIn(i);
            Compute(i);
            CopyOut(i);
        }
        AscendC::printf("Core %d executed %d times in total\n", AscendC::GetBlockIdx(), loopCount);
    }

private:
    __aicore__ inline void CopyIn(int32_t progress)
    {
        AscendC::LocalTensor<dtypeX> xLocal = inQueueX.AllocTensor<dtypeX>();
        AscendC::LocalTensor<dtypeY> yLocal = inQueueY.AllocTensor<dtypeY>();
        AscendC::DataCopy(xLocal, xGm[progress * this->tileLength], this->tileLength);
        AscendC::DataCopy(yLocal, yGm[progress * this->tileLength], this->tileLength);
        inQueueX.EnQue(xLocal);
        inQueueY.EnQue(yLocal);
    }
    
    __aicore__ inline void Compute(int32_t progress)
    {
        AscendC::LocalTensor<dtypeX> xLocal = inQueueX.DeQue<dtypeX>();
        AscendC::LocalTensor<dtypeY> yLocal = inQueueY.DeQue<dtypeY>();
        AscendC::LocalTensor<dtypeZ> zLocal = outQueueZ.AllocTensor<dtypeZ>();
        AscendC::Sub(zLocal, xLocal, yLocal, this->tileLength);
        outQueueZ.EnQue<dtypeZ>(zLocal);
        inQueueX.FreeTensor(xLocal);
        inQueueY.FreeTensor(yLocal);
    }
    
    __aicore__ inline void CopyOut(int32_t progress)
    {
        AscendC::LocalTensor<dtypeZ> zLocal = outQueueZ.DeQue<dtypeZ>();
        AscendC::DataCopy(zGm[progress * this->tileLength], zLocal, this->tileLength);
        outQueueZ.FreeTensor(zLocal);
    }

private:
    AscendC::TPipe pipe;
    AscendC::TQue<AscendC::TPosition::VECIN, BUFFER_NUM> inQueueX;
    AscendC::TQue<AscendC::TPosition::VECIN, BUFFER_NUM> inQueueY;
    AscendC::TQue<AscendC::TPosition::VECOUT, BUFFER_NUM> outQueueZ;
    AscendC::GlobalTensor<dtypeX> xGm;
    AscendC::GlobalTensor<dtypeY> yGm;
    AscendC::GlobalTensor<dtypeZ> zGm;
    uint32_t blockLength;
    uint32_t tileNum;
    uint32_t tileLength;
};

extern "C" __global__ __aicore__ void sub_custom_template(GM_ADDR x, GM_ADDR y, GM_ADDR z, GM_ADDR workspace, GM_ADDR tiling) {
    REGISTER_TILING_DEFAULT(SubCustomTemplateTilingData);
    GET_TILING_DATA(tilingData, tiling);
    KernelSub<DTYPE_X, DTYPE_Y, DTYPE_Z> op;
    op.Init(x, y, z, tilingData.totalLength, tilingData.tileNum);
    op.Process();
}
```

#### 9.1.5 测试程序 (test/main.cpp)

```cpp
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>
#include "acl/acl.h"
#include "aclnn_sub_custom_template.h"

#define SUCCESS 0
#define FAILED 1

#define CHECK_RET(cond, return_expr) \
    do {                             \
        if (!(cond)) {               \
            return_expr;             \
        }                            \
    } while (0)

#define LOG_PRINT(message, ...)         \
    do {                                \
        printf(message, ##__VA_ARGS__); \
    } while (0)

int64_t GetShapeSize(const std::vector<int64_t> &shape)
{
    int64_t shapeSize = 1;
    for (auto i : shape) {
        shapeSize *= i;
    }
    return shapeSize;
}

int Init(int32_t deviceId, aclrtStream *stream)
{
    auto ret = aclInit(nullptr);
    CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("aclInit failed. ERROR: %d\n", ret); return FAILED);
    ret = aclrtSetDevice(deviceId);
    CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("aclrtSetDevice failed. ERROR: %d\n", ret); return FAILED);
    ret = aclrtCreateStream(stream);
    CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("aclrtCreateStream failed. ERROR: %d\n", ret); return FAILED);
    return SUCCESS;
}

template <typename T>
int CreateAclTensor(const std::vector<T> &hostData, const std::vector<int64_t> &shape, void **deviceAddr,
                    aclDataType dataType, aclTensor **tensor)
{
    auto size = GetShapeSize(shape) * sizeof(T);
    auto ret = aclrtMalloc(deviceAddr, size, ACL_MEM_MALLOC_HUGE_FIRST);
    CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("aclrtMalloc failed. ERROR: %d\n", ret); return FAILED);
    ret = aclrtMemcpy(*deviceAddr, size, hostData.data(), size, ACL_MEMCPY_HOST_TO_DEVICE);
    CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("aclrtMemcpy failed. ERROR: %d\n", ret); return FAILED);
    *tensor = aclCreateTensor(shape.data(), shape.size(), dataType, nullptr, 0, aclFormat::ACL_FORMAT_ND, shape.data(),
                              shape.size(), *deviceAddr);
    return SUCCESS;
}

void DestroyResources(std::vector<void *> tensors, std::vector<void *> deviceAddrs, aclrtStream stream,
                      int32_t deviceId, void *workspaceAddr = nullptr)
{
    for (uint32_t i = 0; i < tensors.size(); i++) {
        if (tensors[i] != nullptr) {
            aclDestroyTensor(reinterpret_cast<aclTensor *>(tensors[i]));
        }
        if (deviceAddrs[i] != nullptr) {
            aclrtFree(deviceAddrs[i]);
        }
    }
    if (workspaceAddr != nullptr) {
        aclrtFree(workspaceAddr);
    }
    aclrtDestroyStream(stream);
    aclrtResetDevice(deviceId);
    aclFinalize();
}

int main(int argc, char **argv)
{
    int32_t deviceId = 0;
    aclrtStream stream;
    auto ret = Init(deviceId, &stream);
    CHECK_RET(ret == 0, LOG_PRINT("Init acl failed. ERROR: %d\n", ret); return FAILED);

    std::vector<int64_t> inputXShape = {8, 2048};
    std::vector<int64_t> inputYShape = {8, 2048};
    std::vector<int64_t> outputZShape = {8, 2048};
    
    void *inputXDeviceAddr = nullptr;
    void *inputYDeviceAddr = nullptr;
    void *outputZDeviceAddr = nullptr;
    aclTensor *inputX = nullptr;
    aclTensor *inputY = nullptr;
    aclTensor *outputZ = nullptr;
    
    std::vector<aclFloat16> inputXHostData(inputXShape[0] * inputXShape[1]);
    std::vector<aclFloat16> inputYHostData(inputYShape[0] * inputYShape[1]);
    std::vector<aclFloat16> outputZHostData(outputZShape[0] * outputZShape[1]);
    
    for (int i = 0; i < inputXShape[0] * inputXShape[1]; ++i) {
        inputXHostData[i] = aclFloatToFloat16(1.0);
        inputYHostData[i] = aclFloatToFloat16(2.0);
        outputZHostData[i] = aclFloatToFloat16(0.0);
    }
    
    std::vector<void *> tensors = {inputX, inputY, outputZ};
    std::vector<void *> deviceAddrs = {inputXDeviceAddr, inputYDeviceAddr, outputZDeviceAddr};
    
    ret = CreateAclTensor(inputXHostData, inputXShape, &inputXDeviceAddr, aclDataType::ACL_FLOAT16, &inputX);
    CHECK_RET(ret == ACL_SUCCESS, DestroyResources(tensors, deviceAddrs, stream, deviceId); return FAILED);
    
    ret = CreateAclTensor(inputYHostData, inputYShape, &inputYDeviceAddr, aclDataType::ACL_FLOAT16, &inputY);
    CHECK_RET(ret == ACL_SUCCESS, DestroyResources(tensors, deviceAddrs, stream, deviceId); return FAILED);
    
    ret = CreateAclTensor(outputZHostData, outputZShape, &outputZDeviceAddr, aclDataType::ACL_FLOAT16, &outputZ);
    CHECK_RET(ret == ACL_SUCCESS, DestroyResources(tensors, deviceAddrs, stream, deviceId); return FAILED);

    uint64_t workspaceSize = 0;
    aclOpExecutor *executor;
    ret = aclnnSubCustomTemplateGetWorkspaceSize(inputX, inputY, outputZ, &workspaceSize, &executor);
    CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("aclnnSubCustomTemplateGetWorkspaceSize failed. ERROR: %d\n", ret);
              DestroyResources(tensors, deviceAddrs, stream, deviceId); return FAILED);

    void *workspaceAddr = nullptr;
    if (workspaceSize > 0) {
        ret = aclrtMalloc(&workspaceAddr, workspaceSize, ACL_MEM_MALLOC_HUGE_FIRST);
        CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("allocate workspace failed. ERROR: %d\n", ret);
                  DestroyResources(tensors, deviceAddrs, stream, deviceId, workspaceAddr); return FAILED);
    }
    
    ret = aclnnSubCustomTemplate(workspaceAddr, workspaceSize, executor, stream);
    CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("aclnnSubCustomTemplate failed. ERROR: %d\n", ret);
              DestroyResources(tensors, deviceAddrs, stream, deviceId, workspaceAddr); return FAILED);

    ret = aclrtSynchronizeStream(stream);
    CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("aclrtSynchronizeStream failed. ERROR: %d\n", ret);
              DestroyResources(tensors, deviceAddrs, stream, deviceId, workspaceAddr); return FAILED);

    auto size = GetShapeSize(outputZShape);
    std::vector<aclFloat16> resultData(size, 0);
    ret = aclrtMemcpy(resultData.data(), resultData.size() * sizeof(resultData[0]), outputZDeviceAddr,
                      size * sizeof(aclFloat16), ACL_MEMCPY_DEVICE_TO_HOST);
    CHECK_RET(ret == ACL_SUCCESS, LOG_PRINT("copy result from device to host failed. ERROR: %d\n", ret);
              DestroyResources(tensors, deviceAddrs, stream, deviceId, workspaceAddr); return FAILED);

    DestroyResources(tensors, deviceAddrs, stream, deviceId, workspaceAddr);

    std::vector<aclFloat16> goldenData(size, aclFloatToFloat16(-1.0));
    LOG_PRINT("result is:\n");
    for (int64_t i = 0; i < 10; i++) {
        LOG_PRINT("%.1f ", aclFloat16ToFloat(resultData[i]));
    }
    LOG_PRINT("\n");
    
    if (std::equal(resultData.begin(), resultData.end(), goldenData.begin())) {
        LOG_PRINT("test pass\n");
    } else {
        LOG_PRINT("test failed\n");
        return FAILED;
    }
    return SUCCESS;
}
```





华为昇腾CANN算子工程化开发为我们提供了一套完整的开发范式，通过掌握这套技术，我们可以高效地开发出高性能的自定义算子，为AI应用提供强大的计算支撑。希望本文能够帮助读者快速入门CANN算子工程化开发，在实际项目中发挥重要作用。