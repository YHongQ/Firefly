#ifndef CIRCLE_H
#define CIRCLE_H

#include <string>
#include <stdio.h>
#include "Point.h"
using namespace std;

class Circle
{
public:
    int get_R();
    Point get_Point();
    void set_R(int r);
    void set_Point(Point P);

private:
    int C_r;
    Point C_Point;
};

#endif