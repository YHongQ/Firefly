#ifndef PERSON_H
#define PERSON_H

#include <iostream>
#include <stdio.h>
#include <string>
#include <functional>
#include "Point.h"
using namespace std;

class Person
{
    friend ostream &operator<<(ostream &cout, const Person &P);

public:
    // 无参数构造
    Person()
    {
        cout << "           wucangouzao Person " << endl;
    }

    // 有参数构造
    Person(int age, string s, int height)
    {
        m_age = age;
        m_height = height;
        m_Name = s;
        cout << "           youcangouzao Person " << endl;
    }

    // 初始化列表方式，初始化属性
    Person(int age, int ab) : m_age(age) {}

    // 拷贝构造 这个引用&不能舍去。
    Person(const Person &P)
    {
        cout << "           kaobei gouzao Person " << endl;
        m_age = P.m_age;
        m_Name = P.m_Name;
        // // 浅拷贝，只是将数据的地址赋值过去，因此二者指向的同一个内存块，另一个类释放后，再去释放就会中断。
        // m_height = P.m_height;
        // 深拷贝，先为其分配一篇地址空间存储数据，然后在将分配的地址赋值给height
        m_height = P.m_height;
        // m_point = P.m_point;
    }
    ~Person()
    {
        //     if (m_height != NULL)
        //     {
        //         delete m_height;
        //         m_height = NULL;
        //         // cout << "m_HHH" << endl;
        //         cout << "               xigou Person" << endl;
        //     }
        //     else
        //     {
        //         cout << "        Loss  xigou Person
        // }
    }
    // set name
    void setName(string name)
    {
        m_Name = name;
    }
    // get name
    string getName()
    {
        return m_Name;
    }
    // set idol
    void set_idol(string idol)
    {
        m_idol = idol;
    }

    void set_age(int age)
    {
        if (age < 0 || age > 150)
        {
            cout << "age error" << endl;
            cout << " set age is unseccessed !" << endl;
        }
        else
        {
            m_age = age;
            cout << "age have been changed! " << endl;
        }
    }
    int get_age()
    {
        return m_age;
    }
    void set_height(int hei)
    {
        m_height = hei;
    };
    int get_height()
    {
        return m_height;
    }
    bool operator>(Person &P);
    bool operator<(Person &P);
    bool operator==(const Person &P);

private:
    string m_Name;
    int m_age;
    string m_idol;
    int m_height;
    // Point m_point;
};

ostream &operator<<(ostream &cout, const Person &P)
{
    cout << "Person Name = " << P.m_Name << endl;
    cout << "Person Age =  " << P.m_age << endl;
    cout << "Person Height=" << P.m_height << endl;
    cout << "-------------------------------------";
    return cout;
};
bool Person::operator>(Person &P)
{
    return this->m_age > P.m_age;
};
bool Person::operator<(Person &P)
{
    return this->m_age < P.m_age;
}
bool Person::operator==(const Person &P)
{
    if (this->m_age == P.m_age && this->m_height == P.m_height && this->m_Name == P.m_Name)
    {
        return true;
    }
    return false;
}
#endif