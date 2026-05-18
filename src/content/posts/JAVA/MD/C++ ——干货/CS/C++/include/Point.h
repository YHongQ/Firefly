#ifndef POINT_H
#define POINT_H
// 这个是防止冲定义出现错误 #endif

#include <string>
#include <stdio.h>
#include <iostream>
using namespace std;

class Point
{
public:
    Point();
    Point(int m_X, int m_Y);
    Point(const Point &tempP);
    ~Point();
    void set_X(int x);
    void set_Y(int y);
    int get_X();
    int get_Y();
    int tempP;
    static int st_grid;
    void show()
    {
        cout << "Point show" << endl;
    }

private:
    int m_x;
    int m_y;
};

#endif