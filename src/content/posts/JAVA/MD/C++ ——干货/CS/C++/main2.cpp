#include <iostream>
#include <map>

// 自定义比较函数
struct myCompare
{
    bool operator()(int a, int b) const
    {
        return a > b; // 降序排序
    }
};

int main()
{
    // 定义 multimap
    std::multimap<int, std::pair<int, int>, myCompare> mp;

    // 插入单个键值对
    mp.insert({1, {10, 20}});
    mp.emplace(2, std::make_pair(30, 40));
    mp.insert({1, {50, 60}}); // 允许重复键

    // 插入多个键值对
    std::multimap<int, std::pair<int, int>, myCompare> temp = {
        {3, {70, 80}},
        {4, {90, 100}},
        {2, {110, 120}} // 允许重复键
    };
    mp.insert(temp.begin(), temp.end());

    // 输出 multimap 内容
    for (const auto &[key, value] : mp)
    {
        std::cout << "Key: " << key << ", Value: (" << value.first << ", " << value.second << ")\n";
    }

    return 0;
}