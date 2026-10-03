#include "notice_view.h"
#include "moto/notification_icons.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void render(uint16_t *p,unsigned w,unsigned h,const terminal_notice *notice) {
    for(unsigned i=0;i<w*h;i++) p[i]=0x1234;
    terminal_notice_draw(p,w,h,notice);
}
static void ppm(const char *path,const uint16_t *p,unsigned w,unsigned h) {
    FILE *f=fopen(path,"wb"); assert(f);
    fprintf(f,"P6\n%u %u\n255\n",w,h);
    for(unsigned i=0;i<w*h;i++) {
        unsigned v=p[i];
        unsigned char rgb[]={(v>>11)*255/31,((v>>5)&63)*255/63,(v&31)*255/31};
        assert(fwrite(rgb,1,3,f)==3);
    }
    fclose(f);
}
int main(int argc,char **argv) {
    unsigned w=1024,h=600;
    uint16_t *pixels = malloc(w*h*sizeof *pixels);
    uint16_t *android = malloc(w*h*sizeof *pixels);
    assert(pixels && android);
    terminal_notice notice={0};
    strcpy(notice.app,"com.tencent.mm");
    strcpy(notice.title,"骑行伙伴");
    strcpy(notice.text,"你好，我们在路口等你。请注意安全。🙂");
    render(pixels,w,h,&notice);
    unsigned changed=0,white=0,colored=0;
    for(unsigned y=0;y<h;y++) for(unsigned x=0;x<w;x++) {
        uint16_t p=pixels[y*w+x];
        if(y<16 || y>=136 || x<112 || x>=912) assert(p==0x1234);
        if(p!=0x1234)changed++;
        if(p==0xffff)white++;
        if(x>=136 && x<200 && y>=32 && y<96 && p!=0xffff && p!=0x1083)colored++;
    }
    assert(changed>80000 && white>50000 && colored>500);
    assert(pixels[16*w+112]==0x1234); /* rounded corner preserves the background */
    if(argc>1) ppm(argv[1],pixels,w,h);
    memcpy(android,pixels,w*h*sizeof *pixels);
    strcpy(notice.app,"com.tencent.xin");
    render(pixels,w,h,&notice);
    assert(!memcmp(android,pixels,w*h*sizeof *pixels)); /* same app across platforms */
    strcpy(notice.app,"com.unknown.example");
    render(pixels,w,h,&notice);
    assert(memcmp(android,pixels,w*h*sizeof *pixels));
    /* A long fallback app ID must not erase the four-character title. */
    for(unsigned y=26;y<74;y++)
        assert(!memcmp(android+y*w+216,pixels+y*w+216,160*sizeof *pixels));
    if(argc>2) ppm(argv[2],pixels,w,h);
    memset(notice.title,'A',sizeof notice.title-1);
    memset(notice.text,'A',sizeof notice.text-1);
    for(unsigned width=160;width<=1024;width+=32) {
        for(unsigned height=80;height<=200;height+=20) {
            /* Exact allocation lets ASan catch bottom/row clipping violations. */
            uint16_t *small=malloc(width*height*sizeof *small); assert(small);
            render(small,width,height,&notice); free(small);
        }
    }
    strcpy(notice.text,"\xf0\x9f"); /* truncated UTF-8 */
    render(pixels,w,h,&notice);
    notice.app[0]=notice.title[0]=notice.text[0]=0;
    render(pixels,w,h,&notice);
    terminal_notice_draw(pixels,12,12,&notice);
    terminal_notice_draw(NULL,w,h,&notice);
    free(android); free(pixels);
    puts("notification banner: matched/fallback icons, cross-platform IDs, clipping, long text and UTF-8 passed");
}
