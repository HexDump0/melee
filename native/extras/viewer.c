#include "extras/viewer.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gx/gl.h"
#include "gx/math.h"
#include "gx/overlay.h"
#include "gx/render.h"
#include "hsd/anim.h"
#include "hsd/parts.h"
#include "platform/screenshot.h"

static int g_vis_slot=0;
static int g_vis_variant=0;

/* Interactive model viewer: orbit, zoom, wireframe and model switching. */
typedef struct Viewer {
    float yaw,pitch,distance,radius,target[3];
    int wireframe,textures,lighting,culling,grid,spin,help;
    int show_hidden;
    int batch,mode; /* mode: 0 = all, 1 = only selected, 2 = hide selected */
    int vis_slot;
    int animate;       /* pose and play the loaded animation */
    int playing;       /* playback running */
    float anim_time;   /* current clip frame */
    float speed;       /* clip frames per 60 Hz tick */
    int clip;          /* active clip index */
} Viewer;

static float anim_wrap(const Visual *v,float t)
{
    float frames=v->anim_loaded?anim_end_frame(&v->anim):0.0f;
    if(frames<=0.0f)return 0.0f;
    while(t>=frames)t-=frames;
    while(t<0.0f)t+=frames;
    return t;
}

static int anim_choose_clip(Visual *v,Viewer *vs,const char *wanted)
{
    int index=anim_select_clip(&v->anim,&v->model,wanted);
    if(index<0)return 0;
    vs->clip=index;
    vs->anim_time=0.0f;
    return 1;
}

static void viewer_frame_bounds(Viewer *vs,const float mn[3],const float mx[3])
{
    float size[3];
    float radius;
    int i;
    for(i=0;i<3;++i) {
        size[i]=mx[i]-mn[i];
        vs->target[i]=(mn[i]+mx[i])*.5f;
    }
    radius=.5f*sqrtf(size[0]*size[0]+size[1]*size[1]+size[2]*size[2]);
    if(radius<1e-3f)radius=1.0f;
    vs->radius=radius;
    vs->distance=radius/tanf(0.35f)*1.15f;
}

static void viewer_frame_model(Viewer *vs,const Visual *v)
{
    viewer_frame_bounds(vs,v->model.bounds_min,v->model.bounds_max);
}

/* Frames the camera on one batch so isolated parts fill the view. */
static void viewer_frame_batch(Viewer *vs,const Visual *v,size_t batch)
{
    float mn[3]={1e30f,1e30f,1e30f},mx[3]={-1e30f,-1e30f,-1e30f};
    size_t i,k;
    const HsdBatch *b;
    if(batch>=v->model.batch_count)return;
    b=&v->model.batches[batch];
    for(i=0;i<b->vertex_count;++i) {
        const float *p=v->model.vertices[b->first_vertex+i].position;
        for(k=0;k<3;++k) {
            if(p[k]<mn[k])mn[k]=p[k];
            if(p[k]>mx[k])mx[k]=p[k];
        }
    }
    if(mn[0]>mx[0])return;
    viewer_frame_bounds(vs,mn,mx);
}

/* Fixed world-space floor grid at y = 0 with 1.5-unit cells: it never
 * depends on the model, the pose or the animation frame. */
static void viewer_grid(const Mat4 mvp)
{
    const float y=0.0f;
    const float extent=15.0f;
    const float step=1.5f;
    float x;
    int i;
    ov_set_mvp(mvp);
    ov_color4f(.30f,.38f,.50f,.55f);
    ov_begin(GL_LINES);
    for(i=-10;i<=10;++i) {
        x=i*step;
        ov_vertex3f(x,y,-extent);ov_vertex3f(x,y,extent);
        ov_vertex3f(-extent,y,x);ov_vertex3f(extent,y,x);
    }
    ov_end();
}

static void viewer_hud(int w,int h,const Visual *v,const Viewer *vs,
                       const char *name,int index,int total)
{
    char line[192];
    float s=fmaxf(1.0f,w/1280.0f);
    Mat4 proj;
    glDisable(GL_DEPTH_TEST);
    m4_ortho(proj,0,w,h,0,-1,1);
    ov_set_mvp(proj);
    ov_color4f(.03f,.045f,.075f,.9f);rect(0,0,w,112*s);
    ov_color3f(.92f,.95f,1);draw_text(24*s,16*s,3*s,"MODEL VIEWER");
    ov_color3f(.39f,.8f,.77f);
    draw_text(24*s,44*s,1.5f*s,vs->animate&&v->anim_loaded
              ?"REAL DISC ASSETS / HSD ANIMATION / CUSTOM SANDBOX"
              :"REAL DISC ASSETS / BIND POSE / CUSTOM SANDBOX");
    ov_color3f(.85f,.9f,1);
    if(total>0)snprintf(line,sizeof(line),"%s  [%d/%d]",name,index+1,total);
    else snprintf(line,sizeof(line),"%s",name);
    draw_text(w*.5f-140*s,18*s,2*s,line);
    snprintf(line,sizeof(line),"%zu TRIS  %zu TEXTURES  %zu PARTS",
             v->model.triangle_count,v->model.texture_count,
             v->model.batch_count);
    ov_color3f(.62f,.7f,.82f);
    draw_text(w*.5f-140*s,44*s,1.3f*s,line);
    if(v->model.batch_count>0) {
        int b=vs->batch;
        static const char *mode_names[3]={"ALL PARTS","ONLY PART","HIDE PART"};
        if(b<0)b=0;
        if((size_t)b>=v->model.batch_count)b=(int)v->model.batch_count-1;
        snprintf(line,sizeof(line),"%s  %d/%zu  %zu VERTS",mode_names[vs->mode],
                 b+1,v->model.batch_count,v->model.batches[b].vertex_count);
        ov_color3f(.95f,.8f,.5f);
        draw_text(w*.5f-140*s,66*s,1.3f*s,line);
    }
    if(vs->animate&&v->anim_loaded) {
        float frames=anim_end_frame(&v->anim);
        snprintf(line,sizeof(line),"CLIP %s  %.0f/%.0f  %.2gx  %s",
                 anim_clip_name(&v->anim,(size_t)vs->clip),
                 vs->anim_time,frames,(double)vs->speed,
                 vs->playing?"PLAY":"PAUSE");
        ov_color3f(.55f,.9f,.65f);
        draw_text(w*.5f-140*s,88*s,1.3f*s,line);
    }
    ov_color3f(.68f,.73f,.84f);
    draw_text(24*s,h-56*s,1.4f*s,"DRAG: ORBIT   WHEEL: ZOOM   N/P: MODEL   A: ANIM   ,/.: FRAME   Z/X: CLIP   M: SPEED   SPACE: SPIN   R: RESET");
    draw_text(24*s,h-34*s,1.4f*s,"T:TEX L:LIGHT W:WIRE C:CULL G:GRID V:MODE B:SLOT [ ]:PART Y:HIDDEN F12:SAVE H:HELP ESC:QUIT");
    /* State indicators live in the top panel so the footer stays readable. */
    ov_color3f(.39f,.8f,.77f);
    draw_text(w-118*s,16*s,1.4f*s,vs->textures?"TEX ON":"TEX OFF");
    draw_text(w-238*s,16*s,1.4f*s,vs->lighting?"LIGHT ON":"LIGHT OFF");
    draw_text(w-358*s,16*s,1.4f*s,vs->wireframe?"WIRE":"SOLID");
    draw_text(w-128*s,44*s,1.4f*s,vs->culling?"CULL ON":"CULL OFF");
    draw_text(w-238*s,44*s,1.4f*s,vs->grid?"GRID ON":"GRID OFF");
    draw_text(w-378*s,44*s,1.4f*s,vs->show_hidden?"HIDDEN ON":"HIDDEN OFF");
    snprintf(line,sizeof(line),"SLOT %d",vs->vis_slot);
    ov_color3f(.95f,.8f,.5f);
    draw_text(w-478*s,44*s,1.4f*s,line);
    if(vs->help) {
        ov_color4f(.02f,.03f,.05f,.85f);rect(w*.5f-300*s,h*.5f-120*s,600*s,240*s);
        ov_color3f(1,1,1);draw_text(w*.5f-250*s,h*.5f-90*s,2.4f*s,"VIEWER CONTROLS");
        ov_color3f(.8f,.85f,.92f);
        draw_text(w*.5f-250*s,h*.5f-50*s,1.5f*s,"LEFT DRAG OR ARROWS: ORBIT THE MODEL");
        draw_text(w*.5f-250*s,h*.5f-20*s,1.5f*s,"MOUSE WHEEL OR +/-: ZOOM IN AND OUT");
        draw_text(w*.5f-250*s,h*.5f+10*s,1.5f*s,"N / P: NEXT OR PREVIOUS CHARACTER MODEL");
        draw_text(w*.5f-250*s,h*.5f+40*s,1.5f*s,"A: PLAY OR PAUSE   ,/.: STEP FRAME");
        draw_text(w*.5f-250*s,h*.5f+70*s,1.5f*s,"Z / X: CHANGE CLIP   M: PLAYBACK SPEED");
        draw_text(w*.5f-250*s,h*.5f+100*s,1.5f*s,"H: CLOSE THIS HELP");
    }
}

static void render_viewer(const Visual *v,const Viewer *vs,int w,int h,
                          const char *name,int index,int total)
{
    float aspect=(float)w/(float)h;
    float fovy=.7f;
    float zfar=vs->distance*8.0f+100.0f;
    float znear=zfar/1200.0f;
    float top;
    float cp,sp;
    float eye[3];
    if(znear<.05f)znear=.05f;
    top=znear*tanf(fovy*.5f);
    cp=cosf(vs->pitch);sp=sinf(vs->pitch);
    eye[0]=vs->target[0]+vs->distance*cp*sinf(vs->yaw);
    eye[1]=vs->target[1]+vs->distance*sp;
    eye[2]=vs->target[2]+vs->distance*cp*cosf(vs->yaw);
    glViewport(0,0,w,h);
    glClearColor(.05f,.06f,.09f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    {
        Mat4 proj,view,mvp;
        m4_frustum(proj,-top*aspect,top*aspect,-top,top,znear,zfar);
        m4_look_at(view,eye,vs->target);
        m4_mul(mvp,proj,view);
        render_set_view(mvp,view,view,vs->lighting);
        glEnable(GL_DEPTH_TEST);
        glPolygonMode(GL_FRONT_AND_BACK,vs->wireframe?GL_LINE:GL_FILL);
        {
            size_t bi;
            int animated=vs->animate&&v->anim_loaded;
            for(bi=0;bi<v->model.batch_count&&bi<HSD_MAX_BATCHES;++bi) {
                const HsdBatch *b=&v->model.batches[bi];
                int allowed=b->cull_mode!=3&&
                            (vs->show_hidden||
                             (animated?hsd_model_batch_pose_visible(&v->model,bi)
                                      :hsd_model_batch_visible(&v->model,bi)));
                int visible=allowed&&(vs->mode==0||(vs->mode==1&&(int)bi==vs->batch)||
                            (vs->mode==2&&(int)bi!=vs->batch));
                if(visible&&(animated||v->batch_vao[bi])) {
                    render_apply_cull(vs->culling?(int)b->cull_mode:0);
                    render_batch(v,bi,animated,vs->textures);
                }
            }
        }
        render_reset_state();
        glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        render_apply_cull(0);
        if(vs->grid)viewer_grid(mvp);
    }
    glDisable(GL_DEPTH_TEST);
    viewer_hud(w,h,v,vs,name,index,total);
    glEnable(GL_DEPTH_TEST);
}

static int viewer_open_model(Visual *v,const char *disc,const char *file)
{
    visual_destroy(v);
    memset(v,0,sizeof(*v));
    if(!visual_load_model(v,disc,file,g_vis_slot,g_vis_variant))return 0;
    visual_compile(v);
    return 1;
}

static int viewer_cycle(Visual *v,const char *disc,DiscFileList *models,int *index,int dir)
{
    int attempts=(int)models->count;
    int i=*index;
    while(attempts-->0) {
        i+=dir;
        if(i<0)i=(int)models->count-1;
        if(i>=(int)models->count)i=0;
        if(viewer_open_model(v,disc,models->names[i])){*index=i;return 1;}
        fprintf(stderr,"Skipping %s (could not decode)\n",models->names[i]);
    }
    return 0;
}

int viewer_run(SDL_Window *window,const char *disc,const char *model_file,
               DiscFileList *models,Visual *visuals,const ViewerOptions *opts)
{
    g_vis_slot=opts->vis_slot;
    g_vis_variant=opts->vis_variant;

        Viewer vs;
        int running=1,index=-1,next_model=0,prev_model=0,rendered=0,want_shot=0;
        int w=0,h=1;
        size_t mi;
        int dragging=0,lastx=0,lasty=0;
        double previous=SDL_GetPerformanceCounter()/(double)SDL_GetPerformanceFrequency();
        double anim_accumulator=0.0;
        memset(&vs,0,sizeof(vs));
        vs.yaw=opts->angle*PI/180.0f;
        vs.pitch=-opts->elevation*PI/180.0f;
        vs.textures=1;vs.lighting=1;vs.culling=0;vs.grid=opts->no_grid?0:1;vs.help=0;
        vs.show_hidden=opts->show_hidden;
        vs.vis_slot=g_vis_slot;
        vs.speed=opts->anim_speed;
        vs.animate=0;vs.playing=0;vs.anim_time=0.0f;vs.clip=0;
        if(opts->animate||opts->anim_frame>=0.0f||opts->anim_file!=NULL) {
            if(visual_load_anim(&visuals[0],disc,model_file,opts->anim_file)) {
                vs.animate=anim_choose_clip(&visuals[0],&vs,opts->clip);
                vs.playing=(opts->animate&&vs.animate&&opts->anim_frame<0.0f)?1:0;
                if(vs.animate&&opts->anim_frame>=0.0f)
                    vs.anim_time=anim_wrap(&visuals[0],opts->anim_frame);
            }
        }
        if(opts->force_no_cull)vs.culling=0;
        vs.batch=opts->part>=0?opts->part:0;vs.mode=opts->part_mode;
        for(mi=0;mi<models->count;++mi)
            if(!strcmp(models->names[mi],model_file)){index=(int)mi;break;}
        if(opts->part>=0&&opts->part_mode==1)viewer_frame_batch(&vs,&visuals[0],(size_t)opts->part);
        else viewer_frame_model(&vs,&visuals[0]);
        vs.distance*=opts->zoom>0.01f?opts->zoom:1.0f;
        while(running) {
            SDL_Event e;
            float dt;
            while(SDL_PollEvent(&e)) {
                if(e.type==SDL_QUIT)running=0;
                else if(e.type==SDL_MOUSEBUTTONDOWN&&e.button.button==SDL_BUTTON_LEFT){dragging=1;lastx=e.button.x;lasty=e.button.y;}
                else if(e.type==SDL_MOUSEBUTTONUP&&e.button.button==SDL_BUTTON_LEFT)dragging=0;
                else if(e.type==SDL_MOUSEMOTION&&dragging){
                    vs.yaw-=(e.motion.x-lastx)*0.01f;
                    vs.pitch+=(e.motion.y-lasty)*0.01f;
                    lastx=e.motion.x;lasty=e.motion.y;
                } else if(e.type==SDL_MOUSEWHEEL) {
                    vs.distance*=(1.0f-(float)e.wheel.y*0.1f);
                } else if(e.type==SDL_KEYDOWN&&!e.key.repeat) {
                    switch(e.key.keysym.sym) {
                    case SDLK_ESCAPE:running=0;break;
                    case SDLK_n:next_model=1;break;
                    case SDLK_p:prev_model=1;break;
                    case SDLK_r:vs.yaw=opts->angle*PI/180.0f;vs.pitch=-opts->elevation*PI/180.0f;viewer_frame_model(&vs,&visuals[0]);break;
                    case SDLK_t:vs.textures=!vs.textures;break;
                    case SDLK_l:vs.lighting=!vs.lighting;break;
                    case SDLK_w:vs.wireframe=!vs.wireframe;break;
                    case SDLK_c:vs.culling=!vs.culling;break;
                    case SDLK_g:vs.grid=!vs.grid;break;
                    case SDLK_h:vs.help=!vs.help;break;
                    case SDLK_SPACE:vs.spin=!vs.spin;break;
                    case SDLK_b:
                        vs.vis_slot=(vs.vis_slot+1)%4;
                        {
                            char part_err[128];
                            parts_apply(disc,
                                    index>=0&&(size_t)index<models->count?models->names[index]:model_file,
                                    &visuals[0].model,vs.vis_slot,0,part_err,
                                    sizeof(part_err));
                            printf("Visibility slot %d\n",vs.vis_slot);
                        }
                        break;
                    case SDLK_v:vs.mode=(vs.mode+1)%3;break;
                    case SDLK_y:vs.show_hidden=!vs.show_hidden;break;
                    case SDLK_a:
                        vs.animate=!vs.animate;
                        if(vs.animate) {
                            if(!visuals[0].anim_loaded) {
                                const char *file=index>=0&&(size_t)index<models->count?models->names[index]:model_file;
                                if(visual_load_anim(&visuals[0],disc,file,opts->anim_file))
                                    anim_choose_clip(&visuals[0],&vs,opts->clip);
                            }
                            vs.playing=visuals[0].anim_loaded?1:0;
                        } else {
                            hsd_model_pose_reset(&visuals[0].model);
                            hsd_model_pose_apply(&visuals[0].model);
                        }
                        break;
                    case SDLK_COMMA:
                        if(visuals[0].anim_loaded) {
                            vs.playing=0;
                            vs.animate=1;
                            vs.anim_time=anim_wrap(&visuals[0],vs.anim_time-1.0f);
                        }
                        break;
                    case SDLK_PERIOD:
                        if(visuals[0].anim_loaded) {
                            vs.playing=0;
                            vs.animate=1;
                            vs.anim_time=anim_wrap(&visuals[0],vs.anim_time+1.0f);
                        }
                        break;
                    case SDLK_z:
                        if(visuals[0].anim_loaded&&visuals[0].anim.clip_count>0) {
                            int c=(vs.clip-1+(int)visuals[0].anim.clip_count)%
                                  (int)visuals[0].anim.clip_count;
                            if(anim_set_clip(&visuals[0].anim,(size_t)c,&visuals[0].model,NULL,0)==0) {
                                vs.clip=c;vs.anim_time=0;vs.animate=1;
                                printf("Clip %s (%.0f frames)\n",anim_clip_name(&visuals[0].anim,(size_t)c),anim_end_frame(&visuals[0].anim));
                            }
                        }
                        break;
                    case SDLK_x:
                        if(visuals[0].anim_loaded&&visuals[0].anim.clip_count>0) {
                            int c=(vs.clip+1)%(int)visuals[0].anim.clip_count;
                            if(anim_set_clip(&visuals[0].anim,(size_t)c,&visuals[0].model,NULL,0)==0) {
                                vs.clip=c;vs.anim_time=0;vs.animate=1;
                                printf("Clip %s (%.0f frames)\n",anim_clip_name(&visuals[0].anim,(size_t)c),anim_end_frame(&visuals[0].anim));
                            }
                        }
                        break;
                    case SDLK_m:vs.speed=vs.speed>=2.0f?0.25f:vs.speed*2.0f;break;
                    case SDLK_LEFTBRACKET:vs.batch--;break;
                    case SDLK_RIGHTBRACKET:vs.batch++;break;
                    case SDLK_F12:want_shot=1;break;
                    default:break;
                    }
                }
            }
            {
                const Uint8 *keys=SDL_GetKeyboardState(NULL);
                double now=SDL_GetPerformanceCounter()/(double)SDL_GetPerformanceFrequency();
                dt=(float)(now-previous);previous=now;
                if(dt>0.1f)dt=0.1f;
                vs.yaw+=(keys[SDL_SCANCODE_LEFT]-keys[SDL_SCANCODE_RIGHT])*1.5f*dt;
                vs.pitch+=(keys[SDL_SCANCODE_UP]-keys[SDL_SCANCODE_DOWN])*1.0f*dt;
                if(keys[SDL_SCANCODE_EQUALS]||keys[SDL_SCANCODE_KP_PLUS])vs.distance*=1.0f-1.5f*dt;
                if(keys[SDL_SCANCODE_MINUS]||keys[SDL_SCANCODE_KP_MINUS])vs.distance*=1.0f+1.5f*dt;
                if(vs.spin)vs.yaw+=0.6f*dt;
            }
            if(vs.animate&&visuals[0].anim_loaded) {
                /* HSD_AObjInterpretAnim runs on the game's fixed 60 Hz tick;
                 * accumulate wall time so refresh rate cannot change the
                 * playback speed. */
                if(vs.playing) {
                    anim_accumulator+=dt;
                    while(anim_accumulator>=1.0/60.0) {
                        anim_accumulator-=1.0/60.0;
                        vs.anim_time=anim_wrap(&visuals[0],vs.anim_time+vs.speed);
                    }
                } else {
                    anim_accumulator=0.0;
                }
                anim_apply(&visuals[0].anim,&visuals[0].model,vs.anim_time);
            }
            if(vs.pitch>1.5f)vs.pitch=1.5f;
            if(vs.pitch<-1.5f)vs.pitch=-1.5f;
            if(vs.distance<vs.radius*0.2f)vs.distance=vs.radius*0.2f;
            if(vs.distance>vs.radius*12.0f)vs.distance=vs.radius*12.0f;
            if(vs.batch<0)vs.batch=0;
            if((size_t)vs.batch>=visuals[0].model.batch_count&&visuals[0].model.batch_count>0)
                vs.batch=(int)visuals[0].model.batch_count-1;
            if(next_model||prev_model) {
                char want[CLIP_NAME_MAX];
                want[0]=0;
                if(visuals[0].anim_loaded)
                    snprintf(want,sizeof(want),"%s",
                             anim_clip_name(&visuals[0].anim,(size_t)vs.clip));
                if(models->count>0&&viewer_cycle(&visuals[0],disc,models,&index,next_model?1:-1)) {
                    if(opts->part>=0&&opts->part_mode==1)viewer_frame_batch(&vs,&visuals[0],(size_t)opts->part);
                    else viewer_frame_model(&vs,&visuals[0]);
                    vs.batch=0;
                    vs.anim_time=0.0f;
                    if(vs.animate) {
                        if(visual_load_anim(&visuals[0],disc,models->names[index],opts->anim_file))
                            anim_choose_clip(&visuals[0],&vs,want[0]?want:opts->clip);
                    }
                    printf("Viewing %s\n",models->names[index]);
                } else {
                    fprintf(stderr,"No decodable model in list\n");
                }
                next_model=prev_model=0;
            }
            {
                SDL_GL_GetDrawableSize(window,&w,&h);
                if(h<1)h=1;
                render_viewer(&visuals[0],&vs,w,h,
                              index>=0&&(size_t)index<models->count?models->names[index]:model_file,
                              index,(int)models->count);
                SDL_GL_SwapWindow(window);
                if(want_shot) {
                    if(platform_screenshot(opts->capture?opts->capture:"viewer.bmp",w,h))printf("Saved screenshot\n");
                    else fprintf(stderr,"Screenshot failed: %s\n",SDL_GetError());
                    want_shot=0;
                }
            }
            ++rendered;
            if(opts->frames&&rendered>=opts->frames) {
                if(opts->capture&&!platform_screenshot(opts->capture,w,h))fprintf(stderr,"Screenshot failed: %s\n",SDL_GetError());
                printf("Rendered %d viewer frames of %s\n",rendered,model_file);
                running=0;
            }
            SDL_Delay(1);
        }
    return 0;
}
