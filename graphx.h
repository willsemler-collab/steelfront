#pragma once
#include <stdint.h>
#include <stdbool.h>
extern uint16_t gfx_palette[256];
#define gfx_RGBTo1555(r,g,b) ((uint16_t)((((r)>>3)<<10)|(((g)>>3)<<5)|((b)>>3)))
void gfx_Begin(void); void gfx_End(void); void gfx_SetDrawBuffer(void); void gfx_SwapDraw(void);
void gfx_FillScreen(uint8_t c); uint8_t gfx_SetColor(uint8_t c);
void gfx_FillRectangle(int x,int y,int w,int h); void gfx_FillCircle(int x,int y,unsigned r);
void gfx_Circle(int x,int y,unsigned r); void gfx_Line(int x0,int y0,int x1,int y1);
void gfx_FillTriangle(int x0,int y0,int x1,int y1,int x2,int y2);
void gfx_PrintStringXY(const char*s,int x,int y); void gfx_PrintString(const char*s);
void gfx_SetTextXY(int x,int y); void gfx_PrintInt(int n,uint8_t len); void gfx_PrintUInt(unsigned n,uint8_t len);
unsigned gfx_GetStringWidth(const char*s); uint8_t gfx_SetTextFGColor(uint8_t c);
uint8_t gfx_SetTextBGColor(uint8_t c); uint8_t gfx_SetTextTransparentColor(uint8_t c); void gfx_SetTextScale(uint8_t w,uint8_t h);
