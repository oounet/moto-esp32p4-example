#include "hud_view.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
int main(void) {
    const size_t n=1024*600;
    uint16_t *a=calloc(n+2,sizeof(*a)),*b=calloc(n+2,sizeof(*b));assert(a&&b);
    a[0]=a[n+1]=0x1234;
    moto_hud hud={.id=1,.action=3,.distance_m=120,.remaining_m=2300,.remaining_s=480};
    strcpy(hud.next_road,"北京路");
    terminal_hud_draw(a+1,1024,600,&hud,true,false);
    assert(a[0]==0x1234&&a[n+1]==0x1234);
    terminal_hud_draw(b+1,1024,600,&hud,false,false);
    assert(memcmp(a+1,b+1,n*2));
    hud.distance_m=999999;strcpy(hud.next_road,"旧路线");
    terminal_hud_draw(a+1,1024,600,&hud,false,false);
    assert(!memcmp(a+1,b+1,n*2)); /* Expired HUD must not display stale instructions. */
    terminal_hud_draw(a+1,1024,600,&hud,true,true);
    assert(a[0]==0x1234&&a[n+1]==0x1234);
    free(a);free(b);puts("HUD bounds and stale-data checks passed");return 0;
}
