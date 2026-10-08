#include "route_map_store.h"
#include "moto/route_map.h"
#include "moto/tls.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
static const char *TAG="MotoMap";
static int listener=-1,client=-1;
static uint64_t last_listener_error;
static moto_tls *tls;
static moto_map_wifi *map_link;
static uint64_t token,session,last_io;
static uint8_t input[2048],tx[32768];static size_t queued,sent;
static int random_bytes(void *u,uint8_t *p,size_t n){(void)u;esp_fill_random(p,n);return 0;}
static int output(void *u,const uint8_t *p,size_t n){
    (void)u;
    if(sent){memmove(tx,tx+sent,queued-sent);queued-=sent;sent=0;}
    if(n>sizeof tx-queued)return 0;
    memcpy(tx+queued,p,n);queued+=n;return (int)n;
}
static void close_client(void){
    if(map_link)moto_map_wifi_destroy(map_link);
    map_link=NULL;
    if(tls)moto_tls_destroy(tls);
    tls=NULL;
    if(client>=0)close(client);
    client=-1;queued=sent=0;memset(tx,0,sizeof tx);
}
static bool flush_output(uint64_t now){
    if(queued>sent){
        int n=send(client,tx+sent,queued-sent,0);
        if(n>0){sent+=(size_t)n;last_io=now;}
        else if(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK){close_client();return false;}
    }
    if(queued==sent)queued=sent=0;
    return true;
}
void terminal_map_poll(moto_sdk *sdk){
    uint8_t key[32]={0};uint64_t current=0,owner=0,now=esp_timer_get_time()/1000;
    bool valid=moto_map_credentials(sdk,&current,&owner,key);
    if(client>=0&&(!valid||current!=token||owner!=session||now-last_io>20000))close_client();
    if(!valid){memset(key,0,sizeof key);if(listener>=0)close(listener);listener=-1;return;}
    if(listener<0){
        listener=socket(AF_INET,SOCK_STREAM,IPPROTO_IP);
        if(listener>=0){int yes=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof yes);
            struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons(5003),.sin_addr.s_addr=inet_addr("192.168.4.1")};
            if(bind(listener,(struct sockaddr *)&addr,sizeof addr)<0||listen(listener,1)<0||fcntl(listener,F_SETFL,O_NONBLOCK)<0){int code=errno;close(listener);listener=-1;if(now-last_listener_error>5000){last_listener_error=now;ESP_LOGW(TAG,"listener error=%d",code);}}
            else ESP_LOGI(TAG,"listening port=5003");}
    }
    if(listener<0){memset(key,0,sizeof key);return;}
    if(client<0){
        client=accept(listener,NULL,NULL);
        if(client>=0){
            if(fcntl(client,F_SETFL,O_NONBLOCK)<0){close_client();memset(key,0,sizeof key);return;}
            int yes=1;setsockopt(client,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof yes);
            token=current;session=owner;last_io=now;ESP_LOGI(TAG,"TCP accepted");
            moto_tls_config cfg={NULL,random_bytes,output,key,32,"moto-route-map-v1",false};
            tls=moto_tls_create(&cfg);if(tls)map_link=moto_map_wifi_create(sdk,token,moto_tls_transport(tls));
            if(!map_link){ESP_LOGW(TAG,"TLS/link allocation failed");close_client();}
        }
    }
    memset(key,0,sizeof key);if(client<0)return;
    if(!flush_output(now))return;
    // Drain a fragmented record and send its ACK in this tick. The old single
    // read deferred both the second fragment and ACK to later 20 ms BLE ticks.
    // Keep work bounded and poll TLS after each read so its input ring can drain.
    for(unsigned budget=0;budget<32;budget++){
        int n=recv(client,input,sizeof input,0);
        if(n==0||(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK)){ESP_LOGW(TAG,"receive closed result=%d error=%d",n,n<0?errno:0);close_client();return;}
        if(n>0){last_io=now;if(moto_map_wifi_feed(map_link,input,(size_t)n)!=MOTO_OK){ESP_LOGW(TAG,"TLS input rejected");close_client();return;}}
        int result=moto_map_wifi_poll(map_link);
        if(result<0){ESP_LOGW(TAG,"map poll error=%d",result);close_client();return;}
        if(!flush_output(now))return;
        if(n<0)return;
    }
}
