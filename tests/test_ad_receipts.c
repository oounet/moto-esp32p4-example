#include "ad_receipts.h"
#include <moto_freertos/terminal.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t disk[64][49], pending[49];
static bool exists[64], fail_commit;
static unsigned pending_index;
static bool dirty;
esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *h) {
    assert(!strcmp(ns, "moto_ad_rcpt") && mode == NVS_READWRITE); *h = 1; return ESP_OK;
}
void nvs_close(nvs_handle_t h) { assert(h == 1); dirty = false; }
static unsigned index_of(const char *key) { unsigned n=(unsigned)strtoul(key+1,NULL,10); assert(n<64); return n; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *k, void *out, size_t *size) {
    assert(h == 1); unsigned i=index_of(k); if (!exists[i]) return ESP_ERR_NVS_NOT_FOUND;
    assert(*size >= 49); *size=49; memcpy(out,disk[i],49); return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *k,const void *b,size_t size) {
    assert(h==1 && size==49); pending_index=index_of(k); memcpy(pending,b,49); dirty=true; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    assert(h==1); if(fail_commit) return -1;
    if(dirty){memcpy(disk[pending_index],pending,49);exists[pending_index]=true;dirty=false;} return ESP_OK;
}
int main(void) {
    example_ad_receipts s={0}; uint8_t id[16]={1}, hash[32]={2}, read_hash[32]; bool used;
    assert(example_receipts_open(&s)==MOTO_OK);
    assert(example_receipts_lookup(&s,id,read_hash,&used)==0);
    assert(example_receipts_consume(&s,id)==MOTO_STATE);
    assert(example_receipts_commit(&s,id,hash)==MOTO_OK);
    assert(example_receipts_lookup(&s,id,read_hash,&used)==1 && !used && !memcmp(hash,read_hash,32));
    hash[0]=3; assert(example_receipts_commit(&s,id,hash)==MOTO_STATE); hash[0]=2;
    fail_commit=true; assert(example_receipts_consume(&s,id)==MOTO_IO);
    assert(example_receipts_lookup(&s,id,read_hash,&used)==MOTO_IO); /* poisoned until reload */
    example_receipts_close(&s); fail_commit=false; assert(example_receipts_open(&s)==MOTO_OK);
    assert(example_receipts_consume(&s,id)==MOTO_OK);
    assert(example_receipts_consume(&s,id)==1);
    example_receipts_close(&s); assert(example_receipts_open(&s)==MOTO_OK);
    assert(example_receipts_lookup(&s,id,read_hash,&used)==1 && used);
    for(unsigned i=2;i<=64;++i){id[0]=(uint8_t)i; assert(example_receipts_commit(&s,id,hash)==MOTO_OK);}
    id[0]=65; assert(example_receipts_commit(&s,id,hash)==MOTO_IO); /* no eviction */
    id[0]=1; assert(example_receipts_consume(&s,id)==1);
    example_receipts_close(&s); disk[0][48]=2;
    assert(example_receipts_open(&s)==MOTO_IO && !s.open); /* corrupt row */
    puts("NVS receipt adapter: dedupe, restart, commit failure, capacity and corruption passed (mock NVS)");
    return 0;
}
