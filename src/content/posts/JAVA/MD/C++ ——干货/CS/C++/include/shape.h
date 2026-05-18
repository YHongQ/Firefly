#ifndef SHAPE_H
#define SHAPE_H
#include <string.h>
#include <iostream>
#include "Point.h"

class grid : public Point
{
public:
    void show()
    {
        cout << get_X() << endl;
        cout << get_Y() << endl;
    }
    void set(int x, int y)
    {

        set_X(x);
        set_Y(y);
    }
    grid()
    {
        set_X(0);
        set_Y(0);
        ID_name = 0;
        cout << "wucangouzao Grid" << endl;
    }
    grid(int x, int y, int name)
    {
        set_X(x);
        set_Y(y);
        ID_name = name;
        cout << "youcangouzao Grid" << endl;
    }
    ~grid()
    {
        cout << "Xigou Grid" << endl;
    }
    int tempP;
    static int st_grid;

private:
    int ID_name;
};
#endif