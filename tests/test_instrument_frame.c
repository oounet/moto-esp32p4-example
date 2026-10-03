#include "instrument_frame.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
int main(void) {
    uint16_t *source=malloc(ART_W*ART_H*sizeof(*source));
    uint16_t *buffers[2]={malloc(LCD_W*LCD_H*2),malloc(LCD_W*LCD_H*2)};
    uint16_t xs[LCD_W]; assert(source && buffers[0] && buffers[1]);
    for(unsigned x=0;x<LCD_W;x++) xs[x]=x*ART_W/LCD_W;
    for(unsigned i=0;i<ART_W*ART_H;i++) source[i]=(uint16_t)(i%65534+1);
    for(unsigned transition=0;transition<8;transition++) {
        for(unsigned b=0;b<2;b++) {
            memset(buffers[b],0xab+transition,LCD_W*LCD_H*2);
            instrument_frame_compose(buffers[b],source,xs);
            for(unsigned y=0;y<LCD_H;y++) for(unsigned x=0;x<LCD_W;x++)
                assert(buffers[b][y*LCD_W+x]==source[(y*ART_H/LCD_H)*ART_W+xs[x]]);
            instrument_frame_compose_full(buffers[b],source,xs);
            // Independent floating-point, four-neighbour reference. Two 565
            // rounding stages and fixed-point coordinates allow <= 2 levels.
            for(unsigned y=0;y<LCD_H;y++) for(unsigned x=0;x<LCD_W;x++) {
                double fx=(x+.5)*ART_W/LCD_W-.5;
                double fy=COCKPIT_TOP+(y+.5)*COCKPIT_H/LCD_H-.5;
                unsigned sx=(unsigned)fx,sy=(unsigned)fy;
                double wx=fx-sx,wy=fy-sy;
                unsigned nx=sx+1<ART_W?sx+1:sx,ny=sy+1<ART_H?sy+1:sy;
                uint16_t samples[]={source[sy*ART_W+sx],source[sy*ART_W+nx],
                    source[ny*ART_W+sx],source[ny*ART_W+nx]};
                for(unsigned channel=0;channel<3;channel++) {
                    unsigned shift=channel==0?11:channel==1?5:0,mask=channel==1?63:31;
                    double expected=((samples[0]>>shift)&mask)*(1-wx)*(1-wy)
                        +((samples[1]>>shift)&mask)*wx*(1-wy)
                        +((samples[2]>>shift)&mask)*(1-wx)*wy
                        +((samples[3]>>shift)&mask)*wx*wy;
                    unsigned actual=(buffers[b][y*LCD_W+x]>>shift)&mask;
                    assert(fabs(actual-expected)<=2.0);
                }
            }
        }
    }
    // A source column omitted by the old nearest-neighbour sampler must
    // remain visible as a continuous vertical hairline after filtering.
    unsigned missing=0;
    for(unsigned column=0;column<ART_W;column++) {
        unsigned hits=0;for(unsigned x=0;x<LCD_W;x++)hits+=xs[x]==column;
        if(!hits){missing=column;break;}
    }
    assert(missing>0);
    memset(source,0,ART_W*ART_H*2);
    for(unsigned y=0;y<ART_H;y++)source[y*ART_W+missing]=0xffff;
    instrument_frame_compose_full(buffers[0],source,xs);
    for(unsigned y=0;y<LCD_H;y++) {
        unsigned lit=0;for(unsigned x=0;x<LCD_W;x++)lit+=buffers[0][y*LCD_W+x]!=0;
        assert(lit>0);
    }
    // A solid artwork must reach every edge, with no letterbox strips left.
    for(unsigned i=0;i<ART_W*ART_H;i++)source[i]=0x39e7;
    instrument_frame_compose(buffers[0],source,xs);
    for(unsigned i=0;i<LCD_W*LCD_H;i++)assert(buffers[0][i]==0x39e7);
    // Partial updates must exactly match a complete render, including bilinear
    // neighbours outside the dirty rectangle and repeated changes at all edges.
    for(unsigned style=0;style<2;style++) {
        if(style) instrument_frame_compose_full(buffers[0],source,xs);
        else instrument_frame_compose(buffers[0],source,xs);
        for(unsigned frame=0;frame<40;frame++) {
            unsigned x1=frame<4?(frame%2?ART_W-1:0):(frame*137)%ART_W;
            unsigned y1=frame<4?(frame/2?ART_H-1:0):(frame*83)%ART_H;
            unsigned x2=x1+frame*7%131,y2=y1+frame*3%67;
            if(x2>=ART_W)x2=ART_W-1;
            if(y2>=ART_H)y2=ART_H-1;
            for(unsigned y=y1;y<=y2;y++)for(unsigned x=x1;x<=x2;x++)
                source[y*ART_W+x]=(uint16_t)(frame*935+x*33+y*17);
            instrument_frame_update(buffers[0],source,xs,style,x1,y1,x2,y2);
            if(style) instrument_frame_compose_full(buffers[1],source,xs);
            else instrument_frame_compose(buffers[1],source,xs);
            assert(!memcmp(buffers[0],buffers[1],LCD_W*LCD_H*2));
        }
    }
    free(source);free(buffers[0]);free(buffers[1]);
    puts("fullscreen classic sampling and cockpit filtering: both buffers and every edge verified");
}
