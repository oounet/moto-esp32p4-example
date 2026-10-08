#include "route_map_compression.h"
#include "moto/route_map.h"
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "miniz.h"
#else
#include <zlib.h>
#endif
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
size_t route_map_expanded_size(const uint8_t *p,size_t n,size_t limit){
    if(!p||n<18||memcmp(p,"MZP1",4))return 0;
    uint32_t size=get32(p+4);return size>=24&&size<=limit?size:0;
}
bool route_map_inflate(const uint8_t *p,size_t n,uint8_t *out,size_t size){
    if(!out||route_map_expanded_size(p,n,size)!=size||!size)return false;
    bool valid=false;
#ifdef ESP_PLATFORM
    // Heap state avoids placing miniz's Huffman tables on the BLE task stack.
    tinfl_decompressor *state=calloc(1,sizeof *state);if(!state)return false;
    tinfl_init(state);size_t in=n-12,written=size;
    tinfl_status rc=tinfl_decompress(state,p+12,&in,out,out,&written,TINFL_FLAG_PARSE_ZLIB_HEADER|TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    valid=rc==TINFL_STATUS_DONE&&in==n-12&&written==size;free(state);
#else
    // Host validation of the envelope; real P4 tests also exercise ROM miniz.
    z_stream stream={0};stream.next_in=(Bytef *)p+12;stream.avail_in=(uInt)(n-12);stream.next_out=out;stream.avail_out=(uInt)size;
    if(inflateInit(&stream)!=Z_OK)return false;
    int rc=inflate(&stream,Z_FINISH);valid=rc==Z_STREAM_END&&stream.total_in==n-12&&stream.total_out==size;inflateEnd(&stream);
#endif
    return valid&&moto_map_crc32(0,out,size)==get32(p+8);
}
