#include <iostream>
#include <Person.h>
#include <vector>
#include <algorithm>
#include <functional>
#include <map>
#include <cctype>
using namespace std;
double Pi = 3.1415926;

class NumArray
{
public:
    NumArray(vector<int> &nums)
    {
        this->length = nums.size();
        myLinkNums.assign(4 * this->length, 0);
        createLinkNums(nums, 0, this->length - 1, 1);
    }

    void update(int index, int val)
    {
        this->update(index, val, 0, this->length - 1, 1);
    }

    int sumRange(int left, int right)
    {
        return this->SumRange(left, right, 0, this->length - 1, 1);
    }
    // 打印线段树数组（用于测试）
    void printLinkNums()
    {
        for (int i = 1; i < myLinkNums.size(); ++i)
        {
            cout << "myLinkNums[" << i << "] = " << myLinkNums[i] << endl;
        }
    }

private:
    vector<int> myLinkNums;
    int length;
    void createLinkNums(vector<int> &nums, int left, int right, int pos)
    {
        if (left == right)
        {
            myLinkNums[pos] = nums[left];
            return;
        }
        else
        {
            int middle = (left + right) / 2;
            createLinkNums(nums, left, middle, pos * 2);
            createLinkNums(nums, middle + 1, right, pos * 2 + 1);
            myLinkNums[pos] = myLinkNums[2 * pos] + myLinkNums[2 * pos + 1];
        }
    }
    void update(int index, int value, int left, int right, int pos)
    {
        if (left == right)
        {
            myLinkNums[pos] = value;
        }
        else
        {
            int middle = (left + right) / 2;
            if (index <= middle)
            {
                update(index, value, left, middle, pos * 2);
            }
            else
            {
                update(index, value, middle + 1, right, pos * 2 + 1);
            }
            myLinkNums[pos] = myLinkNums[pos * 2] + myLinkNums[pos * 2 + 1];
        }
    }
    int SumRange(int begin, int end, int left, int right, int pos)
    {

        if (begin <= left && end >= right)
        {
            return myLinkNums[pos];
        }
        else
        {
            int middle = (left + right) / 2, sum = 0;
            if (begin <= middle)
            {
                sum = sum + SumRange(begin, end, left, middle, pos * 2);
            }
            if (end > middle)
            {
                sum = sum + SumRange(begin, end, middle + 1, right, pos * 2 + 1);
            }
            return sum;
        }
    }
};

class MyCompare
{
public:
    bool operator()(const int &a, const int &b) const
    {
        return a > b;
    }
};
class ATM
{
public:
    ATM()
    {
        mp[500] = 0;
        mp[200] = 0;
        mp[100] = 0;
        mp[50] = 0;
        mp[20] = 0;
    }
    map<int, int, MyCompare> mp;

    void deposit(vector<int> banknotesCount)
    {
        int money = 0;
        for (auto &[key, count] : mp)
        {
            mp[key] = banknotesCount[4 - money] + count;
            money++;

            cout << key << mp[key] << endl;
        }
        cout << "-----------------" << endl;
    }

    vector<int> withdraw(int amount)
    {
        vector<int> res;
        int count = 0;
        map<int, int, MyCompare>::iterator it = mp.begin();
        do
        {
            count++;
            int need = amount / it->first;
            if (it->second <= need)
            {
                res.push_back(it->second);
                amount = amount - it->first * it->second;
            }
            else
            {
                res.push_back(need);
                amount = amount - it->first * need;
            }
            it++;
        } while (count != 5);
        if (amount == 0)
        {
            reverse(res.begin(), res.end());
            deposit(res);
            return res;
        }
        else
        {
            res.clear();
            res.push_back(-1);
            return res;
        }
    }
};

int main()
{
    ATM atm;

    // 存款
    vector<int> deposit1 = {0, 0, 1, 2, 1}; // 20:1, 50:2, 100:1
    atm.deposit(deposit1);
    atm.printATM();

    // 取款
    int amount1 = 170;
    vector<int> result1 = atm.withdraw(amount1);
    cout << "\nWithdraw " << amount1 << " result: ";
    for (int val : result1)
    {
        cout << val << " ";
    }
    cout << endl;
    atm.printATM();

    // 再次存款
    vector<int> deposit2 = {1, 0, 0, 0, 0}; // 500:1
    atm.deposit(deposit2);
    atm.printATM();

    // 取款
    int amount2 = 600;
    vector<int> result2 = atm.withdraw(amount2);
    cout << "\nWithdraw " << amount2 << " result: ";
    for (int val : result2)
    {
        cout << val << " ";
    }
    cout << endl;
    atm.printATM();

    // 取款失败
    int amount3 = 1000;
    vector<int> result3 = atm.withdraw(amount3);
    cout << "\nWithdraw " << amount3 << " result: ";
    for (int val : result3)
    {
        cout << val << " ";
    }
    cout << endl;
    atm.printATM();

    return 0;
}
