#pragma once
#include <stdint.h>
extern volatile uint16_t kb_Data[8];
void kb_Scan(void);
#define kb_Down (1<<0)
#define kb_Left (1<<1)
#define kb_Right (1<<2)
#define kb_Up (1<<3)
#define kb_2nd (1<<5)
#define kb_Alpha (1<<7)
#define kb_GraphVar (1<<7)
#define kb_Clear (1<<6)
