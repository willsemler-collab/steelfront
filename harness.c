#include "graphx.h"
#include "keypadc.h"
#include "fileioc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
uint16_t gfx_palette[256]; volatile uint16_t kb_Data[8];
static uint8_t buf[240][320]; static uint8_t col, tfg, sc=1; static int tx, ty;
static long scans, swaps, last_swap_scan, menus; static int shot=0; static int phase=0;
static void px(int x,int y,uint8_t c){ if(x>=0&&x<320&&y>=0&&y<240) buf[y][x]=c; }
void gfx_Begin(void){} void gfx_End(void){} void gfx_SetDrawBuffer(void){}
void gfx_FillScreen(uint8_t c){ memset(buf,c,sizeof buf);} uint8_t gfx_SetColor(uint8_t c){uint8_t o=col;col=c;return o;}
void gfx_FillRectangle(int x,int y,int w,int h){ for(int j=y;j<y+h;j++)for(int i=x;i<x+w;i++)px(i,j,col);}
void gfx_FillCircle(int x,int y,unsigned r){int R=r; for(int j=-R;j<=R;j++)for(int i=-R;i<=R;i++) if(i*i+j*j<=R*R) px(x+i,y+j,col);}
void gfx_Circle(int x,int y,unsigned r){ for(int a=0;a<360;a+=3){ double t=a*3.14159/180; px(x+(int)(r*__builtin_cos(t)),y+(int)(r*__builtin_sin(t)),col);} }
void gfx_Line(int x0,int y0,int x1,int y1){ int n=abs(x1-x0)>abs(y1-y0)?abs(x1-x0):abs(y1-y0); if(!n){px(x0,y0,col);return;} for(int i=0;i<=n;i++) px(x0+(x1-x0)*i/n,y0+(y1-y0)*i/n,col);}
static int edge(int ax,int ay,int bx,int by,int x,int y){return (bx-ax)*(y-ay)-(by-ay)*(x-ax);}
void gfx_FillTriangle(int x0,int y0,int x1,int y1,int x2,int y2){ int mnx=x0<x1?x0:x1; mnx=mnx<x2?mnx:x2; int mxx=x0>x1?x0:x1; mxx=mxx>x2?mxx:x2; int mny=y0<y1?y0:y1; mny=mny<y2?mny:y2; int mxy=y0>y1?y0:y1; mxy=mxy>y2?mxy:y2;
 for(int y=mny;y<=mxy;y++)for(int x=mnx;x<=mxx;x++){int a=edge(x0,y0,x1,y1,x,y),b=edge(x1,y1,x2,y2,x,y),c=edge(x2,y2,x0,y0,x,y); if((a>=0&&b>=0&&c>=0)||(a<=0&&b<=0&&c<=0)) px(x,y,col);} }
void gfx_SetTextXY(int x,int y){tx=x;ty=y;}
void gfx_PrintString(const char*s){ for(;*s;s++){ if(*s!=' ') for(int j=1;j<7*sc;j++)for(int i=1;i<7*sc;i++) if((i+j)%2) px(tx+i,ty+j,tfg); tx+=8*sc; } }
void gfx_PrintStringXY(const char*s,int x,int y){tx=x;ty=y;gfx_PrintString(s);}
void gfx_PrintInt(int n,uint8_t l){char b[16];snprintf(b,16,"%d",n);gfx_PrintString(b);(void)l;}
void gfx_PrintUInt(unsigned n,uint8_t l){char b[16];snprintf(b,16,"%u",n);gfx_PrintString(b);(void)l;}
unsigned gfx_GetStringWidth(const char*s){return strlen(s)*8*sc;}
uint8_t gfx_SetTextFGColor(uint8_t c){tfg=c;return 0;} uint8_t gfx_SetTextBGColor(uint8_t c){(void)c;return 0;}
uint8_t gfx_SetTextTransparentColor(uint8_t c){(void)c;return 0;} void gfx_SetTextScale(uint8_t w,uint8_t h){sc=w;(void)h;}
static void dump(const char*name){ FILE*f=fopen(name,"wb"); fprintf(f,"P6 320 240 255\n"); for(int y=0;y<240;y++)for(int x=0;x<320;x++){uint16_t p=gfx_palette[buf[y][x]]; uint8_t rgb[3]={((p>>10)&31)<<3,((p>>5)&31)<<3,(p&31)<<3}; fwrite(rgb,1,3,f);} fclose(f);}
extern unsigned int frame, score, kills; extern uint8_t wave; /* not exported: static in game; use swaps */
void gfx_SwapDraw(void){ swaps++; last_swap_scan=scans; char n[64];
  if(swaps==1) dump("shot_title.ppm");
  if(swaps==150||swaps==900||swaps==2500){snprintf(n,64,"shot_%ld.ppm",swaps);dump(n);}
  if(swaps==2000) dump("shot_late.ppm");
  { static int dumped; if(dumped<3 && menus>=1 && swaps>200){ /* nothing */ } } }
/* input script: title->2nd, then semi-random play steering toward the middle, clear at 6000 swaps, then clear on game over */

void kb_Scan(void){ scans++; memset((void*)kb_Data,0,16);
  long idle=scans-last_swap_scan;
  if(idle>20){ /* stuck in a menu wait loop */
    if(idle==21){ menus++; printf("menu #%ld at swap %ld score=%u wave=%u kills=%u\n",menus,swaps,score,wave,kills); }
    if(idle>40 && idle%60<10){ if(menus<4) kb_Data[1]=kb_2nd; else kb_Data[6]=kb_Clear; }
    if(idle>100000){ printf("STUCK\n"); exit(3);} return; }
  if(swaps>=30000){ if(scans%4==0) kb_Data[6]=kb_Clear; return; }
  { /* aim bot */
    typedef struct { int x, y; uint8_t hull, turret; int8_t hp, maxhp; uint8_t reload, reload_max, kind, flash, ai_timer, ai_mode; int8_t ai_dir; int speed; bool alive; } tank_t;
    extern tank_t player, en[6]; extern uint8_t angle_to(int,int); extern int8_t adiff(uint8_t,uint8_t);
    int best=-1, bd=1<<30; for(int i=0;i<6;i++) if(en[i].alive){int dx=(en[i].x-player.x)>>8, dy=(en[i].y-player.y)>>8; if(dx*dx+dy*dy<bd){bd=dx*dx+dy*dy;best=i;}}
    if(best>=0){ uint8_t want=angle_to((en[best].x-player.x)>>8,(en[best].y-player.y)>>8);
      int8_t d=adiff(want,player.turret); if(d<0)kb_Data[2]=kb_Alpha; if(d>0)kb_Data[3]=kb_GraphVar;
      if(d>=-1&&d<=1) kb_Data[1]=kb_2nd;
      int8_t h=adiff((uint8_t)(want+16),player.hull); if(h<0)kb_Data[7]|=kb_Left; if(h>0)kb_Data[7]|=kb_Right;
      if((swaps/40)%2) kb_Data[7]|=kb_Up; }
    return; }
  uint32_t r=(uint32_t)(swaps/20)*2654435761u;
  kb_Data[7]= (r>>3)&1 ? kb_Up : ((r>>4)&1?kb_Down:0); if((r>>5)&3)kb_Data[7]|= (r>>7)&1?kb_Left:kb_Right;
  kb_Data[2]= ((r>>9)&3)==0?kb_Alpha:0; kb_Data[3]=((r>>11)&3)==0?kb_GraphVar:0;
  kb_Data[1]= kb_2nd; (void)phase; (void)shot;
}
static int hs_store=-1;
ti_var_t ti_Open(const char*n,const char*m){(void)n; return (m[0]=='r'&&hs_store<0)?0:1;}
size_t ti_Read(void*p,size_t s,size_t c,ti_var_t f){(void)f; memcpy(p,&hs_store,s); return c;}
size_t ti_Write(const void*p,size_t s,size_t c,ti_var_t f){(void)f; memcpy(&hs_store,p,s); printf("saved hiscore %d\n",hs_store); return c;}
int ti_Close(ti_var_t f){(void)f;return 0;} int ti_SetArchiveStatus(bool a,ti_var_t f){(void)a;(void)f;return 0;}
clock_t fake_clock(void){ static clock_t t; return t+=40000; }
