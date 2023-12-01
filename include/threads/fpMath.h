#ifndef FP_MATH_H
#define FP_MATH_H

#define F 14

#include "stdint.h"

/*
    int 32비트 중
    1   17  14 를 각각으로 나누어 사용한다
    1 : 부호 비트 ... 이지만 기본적으로 int 32 비트도 가장 왼쪽 비트를 2의 보수처럼 사용하기에 크게 신경 쓰지 않아도 된다
    17 : 정수 비트
    14 : 소수 비트

    따라서 F(14) 비트 시프트 연산을 통해
    정수 부분과 소수 부분에 대한 연산을 각기 진행한다
*/

/* integer를 fixed point로전환*/
int int_to_fp(int n)
{
    return n << F;
}

/* FP를 int로 전환(반올림) */
int fp_to_int_round(int x)
{
    int result = 0;
    // 1 << 13 의 값을 이용하여
    // 0.5를 표현
    int roundValue = 1 << (F - 1);

    if (x > 0)
    {
        result = x + roundValue;
    }
    else if (x < 0)
    {
        result = x - roundValue;
    }

    return result >> F;
}

/* FP를 int로 전환(버림) */
int fp_to_int(int x)
{
    return x >> F;
}

/* FP의 덧셈*/
int add_fp(int x, int y)
{
    return x + y;
}

/* FP와 int의 덧셈*/
int add_mixed(int x, int n)
{
    return x + (n << F);
}

/* FP의 뺄셈(x-y) */
int sub_fp(int x, int y)
{
    return x - y;
}

/* FP와 int의 뺄셈(x-n) */
int sub_mixed(int x, int n)
{
    return x - (n << F);
}

/* FP의 곱셈*/
int mult_fp(int x, int y)
{
    return (((int64_t)x) * y) >> F;
}

/* FP와 int의 곱셈*/
int mult_mixed(int x, int n)
{
    return x * n;
}

/* FP의 나눗셈(x/y) */
int div_fp(int x, int y)
{
    return (((int64_t)x) << F) / y;
}

/* FP와 int 나눗셈(x/n) */
int div_mixed(int x, int n)
{
    return x / n;
}

#endif