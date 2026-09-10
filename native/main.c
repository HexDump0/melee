#include <SDL.h>
#include <SDL_opengl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "demo_assets.h"
#include "demo_attributes.h"
#include "demo_model.h"
#include "demo_parts.h"
#include "demo_physics.h"
#include "demo_text.h"

#define DEFAULT_DISC "iso/Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).ciso"
#define PI 3.14159265358979323846f

static int g_vis_slot = 0;
static int g_vis_variant = 0;

typedef struct Visual {
    DemoModel model;
    GLuint list;
    GLuint batch_lists[DEMO_MAX_BATCHES];
    GLuint textures[DEMO_MAX_TEXTURES];
    float scale;
    const char *label;
} Visual;

static void destroy_visual(Visual *v);

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
    printf("PObj types: skin %zu, shapeanim %zu, envelope %zu; joints %zu, instances %zu\n",
           v->model.pobj_type_count[0],v->model.pobj_type_count[1],
           v->model.pobj_type_count[2],v->model.joint_count,v->model.instance_count);
    float height = v->model.bounds_max[1]-v->model.bounds_min[1];
    v->scale = height > .001f ? 11.0f/height : 1;
    {
        char parts_error[128];
        int parts = demo_parts_apply(disc,file,&v->model,g_vis_slot,
                                     g_vis_variant,parts_error,
                                     sizeof(parts_error));
        if(parts==0)printf("Parts visibility: %zu of %zu objects hidden (neutral pose)\n",
                           demo_parts_hidden_count(&v->model),v->model.dobj_count);
        else if(parts<0)fprintf(stderr,"Parts visibility failed: %s\n",parts_error);
    }
    return 1;
}

static GLint gx_wrap_to_gl(int wrap)
{
    if(wrap==1)return GL_REPEAT;
    if(wrap==2)return GL_MIRRORED_REPEAT;
    return GL_CLAMP_TO_EDGE;
}

static void apply_wrap(int wrap_s,int wrap_t)
{
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,gx_wrap_to_gl(wrap_s));
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,gx_wrap_to_gl(wrap_t));
}

static void apply_cull(int cull)
{
    if(cull==0) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glCullFace(cull==1?GL_FRONT:GL_BACK);
    }
}

static void list_vertices(Visual *v,size_t first,size_t count,int wrap_s,int wrap_t,uint32_t rendermode)
{
    int current=-2;
    size_t i;
    if(rendermode&(1u<<27))glDepthFunc(GL_ALWAYS);
    if(rendermode&(1u<<29))glDepthMask(GL_FALSE);
    for(i=0;i<count;++i) {
        DemoModelVertex *p=&v->model.vertices[first+i];
        if(p->texture!=current) {
            if(current!=-2)glEnd();
            current=p->texture;
            if(current>=0&&(size_t)current<v->model.texture_count) {
                glBindTexture(GL_TEXTURE_2D,v->textures[current]);
                apply_wrap(wrap_s,wrap_t);
            } else {
                glBindTexture(GL_TEXTURE_2D,0);
            }
            glBegin(GL_TRIANGLES);
        }
        glColor4ubv(p->color);
        glNormal3fv(p->normal);
        glTexCoord2fv(p->uv);
        glVertex3fv(p->position);
    }
    if(current!=-2)glEnd();
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
}

static void compile_full_list(Visual *v)
{
    size_t i;
    if(v->list)glDeleteLists(v->list,1);
    v->list=glGenLists(1);
    glNewList(v->list,GL_COMPILE);
    for(i=0;i<v->model.batch_count&&i<DEMO_MAX_BATCHES;++i) {
        if(v->batch_lists[i]&&demo_model_batch_visible(&v->model,i))
            glCallList(v->batch_lists[i]);
    }
    glEndList();
}

static void compile_model(Visual *v)
{
    size_t i;
    for(i=0;i<v->model.texture_count&&i<DEMO_MAX_TEXTURES;++i) {
        DemoModelTexture *t=&v->model.textures[i];
        if(t->rgba==NULL)continue;
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
    for(i=0;i<v->model.batch_count&&i<DEMO_MAX_BATCHES;++i) {
        DemoModelBatch *b=&v->model.batches[i];
        if(b->cull_mode==3)continue; /* POBJ_CULLFRONT|CULLBACK: never drawn */
        v->batch_lists[i]=glGenLists(1);
        glNewList(v->batch_lists[i],GL_COMPILE);
        list_vertices(v,b->first_vertex,b->vertex_count,b->wrap_s,b->wrap_t,
                      b->rendermode);
        glEndList();
    }
    compile_full_list(v);
}

/* Interactive model viewer: orbit, zoom, wireframe and model switching. */
static void look_at(const float eye[3], const float target[3])
{
    float f[3]={target[0]-eye[0],target[1]-eye[1],target[2]-eye[2]};
    float s[3],u[3],m[16];
    float length=sqrtf(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);
    if(length<1e-6f)length=1e-6f;
    f[0]/=length;f[1]/=length;f[2]/=length;
    s[0]=-f[2];s[1]=0.0f;s[2]=f[0];
    length=sqrtf(s[0]*s[0]+s[1]*s[1]+s[2]*s[2]);
    if(length<1e-6f){s[0]=1.0f;s[1]=0.0f;s[2]=0.0f;length=1.0f;}
    s[0]/=length;s[1]/=length;s[2]/=length;
    u[0]=s[1]*f[2]-s[2]*f[1];
    u[1]=s[2]*f[0]-s[0]*f[2];
    u[2]=s[0]*f[1]-s[1]*f[0];
    m[0]=s[0];m[4]=s[1];m[8]=s[2];m[12]=-(s[0]*eye[0]+s[1]*eye[1]+s[2]*eye[2]);
    m[1]=u[0];m[5]=u[1];m[9]=u[2];m[13]=-(u[0]*eye[0]+u[1]*eye[1]+u[2]*eye[2]);
    m[2]=-f[0];m[6]=-f[1];m[10]=-f[2];m[14]=f[0]*eye[0]+f[1]*eye[1]+f[2]*eye[2];
    m[3]=0.0f;m[7]=0.0f;m[11]=0.0f;m[15]=1.0f;
    glLoadMatrixf(m);
}

typedef struct Viewer {
    float yaw,pitch,distance,radius,target[3];
    int wireframe,textures,lighting,culling,grid,spin,help;
    int show_hidden;
    int batch,mode; /* mode: 0 = all, 1 = only selected, 2 = hide selected */
    int vis_slot;
} Viewer;

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
    DemoModelBatch *b;
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

static void viewer_grid(const Visual *v)
{
    float y=v->model.bounds_min[1];
    float extent=0.0f;
    float step;
    float x;
    int i;
    for(i=0;i<3;++i) {
        float s=v->model.bounds_max[i]-v->model.bounds_min[i];
        if(s>extent)extent=s;
    }
    extent*=1.1f;
    step=extent/10.0f;
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glColor4f(.30f,.38f,.50f,.55f);
    glBegin(GL_LINES);
    for(i=-10;i<=10;++i) {
        x=i*step;
        glVertex3f(x,y,-extent);glVertex3f(x,y,extent);
        glVertex3f(-extent,y,x);glVertex3f(extent,y,x);
    }
    glEnd();
}

static void viewer_hud(int w,int h,const Visual *v,const Viewer *vs,
                       const char *name,int index,int total)
{
    char line[192];
    float s=fmaxf(1.0f,w/1280.0f);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,w,h,0,-1,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glColor4f(.03f,.045f,.075f,.9f);rect(0,0,w,88*s);
    glColor3f(.92f,.95f,1);demo_text(24*s,16*s,3*s,"MODEL VIEWER");
    glColor3f(.39f,.8f,.77f);
    demo_text(24*s,44*s,1.5f*s,"REAL DISC ASSETS / BIND POSE / CUSTOM SANDBOX");
    glColor3f(.85f,.9f,1);
    if(total>0)snprintf(line,sizeof(line),"%s  [%d/%d]",name,index+1,total);
    else snprintf(line,sizeof(line),"%s",name);
    demo_text(w*.5f-140*s,18*s,2*s,line);
    snprintf(line,sizeof(line),"%zu TRIS  %zu TEXTURES  %zu PARTS",
             v->model.triangle_count,v->model.texture_count,
             v->model.batch_count);
    glColor3f(.62f,.7f,.82f);
    demo_text(w*.5f-140*s,44*s,1.3f*s,line);
    if(v->model.batch_count>0) {
        int b=vs->batch;
        static const char *mode_names[3]={"ALL PARTS","ONLY PART","HIDE PART"};
        if(b<0)b=0;
        if((size_t)b>=v->model.batch_count)b=(int)v->model.batch_count-1;
        snprintf(line,sizeof(line),"%s  %d/%zu  %zu VERTS",mode_names[vs->mode],
                 b+1,v->model.batch_count,v->model.batches[b].vertex_count);
        glColor3f(.95f,.8f,.5f);
        demo_text(w*.5f-140*s,66*s,1.3f*s,line);
    }
    glColor3f(.68f,.73f,.84f);
    demo_text(24*s,h-56*s,1.4f*s,"DRAG: ORBIT   WHEEL: ZOOM   ARROWS: ORBIT   N/P: MODEL   R: RESET");
    demo_text(24*s,h-34*s,1.4f*s,"T:TEX L:LIGHT W:WIRE C:CULL G:GRID V:MODE B:SLOT [ ]:PART X:HIDDEN SPACE:SPIN F12:SAVE H:HELP ESC:QUIT");
    glColor3f(.39f,.8f,.77f);
    demo_text(w-388*s,h-56*s,1.4f*s,vs->textures?"TEX ON":"TEX OFF");
    demo_text(w-288*s,h-56*s,1.4f*s,vs->lighting?"LIGHT ON":"LIGHT OFF");
    demo_text(w-168*s,h-56*s,1.4f*s,vs->wireframe?"WIRE":"SOLID");
    demo_text(w-388*s,h-34*s,1.4f*s,vs->culling?"CULL ON":"CULL OFF");
    demo_text(w-288*s,h-34*s,1.4f*s,vs->grid?"GRID ON":"GRID OFF");
    demo_text(w-168*s,h-34*s,1.4f*s,vs->show_hidden?"HIDDEN ON":"HIDDEN OFF");
    snprintf(line,sizeof(line),"SLOT %d",vs->vis_slot);
    glColor3f(.95f,.8f,.5f);
    demo_text(w-538*s,h-34*s,1.4f*s,line);
    if(vs->help) {
        glColor4f(.02f,.03f,.05f,.85f);rect(w*.5f-300*s,h*.5f-120*s,600*s,240*s);
        glColor3f(1,1,1);demo_text(w*.5f-250*s,h*.5f-90*s,2.4f*s,"VIEWER CONTROLS");
        glColor3f(.8f,.85f,.92f);
        demo_text(w*.5f-250*s,h*.5f-40*s,1.5f*s,"LEFT DRAG OR ARROWS: ORBIT THE MODEL");
        demo_text(w*.5f-250*s,h*.5f-10*s,1.5f*s,"MOUSE WHEEL OR +/-: ZOOM IN AND OUT");
        demo_text(w*.5f-250*s,h*.5f+20*s,1.5f*s,"N / P: NEXT OR PREVIOUS CHARACTER MODEL");
        demo_text(w*.5f-250*s,h*.5f+50*s,1.5f*s,"H: CLOSE THIS HELP");
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
    glMatrixMode(GL_PROJECTION);glLoadIdentity();
    glFrustum(-top*aspect,top*aspect,-top,top,znear,zfar);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    look_at(eye,vs->target);
    if(vs->lighting) {
        GLfloat light[]={.35f,.6f,1.0f,0.0f};
        glLightfv(GL_LIGHT0,GL_POSITION,light);
        glEnable(GL_LIGHTING);
    }
    if(vs->culling)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);
    if(vs->textures)glEnable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    glPolygonMode(GL_FRONT_AND_BACK,vs->wireframe?GL_LINE:GL_FILL);
    {
        size_t bi;
        for(bi=0;bi<v->model.batch_count&&bi<DEMO_MAX_BATCHES;++bi) {
            int allowed=v->model.batches[bi].cull_mode!=3&&
                        (vs->show_hidden||demo_model_batch_visible(&v->model,bi));
            int visible=allowed&&(vs->mode==0||(vs->mode==1&&(int)bi==vs->batch)||
                        (vs->mode==2&&(int)bi!=vs->batch));
            if(visible&&v->batch_lists[bi]) {
                apply_cull(vs->culling?(int)v->model.batches[bi].cull_mode:0);
                glCallList(v->batch_lists[bi]);
            }
        }
    }
    glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    apply_cull(0);
    if(vs->grid)viewer_grid(v);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    viewer_hud(w,h,v,vs,name,index,total);
    glEnable(GL_DEPTH_TEST);
}

static int viewer_open_model(Visual *v,const char *disc,const char *file)
{
    destroy_visual(v);
    memset(v,0,sizeof(*v));
    if(!load_model(v,disc,file))return 0;
    compile_model(v);
    return 1;
}

static int viewer_cycle(Visual *v,const char *disc,DemoAssetList *models,int *index,int dir)
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
    {
        size_t bi;
        for(bi=0;bi<v->model.batch_count&&bi<DEMO_MAX_BATCHES;++bi) {
            if(!v->batch_lists[bi]||!demo_model_batch_visible(&v->model,bi))continue;
            apply_cull((int)v->model.batches[bi].cull_mode);
            glCallList(v->batch_lists[bi]);
        }
        apply_cull(0);
    }
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
    for(i=0;i<v->model.batch_count&&i<DEMO_MAX_BATCHES;++i) {
        if(v->batch_lists[i])glDeleteLists(v->batch_lists[i],1);
    }
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
    int list_models=0,all_models=0,model_index=-1,view_part=-1,view_part_mode=0,list_parts=0,show_hidden=0,no_visibility=0;
    const char *dump_textures=NULL;
    const char *extract_file=NULL,*extract_out=NULL;
    int force_no_cull=0;
    float view_angle=25.0f,view_elev=-12.0f,view_zoom=1.0f;
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
        else {printf("Usage: %s [--disc IMAGE] [--model PlMrNr.dat] [--model-index N] [--part N] [--part-mode all|only|hide] [--list-models] [--all-models] [--inspect] [--view [--angle DEG] [--elevation DEG]] [--frames N] [--screenshot FILE.bmp] [--scripted]\n",argv[0]);return strcmp(argv[i],"--help")!=0;}
    }
    if(extract_file&&extract_out) {
        DemoAsset a={0};char err[128];
        if(demo_asset_load(disc,extract_file,&a,err,sizeof(err))!=DEMO_ASSET_OK) {
            fprintf(stderr,"extract %s: %s\n",extract_file,err);return 1;
        }
        FILE *f=fopen(extract_out,"wb");
        if(!f){fprintf(stderr,"cannot write %s\n",extract_out);demo_asset_free(&a);return 1;}
        fwrite(a.data,1,a.size,f);fclose(f);
        printf("Wrote %s (%zu bytes)\n",extract_out,a.size);
        demo_asset_free(&a);return 0;
    }
    DemoAssetList models={0};
    if(view||list_models||model_index>=0) {
        char list_error[128];
        if(demo_asset_list(disc,"Pl",all_models?".dat":"Nr.dat",&models,list_error,sizeof(list_error))!=DEMO_ASSET_OK) {
            fprintf(stderr,"Model list unavailable (%s)\n",list_error);
            if(list_models)return 1;
        }
        if(list_models) {
            size_t i;
            for(i=0;i<models.count;++i)printf("%s\n",models.names[i]);
            printf("%zu model archives\n",models.count);
            demo_asset_list_free(&models);
            return 0;
        }
        if(model_index>=0&&(size_t)model_index<models.count)model_file=models.names[model_index];
    }
    Visual visuals[2];
    memset(visuals,0,sizeof(visuals));
    if(no_visibility) {
        /* Applied after load below. */
    }
    if(!load_model(&visuals[0],disc,model_file))return 1;
    if(no_visibility)demo_parts_show_all(&visuals[0].model);
    if(show_hidden)demo_parts_show_all(&visuals[0].model);
    /* Two instances of the same decoded costume during renderer bring-up. */
    visuals[0].label="P1 / MARIO";visuals[1]=visuals[0];visuals[1].label="P2 / MARIO";
    if(dump_textures) {
        size_t ti,max=visuals[0].model.texture_count;
        for(ti=0;ti<max&&ti<DEMO_MAX_TEXTURES;++ti) {
            DemoModelTexture *t=&visuals[0].model.textures[ti];
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
        demo_model_free(&visuals[0].model);free(visuals[0].model.vertices);return 0;
    }
    if(inspect){
        if(list_parts) {
            size_t bi;
            printf("%-4s %-8s %-8s %-8s %s\n","#","verts","texture","state","y/x-range");
            for(bi=0;bi<visuals[0].model.batch_count;++bi) {
                DemoModelBatch *b=&visuals[0].model.batches[bi];
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
                       demo_model_batch_visible(&visuals[0].model,bi)?"visible":"HIDDEN",
                       ymin,ymax,xmin,xmax);
                if(b->texture>=0&&(size_t)b->texture<visuals[0].model.texture_count) {
                    DemoModelTexture *t=&visuals[0].model.textures[b->texture];
                    printf("  %#x %dx%d f%d",(unsigned)t->source_offset,t->width,t->height,t->format);
                }
                printf("\n");
            }
            printf("%zu parts\n",visuals[0].model.batch_count);
        }
        demo_model_free(&visuals[0].model);free(visuals[0].model.vertices);return 0;
    }
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
    glFrontFace(GL_CW);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_NORMALIZE);glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK,GL_AMBIENT_AND_DIFFUSE);
    GLfloat ambient[]={.78f,.78f,.82f,1},diffuse[]={.9f,.88f,.82f,1};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT,ambient);glLightfv(GL_LIGHT0,GL_DIFFUSE,diffuse);glEnable(GL_LIGHT0);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE,GL_TRUE);
    if(view) {
        Viewer vs;
        int running=1,index=-1,next_model=0,prev_model=0,rendered=0,want_shot=0;
        int w=0,h=1;
        size_t mi;
        int dragging=0,lastx=0,lasty=0;
        double previous=SDL_GetPerformanceCounter()/(double)SDL_GetPerformanceFrequency();
        memset(&vs,0,sizeof(vs));
        vs.yaw=view_angle*PI/180.0f;
        vs.pitch=-view_elev*PI/180.0f;
        vs.textures=1;vs.lighting=1;vs.culling=0;vs.grid=1;vs.help=0;
        vs.show_hidden=show_hidden;
        vs.vis_slot=g_vis_slot;
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
                            if(demo_parts_apply(disc,
                                    index>=0&&(size_t)index<models.count?models.names[index]:model_file,
                                    &visuals[0].model,vs.vis_slot,0,part_err,
                                    sizeof(part_err))==0)
                                compile_full_list(&visuals[0]);
                            printf("Visibility slot %d\n",vs.vis_slot);
                        }
                        break;
                    case SDLK_v:vs.mode=(vs.mode+1)%3;break;
                    case SDLK_x:vs.show_hidden=!vs.show_hidden;break;
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
            if(vs.pitch>1.5f)vs.pitch=1.5f;
            if(vs.pitch<-1.5f)vs.pitch=-1.5f;
            if(vs.distance<vs.radius*0.2f)vs.distance=vs.radius*0.2f;
            if(vs.distance>vs.radius*12.0f)vs.distance=vs.radius*12.0f;
            if(vs.batch<0)vs.batch=0;
            if((size_t)vs.batch>=visuals[0].model.batch_count&&visuals[0].model.batch_count>0)
                vs.batch=(int)visuals[0].model.batch_count-1;
            if(next_model||prev_model) {
                if(models.count>0&&viewer_cycle(&visuals[0],disc,&models,&index,next_model?1:-1)) {
                    if(view_part>=0&&view_part_mode==1)viewer_frame_batch(&vs,&visuals[0],(size_t)view_part);
                    else viewer_frame_model(&vs,&visuals[0]);
                    vs.batch=0;
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
                    if(screenshot(capture?capture:"viewer.bmp",w,h))printf("Saved screenshot\n");
                    else fprintf(stderr,"Screenshot failed: %s\n",SDL_GetError());
                    want_shot=0;
                }
            }
            ++rendered;
            if(frames&&rendered>=frames) {
                if(capture&&!screenshot(capture,w,h))fprintf(stderr,"Screenshot failed: %s\n",SDL_GetError());
                printf("Rendered %d viewer frames of %s\n",rendered,model_file);
                running=0;
            }
            SDL_Delay(1);
        }
        demo_asset_list_free(&models);
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
