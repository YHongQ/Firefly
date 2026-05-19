
#include <cstdint>
#include <iostream>
#include <vector>
#include <algorithm>
#include <iterator>
#include "acl/acl.h"
#include "kernel_operator.h"

constexpr uint32_t BUFFER_NUM = 2; // tensor num for each queue

struct SubCustomTilingData
{
    uint32_t totalLength;
    uint32_t tileNum;
};

class KernelSub {
public:
    __aicore__ inline KernelSub(){}
    __aicore__ inline void Init(GM_ADDR x, GM_ADDR y, GM_ADDR z, uint32_t totalLength, uint32_t tileNum)
    {
        // 请补充……

        this->blockLength = totalLength / AscendC::GetBlockNum();
        this->tileNum = tileNum;
        this->tileLength = this->blockLength / this->tileNum / BUFFER_NUM;

        xGm.SetGlobalBuffer((__gm__ float *)x + this->blockLength *AscendC::GetBlockIdx(),this->blockLength);
        yGm.SetGlobalBuffer((__gm__ float *)y + this->blockLength *AscendC::GetBlockIdx(),this->blockLength);
        zGm.SetGlobalBuffer((__gm__ float *)z + this->blockLength *AscendC::GetBlockIdx(),this->blockLength);

        this->pipe.InitBuffer(this->inQueueX,BUFFER_NUM,this->tileLength * sizeof(float));
        this->pipe.InitBuffer(this->inQueueY,BUFFER_NUM,this->tileLength * sizeof(float));
        this->pipe.InitBuffer(this->outQueueZ,BUFFER_NUM,this->tileLength * sizeof(float));
    }
    __aicore__ inline void Process()
    {
        // 请补充……
        int32_t loopCount = this->tileNum * BUFFER_NUM;
        for(int32_t i = 0; i < loopCount; i++)
        {
            this->CopyIn(i);
            this->Compute(i);
            this->CopyOut(i);
        }
    }

private:
    __aicore__ inline void CopyIn(int32_t progress)
    {
        // 请补充……
        AscendC::LocalTensor<float> xLocal = this->inQueueX.AllocTensor<float>();
        AscendC::LocalTensor<float> yLocal = this->inQueueY.AllocTensor<float>();

        AscendC::DataCopy(xLocal,this->xGm[progress * this->tileLength],this->tileLength);
        AscendC::DataCopy(yLocal,this->yGm[progress * this->tileLength],this->tileLength);

        this->inQueueX.EnQue(xLocal);
        this->inQueueY.EnQue(yLocal);
    }
    __aicore__ inline void Compute(int32_t progress)
    {
        // 请补充……
        AscendC::LocalTensor<float> xLocal = this->inQueueX.DeQue<float>();
        AscendC::LocalTensor<float> yLocal = this->inQueueY.DeQue<float>();

        AscendC::LocalTensor<float> zLocal = this->outQueueZ.AllocTensor<float>();

        AscendC::Sub(zLocal,xLocal,yLocal,this->tileLength);

        this->outQueueZ.EnQue<float>(zLocal);

        this->inQueueX.FreeTensor(xLocal);
        this->inQueueY.FreeTensor(yLocal);
    }
    __aicore__ inline void CopyOut(int32_t progress)
    {
        // 请补充……
        AscendC::LocalTensor<float> zLocal = this->outQueueZ.DeQue<float>();

        AscendC::DataCopy(this->zGm[progress * this->tileLength],zLocal,this->tileLength);

        this->outQueueZ.FreeTensor(zLocal);
    }

private:
    // 请补充……
    AscendC::TPipe pipe; //TPipe内存管理对象
    AscendC::TQue<AscendC::TPosition::VECIN,BUFFER_NUM> inQueueX,inQueueY;
    AscendC::TQue<AscendC::TPosition::VECOUT,BUFFER_NUM> outQueueZ;
    AscendC::GlobalTensor<float> xGm;
    AscendC::GlobalTensor<float> yGm;
    AscendC::GlobalTensor<float> zGm;
    uint32_t blockLength;
    uint32_t tileNum;
    uint32_t tileLength;
};

__global__ __aicore__ void sub_custom(GM_ADDR x, GM_ADDR y, GM_ADDR z, SubCustomTilingData tiling)
{
    // 请补充……
    KERNEL_TASK_TYPE_DEFAULT(KERNEL_TYPE_AIV_ONLY);
    KernelSub kernelSub;
    kernelSub.Init(x, y, z, tiling.totalLength, tiling.tileNum);
    kernelSub.Process();
}

std::vector<float> kernel_sub(std::vector<float> &x, std::vector<float> &y)
{
    // 请补充……
    constexpr uint32_t blockDim = 8;
    uint32_t totalLength = x.size();
    size_t totalByteSize = totalLength * sizeof(float);
    int32_t deviceId = 0;
    aclrtStream stream = nullptr;
    SubCustomTilingData tiling = {totalLength, 8};

    // HOST侧数据指针
    uint8_t *xHost = reinterpret_cast<uint8_t *>(x.data());
    uint8_t *yHost = reinterpret_cast<uint8_t *>(y.data());
    uint8_t *zHost = nullptr;

    // DEVICE侧数据指针
    uint8_t *xDevice = nullptr;
    uint8_t *yDevice = nullptr;
    uint8_t *zDevice = nullptr;

    aclInit(nullptr);

    aclrtSetDevice(deviceId);
    aclrtCreateStream(&stream); 


    aclrtMallocHost((void **)(&zHost),totalByteSize);
    // DEVICE侧数据指针
    aclrtMalloc((void **)(&xDevice),totalByteSize,ACL_MEM_MALLOC_HUGE_FIRST );
    aclrtMalloc((void **)(&yDevice),totalByteSize,ACL_MEM_MALLOC_HUGE_FIRST );
    aclrtMalloc((void **)(&zDevice),totalByteSize,ACL_MEM_MALLOC_HUGE_FIRST );


    aclrtMemcpy(xDevice, totalByteSize, xHost, totalByteSize, ACL_MEMCPY_HOST_TO_DEVICE);
    aclrtMemcpy(yDevice, totalByteSize, yHost, totalByteSize, ACL_MEMCPY_HOST_TO_DEVICE);


    sub_custom<<<blockDim,nullptr,stream>>>(xDevice,yDevice,zDevice,tiling);
    aclrtSynchronizeStream(stream);


    aclrtMemcpy(zHost, totalByteSize, zDevice, totalByteSize, ACL_MEMCPY_DEVICE_TO_HOST);

    std::vector<float> output((float *)zHost, (float *)(zHost + totalByteSize));



    aclrtFree((void *)xDevice);
    aclrtFree((void *)yDevice);
    aclrtFree((void *)zDevice);
    aclrtFreeHost((void *)zHost);
    aclrtDestroyStream(stream);
    aclrtResetDevice(deviceId);

    aclFinalize();

    return output;

}

uint32_t VerifyResult(std::vector<float> &output, std::vector<float> &golden)
{
    auto printTensor = [](std::vector<float> &tensor, const char *name) {
        constexpr size_t maxPrintSize = 200;
        std::cout << name << ": ";
        std::copy(tensor.begin(), tensor.begin() + std::min(tensor.size(), maxPrintSize),
            std::ostream_iterator<float>(std::cout, " "));
        if (tensor.size() > maxPrintSize) {
            std::cout << "...";
        }
        std::cout << std::endl;
    };
    printTensor(output, "Output");
    printTensor(golden, "Golden");
    if (std::equal(golden.begin(), golden.end(), output.begin())) {
        std::cout << "[Success] Case accuracy is verification passed." << std::endl;
        return 0;
    } else {
        std::cout << "[Failed] Case accuracy is verification failed!" << std::endl;
        return 1;
    }
    return 0;
}

int32_t main(int32_t argc, char *argv[])
{
    constexpr uint32_t totalLength = 8 * 2048;
    constexpr float valueX = 1.2f;
    constexpr float valueY = 2.3f;
    std::vector<float> x(totalLength, valueX);
    std::vector<float> y(totalLength, valueY);

    // 请补充……
    std::vector<float> output = kernel_sub(x, y);

    std::vector<float> golden(totalLength, valueX - valueY);
    return VerifyResult(output, golden);
}