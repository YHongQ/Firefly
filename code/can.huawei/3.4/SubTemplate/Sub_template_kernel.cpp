// %%writefile  Sources/03.04/custom_op/op_kernel/sub_custom_template.cpp
#include "kernel_operator.h"
#include "sub_custom_template_tiling.h"
#include "kernel_operator_dump_tensor_intf_impl.h"
constexpr int32_t BUFFER_NUM = 2;

// 泛型，支持不同的数据类型输入输出
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
        // cann一份代码，多个核共用，对于不同的核，处理的数据量不一样，需要区分初始化
        uint32_t coreNum = AscendC::GetBlockIdx(); // 获取当前核的编号，每个核上运行相同的代码，根据编号区分处理数据量
        uint32_t globalBufferIndex = bigCoreDataNum * AscendC::GetBlockIdx(); 
        // 计算当前核处理数据在全局内存中的起始位置，这里按照偏移量，大核在前，小核在后，
        //对于小核偏移量，实际上要再减去若干个Block_Size的便宜量

        this->tileDataNum = tileDataNum; // 每个Tile搬运的数据量 Bytes
        if (coreNum < tailBlockNum) { // 大核处理前tailBlockNum个核
          this->coreDataNum = bigCoreDataNum; // 大核处理的数据量Bytes
          this->tileNum = finalBigTileNum; // 大核处理的Tile数量
          this->tailDataNum = bigTailDataNum; // 大核处理的尾数据量
        }
        else { // 小核处理后面的核心
          this->coreDataNum = smallCoreDataNum; // 小核处理的数据量Bytes
          this->tileNum = finalSmallTileNum; // 小核处理的Tile数量
          this->tailDataNum = smallTailDataNum; // 小核处理的尾数据量
          globalBufferIndex -= (bigCoreDataNum - smallCoreDataNum) * (AscendC::GetBlockIdx() - tailBlockNum);
          // 小核的偏移量=大核偏移量-（大核每个核心处理的数据量-小核每个核心处理的数据量）*（当前核心编号-大核数量），因为小核在后面，所以要减去这个偏移量，确保小核从正确的位置开始处理数据
        }
        this->xGm.SetGlobalBuffer((__gm__ TYPE_X*)x + globalBufferIndex, this->coreDataNum); // 设置输入x的全局内存地址和处理数据量
        this->yGm.SetGlobalBuffer((__gm__ TYPE_Y*)y + globalBufferIndex, this->coreDataNum); // 设置输入y的全局内存地址和处理数据量
        this->zGm.SetGlobalBuffer((__gm__ TYPE_Z*)z + globalBufferIndex, this->coreDataNum); // 设置输出z的全局内存地址和处理数据量
        this->pipe.InitBuffer(inQueueX, BUFFER_NUM, this->tileDataNum * sizeof(TYPE_X)); // 初始化输入队列x，每个Tile处理的数据量Bytes
        this->pipe.InitBuffer(inQueueY, BUFFER_NUM, this->tileDataNum * sizeof(TYPE_Y)); // 初始化输入队列y，每个Tile处理的数据量Bytes
        this->pipe.InitBuffer(outQueueZ, BUFFER_NUM, this->tileDataNum * sizeof(TYPE_Z)); // 初始化输出队列z，每个Tile处理的数据量Bytes


    }
    __aicore__ inline void Process()
    {
        // 请补充完成实现
        int32_t loopCount = this->tileNum; // 循环次数等于Tile个数
        this->processDataNum = this->tileDataNum; // 当前处理的数据量，默认是Tile数据量
        for (int32_t i = 0; i < loopCount; i++) { // 循环处理每个Tile
            if (i == this->tileNum - 1) { // 如果是最后一个Tile
              this->processDataNum = this->tailDataNum; // 最后一个Tile处理的可能是尾数据量
            }
            CopyIn(i); // 搬入数据
            Compute(i); // 计算
            CopyOut(i); // 搬出数据
        }
    }

private:
    __aicore__ inline void CopyIn(int32_t progress)
    {
        // 请补充完成实现
        AscendC::LocalTensor<TYPE_X> xLocal = inQueueX.AllocTensor<TYPE_X>();
        AscendC::LocalTensor<TYPE_Y> yLocal = inQueueY.AllocTensor<TYPE_Y>();
        AscendC::DataCopy(xLocal, xGm[progress * this->tileDataNum], this->processDataNum);
        AscendC::DataCopy(yLocal, yGm[progress * this->tileDataNum], this->processDataNum);
        inQueueX.EnQue(xLocal);
        inQueueY.EnQue(yLocal);
    }
    __aicore__ inline void Compute(int32_t progress)
    {
        // 请补充完成实现
        
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
        // 请补充完成实现
    AscendC::LocalTensor<TYPE_Z> zLocal = outQueueZ.DeQue<TYPE_Z>();  
      AscendC::DataCopy(zGm[progress * this->tileDataNum], zLocal, this->processDataNum);
      outQueueZ.FreeTensor(zLocal);
    }

};

private:
    AscendC::TPipe pipe; // 用于数据搬运的管道,管理搬运队列的，需要用他管理队列搬入搬出 初始化队列
    // 队列其实都在device侧，应该就是显存上，需要从Host内存搬运进来，计算时从device侧在搬运到核内闪存计算
    AscendC::TQue<AscendC::QuePosition::VECIN, BUFFER_NUM> inQueueX, inQueueY; // 输入队列 
    AscendC::TQue<AscendC::QuePosition::VECOUT, BUFFER_NUM> outQueueZ; // 输出队列
    AscendC::GlobalTensor<TYPE_X> xGm;  // 输入x的全局张量地址 HOST侧传入的全局内存地址，核函数侧通过这个地址搬运数据到核内进行计算
    AscendC::GlobalTensor<TYPE_Y> yGm;  // 输入y的全局张量地址
    AscendC::GlobalTensor<TYPE_Z> zGm;  // 输出z的全局张量地址
    uint32_t coreDataNum; // 每个核心处理的数据量
    uint32_t tileNum; // Tile个数，AI Core每次处理一个Tile的数据量，tileNum就是循环次数
    uint32_t tileDataNum; // 每个Tile处理的数据个数(根据Unified Buffer大小和数据类型以及算子的输入输出决定)
    uint32_t tailDataNum; // 最后一次搬运的数据个数，可能不足一个TileDataNum
    uint32_t processDataNum; // 当前计算处理的数据量



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
