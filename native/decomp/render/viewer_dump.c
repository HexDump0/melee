/*
 * Draw-list dump used by the render triage environment variables.
 *
 * Split out of viewer_main.c; see viewer_internal.h.
 */
#include "decomp/render/viewer_internal.h"

void dump_draws(unsigned frame)
{
    const GxHleVertex* vertices = NULL;
    const GxHleDraw* draws = NULL;
    GxHleTexture* textures = NULL;
    size_t vertex_count = 0;
    size_t draw_count = 0;
    size_t texture_count = 0;
    size_t i;

    gx_hle_get_frame(&vertices, &vertex_count, &draws, &draw_count,
                     &textures, &texture_count);
    fprintf(stderr, "[draws] frame %u: %zu draws, %zu verts, %zu textures\n",
            frame, draw_count, vertex_count, texture_count);
    for (i = 0; i < draw_count; ++i) {
        const GxHleDraw* d = &draws[i];
        float min_x = 1e9f;
        float max_x = -1e9f;
        float min_y = 1e9f;
        float max_y = -1e9f;
        size_t v;
        if (d->kind == GX_HLE_DRAW_COPY_TEX) {
            fprintf(stderr, "[draw %zu] copytex %ux%u\n", i, d->copy_w,
                    d->copy_h);
            continue;
        }
        for (v = d->first_vertex; v < d->first_vertex + d->vertex_count;
             ++v) {
            float w = vertices[v].clip[3];
            float x = vertices[v].clip[0] / (w != 0.0f ? w : 1.0f);
            float y = vertices[v].clip[1] / (w != 0.0f ? w : 1.0f);
            if (x < min_x) {
                min_x = x;
            }
            if (x > max_x) {
                max_x = x;
            }
            if (y < min_y) {
                min_y = y;
            }
            if (y > max_y) {
                max_y = y;
            }
        }
        fprintf(stderr,
                "[draw %zu] v=%zu tex0=%d tex1=%d blend=%d/%d/%d z=%d/%d "
                "gens=%d ndc x[%.2f,%.2f] y[%.2f,%.2f]",
                i, d->vertex_count, d->state.texmap[0], d->state.texmap[1],
                d->state.blend_type, d->state.blend_src, d->state.blend_dst,
                d->state.z_enable, d->state.z_func,
                (int) d->state.num_texgens, min_x, max_x, min_y, max_y);
        {
            int g;
            for (g = 0; g < (int) d->state.num_texgens && g < 8; ++g) {
                fprintf(stderr, " %d:%d/m%u/p%u", (int) d->state.texgen[g].type,
                        (int) d->state.texgen[g].src,
                        (unsigned) d->state.texgen[g].mtx_id,
                        (unsigned) d->state.texgen[g].postmtx);
            }
        }
        fprintf(stderr, "\n");
        {
            int ch;
            fprintf(stderr, "    chan:");
            for (ch = 0; ch < 2; ++ch) {
                fprintf(stderr, " ch%d[en=%d attn=%d diff=%d mask=0x%x]", ch,
                        (int) d->state.ch_enable[ch],
                        (int) d->state.ch_attn_fn[ch],
                        (int) d->state.ch_diff_fn[ch],
                        (unsigned) d->state.ch_light_mask[ch]);
            }
            fprintf(stderr, "\n");
            /* colup=0 makes a draw invisible however well lit it is; that is
             * what G-148 looked like from the outside. */
            fprintf(stderr,
                    "    state: ztex_op=%u cull=%u colup=%u "
                    "scissor=%u,%u,%ux%u vp=%.0f,%.0f,%.0fx%.0f\n",
                    (unsigned) d->state.ztex_op,
                    (unsigned) d->state.cull_mode,
                    (unsigned) d->state.color_update,
                    (unsigned) d->state.scissor_x,
                    (unsigned) d->state.scissor_y,
                    (unsigned) d->state.scissor_w,
                    (unsigned) d->state.scissor_h, d->state.viewport[0],
                    d->state.viewport[1], d->state.viewport[2],
                    d->state.viewport[3]);
            for (ch = 0; ch < 8; ++ch) {
                unsigned m = d->state.ch_light_mask[0] |
                             d->state.ch_light_mask[1];
                const GxHleLight* l = &d->state.lights[ch];
                if ((m & (1u << ch)) == 0) {
                    continue;
                }
                fprintf(stderr,
                        "    light%d: col=%u,%u,%u a=%.2f,%.2f,%.2f "
                        "k=%.2f,%.2f,%.2f pos=%.0f,%.0f,%.0f "
                        "dir=%.2f,%.2f,%.2f\n",
                        ch, l->color.r, l->color.g, l->color.b, l->a[0],
                        l->a[1], l->a[2], l->k[0], l->k[1], l->k[2],
                        l->pos[0], l->pos[1], l->pos[2], l->dir[0], l->dir[1],
                        l->dir[2]);
            }
            {
                size_t vv;
                fprintf(stderr, "    pos:");
                for (vv = d->first_vertex;
                     vv < d->first_vertex + d->vertex_count &&
                     vv < d->first_vertex + 9;
                     ++vv)
                {
                    const GxHleVertex* p = &vertices[vv];
                    float w = p->clip[3] != 0.0f ? p->clip[3] : 1.0f;
                    fprintf(stderr, " (%.3f,%.3f)", p->clip[0] / w,
                            p->clip[1] / w);
                }
                fprintf(stderr, "\n");
            }
        }
        if (d->state.texmap[0] >= 0 &&
            (size_t) d->state.texmap[0] < texture_count)
        {
            const GxHleTexture* t = &textures[d->state.texmap[0]];
            fprintf(stderr, "    tex0: %p %ux%u fmt=%u pal=%p\n", t->image,
                    t->width, t->height, t->format, t->palette);
        }
        if (d->state.texmap[1] >= 0 &&
            (size_t) d->state.texmap[1] < texture_count)
        {
            const GxHleTexture* t = &textures[d->state.texmap[1]];
            fprintf(stderr, "    tex1: %p %ux%u fmt=%u pal=%p\n", t->image,
                    t->width, t->height, t->format, t->palette);
        }
        if (d->state.blend_type != 0 || d->state.num_stages > 1) {
            int stage;
            fprintf(stderr,
                    "    alpha: tev C0=%.3f C1=%.3f C2=%.3f K0=%.3f "
                    "ras0_amb=%.3f ras0_mat=%.3f chan=%u/%u\n",
                    d->state.tev_color[1][3], d->state.tev_color[2][3],
                    d->state.tev_color[3][3], d->state.tev_kcolor[0][3],
                    d->state.ch_amb[2][3], d->state.ch_mat[2][3],
                    d->state.ch_enable[2], d->state.ch_mat_src[2]);
            fprintf(stderr,
                    "    ch0: en=%u amb_src=%u mat_src=%u mask=0x%x "
                    "amb=%.3f,%.3f,%.3f,%.3f mat=%.3f,%.3f,%.3f,%.3f "
                    "K0rgb=%.3f,%.3f,%.3f\n",
                    d->state.ch_enable[0], d->state.ch_amb_src[0],
                    d->state.ch_mat_src[0], d->state.ch_light_mask[0],
                    d->state.ch_amb[0][0], d->state.ch_amb[0][1],
                    d->state.ch_amb[0][2], d->state.ch_amb[0][3],
                    d->state.ch_mat[0][0], d->state.ch_mat[0][1],
                    d->state.ch_mat[0][2], d->state.ch_mat[0][3],
                    d->state.tev_kcolor[0][0], d->state.tev_kcolor[0][1],
                    d->state.tev_kcolor[0][2]);
            if (d->vertex_count != 0) {
                const GxHleVertex* v0 = &vertices[d->first_vertex];
                fprintf(stderr,
                        "    v0: color=%u,%u,%u,%u has_color=%.0f "
                        "uv0=%.3f,%.3f uv1=%.3f,%.3f nrm=%.2f,%.2f,%.2f\n",
                        v0->color[0], v0->color[1], v0->color[2], v0->color[3],
                        (double) v0->has_color, (double) v0->uv[0][0],
                        (double) v0->uv[0][1], (double) v0->uv[1][0],
                        (double) v0->uv[1][1], (double) v0->nrm[0],
                        (double) v0->nrm[1], (double) v0->nrm[2]);
                if (d->vertex_count == 6) {
                    size_t k;
                    fprintf(stderr, "    quads:");
                    for (k = 0; k < 6; ++k) {
                        const GxHleVertex* vk = &vertices[d->first_vertex + k];
                        fprintf(stderr, " (%.3f,%.3f|%.3f,%.3f)",
                                (double) vk->uv[0][0], (double) vk->uv[0][1],
                                (double) vk->uv[1][0], (double) vk->uv[1][1]);
                    }
                    fprintf(stderr, "\n");
                }
            }
            for (stage = 0; stage < d->state.num_stages &&
                            stage < GX_HLE_MAX_STAGES;
                 ++stage)
            {
                const GxHleTevStage* s = &d->state.stages[stage];
                fprintf(stderr,
                        "    tev%d order=%u/%u/%u cin=%u,%u,%u,%u "
                        "ain=%u,%u,%u,%u aop=%u/%u/%u/%u reg=%u/%u "
                        "kc=%u ka=%u\n",
                        stage, s->order_coord, s->order_map, s->order_chan,
                        s->color_a, s->color_b, s->color_c, s->color_d,
                        s->alpha_a, s->alpha_b, s->alpha_c, s->alpha_d,
                        s->alpha_op, s->alpha_bias, s->alpha_scale,
                        s->alpha_clamp, s->color_reg, s->alpha_reg,
                        s->kc_sel, s->ka_sel);
            }
        }
    }
}
