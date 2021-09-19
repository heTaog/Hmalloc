#include "MemManage.h"
#include <iostream>
using namespace std;

#define MALLOC(type) static_cast<type*>(kMalloc(sizeof(type)))

int main()
{
    int* a = MALLOC(int);
    int* b = MALLOC(int);

    printf("a -> %p\nb -> %p\n", a, b);
}