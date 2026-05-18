#include "Circle.h"

int Circle::get_R()
{
    return C_r;
};
Point Circle::get_Point()
{
    return C_Point;
};
void Circle::set_Point(Point P)
{
    C_Point = P;
};
void Circle::set_R(int r)
{
    C_r = r;
};
