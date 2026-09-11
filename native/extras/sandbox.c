#include "extras/sandbox.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "gx/gl.h"
#include "gx/math.h"
#include "gx/overlay.h"
#include "gx/render.h"
#include "hsd/anim.h"
#include "hsd/parts.h"
#include "game/attributes.h"
#include "extras/physics.h"
#include "platform/disc.h"
#include "platform/screenshot.h"
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

int sandbox_run(SDL_Window *window,const char *disc,const char *model_file,
                 Visual *visuals,const SandboxOptions *opts)
{
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
        if(visual_load_model(&visuals[1],disc,model_file,opts->vis_slot,opts->vis_variant)) {
            if(opts->no_visibility||opts->show_hidden)parts_show_all(&visuals[1].model);
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
        if(visual_load_anim(&visuals[i],disc,model_file,opts->anim_file))
            fanim[i].clip=anim_select_clip(&visuals[i].anim,&visuals[i].model,"Wait1");
    }
    for(int i=0;i<2;++i){sandbox_reset(&fighters[i],&world);fighters[i].x=i?20:-20;fighters[i].facing=i?-1:1;}
    SDL_GameController *pad=NULL;
    if(!opts->no_controller)
        for(int i=0;i<SDL_NumJoysticks();++i)if(SDL_IsGameController(i)){pad=SDL_GameControllerOpen(i);break;}
    int run=1,cpu=1,paused=0,rendered=0;
    int pad_axis_seen=0;
    unsigned tick=0;double previous=SDL_GetPerformanceCounter()/(double)SDL_GetPerformanceFrequency(),accumulator=0;
    SandboxInput pending[2]={{0}};int previous_pad_jump=0,previous_pad_attack=0;
    while(run) {
        SDL_Event e;
        while(SDL_PollEvent(&e)) {
            if(e.type==SDL_QUIT)run=0;
            if(e.type==SDL_CONTROLLERAXISMOTION)pad_axis_seen=1;
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
        float keyboard_axis=(float)(keys[SDL_SCANCODE_D]-keys[SDL_SCANCODE_A]);
        pending[0].axis=keyboard_axis;pending[0].shield=keys[SDL_SCANCODE_G];
        pending[1].axis=(float)(keys[SDL_SCANCODE_RIGHT]-keys[SDL_SCANCODE_LEFT]);pending[1].shield=keys[SDL_SCANCODE_L];
        if(pad&&SDL_GameControllerGetAttached(pad)) {
            /* Only trust the stick after it has produced a motion event:
             * some cheap pads report a stuck axis from power-on and would
             * otherwise walk the fighter left forever.  Keyboard wins while
             * a movement key is held. */
            float axis=SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTX)/32767.0f;
            if(pad_axis_seen&&fabsf(keyboard_axis)<0.01f&&fabsf(axis)>.2f)
                pending[0].axis=axis;
            int j=SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_A),a=SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_X);
            pending[0].jump_pressed|=j&&!previous_pad_jump;pending[0].attack_pressed|=a&&!previous_pad_attack;
            previous_pad_jump=j;previous_pad_attack=a;
            pending[0].shield|=SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
        }
        double now=SDL_GetPerformanceCounter()/(double)SDL_GetPerformanceFrequency(),dt=now-previous;previous=now;
        if(dt>.1)dt=.1;
        accumulator+=dt;
        if(opts->scripted)accumulator=1.0/60;
        while(accumulator>=1.0/60) {
            accumulator-=1.0/60;
            if(!paused) {
                ++tick;
                if(opts->scripted){pending[0].axis=(tick/100)%2?-.6f:.6f;pending[0].jump_pressed=tick%90==1;pending[0].attack_pressed=tick%25==1;}
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
        if(opts->frames&&rendered>=opts->frames) {
            if(opts->capture&&!platform_screenshot(opts->capture,w,h))fprintf(stderr,"Screenshot failed: %s\n",SDL_GetError());
            printf("Completed %d render frames, %u simulation ticks\n",rendered,tick);run=0;
        }
        SDL_GL_SwapWindow(window);
        SDL_Delay(1);
    }
    if(pad)SDL_GameControllerClose(pad);
    return 0;
}
