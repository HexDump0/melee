#include "platform/screenshot.h"
#include "gx/gl.h"
#include <SDL.h>
#include <stdlib.h>
#include <string.h>
int platform_screenshot(const char *path,int w,int h)
{
    SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,w,h,24,SDL_PIXELFORMAT_RGB24);
    if(!s)return 0;
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,s->pixels);
    unsigned char *tmp=malloc((size_t)s->pitch);
    if(!tmp){SDL_FreeSurface(s);return 0;}
    for(int y=0;y<h/2;++y) {
        void *a=(char*)s->pixels+y*s->pitch,*b=(char*)s->pixels+(h-1-y)*s->pitch;
        memcpy(tmp,a,s->pitch);memcpy(a,b,s->pitch);memcpy(b,tmp,s->pitch);
    }
    free(tmp);int ok=SDL_SaveBMP(s,path)==0;SDL_FreeSurface(s);return ok;
}
