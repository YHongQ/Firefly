#pragma once

#include <iostream>
#include <string>
#include <stdlib.h>
using namespace std;

template <class MusicNameType, class MusicHotType, class MusicIDType>
class Music
{
private:
    MusicNameType MName;
    MusicHotType Hot;
    MusicIDType ID;

public:
    Music();
    ~Music();
    void Show();
};

template <class MusicNameType, class MusicHotType, class MusicIDType>
Music<MusicNameType, MusicHotType, MusicIDType>::Music()
{
    this->Hot = 100;
    this->ID = 1;
    this->MName = "hello World";
}
template <class MusicNameType, class MusicHotType, class MusicIDType>
Music<MusicNameType, MusicHotType, MusicIDType>::~Music()
{
}

template <class MusicNameType, class MusicHotType, class MusicIDType>
void Music<MusicNameType, MusicHotType, MusicIDType>::Show()
{

    cout << "Music Name: " << this->MName << endl;
    cout << "Music ID: " << this->ID << endl;
    cout << "Music Hot: " << this->Hot << endl;
}