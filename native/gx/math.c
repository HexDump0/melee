#include "gx/math.h"
#include <math.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Column-major 4x4 math.  The fixed-function matrix stack is gone in a core
 * profile, so transforms are built on the CPU and passed as uniforms.
 */
typedef float Mat4[16];

void m4_identity(Mat4 m)
{
    memset(m,0,sizeof(Mat4));
    m[0]=m[5]=m[10]=m[15]=1.0f;
}

/* out = a * b, so b is applied to the vector first (glMultMatrixf order). */
void m4_mul(Mat4 out,const Mat4 a,const Mat4 b)
{
    Mat4 r;
    int c,row,k;
    for(c=0;c<4;++c) {
        for(row=0;row<4;++row) {
            float sum=0.0f;
            for(k=0;k<4;++k) sum+=a[k*4+row]*b[c*4+k];
            r[c*4+row]=sum;
        }
    }
    memcpy(out,r,sizeof(Mat4));
}

void m4_ortho(Mat4 m,float l,float r,float b,float t,float n,float f)
{
    m4_identity(m);
    m[0]=2.0f/(r-l);
    m[5]=2.0f/(t-b);
    m[10]=-2.0f/(f-n);
    m[12]=-(r+l)/(r-l);
    m[13]=-(t+b)/(t-b);
    m[14]=-(f+n)/(f-n);
}

void m4_frustum(Mat4 m,float l,float r,float b,float t,float n,float f)
{
    m4_identity(m);
    m[0]=2.0f*n/(r-l);
    m[5]=2.0f*n/(t-b);
    m[8]=(r+l)/(r-l);
    m[9]=(t+b)/(t-b);
    m[10]=-(f+n)/(f-n);
    m[11]=-1.0f;
    m[14]=-2.0f*f*n/(f-n);
    m[15]=0.0f;
}

void m4_mul_translate(Mat4 m,float x,float y,float z)
{
    Mat4 t;
    m4_identity(t);
    t[12]=x;t[13]=y;t[14]=z;
    m4_mul(m,m,t);
}

void m4_mul_scale(Mat4 m,float x,float y,float z)
{
    Mat4 t;
    m4_identity(t);
    t[0]=x;t[5]=y;t[10]=z;
    m4_mul(m,m,t);
}

void m4_mul_rotate(Mat4 m,float angle_deg,float x,float y,float z)
{
    float a=angle_deg*PI/180.0f;
    float c=cosf(a),s=sinf(a),len=sqrtf(x*x+y*y+z*z);
    Mat4 r;
    if(len<1e-8f)return;
    x/=len;y/=len;z/=len;
    m4_identity(r);
    r[0]=c+x*x*(1-c);   r[4]=x*y*(1-c)-z*s; r[8]=x*z*(1-c)+y*s;
    r[1]=y*x*(1-c)+z*s; r[5]=c+y*y*(1-c);   r[9]=y*z*(1-c)-x*s;
    r[2]=z*x*(1-c)-y*s; r[6]=z*y*(1-c)+x*s; r[10]=c+z*z*(1-c);
    m4_mul(m,m,r);
}

void m4_look_at(Mat4 m,const float eye[3],const float target[3])
{
    float f[3]={target[0]-eye[0],target[1]-eye[1],target[2]-eye[2]};
    float s[3],u[3];
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
}

/* Upper-left 3x3 inverse transpose, matching glEnable(GL_NORMALIZE). */
void m4_normal_mtx(float out[9],const Mat4 mv)
{
    float c00=mv[5]*mv[10]-mv[9]*mv[6];
    float c01=mv[9]*mv[2]-mv[1]*mv[10];
    float c02=mv[1]*mv[6]-mv[5]*mv[2];
    float c10=mv[8]*mv[6]-mv[4]*mv[10];
    float c11=mv[0]*mv[10]-mv[8]*mv[2];
    float c12=mv[4]*mv[2]-mv[0]*mv[6];
    float c20=mv[4]*mv[9]-mv[8]*mv[5];
    float c21=mv[8]*mv[1]-mv[0]*mv[9];
    float c22=mv[0]*mv[5]-mv[4]*mv[1];
    float det=mv[0]*c00+mv[4]*c01+mv[8]*c02;
    if(fabsf(det)<1e-12f) {
        out[0]=mv[0];out[1]=mv[1];out[2]=mv[2];
        out[3]=mv[4];out[4]=mv[5];out[5]=mv[6];
        out[6]=mv[8];out[7]=mv[9];out[8]=mv[10];
        return;
    }
    /* normal = cofactor(M) / det (the transpose of inverse(M)). */
    out[0]=c00/det;out[1]=c10/det;out[2]=c20/det;
    out[3]=c01/det;out[4]=c11/det;out[5]=c21/det;
    out[6]=c02/det;out[7]=c12/det;out[8]=c22/det;
}

/* Direction (w = 0) transform plus normalize; the fixed-function light
 * position is transformed by the modelview at glLightfv time. */
void m4_transform_dir(float out[3],const Mat4 m,const float v[3])
{
    float len;
    out[0]=m[0]*v[0]+m[4]*v[1]+m[8]*v[2];
    out[1]=m[1]*v[0]+m[5]*v[1]+m[9]*v[2];
    out[2]=m[2]*v[0]+m[6]*v[1]+m[10]*v[2];
    len=sqrtf(out[0]*out[0]+out[1]*out[1]+out[2]*out[2]);
    if(len<1e-8f)len=1.0f;
    out[0]/=len;out[1]/=len;out[2]/=len;
}

void m4_transform_point(float out[3],const Mat4 m,const float v[3])
{
    out[0]=m[0]*v[0]+m[4]*v[1]+m[8]*v[2]+m[12];
    out[1]=m[1]*v[0]+m[5]*v[1]+m[9]*v[2]+m[13];
    out[2]=m[2]*v[0]+m[6]*v[1]+m[10]*v[2]+m[14];
}

