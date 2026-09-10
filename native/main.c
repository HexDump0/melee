#include <SDL.h>
#include <SDL_opengl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "demo_assets.h"
#include "demo_attributes.h"
#include "demo_model.h"
#include "demo_physics.h"
#include "demo_text.h"

#define DEFAULT_DISC "iso/Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).ciso"
#define PI 3.14159265358979323846f

typedef struct Visual {
    DemoModel model;
    GLuint list;
    GLuint textures[DEMO_MAX_TEXTURES];
    float scale;
    const char *label;
} Visual;

static void rect(float x, float y, float w, float h)
{
    glBegin(GL_QUADS);
    glVertex2f(x,y); glVertex2f(x+w,y);
    glVertex2f(x+w,y+h); glVertex2f(x,y+h);
    glEnd();
}

static void circle(float x, float y, float z, float radius, int filled)
{
    glBegin(filled ? GL_TRIANGLE_FAN : GL_LINE_LOOP);
    if (filled) glVertex3f(x,y,z);
    for (int i=0; i<=48; ++i) {
        float a = i*2*PI/48;
        glVertex3f(x+cosf(a)*radius,y+sinf(a)*radius,z);
    }
    glEnd();
}

static int symbol_log(const char *name, unsigned int off, void *user)
{
    (void)user;
    printf("  %s @ 0x%x\n",name,off);
    return 0;
}

static int load_model(Visual *v, const char *disc, const char *file)
{
    DemoAsset asset = {0};
    char error[256];
    if (demo_asset_load(disc,file,&asset,error,sizeof(error))) {
        fprintf(stderr,"%s: %s\n",file,error); return 0;
    }
    printf("Loaded %s: %zu bytes\n",file,asset.size);
    demo_asset_enumerate_public_symbols(&asset,symbol_log,NULL,error,sizeof(error));
    DemoModelVertex *storage = calloc(300000,sizeof(*storage));
    if (!storage) { demo_asset_free(&asset); return 0; }
    demo_model_init(&v->model,storage,300000);
    int ok = demo_model_load(&v->model,asset.data,asset.size,0,error,sizeof(error));
    demo_asset_free(&asset);
    if (!ok || !v->model.vertex_count) {
        fprintf(stderr,"Model %s: %s (%zu vertices)\n",file,error,v->model.vertex_count);
        free(storage); memset(v,0,sizeof(*v)); return 0;
    }
    printf("Decoded %s: %zu triangles, %zu textures; bounds [%.2f %.2f %.2f] to [%.2f %.2f %.2f]\n",
           file,v->model.vertex_count/3,v->model.texture_count,v->model.bounds_min[0],v->model.bounds_min[1],
           v->model.bounds_min[2],v->model.bounds_max[0],v->model.bounds_max[1],v->model.bounds_max[2]);
    float height = v->model.bounds_max[1]-v->model.bounds_min[1];
    v->scale = height > .001f ? 11.0f/height : 1;
    return 1;
}

static void compile_model(Visual *v)
{
    size_t i;
    for(i=0;i<v->model.texture_count&&i<DEMO_MAX_TEXTURES;++i) {
        DemoModelTexture *t=&v->model.textures[i];
        glGenTextures(1,&v->textures[i]);
        glBindTexture(GL_TEXTURE_2D,v->textures[i]);
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,t->width,t->height,0,GL_RGBA,
                     GL_UNSIGNED_BYTE,t->rgba);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    }
    v->list=glGenLists(1);
    glNewList(v->list,GL_COMPILE);
    {
        int current=-2;
        for(i=0;i<v->model.vertex_count;++i) {
            DemoModelVertex *p=&v->model.vertices[i];
            if(p->texture!=current) {
                if(current!=-2)glEnd();
                current=p->texture;
                if(current>=0&&(size_t)current<v->model.texture_count)
                    glBindTexture(GL_TEXTURE_2D,v->textures[current]);
                else
                    glBindTexture(GL_TEXTURE_2D,0);
                glBegin(GL_TRIANGLES);
            }
            glColor4ubv(p->color);
            glNormal3fv(p->normal);
            glTexCoord2fv(p->uv);
            glVertex3fv(p->position);
        }
        if(current!=-2)glEnd();
    }
    glEndList();
}

/* Isolated turntable view used to inspect decoded bind-pose geometry. */static void render_model_view(const Visual *v,float angle,float elevation,int w,int h)
{
    float cx=(v->model.bounds_min[0]+v->model.bounds_max[0])*.5f;
    float cy=(v->model.bounds_min[1]+v->model.bounds_max[1])*.5f;
    float cz=(v->model.bounds_min[2]+v->model.bounds_max[2])*.5f;
    float height=v->model.bounds_max[1]-v->model.bounds_min[1];
    float half=height*.62f,aspect=(float)w/(float)h;
    glViewport(0,0,w,h);
    glClearColor(.055f,.075f,.11f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();
    if(aspect>=1)glOrtho(-half*aspect,half*aspect,-half,half,-200,200);
    else glOrtho(-half,half,-half/aspect,half/aspect,-200,200);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glRotatef(elevation,1,0,0);
    glRotatef(angle,0,1,0);
    glTranslatef(-cx,-cy,-cz);
    GLfloat light[]={-40,80,120,0};glLightfv(GL_LIGHT0,GL_POSITION,light);
    glEnable(GL_DEPTH_TEST);glEnable(GL_LIGHTING);glEnable(GL_TEXTURE_2D);
    glCallList(v->list);
    glDisable(GL_TEXTURE_2D);glDisable(GL_LIGHTING);
}

static void platform(const DemoPhysicsPlatform *p, int main_stage)
{
    float l=p->left,r=p->right,y=p->top,d=main_stage?5:2,t=main_stage?3:1;
    glColor3f(.18f,.25f,.37f);
    glBegin(GL_QUADS);
    glVertex3f(l,y,-d); glVertex3f(r,y,-d); glVertex3f(r,y,d); glVertex3f(l,y,d);
    glColor3f(.08f,.12f,.20f);
    glVertex3f(l,y,d); glVertex3f(r,y,d); glVertex3f(r,y-t,d); glVertex3f(l,y-t,d);
    glEnd();
    glColor3f(.23f,.85f,.79f); glLineWidth(2);
    glBegin(GL_LINES); glVertex3f(l,y+.08f,d+.05f); glVertex3f(r,y+.08f,d+.05f); glEnd();
    if(main_stage) {
        glColor3f(.07f,.09f,.15f);
        glBegin(GL_TRIANGLES);
        glVertex3f(l,y-t,d);glVertex3f(r,y-t,d);glVertex3f(0,y-18,0);
        glEnd();
    }
}

static void draw_fighter(const Visual *v, const DemoPhysicsFighter *f,
                         int player, unsigned tick)
{
    float r=player?.35f:1, g=player?.66f:.35f, b=player?1:.40f;
    glColor4f(r,g,b,.28f);
    glPushMatrix(); glTranslatef(f->x,f->y+.12f,0);glRotatef(-90,1,0,0);
    circle(0,0,0,4.5f,1);glPopMatrix();
    glPushMatrix();
    glTranslatef(f->x,f->y,0);
    float lean = -f->vx*2;
    glRotatef(lean,0,0,1);
    /* Costume models are shown in bind pose during this bring-up. */
    glRotatef(f->facing>0?65:-65,0,1,0);
    glScalef(v->scale,v->scale,v->scale);
    glTranslatef(-(v->model.bounds_min[0]+v->model.bounds_max[0])*.5f,
                 -v->model.bounds_min[1],
                 -(v->model.bounds_min[2]+v->model.bounds_max[2])*.5f);
    glEnable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
    glCallList(v->list);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glPopMatrix();
    if(f->shield) {
        glColor4f(r,g,b,.17f);circle(f->x,f->y+5.5f,7,8,1);
        glColor4f(r,g,b,.8f);circle(f->x,f->y+5.5f,7.1f,8,0);
    }
    if(f->attack_timer>8) {
        float a=(18-f->attack_timer)*.18f;
        glColor4f(1,.85f,.4f,.8f);
        glLineWidth(4);
        glBegin(GL_LINE_STRIP);
        for(int i=0;i<20;++i) {
            float angle=-1.0f+i*.1f+a;
            glVertex3f(f->x+f->facing*(3+cosf(angle)*10),f->y+5+sinf(angle)*6,8);
        }
        glEnd();
    }
    glColor3f(r,g,b);
    float top=f->y+15+.3f*sinf(tick*.08f);
    glBegin(GL_TRIANGLES);
    glVertex3f(f->x-1.4f,top+2,0);glVertex3f(f->x+1.4f,top+2,0);glVertex3f(f->x,top,0);
    glEnd();
}

static void hud(int w,int h,const DemoPhysicsFighter f[2], int cpu,int paused,
                const Visual v[2])
{
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,w,h,0,-1,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    float s=fmaxf(1.0f,w/1280.0f);
    glColor4f(.025f,.035f,.065f,.92f);rect(0,0,w,85*s);
    glColor3f(.92f,.95f,1);demo_text(28*s,22*s,3*s,"MELEE / NATIVE LAB");
    glColor3f(.39f,.8f,.77f);demo_text(29*s,54*s,1.5f*s,"REAL DISC MODELS / CUSTOM SANDBOX / 60 HZ");
    glColor3f(.65f,.71f,.8f);demo_text(w-324*s,27*s,1.5f*s,cpu?"F2: CPU ON / P: PAUSE":"F2: TWO PLAYERS / P: PAUSE");
    demo_text(w-324*s,49*s,1.5f*s,"R: RESET / ESC: QUIT");
    for(int i=0;i<2;++i) {
        float x=w*.5f+(i?95:-355)*s, y=h-153*s;
        glColor4f(.035f,.045f,.075f,.95f);rect(x,y,260*s,98*s);
        glColor3f(i?.35f:1,i?.66f:.35f,i?1:.4f);rect(x,y,5*s,98*s);
        demo_text(x+20*s,y+15*s,2*s,v[i].label);
        char line[64];snprintf(line,sizeof(line),"%.0f%%",f[i].damage);
        glColor3f(1,.95f,.85f);demo_text(x+20*s,y+40*s,4*s,line);
        snprintf(line,sizeof(line),"STOCK %d",f[i].stocks);
        glColor3f(.65f,.71f,.8f);demo_text(x+155*s,y+65*s,1.5f*s,line);
    }
    glColor3f(.68f,.73f,.84f);
    demo_text(25*s,h-33*s,1.3f*s,"P1: A/D MOVE  W/SPACE JUMP  F ATTACK  G SHIELD");
    demo_text(w-568*s,h-33*s,1.3f*s,"P2: ARROWS MOVE/JUMP  K ATTACK  L SHIELD");
    if(paused) {
        glColor4f(.02f,.025f,.04f,.8f);rect(w*.5f-135*s,h*.5f-35*s,270*s,70*s);
        glColor3f(1,1,1);demo_text(w*.5f-90*s,h*.5f-13*s,4*s,"PAUSED");
    }
}

static int screenshot(const char *path,int w,int h)
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

static void destroy_visual(Visual *v)
{
    size_t i;
    if(v->list)glDeleteLists(v->list,1);
    for(i=0;i<v->model.texture_count&&i<DEMO_MAX_TEXTURES;++i) {
        if(v->textures[i])glDeleteTextures(1,&v->textures[i]);
    }
    demo_model_free(&v->model);
    free(v->model.vertices);
    v->model.vertices=NULL;
    v->list=0;
}

int main(int argc,char **argv)
{
    const char *disc=DEFAULT_DISC,*capture=NULL,*model_file="PlMrNr.dat";
    int frames=0,inspect=0,scripted=0,view=0;
    float view_angle=210.0f,view_elev=-15.0f;
    for(int i=1;i<argc;++i) {
        if(!strcmp(argv[i],"--disc")&&i+1<argc)disc=argv[++i];
        else if(!strcmp(argv[i],"--model")&&i+1<argc)model_file=argv[++i];
        else if(!strcmp(argv[i],"--frames")&&i+1<argc)frames=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--screenshot")&&i+1<argc)capture=argv[++i];
        else if(!strcmp(argv[i],"--inspect"))inspect=1;
        else if(!strcmp(argv[i],"--scripted"))scripted=1;
        else if(!strcmp(argv[i],"--view"))view=1;
        else if(!strcmp(argv[i],"--angle")&&i+1<argc)view_angle=(float)atof(argv[++i]);
        else if(!strcmp(argv[i],"--elevation")&&i+1<argc)view_elev=(float)atof(argv[++i]);
        else {printf("Usage: %s [--disc IMAGE] [--model PlMrNr.dat] [--inspect] [--view [--angle DEG] [--elevation DEG]] [--frames N] [--screenshot FILE.bmp] [--scripted]\n",argv[0]);return strcmp(argv[i],"--help")!=0;}
    }
    Visual visuals[2];
    memset(visuals,0,sizeof(visuals));
    if(!load_model(&visuals[0],disc,model_file))return 1;
    /* Two instances of the same decoded costume during renderer bring-up. */
    visuals[0].label="P1 / MARIO";visuals[1]=visuals[0];visuals[1].label="P2 / MARIO";
    if(inspect){demo_model_free(&visuals[0].model);free(visuals[0].model.vertices);return 0;}
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER|SDL_INIT_TIMER)) {
        fprintf(stderr,"SDL: %s\n",SDL_GetError());return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    SDL_Window *window=SDL_CreateWindow("Melee Native Lab - custom playable sandbox",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1280,800,SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE);
    SDL_GLContext context=window?SDL_GL_CreateContext(window):NULL;
    if(!context){fprintf(stderr,"Graphics: %s\n",SDL_GetError());SDL_Quit();return 1;}
    SDL_GL_SetSwapInterval(1);
    printf("Renderer: %s\n",glGetString(GL_RENDERER));
    compile_model(&visuals[0]);visuals[1].list=visuals[0].list;
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_NORMALIZE);glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK,GL_AMBIENT_AND_DIFFUSE);
    GLfloat ambient[]={.78f,.78f,.82f,1},diffuse[]={.9f,.88f,.82f,1};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT,ambient);glLightfv(GL_LIGHT0,GL_DIFFUSE,diffuse);glEnable(GL_LIGHT0);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE,GL_TRUE);
    if(view) {
        int w,h;SDL_GL_GetDrawableSize(window,&w,&h);
        render_model_view(&visuals[0],view_angle,view_elev,w,h);
        SDL_GL_SwapWindow(window);
        if(capture&&!screenshot(capture,w,h))fprintf(stderr,"Screenshot failed: %s\n",SDL_GetError());
        printf("Rendered %s at angle %.1f elevation %.1f\n",model_file,view_angle,view_elev);
        destroy_visual(&visuals[0]);
        SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();return 0;
    }
    DemoPhysicsWorld world;DemoPhysicsAttrs attrs;DemoPhysicsFighter fighters[2];
    demo_physics_init_world(&world);demo_physics_default_attrs(&attrs);
    {
        char attr_error[128];
        if(demo_load_mario_attrs(disc,&attrs,attr_error,sizeof(attr_error))==DEMO_ASSET_OK) {
            printf("Mario attributes: accel %.3f friction %.3f run %.3f gravity %.3f terminal %.2f air %.3f jump %.2f jumps %d\n",
                   attrs.ground_accel,attrs.ground_friction,attrs.ground_max_speed,
                   attrs.gravity,attrs.terminal_velocity,attrs.air_accel,
                   attrs.jump_velocity,attrs.max_jumps);
        } else {
            fprintf(stderr,"Mario attributes unavailable (%s); using demo defaults\n",attr_error);
        }
    }
    for(int i=0;i<2;++i){demo_physics_reset(&fighters[i],&world);fighters[i].x=i?20:-20;fighters[i].facing=i?-1:1;}
    SDL_GameController *pad=NULL;
    for(int i=0;i<SDL_NumJoysticks();++i)if(SDL_IsGameController(i)){pad=SDL_GameControllerOpen(i);break;}
    int run=1,cpu=1,paused=0,rendered=0;
    unsigned tick=0;double previous=SDL_GetPerformanceCounter()/(double)SDL_GetPerformanceFrequency(),accumulator=0;
    DemoPhysicsInput pending[2]={{0}};int previous_pad_jump=0,previous_pad_attack=0;
    while(run) {
        SDL_Event e;
        while(SDL_PollEvent(&e)) {
            if(e.type==SDL_QUIT)run=0;
            if(e.type==SDL_KEYDOWN&&!e.key.repeat) {
                SDL_Keycode k=e.key.keysym.sym;
                if(k==SDLK_ESCAPE)run=0;
                if(k==SDLK_F2)cpu=!cpu;
                if(k==SDLK_p)paused=!paused;
                if(k==SDLK_r)for(int i=0;i<2;++i){demo_physics_reset(&fighters[i],&world);fighters[i].x=i?20:-20;}
                if(k==SDLK_w||k==SDLK_SPACE)pending[0].jump_pressed=1;
                if(k==SDLK_f)pending[0].attack_pressed=1;
                if(k==SDLK_UP)pending[1].jump_pressed=1;
                if(k==SDLK_k)pending[1].attack_pressed=1;
            }
        }
        const Uint8 *keys=SDL_GetKeyboardState(NULL);
        pending[0].axis=(float)(keys[SDL_SCANCODE_D]-keys[SDL_SCANCODE_A]);pending[0].shield=keys[SDL_SCANCODE_G];
        pending[1].axis=(float)(keys[SDL_SCANCODE_RIGHT]-keys[SDL_SCANCODE_LEFT]);pending[1].shield=keys[SDL_SCANCODE_L];
        if(pad&&SDL_GameControllerGetAttached(pad)) {
            float axis=SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTX)/32767.0f;
            if(fabsf(axis)>.2f)pending[0].axis=axis;
            int j=SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_A),a=SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_X);
            pending[0].jump_pressed|=j&&!previous_pad_jump;pending[0].attack_pressed|=a&&!previous_pad_attack;
            previous_pad_jump=j;previous_pad_attack=a;
            pending[0].shield|=SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
        }
        double now=SDL_GetPerformanceCounter()/(double)SDL_GetPerformanceFrequency(),dt=now-previous;previous=now;
        if(dt>.1)dt=.1;
        accumulator+=dt;
        if(scripted)accumulator=1.0/60;
        while(accumulator>=1.0/60) {
            accumulator-=1.0/60;
            if(!paused) {
                ++tick;
                if(scripted){pending[0].axis=(tick/100)%2?-.6f:.6f;pending[0].jump_pressed=tick%90==1;pending[0].attack_pressed=tick%25==1;}
                if(cpu) {
                    float dx=fighters[0].x-fighters[1].x;
                    pending[1].axis=fabsf(dx)>10?(dx>0?.55f:-.55f):0;
                    if(fabsf(fighters[1].x)>58)pending[1].axis=fighters[1].x>0?-1:1;
                    pending[1].jump_pressed=(fighters[0].y>fighters[1].y+12&&tick%45==0)||(!fighters[1].grounded&&fighters[1].y<-2&&tick%20==0);
                    pending[1].attack_pressed=fabsf(dx)<16&&tick%25==0;
                    pending[1].shield=tick%150>130&&fabsf(dx)<20;
                }
                for(int i=0;i<2;++i)demo_physics_step(&fighters[i],&attrs,&world,pending[i]);
                demo_physics_try_hit(&fighters[0],&fighters[1]);demo_physics_try_hit(&fighters[1],&fighters[0]);
            }
            for(int i=0;i<2;++i)pending[i].jump_pressed=pending[i].attack_pressed=0;
        }
        int w,h;SDL_GL_GetDrawableSize(window,&w,&h);
        if(h<1)h=1;
        glViewport(0,0,w,h);glClearColor(.025f,.035f,.065f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);glLoadIdentity();
        float halfx=55,halfy=halfx*h/w;
        float midx=(fighters[0].x+fighters[1].x)*.5f;
        float midy=(fighters[0].y+fighters[1].y)*.5f+9;
        if(midx<-30)midx=-30;
        if(midx>30)midx=30;
        if(midy<10)midy=10;
        if(midy>32)midy=32;
        glOrtho(midx-halfx,midx+halfx,midy-halfy,midy+halfy,-250,250);
        glMatrixMode(GL_MODELVIEW);glLoadIdentity();
        glDisable(GL_DEPTH_TEST);
        glBegin(GL_QUADS);
        glColor3f(.025f,.04f,.085f);glVertex3f(-150,-90,-80);glVertex3f(150,-90,-80);
        glColor3f(.07f,.12f,.20f);glVertex3f(150,100,-80);glVertex3f(-150,100,-80);glEnd();
        glColor4f(.25f,.5f,.6f,.12f);glLineWidth(1);glBegin(GL_LINES);
        for(int x=-150;x<=150;x+=10){glVertex3f(x,-90,-70);glVertex3f(x,100,-70);}
        for(int y=-90;y<=100;y+=10){glVertex3f(-150,y,-70);glVertex3f(150,y,-70);}glEnd();
        glEnable(GL_DEPTH_TEST);glRotatef(7,1,0,0);
        GLfloat light[]={-40,80,100,0};glLightfv(GL_LIGHT0,GL_POSITION,light);
        for(int i=0;i<world.platform_count;++i)platform(&world.platforms[i],i==0);
        draw_fighter(&visuals[0],&fighters[0],0,tick);draw_fighter(&visuals[1],&fighters[1],1,tick);
        hud(w,h,fighters,cpu,paused,visuals);
        ++rendered;
        if(frames&&rendered>=frames) {
            if(capture&&!screenshot(capture,w,h))fprintf(stderr,"Screenshot failed: %s\n",SDL_GetError());
            printf("Completed %d render frames, %u simulation ticks\n",rendered,tick);run=0;
        }
        SDL_GL_SwapWindow(window);
        SDL_Delay(1);
    }
    if(pad)SDL_GameControllerClose(pad);
    destroy_visual(&visuals[0]);
    SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();return 0;
}
