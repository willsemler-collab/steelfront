#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef uint8_t ti_var_t;
ti_var_t ti_Open(const char*n,const char*m); size_t ti_Read(void*p,size_t s,size_t c,ti_var_t f);
size_t ti_Write(const void*p,size_t s,size_t c,ti_var_t f); int ti_Close(ti_var_t f); int ti_SetArchiveStatus(bool a,ti_var_t f);
