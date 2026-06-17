#ifndef __UTILS_H__
#define __UTILS_H__

#include "bitops.h"

//在编译阶段，计算出一个结构体（struct）中的某个成员（member），距离结构体开头起始位置的“字节偏移量
#define offset_of(type, member) ((size_t) &((type *)0)->member)//size_t 无符号整数

//1.(type *)0:整数0强制类型转换成了一个指向 header_t 结构体的指针
//假设在内存的绝对地址 0x00000000 处，存放着一个 header_t 类型的结构体

//2. ->member:访问那个“幽灵结构体”里的成员

//3. & “计算”该成员的绝对内存地址
//成员的绝对地址 = 结构体的基地址 + 成员的偏移量
//因为我们在第 1 步里，故意把结构体的基地址设成了0:
//成员的绝对地址 = 0 + 成员的偏移量
//成员的绝对地址 = 成员的偏移量

//4.(size_t) 算出来的这个指针地址，强制转换成 size_t（无符号整型）


#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))//计算一个静态数组包含的元素个数

#define container_of(ptr, type, member) ({          \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - offset_of(type, member)); })


#endif /* __UTILS_H__ */
