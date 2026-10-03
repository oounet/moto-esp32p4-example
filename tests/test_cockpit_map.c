#include "../main/cockpit_map_mask.h"
#include "../main/instrument_frame.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
static uint16_t image[1024*600], cached[448*448], video[1024*600];
int main(int argc,char **argv) {
    for(unsigned i=0;i<448*448;i++)cached[i]=(uint16_t)(i|1);
    for(unsigned y=0;y<600;y++) for(unsigned x=0;x<1024;x++) {
        video[y*1024+x]=(uint16_t)(((x/24+y/24)%2)?0x5d96:0xb71c);
        image[y*1024+x]=0x1111;
    }
    cockpit_map_copy(image,cached,448,680,124);
    for(unsigned y=0;y<600;y++) for(unsigned x=0;x<1024;x++) {
        bool inside=y>=124 && y<572 && x>=cockpit_map_left(y);
        assert(image[y*1024+x]==(inside?cached[(y-124)*448+x-680]:0x1111));
    }
    assert(cockpit_map_left(125)<740 && cockpit_map_left(320)<790);
    assert(cockpit_map_left(470)<755 && cockpit_map_left(535)<720);
    assert(cockpit_map_left(0)==1024 && cockpit_map_left(599)==1024);
    if(argc==3) {
        uint16_t *art=malloc(1080*608*2);assert(art);
        FILE *in=fopen(argv[1],"rb");assert(in && fread(art,2,1080*608,in)==1080*608);fclose(in);
        instrument_frame_compose_full(image,art,NULL);free(art);
        cockpit_map_copy(image,video,1024,904-512,348-300);
        FILE *out=fopen(argv[2],"wb");assert(out);fprintf(out,"P6\n1024 600\n255\n");
        for(unsigned i=0;i<1024*600;i++){unsigned p=image[i];unsigned char rgb[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};fwrite(rgb,1,3,out);}fclose(out);
    }
    puts("cockpit: curve coverage, source bounds and native-pixel crop passed");
}
