#include "hud_view.h"
#include "fonts/hud_font.h"
#include <stdio.h>
static uint16_t rgb(unsigned c) { return (uint16_t)(((c>>19)<<11)|(((c>>10)&63)<<5)|((c>>3)&31)); }
static uint32_t codepoint(const char **text) {
    const unsigned char *p=(const unsigned char *)*text;
    uint32_t c=*p++;unsigned n=0;
    if(c>=0xc2&&c<=0xdf){c&=31;n=1;}else if(c>=0xe0&&c<=0xef){c&=15;n=2;}
    else if(c>=0xf0&&c<=0xf4){c&=7;n=3;}else if(c>=128)c=0x25a1;
    while(n--){if((*p&0xc0)!=0x80){c=0x25a1;break;}c=(c<<6)|(*p++&63);}
    *text=(const char *)p;return c;
}
static void text(uint16_t *p,int w,int h,int x,int y,int end,int scale,const char *s,uint16_t color) {
    while(*s){uint32_t c=codepoint(&s);unsigned lo=0,hi=hud_font_count;
        while(lo<hi){unsigned m=lo+(hi-lo)/2;if(hud_font[m].codepoint<c)lo=m+1;else hi=m;}
        if(lo==hud_font_count||hud_font[lo].codepoint!=c){x+=20*scale;continue;}
        const hud_glyph *g=&hud_font[lo];if(x+g->advance*scale>end)break;
        for(int row=0;row<24;++row)for(int col=0;col<20;++col)
            if(g->bits[row*3+col/8]&(0x80>>(col%8)))
                for(int dy=0;dy<scale;++dy)for(int dx=0;dx<scale;++dx){
                    int px=x+col*scale+dx,py=y+row*scale+dy;
                    if(px>=0&&px<w&&px<end&&py>=0&&py<h)p[py*w+px]=color;}
        x+=g->advance*scale;
    }
}
static void distance(char *out,size_t size,uint32_t metres) {
    if(metres>=1000)snprintf(out,size,"%.1f km",metres/1000.0);
    else snprintf(out,size,"%lu m",(unsigned long)metres);
}

static const char *action_text(uint32_t action) {
    return action==2?"左转":action==3?"右转":action==4?"左前方":
        action==5?"右前方":action==6?"左后方":action==7?"右后方":
        action==8?"掉头":action==9?"直行":action==15?"到达终点":"沿路线行驶";
}
void terminal_hud_draw(uint16_t *p,unsigned width,unsigned height,const moto_hud *hud,bool fresh,bool night) {
    if(!p||!hud||width<320||height<200||width>4096||height>4096)return;
    int w=(int)width,h=(int)height,scale=w>=800&&h>=480?2:1;
    uint16_t bg=rgb(night?0x101419:0xf3f5f6),ink=rgb(night?0xf4f6f8:0x1c242c);
    uint16_t muted=rgb(night?0x909aa4:0x65717b);
    for(unsigned i=0;i<width*height;++i)p[i]=bg;
    text(p,w,h,24,16*scale,w-24,scale,"导航",muted);
    if(!fresh){text(p,w,h,24,h/2-24,w-24,scale,"等待导航更新",muted);return;}
    const char *action=action_text(hud->action);
    text(p,w,h,24,54*scale,w-24,scale*2,action,ink);
    char value[64];distance(value,sizeof value,hud->distance_m);
    text(p,w,h,24,116*scale,w-24,scale*3,value,ink);
    text(p,w,h,24,h-82*scale,w-24,scale,hud->next_road[0]?hud->next_road:hud->road,muted);
    distance(value,sizeof value,hud->remaining_m);text(p,w,h,24,h-38*scale,w/2,scale,value,ink);
    snprintf(value,sizeof value,"%lu min",(unsigned long)((hud->remaining_s+59)/60));
    text(p,w,h,w/2,h-38*scale,w-24,scale,value,muted);
}

void terminal_hud_map_overlay(uint16_t *p,unsigned width,unsigned height,const moto_hud *hud,
    bool fresh,bool night,bool positioned,bool complete){
    if(!p||!hud||width<320||height<200||width>2048||height>2048)return;
    int w=(int)width,h=(int)height,scale=w>=800?2:1,bar=44*scale;
    uint16_t bg=rgb(night?0x101419:0xf3f5f6),ink=rgb(night?0xf4f6f8:0x1c242c);
    int footer=positioned&&complete?0:36*scale;
    for(int y=0;y<h;y++)if(y<bar||y>=h-footer)for(int x=0;x<w;x++)p[y*w+x]=bg;
    char value[200];
    if(fresh){char dist[32];distance(dist,sizeof dist,hud->distance_m);
        const char *action=action_text(hud->action);
        snprintf(value,sizeof value,"%s  %s  %s",dist,action,hud->next_road[0]?hud->next_road:hud->road);
    }else snprintf(value,sizeof value,"等待导航更新");
    text(p,w,h,16,10*scale,w-16,scale,value,ink);
    if(footer){
        const char *status=!positioned?"定位已暂停":"底图未缓存";
        text(p,w,h,16,h-28*scale,w-16,scale,status,ink);
    }
}
