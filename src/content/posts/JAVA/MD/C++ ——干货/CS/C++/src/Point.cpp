#include "Point.h"

Point::Point()
{
    cout << "wucangouzao Point " << endl;
}

Point ::Point(int m_X, int m_Y)
{
    m_x = m_X;
    m_y = m_Y;
    cout << " youcangouzao Point " << endl;
}

Point ::Point(const Point &tempP)
{
    m_x = tempP.m_x;
    m_y = tempP.m_y;
    cout << "kaobeigouzao Point " << endl;
}

Point::~Point()
{
    cout << "Xi gou Point " << endl;
}

void Point::set_X(int x)
{
    m_x = x;
};
void Point::set_Y(int y)
{
    m_y = y;
};
int Point::get_X()
{
    return m_x;
};
int Point::get_Y()
{
    return m_y;
};
