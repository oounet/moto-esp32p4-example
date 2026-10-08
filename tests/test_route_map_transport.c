/* Exercise the production socket pump. TLS/protocol are tested separately. */
#include "lwip/sockets.h"
#include "moto/route_map.h"
#include "moto/tls.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static ssize_t test_recv(int,void *,size_t,int);
static ssize_t test_send(int,const void *,size_t,int);
static int test_close(int);
#define recv test_recv
#define send test_send
#define close test_close
#include "../main/route_map_transport.c"
#undef recv
#undef send
#undef close
static size_t available,fragment,received,acknowledged,send_limit,wire_size;
static unsigned reads,polls,closes;
static int receive_error,send_error,feed_error,poll_error;
static bool authorized;
static uint8_t wire[32768];
static const uint8_t ack[]={0xac,0x01,0x00,0x7f};
static ssize_t test_recv(int fd,void *p,size_t n,int flags){
    (void)fd;(void)flags;reads++;
    if(!available){errno=receive_error;return receive_error?-1:0;}
    if(n>available)n=available;if(n>fragment)n=fragment;
    memset(p,0x42,n);available-=n;return (ssize_t)n;
}
static ssize_t test_send(int fd,const void *p,size_t n,int flags){
    (void)fd;(void)flags;
    if(send_error){errno=send_error;return -1;}
    if(n>send_limit)n=send_limit;
    assert(wire_size+n<=sizeof wire);memcpy(wire+wire_size,p,n);wire_size+=n;return (ssize_t)n;
}
static int test_close(int fd){(void)fd;closes++;return 0;}
int64_t esp_timer_get_time(void){return 9000000;}
void esp_fill_random(void *p,size_t n){memset(p,1,n);}
bool moto_map_credentials(moto_sdk *s,uint64_t *t,uint64_t *o,uint8_t key[32]){
    (void)s;*t=7;*o=9;memset(key,1,32);return authorized;
}
moto_tls *moto_tls_create(const moto_tls_config *c){(void)c;return (moto_tls *)&wire;}
void moto_tls_destroy(moto_tls *t){(void)t;}
moto_secure_transport moto_tls_transport(moto_tls *t){(void)t;return (moto_secure_transport){0};}
moto_map_wifi *moto_map_wifi_create(moto_sdk *s,uint64_t t,moto_secure_transport p){
    (void)s;(void)t;(void)p;return (moto_map_wifi *)&received;
}
void moto_map_wifi_destroy(moto_map_wifi *m){(void)m;}
int moto_map_wifi_feed(moto_map_wifi *m,const uint8_t *p,size_t n){
    (void)m;(void)p;if(feed_error)return feed_error;received+=n;return MOTO_OK;
}
int moto_map_wifi_poll(moto_map_wifi *m){
    (void)m;polls++;if(poll_error)return poll_error;
    /* A complete 3,000-byte payload plus synthetic record overhead emits ACK. */
    while(received-acknowledged>=3050){assert(output(NULL,ack,sizeof ack)==sizeof ack);acknowledged+=3050;}
    return MOTO_OK;
}
static void reset(void){
    client=100;listener=99;token=7;session=9;last_io=9000;queued=sent=0;
    tls=(moto_tls *)&wire;map_link=(moto_map_wifi *)&received;
    available=received=acknowledged=wire_size=0;fragment=2048;send_limit=sizeof wire;
    reads=polls=closes=0;receive_error=EAGAIN;send_error=feed_error=poll_error=0;authorized=true;
}
int main(void){
    reset();available=3050;terminal_map_poll(NULL);
    assert(reads==3&&polls==3&&available==0&&wire_size==sizeof ack);
    assert(!memcmp(wire,ack,sizeof ack)); /* ACK in same tick, not next tick. */
    reset();available=3050;fragment=700;terminal_map_poll(NULL);
    assert(reads==6&&wire_size==sizeof ack);
    reset();available=100000;terminal_map_poll(NULL);
    assert(reads==32&&polls==32&&available==100000-32*2048); /* BLE work is bounded. */
    reset();available=3050;send_error=EAGAIN;terminal_map_poll(NULL);
    assert(queued==sizeof ack&&sent==0&&wire_size==0&&client>=0);
    send_error=0;send_limit=2;terminal_map_poll(NULL);
    assert(queued==0&&wire_size==sizeof ack&&!memcmp(wire,ack,sizeof ack));
    reset();uint8_t data[32760];memset(data,0x55,sizeof data);
    assert(output(NULL,data,sizeof data)==sizeof data);send_limit=16000;
    assert(flush_output(9000)&&sent==16000);
    assert(output(NULL,data,100)==100);assert(sent==0&&queued==16860); /* Compact partial send. */
    assert(output(NULL,data,sizeof data)==0); /* Backpressure, no overflow. */
    reset();available=100;feed_error=MOTO_IO;terminal_map_poll(NULL);assert(client<0&&closes==1);
    reset();available=100;feed_error=MOTO_AGAIN;terminal_map_poll(NULL);assert(client<0&&closes==1);
    reset();poll_error=MOTO_AUTH;terminal_map_poll(NULL);assert(client<0&&closes==1);
    reset();receive_error=0;terminal_map_poll(NULL);assert(client<0&&closes==1);
    reset();receive_error=ECONNRESET;terminal_map_poll(NULL);assert(client<0&&closes==1);
    reset();output(NULL,ack,sizeof ack);send_error=EPIPE;terminal_map_poll(NULL);assert(client<0&&closes==1);
    reset();authorized=false;terminal_map_poll(NULL);assert(client<0&&listener<0&&closes==2);
    puts("map socket pump: fragmented records, immediate ACK, bounded work, backpressure, revocation and failures passed");
}
