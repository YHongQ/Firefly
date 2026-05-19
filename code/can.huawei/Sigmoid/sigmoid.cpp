
#include <cstdint>
#include <iostream>
#include <vector>
#include <cmath> 
#include <algorithm>
#include <iterator>
#include "acl/acl.h"
#include "kernel_operator.h"

constexpr uint32_t BUFFER_NUM = 2; // tensor num for each queue

struct SigmoidCustomTilingData
{
    uint32_t totalLength;
    uint32_t tileNum;
};

// 待补充……
class KernelSigmoid {
public:
    __aicore__ inline KernelSigmoid(){}
    __aicore__ inline void Init(GM_ADDR x, GM_ADDR y, uint32_t totalLength, uint32_t tileNum)
    {
        this->blockLength = totalLength / AscendC::GetBlockNum();
        this->tileNum = tileNum;
        this->tileLength = this->blockLength / this->tileNum / BUFFER_NUM;

        xGm.SetGlobalBuffer((__gm__ float *)x + this->blockLength *AscendC::GetBlockIdx(),this->blockLength);
        yGm.SetGlobalBuffer((__gm__ float *)y + this->blockLength *AscendC::GetBlockIdx(),this->blockLength);

        this->pipe.InitBuffer(this->inQueueX,BUFFER_NUM,this->tileLength * sizeof(float));
        this->pipe.InitBuffer(this->outQueueY,BUFFER_NUM,this->tileLength * sizeof(float));

    }
    __aicore__ inline void Process()
    {
        // 待补充……
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
        // 待补充……
        AscendC::LocalTensor<float> xLocal = this->inQueueX.AllocTensor<float>();
        AscendC::DataCopy(xLocal,this->xGm[progress * this->tileLength],this->tileLength);
        this->inQueueX.EnQue(xLocal);


    }
    __aicore__ inline void Compute(int32_t progress)
    {
        // 待补充……
        AscendC::LocalTensor<float> xLocal = this->inQueueX.DeQue<float>();
        AscendC::LocalTensor<float> yLocal = this->outQueueY.AllocTensor<float>();
        AscendC::Sigmoid(yLocal,xLocal,this->tileLength);
        this->outQueueY.EnQue<float>(yLocal);
        this->inQueueX.FreeTensor(xLocal);

    }
    __aicore__ inline void CopyOut(int32_t progress)
    {
        // 待补充……
        AscendC::LocalTensor<float> yLocal = this->outQueueY.DeQue<float>();
        AscendC::DataCopy(this->yGm[progress * this->tileLength],yLocal,this->tileLength);
        this->outQueueY.FreeTensor(yLocal);
    }

private:
    AscendC::TPipe pipe; //TPipe内存管理对象
    AscendC::TQue<AscendC::TPosition::VECIN,BUFFER_NUM> inQueueX;
    AscendC::TQue<AscendC::TPosition::VECOUT,BUFFER_NUM> outQueueY;
    AscendC::GlobalTensor<float> xGm;
    AscendC::GlobalTensor<float> yGm;
    uint32_t blockLength;
    uint32_t tileNum;
    uint32_t tileLength;

};

__global__ __aicore__ void sigmoid_custom(GM_ADDR x, GM_ADDR y, SigmoidCustomTilingData tiling)
{
    // 待补充……
    KERNEL_TASK_TYPE_DEFAULT(KERNEL_TYPE_AIV_ONLY);
    KernelSigmoid kernelSigmoid;
    kernelSigmoid.Init(x, y, tiling.totalLength, tiling.tileNum);
    kernelSigmoid.Process();



}

std::vector<float> kernel_sigmoid(std::vector<float> &x)
{
    // 待补充……
    constexpr uint32_t blockDim = 8;
    uint32_t totalLength = x.size();
    size_t totalByteSize = totalLength * sizeof(float);
    int32_t deviceId = 0;
    aclrtStream stream = nullptr;
    SigmoidCustomTilingData tiling = {totalLength,8};

    // HOST侧数据指针
    uint8_t *xHost = reinterpret_cast<uint8_t *>(x.data());
    uint8_t *yHost = nullptr;

    // DEVICE侧数据指针
    uint8_t *xDevice = nullptr;
    uint8_t *yDevice = nullptr;

    aclInit(nullptr);
    aclrtSetDevice(deviceId);
    aclrtCreateStream(&stream);

    aclrtMallocHost((void **)(&yHost),totalByteSize);
    // DEVICE侧数据指针
    aclrtMalloc((void **)(&xDevice),totalByteSize,ACL_MEM_MALLOC_HUGE_FIRST );
    aclrtMalloc((void **)(&yDevice),totalByteSize,ACL_MEM_MALLOC_HUGE_FIRST );

    aclrtMemcpy(xDevice, totalByteSize, xHost, totalByteSize, ACL_MEMCPY_HOST_TO_DEVICE);

    sigmoid_custom<<<blockDim,nullptr,stream>>>(xDevice,yDevice,tiling);
    aclrtSynchronizeStream(stream);

    aclrtMemcpy(yHost, totalByteSize, yDevice, totalByteSize, ACL_MEMCPY_DEVICE_TO_HOST);
    std::vector<float> output((float *)yHost, (float *)(yHost + totalByteSize));

    aclrtFree((void *)xDevice);
    aclrtFree((void *)yDevice);
    aclrtFreeHost((void *)yHost);
    aclrtDestroyStream(stream);
    aclrtResetDevice(deviceId);

    aclFinalize();

    return output;
}

uint32_t VerifyResult(std::vector<float> &output, std::vector<float> &golden)
{
    auto printTensor = [](std::vector<float> &tensor, const char *name) {
        constexpr size_t maxPrintSize = 20;
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

std::vector<float> sigmoid(const std::vector<float>& x) {
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) {
        y[i] = 1.0f / (1.0f + std::exp(-x[i]));
    }
    return y;
}

int32_t main(int32_t argc, char *argv[])
{
    constexpr uint32_t totalLength = 8 * 2048;
    constexpr float valueX = 5.5f;
    std::vector<float> x(totalLength, valueX);
    std::vector<float> output = kernel_sigmoid(x);
    std::vector<float> golden = sigmoid(x);
    return VerifyResult(output, golden);
}
