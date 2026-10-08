#include "route_map_view.h"
#include <math.h>
#include <stdint.h>
#include <stddef.h>
static uint32_t u32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
unsigned route_map_project_route(const moto_map_package *m,route_map_xy *xy,unsigned capacity){
    if(!m||!xy||m->route_points>capacity)return 0;
    for(unsigned i=0;i<m->route_points;i++){
        const uint8_t *p=m->bytes.data+24+i*8;
        moto_route_point point={(int32_t)u32(p)-90000000,(int32_t)u32(p+4)-180000000};
        moto_map_project(point,18,&xy[i].x,&xy[i].y);
    }return m->route_points;
}
static bool clip(double p,double q,double *a,double *b){
    if(p==0)return q>=0;
    double t=q/p;
    if(p<0){if(t>*b)return false;if(t>*a)*a=t;}else{if(t<*a)return false;if(t<*b)*b=t;}return true;
}
static void line(uint16_t *p,int w,int h,double x0,double y0,double x1,double y1,uint16_t c,int radius){
    double dx=x1-x0,dy=y1-y0,a=0,b=1;
    if(!clip(-dx,x0,&a,&b)||!clip(dx,w-1-x0,&a,&b)||!clip(-dy,y0,&a,&b)||!clip(dy,h-1-y0,&a,&b))return;
    double ex=x0+b*dx,ey=y0+b*dy;x0+=a*dx;y0+=a*dy;dx=ex-x0;dy=ey-y0;
    int steps=(int)ceil(fmax(fabs(dx),fabs(dy)));if(!steps)steps=1;
    for(int i=0;i<=steps;i++){int x=(int)lround(x0+dx*i/steps),y=(int)lround(y0+dy*i/steps);
        for(int oy=-radius;oy<=radius;oy++)for(int ox=-radius;ox<=radius;ox++)
            if(x+ox>=0&&x+ox<w&&y+oy>=0&&y+oy<h)p[(y+oy)*w+x+ox]=c;}
}
bool route_map_draw_tiles(uint16_t *p,unsigned width,unsigned height,const moto_map_package *m,
    const route_map_xy *xy,unsigned count,const moto_position *position,bool fresh,route_map_row_reader reader,void *user){
    if(!p||!position||width<320||height<200||width>2048||height>2048)return false;
    unsigned z=m&&m->bytes.data?m->zoom:17;double x,y;moto_map_project(position->point,z,&x,&y);
    int w=(int)width,h=(int)height;int64_t left=(int64_t)floor(x)-w/2,top=(int64_t)floor(y)-h/2;
    uint16_t row[256];bool complete=m&&m->bytes.data;
    for(int py=0;py<h;py++){
        int64_t world_y=top+py;int64_t ty=(int64_t)floor(world_y/256.0);unsigned ry=(unsigned)(world_y-ty*256);
        for(int px=0;px<w;){int64_t world_x=left+px,tx=(int64_t)floor(world_x/256.0);unsigned rx=(unsigned)(world_x-tx*256);
            int n=256-(int)rx;if(n>w-px)n=w-px;
            bool found=m&&m->bytes.data&&tx>=0&&ty>=0&&(reader?reader(user,m,z,(uint32_t)tx,(uint32_t)ty,ry,row):moto_map_tile_row(m,z,(uint32_t)tx,(uint32_t)ty,ry,row));
            if(!found)complete=false;
            for(int i=0;i<n;i++)p[py*w+px+i]=found?row[rx+i]:0xe71c;
            px+=n;
        }
    }
    double scale=ldexp(1.0,(int)z-18);
    for(unsigned i=1;i<count;i++)line(p,w,h,xy[i-1].x*scale-left,xy[i-1].y*scale-top,
        xy[i].x*scale-left,xy[i].y*scale-top,0x059f,2);
    double angle=position->heading_deg*3.141592653589793/180,dx=sin(angle),dy=-cos(angle),cx=x-left,cy=y-top;
    double tipx=cx+dx*22,tipy=cy+dy*22,lx=cx-dx*12+dy*13,ly=cy-dy*12-dx*13,rx=cx-dx*12-dy*13,ry=cy-dy*12+dx*13;
    uint16_t ink=fresh?0x001f:0x8410;
    line(p,w,h,tipx,tipy,lx,ly,0xffff,4);line(p,w,h,tipx,tipy,rx,ry,0xffff,4);line(p,w,h,lx,ly,rx,ry,0xffff,4);
    line(p,w,h,tipx,tipy,lx,ly,ink,2);line(p,w,h,tipx,tipy,rx,ry,ink,2);line(p,w,h,lx,ly,rx,ry,ink,2);
    return complete;
}

bool route_map_draw(uint16_t *p,unsigned w,unsigned h,const moto_map_package *m,const route_map_xy *xy,unsigned count,const moto_position *position,bool fresh){
    return route_map_draw_tiles(p,w,h,m,xy,count,position,fresh,NULL,NULL);
}
