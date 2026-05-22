
#include "register/op_def_registry.h"
#include "../op_kernel/sub_custom_template_tiling.h"
#include "graph/utils/type_utils.h"
#include "tiling/platform/platform_ascendc.h"

namespace optiling {
static ge::graphStatus TilingFunc(gert::TilingContext* context)
{
   auto ascendcPlatform = platform_ascendc::PlatformAscendC(context->GetPlatformInfo()); // 获取设备device侧平台信息
   auto coreNum = ascendcPlatform.GetCoreNum(); // 获取核心数量


   // 获取输入数据的长度和数据类型，根据类型和长度，决定tiling策略
   uint32_t inputNum = context->GetInputShape(0)->GetStorageShape().GetShapeSize();// 获取输入数据的元素数量
   uint32_t typeLength = 0;
   ge::TypeUtils::GetDataTypeLength(context->GetInputDesc(0)->GetDataType(), typeLength); // 获取输入数据类型的长度
   uint32_t inputLength = inputNum * typeLength; // 计算输入数据的字节长度
   
   // 这里块的大小根据平台硬件决定，指每次搬运/计算的最小数据量，通常是32字节对齐
   const uint32_t BLOCK_SIZE = 32; // 定义块大小,定义一个Block表明32字节

   // 对于输入数据长度不足N*BLOCK_SIZE的情况，按照BLOCK_SIZE对齐，保证每个核心至少处理一个块的数据
   uint32_t inputLengthAlgin32 = (((inputLength + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE); 
   // 对齐到32字节后，输入数据的字节长度= 输入数据长度+对齐字节（0-31字节对齐）
   
   // 这里涉及核心数和输入长度的关系，最好的划分肯定是平均给每个核心近似的数据量。
   auto min_Use_CoreNum = std::min(coreNum, inputLengthAlgin32 / BLOCK_SIZE);
    // 每个核至少处理一个块的数据量，因此如果对齐之后的输入数据块数小于核心数，那就只需要block数的核心数即可
   coreNum = std::max(min_Use_CoreNum, static_cast<uint32_t>(1)); // 至少使用一个核心,避免0个核心处理数据异常

   uint32_t everyCoreInputBlockNum = inputLengthAlgin32 / BLOCK_SIZE / coreNum; // 每个核心处理的块数量,这里是平均分配部分，还有尾数据需要处理
   uint32_t tailBlockNum = (inputLengthAlgin32 / BLOCK_SIZE) % coreNum; // 计算尾块数量,如果输入数据块数不能被核心数整除，那么就会有一些核心需要处理多一个块的数据
   context->SetBlockDim(coreNum); // 设置核心数量

   uint64_t ubSize; // Unified Buffer的大小，我把他理解为一种核内快速缓存，计算时能使用的空间大小，也决定一次最多的计算量Bytes
   ascendcPlatform.GetCoreMemSize(platform_ascendc::CoreMemType::UB, ubSize); // 获取UB大小

   uint32_t ubDataNumber = 3; // 这里是根据算法需要同时在UB中存放的输入输出数据数量来定的，减法涉及两个输入一个输出，同时要在UB中
   uint32_t tileBlockNum = (ubSize / BLOCK_SIZE ) / ubDataNumber; // Tile是每次搬运的量，这里计算搬运的Block数量（也就是核每次最大的计算量）
   uint32_t tileDataNum = (tileBlockNum * BLOCK_SIZE) / typeLength; // 每次搬运的元素数量,根据数据类型长度计算出每次搬运的元素数量
   uint32_t smallCoreDataNum = everyCoreInputBlockNum * BLOCK_SIZE / typeLength; // 每个小核处理的数据数量，小核不处理尾数据
   uint32_t smallTileNum = everyCoreInputBlockNum / tileBlockNum; // 每个小核处理的满Tile(tile满容量搬运)的次数，但是有可能core分配的不能被tile整除，所以还要处理剩余数据
   uint32_t finalSmallTileNum = (everyCoreInputBlockNum % tileBlockNum) == 0 ? smallTileNum : smallTileNum + 1; // 每个小核处理的Tile次数，如果不能整除，那么就需要多搬运一次来处理剩余数据
   uint32_t smallTailDataNum = smallCoreDataNum - (tileDataNum * smallTileNum); // 小核处理的尾数据量，尾数据量=核心分配的数据量-满Tile搬运的数据量
   smallTailDataNum = smallTailDataNum == 0 ? tileDataNum : smallTailDataNum; // 如果尾数据量为0，说明正好被Tile整除，那么最后一次搬运应该是Tile满数据量，这样才对

   // 同理，大核只是多处理一个Block的数据
   everyCoreInputBlockNum += 1; // 大核处理的数据量比小核多一个Block
   uint32_t bigCoreDataNum = everyCoreInputBlockNum * BLOCK_SIZE / typeLength; // 大核处理的数据数量
   uint32_t bigTileNum = everyCoreInputBlockNum / tileBlockNum; // 大核处理的满Tile的次数
   uint32_t finalBigTileNum = (everyCoreInputBlockNum % tileBlockNum) == 0 ? bigTileNum : bigTileNum + 1; // 大核处理的Tile次数，如果不能整除，那么就需要多搬运一次来处理剩余数据
   uint32_t bigTailDataNum = bigCoreDataNum - (tileDataNum * bigTileNum); // 大核处理的尾数据量，尾数据量=核心分配的数据量-满Tile搬运的数据量
   bigTailDataNum = bigTailDataNum == 0 ? tileDataNum : bigTailDataNum; // 如果尾数据量为0，说明正好被Tile整除，那么最后一次搬运应该是Tile满数据量，这样才对

   // 将数据传给定义结构体，核函数侧可以直接获取
   SubCustomTemplateTilingData *tiling = context->GetTilingData<SubCustomTemplateTilingData>(); // 获取tiling数据结构体指针
   tiling->smallCoreDataNum = smallCoreDataNum; // 每个小核处理的数据量
   tiling->bigCoreDataNum = bigCoreDataNum; // 每个大核处理的数据量
    tiling->tileDataNum = tileDataNum;
    tiling->smallTailDataNum = smallTailDataNum;
    tiling->bigTailDataNum = bigTailDataNum;
    tiling->finalSmallTileNum = finalSmallTileNum;
    tiling->finalBigTileNum = finalBigTileNum;
    tiling->tailBlockNum = tailBlockNum;

    return ge::GRAPH_SUCCESS;






}

//  创建工程时，根据Sub.json文件，自动创建，一般保持不变

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