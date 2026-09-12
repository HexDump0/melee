#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gx/gl.h"
#include "gx/math.h"
#include "gx/overlay.h"
#include "extras/viewer.h"
#include "extras/sandbox.h"
#include "gx/render.h"
#include "platform/disc.h"
#include "hsd/anim.h"
#include "hsd/light.h"
#include "hsd/parts.h"

#define DEFAULT_DISC "iso/Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).ciso"

static int g_vis_slot = 0;
static int g_vis_variant = 0;



int main(int argc,char **argv)
{
    const char *disc=DEFAULT_DISC,*capture=NULL,*model_file="PlMrNr.dat";
    int frames=0,inspect=0,scripted=0,view=0;
    int list_models=0,all_models=0,model_index=-1,view_part=-1,view_part_mode=0,list_parts=0,show_hidden=0,no_visibility=0;
    const char *dump_textures=NULL;
    const char *extract_file=NULL,*extract_out=NULL;
    const char *clip_name="Wait1",*anim_file=NULL,*dump_clip=NULL;
    const char *dump_verts_file=NULL;
    const char *dump_raw_file=NULL;
    int animate=0,list_clips=0,force_no_cull=0,dump_tev=0,dump_lights=0,no_grid=0,dump_joints=0,no_controller=0,dump_verts=0,dump_raw=0;
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
        else if(!strcmp(argv[i],"--no-controller"))no_controller=1;
        else if(!strcmp(argv[i],"--dump-joints"))dump_joints=1;
        else if(!strcmp(argv[i],"--dump-verts")&&i+1<argc){dump_verts=1;dump_verts_file=argv[++i];}
        else if(!strcmp(argv[i],"--dump-raw")&&i+1<argc){dump_raw=1;dump_raw_file=argv[++i];}
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
        else {printf("Usage: %s [--disc IMAGE] [--model PlMrNr.dat] [--model-index N] [--part N] [--part-mode all|only|hide] [--list-models] [--all-models] [--inspect] [--view [--angle DEG] [--elevation DEG]] [--animate [--clip NAME|N] [--anim-frame F] [--anim-speed S] [--anim-file PlMrAJ.dat] [--list-clips]] [--frames N] [--screenshot FILE.bmp] [--scripted] [--no-controller]\n",argv[0]);return strcmp(argv[i],"--help")!=0;}
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
    if(dump_verts) {
        /* Dev tool: dump posed model-space vertices per batch for S2 parity. */
        FILE *f=fopen(dump_verts_file,"wb");
        size_t bi;
        if(!f){fprintf(stderr,"cannot open %s\n",dump_verts_file);return 1;}
        for(bi=0;bi<visuals[0].model.batch_count;++bi) {
            HsdBatch *b=&visuals[0].model.batches[bi];
            uint32_t count=(uint32_t)b->vertex_count;
            size_t vi;
            fwrite(&count,4,1,f);
            for(vi=0;vi<b->vertex_count;++vi)
                fwrite(visuals[0].model.vertices[b->first_vertex+vi].position,4,3,f);
        }
        fclose(f);
        printf("Dumped %zu batches to %s\n",visuals[0].model.batch_count,dump_verts_file);
        return 0;
    }
    if(dump_raw) {
        /* Dev tool: dump the parsed (pre-skin) positions per batch. */
        FILE *f=fopen(dump_raw_file,"wb");
        size_t bi;
        if(!f){fprintf(stderr,"cannot open %s\n",dump_raw_file);return 1;}
        for(bi=0;bi<visuals[0].model.batch_count;++bi) {
            HsdBatch *b=&visuals[0].model.batches[bi];
            uint32_t count=(uint32_t)b->vertex_count;
            size_t vi;
            fwrite(&count,4,1,f);
            for(vi=0;vi<b->vertex_count;++vi)
                fwrite(&visuals[0].model.raw[(b->first_vertex+vi)*6],4,3,f);
        }
        fclose(f);
        printf("Dumped raw %zu batches to %s\n",visuals[0].model.batch_count,dump_raw_file);
        return 0;
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
        ViewerOptions opts;
        memset(&opts,0,sizeof(opts));
        opts.angle=view_angle;
        opts.elevation=view_elev;
        opts.zoom=view_zoom;
        opts.animate=animate;
        opts.show_hidden=show_hidden;
        opts.no_grid=no_grid;
        opts.force_no_cull=force_no_cull;
        opts.frames=frames;
        opts.capture=capture;
        opts.clip=clip_name;
        opts.anim_file=anim_file;
        opts.anim_frame=anim_frame;
        opts.anim_speed=anim_speed;
        opts.part=view_part;
        opts.part_mode=view_part_mode;
        opts.vis_slot=g_vis_slot;
        opts.vis_variant=g_vis_variant;
        viewer_run(window,disc,model_file,&models,&visuals[0],&opts);
    } else {
        SandboxOptions opts;
        memset(&opts,0,sizeof(opts));
        opts.frames=frames;
        opts.scripted=scripted;
        opts.show_hidden=show_hidden;
        opts.no_visibility=no_visibility;
        opts.no_controller=no_controller;
        opts.vis_slot=g_vis_slot;
        opts.vis_variant=g_vis_variant;
        opts.capture=capture;
        opts.anim_file=anim_file;
        sandbox_run(window,disc,model_file,visuals,&opts);
    }
    disc_list_free(&models);
    visual_destroy(&visuals[0]);
    if(!visuals[1].shared)visual_destroy(&visuals[1]);
    overlay_shutdown();
    SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
    return 0;
}
