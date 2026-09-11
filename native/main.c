#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gx/gl.h"
#include "gx/math.h"
#include "gx/overlay.h"
#include "gx/render.h"
#include "platform/disc.h"
#include "platform/screenshot.h"
#include "hsd/anim.h"
#include "hsd/light.h"
#include "hsd/parts.h"
#include "game/attributes.h"
#include "extras/physics.h"

#define DEFAULT_DISC "iso/Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).ciso"

static int g_vis_slot = 0;
static int g_vis_variant = 0;


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

static int anim_select_clip(Visual *v,const char *wanted)
{
    int index;
    if(!v->anim_loaded)return -1;
    index=anim_clip_find(&v->anim,wanted);
    if(index<0)index=anim_clip_find(&v->anim,"Wait1");
    if(index<0&&v->anim.clip_count>0)index=0;
    if(index<0)return -1;
    if(anim_set_clip(&v->anim,(size_t)index,&v->model,NULL,0)!=0)return -1;
    return index;
}

static int anim_choose_clip(Visual *v,Viewer *vs,const char *wanted)
{
    int index=anim_select_clip(v,wanted);
    if(index<0)return 0;
    vs->clip=index;
    vs->anim_time=0.0f;
    return 1;
}

/* Match-mode animation state: one clip and cursor per fighter. */
typedef struct FighterAnim {
    int clip;
    float frame;
} FighterAnim;

/*
 * Picks the action clip for the sandbox fighter's current movement state.
 * The names are real `Pl<Char>AJ.dat` clips; the state machine that chooses
 * them is still the sandbox's until the real fighter states are ported.
 */
static const char *fighter_clip_name(const SandboxFighter *f,
                                     const FighterAttrs *attrs)
{
    if(!f->grounded) {
        if(f->vy>0.0f)return "JumpF";
        return "Fall";
    }
    if(fabsf(f->vx)<0.01f)return "Wait1";
    if(fabsf(f->vx)<attrs->ground_max_speed*0.55f)return "WalkMiddle";
    return "Dash";
}

static void fighter_anim_step(Visual *v,FighterAnim *fa,
                              const SandboxFighter *f,
                              const FighterAttrs *attrs)
{
    const char *want;
    int index;
    float end;
    if(!v->anim_loaded||fa->clip<0)return;
    want=fighter_clip_name(f,attrs);
    index=anim_clip_find(&v->anim,want);
    if(index<0)index=fa->clip;
    if(index!=fa->clip&&
       anim_set_clip(&v->anim,(size_t)index,&v->model,NULL,0)==0) {
        fa->clip=index;
        fa->frame=0.0f;
    }
    fa->frame+=1.0f;
    end=anim_end_frame(&v->anim);
    if(end>0.0f) {
        while(fa->frame>=end)fa->frame-=end;
    }
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

static void platform(const SandboxPlatform *p,int main_stage,const Mat4 mvp)
{
    float l=p->left,r=p->right,y=p->top,d=main_stage?5:2,t=main_stage?3:1;
    ov_set_mvp(mvp);
    ov_begin(GL_QUADS);
    ov_color3f(.18f,.25f,.37f);
    ov_vertex3f(l,y,-d); ov_vertex3f(r,y,-d); ov_vertex3f(r,y,d); ov_vertex3f(l,y,d);
    ov_color3f(.08f,.12f,.20f);
    ov_vertex3f(l,y,d); ov_vertex3f(r,y,d); ov_vertex3f(r,y-t,d); ov_vertex3f(l,y-t,d);
    ov_end();
    ov_color3f(.23f,.85f,.79f); glLineWidth(2);
    ov_begin(GL_LINES); ov_vertex3f(l,y+.08f,d+.05f); ov_vertex3f(r,y+.08f,d+.05f); ov_end();
    if(main_stage) {
        ov_color3f(.07f,.09f,.15f);
        ov_begin(GL_TRIANGLES);
        ov_vertex3f(l,y-t,d);ov_vertex3f(r,y-t,d);ov_vertex3f(0,y-18,0);
        ov_end();
    }
}

static void draw_fighter(const Visual *v,const SandboxFighter *f,
                         int player,unsigned tick,const FighterAnim *anim,
                         const Mat4 proj,const Mat4 base)
{
    float r=player?.35f:1, g=player?.66f:.35f, b=player?1:.40f;
    Mat4 mv,mvp;
    int animated=v->anim_loaded&&anim->clip>=0;
    render_reset_state();
    ov_color4f(r,g,b,.28f);
    memcpy(mv,base,sizeof(Mat4));
    m4_mul_translate(mv,f->x,f->y+.12f,0);
    m4_mul_rotate(mv,-90,1,0,0);
    m4_mul(mvp,proj,mv);
    ov_set_mvp(mvp);
    circle(0,0,0,4.5f,1);
    memcpy(mv,base,sizeof(Mat4));
    m4_mul_translate(mv,f->x,f->y,0);
    m4_mul_rotate(mv,-f->vx*2,0,0,1);
    /* Costume models are shown in bind pose during this bring-up. */
    m4_mul_rotate(mv,f->facing>0?65:-65,0,1,0);
    m4_mul_scale(mv,v->scale,v->scale,v->scale);
    m4_mul_translate(mv,-(v->model.bounds_min[0]+v->model.bounds_max[0])*.5f,
                     -v->model.bounds_min[1],
                     -(v->model.bounds_min[2]+v->model.bounds_max[2])*.5f);
    m4_mul(mvp,proj,mv);
    render_set_view(mvp,mv,base,1);
    {
        size_t bi;
        for(bi=0;bi<v->model.batch_count&&bi<HSD_MAX_BATCHES;++bi) {
            const HsdBatch *b=&v->model.batches[bi];
            if(b->cull_mode==3)continue;
            if(animated?(!hsd_model_batch_pose_visible(&v->model,bi)):
                        (!v->batch_vao[bi]||!hsd_model_batch_visible(&v->model,bi)))
                continue;
            render_apply_cull((int)b->cull_mode);
            render_batch(v,bi,animated,1);
        }
        render_apply_cull(0);
    }
    render_reset_state();
    if(f->shield) {
        m4_mul(mvp,proj,base);
        ov_set_mvp(mvp);
        ov_color4f(r,g,b,.17f);circle(f->x,f->y+5.5f,7,8,1);
        glLineWidth(2);
        ov_color4f(r,g,b,.8f);circle(f->x,f->y+5.5f,7.1f,8,0);
    }
    if(f->attack_timer>8) {
        float a=(18-f->attack_timer)*.18f;
        int i;
        m4_mul(mvp,proj,base);
        ov_set_mvp(mvp);
        ov_color4f(1,.85f,.4f,.8f);
        glLineWidth(4);
        ov_begin(GL_LINE_STRIP);
        for(i=0;i<20;++i) {
            float angle=-1.0f+i*.1f+a;
            ov_vertex3f(f->x+f->facing*(3+cosf(angle)*10),f->y+5+sinf(angle)*6,8);
        }
        ov_end();
    }
    {
        float top=f->y+15+.3f*sinf(tick*.08f);
        m4_mul(mvp,proj,base);
        ov_set_mvp(mvp);
        ov_color3f(r,g,b);
        ov_begin(GL_TRIANGLES);
        ov_vertex3f(f->x-1.4f,top+2,0);ov_vertex3f(f->x+1.4f,top+2,0);ov_vertex3f(f->x,top,0);
        ov_end();
    }
}

static void hud(int w,int h,const SandboxFighter f[2], int cpu,int paused,
                const Visual v[2])
{
    float s=fmaxf(1.0f,w/1280.0f);
    Mat4 proj;
    glDisable(GL_DEPTH_TEST);
    m4_ortho(proj,0,w,h,0,-1,1);
    ov_set_mvp(proj);
    ov_color4f(.025f,.035f,.065f,.92f);rect(0,0,w,85*s);
    ov_color3f(.92f,.95f,1);draw_text(28*s,22*s,3*s,"MELEE / NATIVE LAB");
    ov_color3f(.39f,.8f,.77f);draw_text(29*s,54*s,1.5f*s,"REAL DISC MODELS / CUSTOM SANDBOX / 60 HZ");
    ov_color3f(.65f,.71f,.8f);draw_text(w-324*s,27*s,1.5f*s,cpu?"F2: CPU ON / P: PAUSE":"F2: TWO PLAYERS / P: PAUSE");
    draw_text(w-324*s,49*s,1.5f*s,"R: RESET / ESC: QUIT");
    for(int i=0;i<2;++i) {
        float x=w*.5f+(i?95:-355)*s, y=h-153*s;
        ov_color4f(.035f,.045f,.075f,.95f);rect(x,y,260*s,98*s);
        ov_color3f(i?.35f:1,i?.66f:.35f,i?1:.4f);rect(x,y,5*s,98*s);
        draw_text(x+20*s,y+15*s,2*s,v[i].label);
        char line[64];snprintf(line,sizeof(line),"%.0f%%",f[i].damage);
        ov_color3f(1,.95f,.85f);draw_text(x+20*s,y+40*s,4*s,line);
        snprintf(line,sizeof(line),"STOCK %d",f[i].stocks);
        ov_color3f(.65f,.71f,.8f);draw_text(x+155*s,y+65*s,1.5f*s,line);
    }
    ov_color3f(.68f,.73f,.84f);
    draw_text(25*s,h-33*s,1.3f*s,"P1: A/D MOVE  W/SPACE JUMP  F ATTACK  G SHIELD");
    draw_text(w-568*s,h-33*s,1.3f*s,"P2: ARROWS MOVE/JUMP  K ATTACK  L SHIELD");
    if(paused) {
        ov_color4f(.02f,.025f,.04f,.8f);rect(w*.5f-135*s,h*.5f-35*s,270*s,70*s);
        ov_color3f(1,1,1);draw_text(w*.5f-90*s,h*.5f-13*s,4*s,"PAUSED");
    }
}

int main(int argc,char **argv)
{
    const char *disc=DEFAULT_DISC,*capture=NULL,*model_file="PlMrNr.dat";
    int frames=0,inspect=0,scripted=0,view=0;
    int list_models=0,all_models=0,model_index=-1,view_part=-1,view_part_mode=0,list_parts=0,show_hidden=0,no_visibility=0;
    const char *dump_textures=NULL;
    const char *extract_file=NULL,*extract_out=NULL;
    const char *clip_name="Wait1",*anim_file=NULL,*dump_clip=NULL;
    int animate=0,list_clips=0,force_no_cull=0,dump_tev=0,dump_lights=0,no_grid=0,dump_joints=0;
    float view_angle=25.0f,view_elev=-12.0f,view_zoom=1.0f;
    float anim_frame=-1.0f,anim_speed=1.0f;
    for(int i=1;i<argc;++i) {
        if(!strcmp(argv[i],"--disc")&&i+1<argc)disc=argv[++i];
        else if(!strcmp(argv[i],"--model")&&i+1<argc)model_file=argv[++i];
        else if(!strcmp(argv[i],"--frames")&&i+1<argc)frames=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--screenshot")&&i+1<argc)capture=argv[++i];
        else if(!strcmp(argv[i],"--inspect"))inspect=1;
        else if(!strcmp(argv[i],"--scripted"))scripted=1;
        else if(!strcmp(argv[i],"--view"))view=1;
        else if(!strcmp(argv[i],"--list-models"))list_models=1;
        else if(!strcmp(argv[i],"--all-models"))all_models=1;
        else if(!strcmp(argv[i],"--model-index")&&i+1<argc)model_index=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--part")&&i+1<argc)view_part=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--list-parts"))list_parts=1;
        else if(!strcmp(argv[i],"--show-hidden"))show_hidden=1;
        else if(!strcmp(argv[i],"--no-visibility"))no_visibility=1;
        else if(!strcmp(argv[i],"--dump-textures")&&i+1<argc)dump_textures=argv[++i];
        else if(!strcmp(argv[i],"--no-cull"))force_no_cull=1;
        else if(!strcmp(argv[i],"--dump-tev"))dump_tev=1;
        else if(!strcmp(argv[i],"--dump-lights"))dump_lights=1;
        else if(!strcmp(argv[i],"--no-grid"))no_grid=1;
        else if(!strcmp(argv[i],"--dump-joints"))dump_joints=1;
        else if(!strcmp(argv[i],"--animate"))animate=1;
        else if(!strcmp(argv[i],"--clip")&&i+1<argc)clip_name=argv[++i];
        else if(!strcmp(argv[i],"--anim-frame")&&i+1<argc)anim_frame=(float)atof(argv[++i]);
        else if(!strcmp(argv[i],"--anim-speed")&&i+1<argc)anim_speed=(float)atof(argv[++i]);
        else if(!strcmp(argv[i],"--anim-file")&&i+1<argc)anim_file=argv[++i];
        else if(!strcmp(argv[i],"--list-clips"))list_clips=1;
        else if(!strcmp(argv[i],"--dump-clip")&&i+1<argc)dump_clip=argv[++i];
        else if(!strcmp(argv[i],"--extract")&&i+2<argc){extract_file=argv[++i];extract_out=argv[++i];}
        else if(!strcmp(argv[i],"--vis-slot")&&i+1<argc)g_vis_slot=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--vis-variant")&&i+1<argc)g_vis_variant=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--part-mode")&&i+1<argc) {
            const char *m=argv[++i];
            view_part_mode=!strcmp(m,"only")?1:(!strcmp(m,"hide")?2:0);
        }
        else if(!strcmp(argv[i],"--angle")&&i+1<argc)view_angle=(float)atof(argv[++i]);
        else if(!strcmp(argv[i],"--elevation")&&i+1<argc)view_elev=(float)atof(argv[++i]);
        else if(!strcmp(argv[i],"--zoom")&&i+1<argc)view_zoom=(float)atof(argv[++i]);
        else {printf("Usage: %s [--disc IMAGE] [--model PlMrNr.dat] [--model-index N] [--part N] [--part-mode all|only|hide] [--list-models] [--all-models] [--inspect] [--view [--angle DEG] [--elevation DEG]] [--animate [--clip NAME|N] [--anim-frame F] [--anim-speed S] [--anim-file PlMrAJ.dat] [--list-clips]] [--frames N] [--screenshot FILE.bmp] [--scripted]\n",argv[0]);return strcmp(argv[i],"--help")!=0;}
    }
    if(dump_lights) {
        SceneLights lights;
        char light_error[128];
        if(lights_load(disc,&lights,light_error,sizeof(light_error))!=0)
            fprintf(stderr,"Lights unavailable (%s)\n",light_error);
        else
            lights_dump(&lights);
        return 0;
    }
    if(extract_file&&extract_out) {
        DiscFile a={0};char err[128];
        if(disc_load(disc,extract_file,&a,err,sizeof(err))!=DISC_OK) {
            fprintf(stderr,"extract %s: %s\n",extract_file,err);return 1;
        }
        FILE *f=fopen(extract_out,"wb");
        if(!f){fprintf(stderr,"cannot write %s\n",extract_out);disc_free(&a);return 1;}
        fwrite(a.data,1,a.size,f);fclose(f);
        printf("Wrote %s (%zu bytes)\n",extract_out,a.size);
        disc_free(&a);return 0;
    }
    DiscFileList models={0};
    if(view||list_models||model_index>=0) {
        char list_error[128];
        if(disc_list(disc,"Pl",all_models?".dat":"Nr.dat",&models,list_error,sizeof(list_error))!=DISC_OK) {
            fprintf(stderr,"Model list unavailable (%s)\n",list_error);
            if(list_models)return 1;
        }
        if(list_models) {
            size_t i;
            for(i=0;i<models.count;++i)printf("%s\n",models.names[i]);
            printf("%zu model archives\n",models.count);
            disc_list_free(&models);
            return 0;
        }
        if(model_index>=0&&(size_t)model_index<models.count)model_file=models.names[model_index];
    }
    Visual visuals[2];
    memset(visuals,0,sizeof(visuals));
    if(no_visibility) {
        /* Applied after load below. */
    }
    if(!visual_load_model(&visuals[0],disc,model_file,g_vis_slot,g_vis_variant))return 1;
    if(no_visibility)parts_show_all(&visuals[0].model);
    if(show_hidden)parts_show_all(&visuals[0].model);
    if(list_clips) {
        if(visual_load_anim(&visuals[0],disc,model_file,anim_file)) {
            size_t ci;
            for(ci=0;ci<anim_clip_count(&visuals[0].anim);++ci)
                printf("%3zu  %-28s %6.1f frames\n",ci,
                       anim_clip_name(&visuals[0].anim,ci),
                       anim_clip_frames(&visuals[0].anim,ci));
        }
        anim_free(&visuals[0].anim);
        hsd_model_free(&visuals[0].model);free(visuals[0].model.vertices);return 0;
    }
    if(dump_clip) {
        if(visual_load_anim(&visuals[0],disc,model_file,anim_file)) {
            int ci=anim_clip_find(&visuals[0].anim,dump_clip);
            if(ci<0)ci=0;
            if(anim_set_clip(&visuals[0].anim,(size_t)ci,&visuals[0].model,NULL,0)==0) {
                float frames=anim_end_frame(&visuals[0].anim);
                int f;
                for(f=0;(float)f<=frames;++f) {
                    size_t j;
                    for(j=0;j<visuals[0].model.joint_count;++j) {
                        size_t first=visuals[0].anim.joint_first[j];
                        size_t count=visuals[0].anim.joint_tracks[j];
                        size_t k;
                        for(k=0;k<count&&first+k<visuals[0].anim.fobj_count;++k) {
                            Fobj *fo=&visuals[0].anim.fobjs[first+k];
                            float value;
                            fobj_req_anim(fo,(float)f);
                            if(fobj_interpret(fo,0.0f,&value))
                                printf("%d %zu %zu %u %.6f\n",f,j,k,
                                       fo->obj_type,value);
                        }
                    }
                }
            }
            anim_free(&visuals[0].anim);
        }
        hsd_model_free(&visuals[0].model);free(visuals[0].model.vertices);return 0;
    }
    /* Two instances of the same decoded costume during renderer bring-up. */
    visuals[0].label="P1 / MARIO";
    if(dump_textures) {
        size_t ti,max=visuals[0].model.texture_count;
        for(ti=0;ti<max&&ti<HSD_MAX_TEXTURES;++ti) {
            HsdTexture *t=&visuals[0].model.textures[ti];
            char path[512];
            if(t->rgba==NULL)continue;
            snprintf(path,sizeof(path),"%s/tex_%02zu.ppm",dump_textures,ti);
            FILE *f=fopen(path,"wb");
            if(!f)continue;
            fprintf(f,"P6\n%u %u\n255\n",t->width,t->height);
            {
                size_t p;
                size_t transparent=0;
                for(p=0;p<(size_t)t->width*t->height;++p) {
                    if(t->rgba[p*4+3]<128)transparent++;
                    fwrite(&t->rgba[p*4],1,3,f);
                }
                printf("tex %zu: %ux%u fmt %u transparent %.1f%%\n",ti,
                       t->width,t->height,t->format,
                       100.0*(double)transparent/((double)t->width*t->height));
            }
            fclose(f);
        }
        printf("Dumped %zu textures\n",max);
        hsd_model_free(&visuals[0].model);free(visuals[0].model.vertices);return 0;
    }
    if(dump_tev) {
        size_t bi,ti;
        printf("batches: %zu\n",visuals[0].model.batch_count);
        for(bi=0;bi<visuals[0].model.batch_count;++bi) {
            HsdBatch *b=&visuals[0].model.batches[bi];
            HsdMaterial *mat=&b->material;
            printf("batch %-3zu rm=%#010x tex=%-3d amb=%3u,%3u,%3u mat=%3u,%3u,%3u spe=%3u,%3u,%3u,a=%.2f sh=%.1f tobjs=%u",
                   bi,(unsigned)mat->rendermode,(int)b->texture,
                   mat->ambient[0],mat->ambient[1],mat->ambient[2],
                   mat->diffuse[0],mat->diffuse[1],mat->diffuse[2],
                   mat->specular[0],mat->specular[1],mat->specular[2],
                   (double)mat->alpha,(double)mat->shininess,mat->tobj_count);
            if(mat->pe_present) {
                printf(" pe=fl%02x r%u/%u t%u s%u d%u op%u z%u c%u/%u/%u",
                       mat->pe_flags,mat->pe_ref0,mat->pe_ref1,mat->pe_type,
                       mat->pe_src_factor,mat->pe_dst_factor,mat->pe_logic_op,
                       mat->pe_z_comp,mat->pe_alpha_comp0,mat->pe_alpha_op,
                       mat->pe_alpha_comp1);
            }
            for(ti=0;ti<mat->tobj_count;++ti) {
                HsdTobj *t=&mat->tobjs[ti];
                printf(" | t%zu tex=%d id=%u src=%u flags=%#x cm=%u am=%u tev=%u",
                       ti,(int)t->texture,t->id,t->src,(unsigned)t->flags,
                       (unsigned)((t->flags>>16)&0xf),
                       (unsigned)((t->flags>>20)&0xf),t->has_tev);
                if(t->has_tev) {
                    printf("(c=%u/%u a=%u/%u act=%#x)",t->tev_color_op,
                           t->tev_alpha_op,t->tev_color_a,t->tev_alpha_a,
                           (unsigned)t->tev_active);
                }
            }
            printf("\n");
        }
        hsd_model_free(&visuals[0].model);free(visuals[0].model.vertices);return 0;
    }
    if(dump_joints) {
        size_t ji;
        for(ji=0;ji<visuals[0].model.joint_count;++ji) {
            const HsdJoint *j=&visuals[0].model.joints[ji];
            printf("joint %-3zu parent=%-4d flags=%#06x pos=[%7.3f %7.3f %7.3f] rot=[%7.3f %7.3f %7.3f]\n",
                   ji,j->parent,(unsigned)j->flags,
                   (double)j->position_bind[0],(double)j->position_bind[1],
                   (double)j->position_bind[2],
                   (double)j->rotation_bind[0],(double)j->rotation_bind[1],
                   (double)j->rotation_bind[2]);
        }
        hsd_model_free(&visuals[0].model);free(visuals[0].model.vertices);return 0;
    }
    if(inspect){
        if(list_parts) {
            size_t bi;
            printf("%-4s %-8s %-8s %-8s %s\n","#","verts","texture","state","y/x-range");
            for(bi=0;bi<visuals[0].model.batch_count;++bi) {
                HsdBatch *b=&visuals[0].model.batches[bi];
                float ymin=1e30f,ymax=-1e30f,xmin=1e30f,xmax=-1e30f;
                size_t vi;
                for(vi=0;vi<b->vertex_count;++vi) {
                    float *p=visuals[0].model.vertices[b->first_vertex+vi].position;
                    if(p[1]<ymin)ymin=p[1];
                    if(p[1]>ymax)ymax=p[1];
                    if(p[0]<xmin)xmin=p[0];
                    if(p[0]>xmax)xmax=p[0];
                }
                printf("%-4zu %-6zu dobj=%-3zu tex=%-3d rm=%#08x cull=%u w=%u,%u %-8s y[%6.2f %6.2f] x[%6.2f %6.2f]",
                       bi,b->vertex_count,b->dobj_index,(int)b->texture,(unsigned)b->rendermode,b->cull_mode,b->wrap_s,b->wrap_t,
                       hsd_model_batch_visible(&visuals[0].model,bi)?"visible":"HIDDEN",
                       ymin,ymax,xmin,xmax);
                if(b->texture>=0&&(size_t)b->texture<visuals[0].model.texture_count) {
                    HsdTexture *t=&visuals[0].model.textures[b->texture];
                    printf("  %#x %dx%d f%d",(unsigned)t->source_offset,t->width,t->height,t->format);
                }
                printf("\n");
            }
            printf("%zu parts\n",visuals[0].model.batch_count);
        }
        hsd_model_free(&visuals[0].model);free(visuals[0].model.vertices);return 0;
    }
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER|SDL_INIT_TIMER)) {
        fprintf(stderr,"SDL: %s\n",SDL_GetError());return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    SDL_Window *window=SDL_CreateWindow("Melee Native Lab - custom playable sandbox",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1280,800,SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE);
    SDL_GLContext context=window?SDL_GL_CreateContext(window):NULL;
    if(!context){fprintf(stderr,"Graphics: %s\n",SDL_GetError());SDL_Quit();return 1;}
    SDL_GL_SetSwapInterval(1);
    printf("Renderer: %s\n",glGetString(GL_RENDERER));
    printf("OpenGL: %s (GLSL %s)\n",glGetString(GL_VERSION),
           glGetString(GL_SHADING_LANGUAGE_VERSION));
    if(!renderer_init()) {
        fprintf(stderr,"Shader setup failed\n");
        SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();return 1;
    }
    {
        SceneLights lights;
        char light_error[128];
        if(lights_load(disc,&lights,light_error,sizeof(light_error))==0) {
            render_set_lights(&lights);
            printf("Scene lights: character select (%zu lights, fog %s)\n",
                   lights.count,lights.fog.present?"on":"off");
        } else {
            fprintf(stderr,"Scene lights unavailable (%s); using stand-in\n",
                    light_error);
        }
    }
    visual_compile(&visuals[0]);
    /* P2 shares the compiled buffers and textures; copy after compile so the
     * per-batch draw has valid GL object names.  Match mode replaces this with
     * an independent decode so both fighters can pose separately. */
    visuals[1]=visuals[0];
    visuals[1].label="P2 / MARIO";
    visuals[1].shared=1;
    glFrontFace(GL_CW);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    if(view) {
        Viewer vs;
        int running=1,index=-1,next_model=0,prev_model=0,rendered=0,want_shot=0;
        int w=0,h=1;
        size_t mi;
        int dragging=0,lastx=0,lasty=0;
        double previous=SDL_GetPerformanceCounter()/(double)SDL_GetPerformanceFrequency();
        double anim_accumulator=0.0;
        memset(&vs,0,sizeof(vs));
        vs.yaw=view_angle*PI/180.0f;
        vs.pitch=-view_elev*PI/180.0f;
        vs.textures=1;vs.lighting=1;vs.culling=0;vs.grid=no_grid?0:1;vs.help=0;
        vs.show_hidden=show_hidden;
        vs.vis_slot=g_vis_slot;
        vs.speed=anim_speed;
        vs.animate=0;vs.playing=0;vs.anim_time=0.0f;vs.clip=0;
        if(animate||anim_frame>=0.0f||anim_file!=NULL) {
            if(visual_load_anim(&visuals[0],disc,model_file,anim_file)) {
                vs.animate=anim_choose_clip(&visuals[0],&vs,clip_name);
                vs.playing=(animate&&vs.animate&&anim_frame<0.0f)?1:0;
                if(vs.animate&&anim_frame>=0.0f)
                    vs.anim_time=anim_wrap(&visuals[0],anim_frame);
            }
        }
        if(force_no_cull)vs.culling=0;
        vs.batch=view_part>=0?view_part:0;vs.mode=view_part_mode;
        for(mi=0;mi<models.count;++mi)
            if(!strcmp(models.names[mi],model_file)){index=(int)mi;break;}
        if(view_part>=0&&view_part_mode==1)viewer_frame_batch(&vs,&visuals[0],(size_t)view_part);
        else viewer_frame_model(&vs,&visuals[0]);
        vs.distance*=view_zoom>0.01f?view_zoom:1.0f;
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
                    case SDLK_r:vs.yaw=view_angle*PI/180.0f;vs.pitch=-view_elev*PI/180.0f;viewer_frame_model(&vs,&visuals[0]);break;
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
                                    index>=0&&(size_t)index<models.count?models.names[index]:model_file,
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
                                const char *file=index>=0&&(size_t)index<models.count?models.names[index]:model_file;
                                if(visual_load_anim(&visuals[0],disc,file,anim_file))
                                    anim_choose_clip(&visuals[0],&vs,clip_name);
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
                if(models.count>0&&viewer_cycle(&visuals[0],disc,&models,&index,next_model?1:-1)) {
                    if(view_part>=0&&view_part_mode==1)viewer_frame_batch(&vs,&visuals[0],(size_t)view_part);
                    else viewer_frame_model(&vs,&visuals[0]);
                    vs.batch=0;
                    vs.anim_time=0.0f;
                    if(vs.animate) {
                        if(visual_load_anim(&visuals[0],disc,models.names[index],anim_file))
                            anim_choose_clip(&visuals[0],&vs,want[0]?want:clip_name);
                    }
                    printf("Viewing %s\n",models.names[index]);
                } else {
                    fprintf(stderr,"No decodable model in list\n");
                }
                next_model=prev_model=0;
            }
            {
                SDL_GL_GetDrawableSize(window,&w,&h);
                if(h<1)h=1;
                render_viewer(&visuals[0],&vs,w,h,
                              index>=0&&(size_t)index<models.count?models.names[index]:model_file,
                              index,(int)models.count);
                SDL_GL_SwapWindow(window);
                if(want_shot) {
                    if(platform_screenshot(capture?capture:"viewer.bmp",w,h))printf("Saved screenshot\n");
                    else fprintf(stderr,"Screenshot failed: %s\n",SDL_GetError());
                    want_shot=0;
                }
            }
            ++rendered;
            if(frames&&rendered>=frames) {
                if(capture&&!platform_screenshot(capture,w,h))fprintf(stderr,"Screenshot failed: %s\n",SDL_GetError());
                printf("Rendered %d viewer frames of %s\n",rendered,model_file);
                running=0;
            }
            SDL_Delay(1);
        }
        disc_list_free(&models);
        visual_destroy(&visuals[0]);
        overlay_shutdown();
        SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();return 0;
    }
    SandboxWorld world;FighterAttrs attrs;SandboxFighter fighters[2];
    sandbox_init_world(&world);sandbox_default_attrs(&attrs);
    {
        char attr_error[128];
        if(load_mario_attrs(disc,&attrs,attr_error,sizeof(attr_error))==DISC_OK) {
            printf("Mario attributes: accel %.3f friction %.3f run %.3f gravity %.3f terminal %.2f air %.3f jump %.2f jumps %d\n",
                   attrs.ground_accel,attrs.ground_friction,attrs.ground_max_speed,
                   attrs.gravity,attrs.terminal_velocity,attrs.air_accel,
                   attrs.jump_velocity,attrs.max_jumps);
        } else {
            fprintf(stderr,"Mario attributes unavailable (%s); using sandbox defaults\n",attr_error);
        }
    }
    /* Give P2 its own model and animation state so both fighters can be posed
     * independently.  Falls back to the shared bind-pose visual on failure. */
    {
        memset(&visuals[1],0,sizeof(visuals[1]));
        if(visual_load_model(&visuals[1],disc,model_file,g_vis_slot,g_vis_variant)) {
            if(no_visibility||show_hidden)parts_show_all(&visuals[1].model);
            visuals[1].label="P2 / MARIO";
            visual_compile(&visuals[1]);
        } else {
            visuals[1]=visuals[0];
            visuals[1].label="P2 / MARIO";
            visuals[1].shared=1;
        }
    }
    FighterAnim fanim[2]={{-1,0.0f},{-1,0.0f}};
    for(int i=0;i<2;++i) {
        if(visual_load_anim(&visuals[i],disc,model_file,anim_file))
            fanim[i].clip=anim_select_clip(&visuals[i],"Wait1");
    }
    for(int i=0;i<2;++i){sandbox_reset(&fighters[i],&world);fighters[i].x=i?20:-20;fighters[i].facing=i?-1:1;}
    SDL_GameController *pad=NULL;
    for(int i=0;i<SDL_NumJoysticks();++i)if(SDL_IsGameController(i)){pad=SDL_GameControllerOpen(i);break;}
    int run=1,cpu=1,paused=0,rendered=0;
    unsigned tick=0;double previous=SDL_GetPerformanceCounter()/(double)SDL_GetPerformanceFrequency(),accumulator=0;
    SandboxInput pending[2]={{0}};int previous_pad_jump=0,previous_pad_attack=0;
    while(run) {
        SDL_Event e;
        while(SDL_PollEvent(&e)) {
            if(e.type==SDL_QUIT)run=0;
            if(e.type==SDL_KEYDOWN&&!e.key.repeat) {
                SDL_Keycode k=e.key.keysym.sym;
                if(k==SDLK_ESCAPE)run=0;
                if(k==SDLK_F2)cpu=!cpu;
                if(k==SDLK_p)paused=!paused;
                if(k==SDLK_r)for(int i=0;i<2;++i){sandbox_reset(&fighters[i],&world);fighters[i].x=i?20:-20;}
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
                for(int i=0;i<2;++i)sandbox_step(&fighters[i],&attrs,&world,pending[i]);
                sandbox_try_hit(&fighters[0],&fighters[1]);sandbox_try_hit(&fighters[1],&fighters[0]);
                for(int i=0;i<2;++i)fighter_anim_step(&visuals[i],&fanim[i],&fighters[i],&attrs);
            }
            for(int i=0;i<2;++i)pending[i].jump_pressed=pending[i].attack_pressed=0;
        }
        int w,h;SDL_GL_GetDrawableSize(window,&w,&h);
        if(h<1)h=1;
        glViewport(0,0,w,h);glClearColor(.025f,.035f,.065f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        {
            Mat4 proj,base,mvp;
            float halfx=55,halfy=halfx*h/w;
            float midx=(fighters[0].x+fighters[1].x)*.5f;
            float midy=(fighters[0].y+fighters[1].y)*.5f+9;
            if(midx<-30)midx=-30;
            if(midx>30)midx=30;
            if(midy<10)midy=10;
            if(midy>32)midy=32;
            m4_ortho(proj,midx-halfx,midx+halfx,midy-halfy,midy+halfy,-250,250);
            glDisable(GL_DEPTH_TEST);
            ov_set_mvp(proj);
            ov_begin(GL_QUADS);
            ov_color3f(.025f,.04f,.085f);ov_vertex3f(-150,-90,-80);ov_vertex3f(150,-90,-80);
            ov_color3f(.07f,.12f,.20f);ov_vertex3f(150,100,-80);ov_vertex3f(-150,100,-80);
            ov_end();
            ov_color4f(.25f,.5f,.6f,.12f);glLineWidth(1);
            ov_begin(GL_LINES);
            for(int x=-150;x<=150;x+=10){ov_vertex3f(x,-90,-70);ov_vertex3f(x,100,-70);}
            for(int y=-90;y<=100;y+=10){ov_vertex3f(-150,y,-70);ov_vertex3f(150,y,-70);}
            ov_end();
            glEnable(GL_DEPTH_TEST);
            m4_identity(base);
            m4_mul_rotate(base,7,1,0,0);
            m4_mul(mvp,proj,base);
            for(int i=0;i<world.platform_count;++i)platform(&world.platforms[i],i==0,mvp);
            for(int i=0;i<2;++i)
                if(visuals[i].anim_loaded&&fanim[i].clip>=0)
                    anim_apply(&visuals[i].anim,&visuals[i].model,fanim[i].frame);
            draw_fighter(&visuals[0],&fighters[0],0,tick,&fanim[0],proj,base);
            draw_fighter(&visuals[1],&fighters[1],1,tick,&fanim[1],proj,base);
            hud(w,h,fighters,cpu,paused,visuals);
        }
        ++rendered;
        if(frames&&rendered>=frames) {
            if(capture&&!platform_screenshot(capture,w,h))fprintf(stderr,"Screenshot failed: %s\n",SDL_GetError());
            printf("Completed %d render frames, %u simulation ticks\n",rendered,tick);run=0;
        }
        SDL_GL_SwapWindow(window);
        SDL_Delay(1);
    }
    if(pad)SDL_GameControllerClose(pad);
    visual_destroy(&visuals[0]);
    if(!visuals[1].shared)visual_destroy(&visuals[1]);
    overlay_shutdown();
    SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();return 0;
}
