#include "route_vector_view.h"
#include <math.h>
#include <stdint.h>

static double wrap_x(double delta){
    const double world=MOTO_VECTOR_WORLD*.25; /* RVP1 z19 to display z17. */
    if(delta>world/2)delta-=world;else if(delta< -world/2)delta+=world;
    return delta;
}
static uint32_t wrap_block(int x){return (uint32_t)((x%32768+32768)%32768);}
static bool clip(double p,double q,double *a,double *b){
    if(p==0)return q>=0;
    double t=q/p;
    if(p<0){if(t>*b)return false;if(t>*a)*a=t;}else{if(t<*a)return false;if(t<*b)*b=t;}return true;
}
static void line(uint16_t *p,int w,int h,double x0,double y0,double x1,double y1,uint16_t color,int radius){
    double dx=x1-x0,dy=y1-y0,a=0,b=1;
    if(!clip(-dx,x0+radius,&a,&b)||!clip(dx,w-1+radius-x0,&a,&b)||
       !clip(-dy,y0+radius,&a,&b)||!clip(dy,h-1+radius-y0,&a,&b))return;
    double ex=x0+b*dx,ey=y0+b*dy;x0+=a*dx;y0+=a*dy;dx=ex-x0;dy=ey-y0;
    int steps=(int)ceil(fmax(fabs(dx),fabs(dy)));if(!steps)steps=1;
    for(int i=0;i<=steps;i++){
        int x=(int)lround(x0+dx*i/steps),y=(int)lround(y0+dy*i/steps);
        for(int oy=-radius;oy<=radius;oy++)for(int ox=-radius;ox<=radius;ox++)
            if(x+ox>=0&&x+ox<w&&y+oy>=0&&y+oy<h)p[(size_t)(y+oy)*w+x+ox]=color;
    }
}
static void road(uint16_t *p,int w,int h,const moto_vector_block *b,const moto_vector_feature *f,
                 double left,double top,uint16_t color,int radius){
    moto_vector_points it;moto_vector_points_begin(f,&it);uint32_t x,y,px,py;
    if(!moto_vector_point_next(&it,&px,&py))return;
    const double bx=wrap_x(b->x*1024.0-left),by=b->y*1024.0-top; /* z15 extent4096 -> z17 pixels */
    while(moto_vector_point_next(&it,&x,&y)){
        line(p,w,h,bx+px*.25,by+py*.25,bx+x*.25,by+y*.25,color,radius);px=x;py=y;
    }
}
static bool draw(uint16_t *p,unsigned width,unsigned height,const moto_vector_map *m,
                 const moto_route_point *coarse,unsigned coarse_count,const moto_position *position,bool fresh,bool night){
    if(!p||!position||width<320||height<200||width>2048||height>2048||
       (m&&(!m->bytes.data||position->id!=m->navigation||position->route_revision!=m->route))||
       position->point.latitude_e6< -90000000||position->point.latitude_e6>90000000||
       position->point.longitude_e6< -180000000||position->point.longitude_e6>180000000)return false;
    int w=(int)width,h=(int)height;double x,y;moto_map_project(position->point,17,&x,&y);
    if(!isfinite(x)||!isfinite(y))return false;
    double left=floor(x)-w/2,top=floor(y)-h/2;
    uint16_t background=night?0x1082:0xef5d;
    for(size_t i=0;i<(size_t)w*h;i++)p[i]=background;
    /* At most 4x4 blocks for the supported viewport sizes. Extra margin keeps
     * strokes crossing a viewport boundary visible. Coverage excludes margin. */
    int minx=(int)floor((left-8)/1024),maxx=(int)floor((left+w+7)/1024);
    int miny=(int)floor((top-8)/1024),maxy=(int)floor((top+h+7)/1024);
    struct cursor {moto_vector_block block;moto_vector_features it;moto_vector_feature feature;bool ready;} visible[16];
    unsigned count=0;
    for(int bx=minx;bx<=maxx;bx++)for(int by=miny;by<=maxy;by++){
        moto_vector_block block;if(!m||by<0||by>=32768||!moto_vector_block_find(m,wrap_block(bx),(uint32_t)by,&block))continue;
        if(count==16)return false;
        struct cursor *c=&visible[count++];c->block=block;
        moto_vector_features_begin(&c->block,&c->it);c->ready=moto_vector_feature_next(&c->it,&c->feature);
    }
    /* RVP1 validates sorted layers. Each feature header is visited once; only
     * visible geometry is decoded, with a second pass for bridge casing. */
    for(int layer=-8;layer<=7;layer++)for(unsigned i=0;i<count;i++){
        struct cursor *c=&visible[i];
        while(c->ready&&c->feature.layer==layer){
            moto_vector_feature *f=&c->feature;int radius=f->road_class==0?2:1;
            uint16_t color=night?(f->road_class<2?0x7bcf:0x528a):(f->road_class<2?0x8c71:0xb596);
            if(f->flags&2)color=night?0x39c7:0xc618;
            if(f->flags&1)road(p,w,h,&c->block,f,left,top,background,radius+2);
            road(p,w,h,&c->block,f,left,top,color,radius);
            c->ready=moto_vector_feature_next(&c->it,&c->feature);
        }
    }
    if(m){
        uint32_t px,py;moto_vector_route_point(m,0,&px,&py);
        for(unsigned i=1;i<m->route_points;i++){
            uint32_t qx,qy;moto_vector_route_point(m,i,&qx,&qy);
            double start=wrap_x(px*.25-left);
            line(p,w,h,start,py*.25-top,start+wrap_x(((double)qx-px)*.25),qy*.25-top,0x059f,3);px=qx;py=qy;
        }
    }else if(coarse&&coarse_count>=2){
        double px,py;moto_map_project(coarse[0],17,&px,&py);
        for(unsigned i=1;i<coarse_count;i++){
            double qx,qy;moto_map_project(coarse[i],17,&qx,&qy);double start=wrap_x(px-left);
            line(p,w,h,start,py-top,start+wrap_x(qx-px),qy-top,0x059f,3);px=qx;py=qy;
        }
    }
    double angle=(position->heading_deg%360)*3.141592653589793/180,dx=sin(angle),dy=-cos(angle),cx=x-left,cy=y-top;
    double tx=cx+dx*22,ty=cy+dy*22,lx=cx-dx*12+dy*13,ly=cy-dy*12-dx*13,rx=cx-dx*12-dy*13,ry=cy-dy*12+dx*13;
    uint16_t ink=fresh?0x03bf:0x8410;
    for(int pass=0;pass<2;pass++){
        uint16_t c=pass?ink:0xffff;int r=pass?2:4;
        line(p,w,h,tx,ty,lx,ly,c,r);line(p,w,h,tx,ty,rx,ry,c,r);line(p,w,h,lx,ly,rx,ry,c,r);
    }
    if(!m)return true; /* Route and position rendered; roads are not claimed as covered. */
    for(int bx=(int)floor(left/1024);bx<=(int)floor((left+w-1)/1024);bx++)
        for(int by=(int)floor(top/1024);by<=(int)floor((top+h-1)/1024);by++){
            if(by<0||by>=32768)continue;
            moto_vector_block b;if(!moto_vector_block_find(m,wrap_block(bx),(uint32_t)by,&b))return false;
        }
    return true;
}

bool route_vector_draw(uint16_t *p,unsigned w,unsigned h,const moto_vector_map *m,
                       const moto_position *position,bool fresh,bool night){
    return m&&draw(p,w,h,m,NULL,0,position,fresh,night);
}
bool route_vector_draw_navigation(uint16_t *p,unsigned w,unsigned h,const moto_route_point *points,unsigned count,
                                  const moto_position *position,bool fresh,bool night){
    return count<=MOTO_ROUTE_MAX_POINTS&&draw(p,w,h,NULL,points,count,position,fresh,night);
}
