#ifndef SUB_CUSTOM_TEMPLATE_TILING_H
#define SUB_CUSTOM_TEMPLATE_TILING_H
#include <cstdint>

struct SubCustomTemplateTilingData {
    // 大小核是相对于处理数据量，并不是核的能力
    uint32_t smallCoreDataNum; //每个小核处理的数据个数
    uint32_t bigCoreDataNum; //每个大核处理的数据个数
    uint32_t finalBigTileNum; // 大核搬运Tile的次数
    uint32_t finalSmallTileNum; // 小核搬运Tile的次数
    uint32_t tileDataNum; // 每个Tile处理的数据个数(根据Unified Buffer大小和数据类型以及算子的输入输出决定)
    uint32_t smallTailDataNum; // 小核最后一轮搬运Tile的数据个数
    uint32_t bigTailDataNum; // 大核最后一轮搬运Tile的数据个数
    uint32_t tailBlockNum; // 处理尾数据(其实就是划分数据时候的前几个核数量）的大核数量
};

#endif