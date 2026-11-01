// GTAVGraphics v2 - plugin post-processing "GTA 5 look" untuk GTA SA Android 32-bit (armeabi-v7a)
// Loader : AML (Android Mod Loader) by RusJJ
// Cara kerja: hook eglSwapBuffers -> salin frame ke texture -> gambar ulang lewat fragment shader
// Efek: FXAA ringan, bloom 2 skala, tone mapping ACES, split-toning (teal/orange), saturasi,
//       kontras, film grain halus, vignette. Semua bisa diatur lewat file .ini (tanpa build ulang).

#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>

#include <dlfcn.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <string.h>

MYMODCFG(net.gtavgraphics.postfx, GTAV Graphics, 2.0, YourName)

// ====================== PENGATURAN (dibaca dari .ini) ======================
struct Settings
{
    bool  enabled      = true;
    bool  fxaa         = true;
    float exposure     = 1.10f;
    float contrast     = 1.10f;
    float saturation   = 1.15f;
    float bloomAmount  = 0.35f;
    float bloomThresh  = 0.70f;
    float sharpen      = 0.25f;
    float vignette     = 0.25f;
    float grain        = 0.02f;
    float shadowTint[3]    = { 0.92f, 1.00f, 1.06f }; // bayangan condong teal
    float highlightTint[3] = { 1.06f, 1.00f, 0.93f }; // highlight condong hangat
    float tintStrength = 0.50f;
};
static Settings S;

static void LoadSettings()
{
    S.enabled      = cfg->GetBool ("Enabled",       S.enabled,      "General");
    S.fxaa         = cfg->GetBool ("FXAA",          S.fxaa,         "General");
    S.exposure     = cfg->GetFloat("Exposure",      S.exposure,     "Color");
    S.contrast     = cfg->GetFloat("Contrast",      S.contrast,     "Color");
    S.saturation   = cfg->GetFloat("Saturation",    S.saturation,   "Color");
    S.tintStrength = cfg->GetFloat("TintStrength",  S.tintStrength, "Color");
    S.bloomAmount  = cfg->GetFloat("BloomAmount",   S.bloomAmount,  "Bloom");
    S.bloomThresh  = cfg->GetFloat("BloomThreshold",S.bloomThresh,  "Bloom");
    S.sharpen      = cfg->GetFloat("Sharpen",       S.sharpen,      "Detail");
    S.vignette     = cfg->GetFloat("Vignette",      S.vignette,     "Detail");
    S.grain        = cfg->GetFloat("FilmGrain",     S.grain,        "Detail");
    cfg->Save();
}
// ===========================================================================

static const char* VS_SRC =
    "attribute vec2 aPos;\n"
    "varying vec2 vUV;\n"
    "void main(){ vUV = aPos*0.5+0.5; gl_Position = vec4(aPos,0.0,1.0); }\n";

static const char* FS_SRC =
    "precision mediump float;\n"
    "varying vec2 vUV;\n"
    "uniform sampler2D uTex;\n"
    "uniform vec2  uTexel;\n"
    "uniform float uFxaa, uExposure, uContrast, uSaturation;\n"
    "uniform float uBloom, uThresh, uSharpen, uVignette, uGrain, uTintStr, uTime;\n"
    "uniform vec3  uShadowTint, uHighTint;\n"
    "\n"
    "float luma(vec3 c){ return dot(c, vec3(0.299,0.587,0.114)); }\n"
    "vec3 aces(vec3 x){\n"
    "  return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0);\n"
    "}\n"
    "vec3 bright(vec2 uv){\n"
    "  vec3 c = texture2D(uTex, uv).rgb;\n"
    "  return c * smoothstep(uThresh, 1.0, luma(c));\n"
    "}\n"
    "float hash(vec2 p){ return fract(sin(dot(p, vec2(12.9898,78.233))) * 43758.5453); }\n"
    "\n"
    "void main(){\n"
    "  vec3 c = texture2D(uTex, vUV).rgb;\n"
    "  vec3 cN = texture2D(uTex, vUV + vec2(0.0, uTexel.y)).rgb;\n"
    "  vec3 cS = texture2D(uTex, vUV - vec2(0.0, uTexel.y)).rgb;\n"
    "  vec3 cE = texture2D(uTex, vUV + vec2(uTexel.x, 0.0)).rgb;\n"
    "  vec3 cW = texture2D(uTex, vUV - vec2(uTexel.x, 0.0)).rgb;\n"
    "\n"
    "  // FXAA ringan: blur hanya di tepi dengan kontras luma tinggi\n"
    "  if(uFxaa > 0.5){\n"
    "    float lM = luma(c), lN = luma(cN), lS = luma(cS), lE = luma(cE), lW = luma(cW);\n"
    "    float lMin = min(lM, min(min(lN,lS), min(lE,lW)));\n"
    "    float lMax = max(lM, max(max(lN,lS), max(lE,lW)));\n"
    "    float range = lMax - lMin;\n"
    "    if(range > max(0.05, lMax*0.125)){\n"
    "      vec3 avg = (cN + cS + cE + cW) * 0.25;\n"
    "      c = mix(c, avg, clamp(range*2.0, 0.0, 0.6));\n"
    "    }\n"
    "  }\n"
    "\n"
    "  // Sharpen (unsharp mask)\n"
    "  if(uSharpen > 0.0){\n"
    "    c += (c - (cN+cS+cE+cW)*0.25) * uSharpen;\n"
    "  }\n"
    "\n"
    "  // Bloom 2 skala (dekat + jauh), 8 tap tiap skala\n"
    "  if(uBloom > 0.0){\n"
    "    vec3 b = vec3(0.0);\n"
    "    for(int s=0; s<2; s++){\n"
    "      vec2 r = uTexel * (s==0 ? 5.0 : 14.0);\n"
    "      vec3 t = bright(vUV + vec2( r.x, 0.0)) + bright(vUV + vec2(-r.x, 0.0))\n"
    "             + bright(vUV + vec2(0.0,  r.y)) + bright(vUV + vec2(0.0, -r.y))\n"
    "             + bright(vUV + r*0.7071)        + bright(vUV - r*0.7071)\n"
    "             + bright(vUV + vec2( r.x,-r.y)*0.7071) + bright(vUV + vec2(-r.x, r.y)*0.7071);\n"
    "      b += t * (s==0 ? 0.0625 : 0.0625);\n"
    "    }\n"
    "    c += b * uBloom;\n"
    "  }\n"
    "\n"
    "  // Exposure + tone mapping film\n"
    "  c = aces(c * uExposure);\n"
    "\n"
    "  // Split-toning: bayangan teal, highlight hangat\n"
    "  float l = luma(c);\n"
    "  vec3 tone = mix(uShadowTint, uHighTint, smoothstep(0.2, 0.8, l));\n"
    "  c *= mix(vec3(1.0), tone, uTintStr);\n"
    "\n"
    "  // Saturasi + kontras\n"
    "  l = luma(c);\n"
    "  c = mix(vec3(l), c, uSaturation);\n"
    "  c = (c - 0.5) * uContrast + 0.5;\n"
    "\n"
    "  // Vignette\n"
    "  vec2 d = vUV - 0.5;\n"
    "  c *= 1.0 - dot(d, d) * uVignette * 2.0;\n"
    "\n"
    "  // Film grain halus\n"
    "  c += (hash(vUV * (1.0/uTexel) + uTime) - 0.5) * uGrain;\n"
    "\n"
    "  gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);\n"
    "}\n";

// ---------- state ----------
static EGLBoolean (*eglSwapBuffers_orig)(EGLDisplay, EGLSurface) = nullptr;
static bool   g_Ready  = false;
static bool   g_Failed = false;
static GLuint g_Prog   = 0;
static GLuint g_Tex    = 0;
static int    g_W = 0, g_H = 0;
static unsigned g_Frame = 0;

static GLint uTex, uTexel, uFxaa, uExposure, uContrast, uSaturation, uBloom, uThresh;
static GLint uSharpen, uVignette, uGrain, uTintStr, uTime, uShadowTint, uHighTint;

static GLuint Compile(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        logger->Error("Shader error: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static bool InitGL()
{
    GLuint vs = Compile(GL_VERTEX_SHADER, VS_SRC);
    GLuint fs = Compile(GL_FRAGMENT_SHADER, FS_SRC);
    if (!vs || !fs) return false;

    g_Prog = glCreateProgram();
    glAttachShader(g_Prog, vs);
    glAttachShader(g_Prog, fs);
    glBindAttribLocation(g_Prog, 0, "aPos");
    glLinkProgram(g_Prog);

    GLint ok = 0;
    glGetProgramiv(g_Prog, GL_LINK_STATUS, &ok);
    if (!ok) { logger->Error("Link program gagal"); return false; }

    uTex        = glGetUniformLocation(g_Prog, "uTex");
    uTexel      = glGetUniformLocation(g_Prog, "uTexel");
    uFxaa       = glGetUniformLocation(g_Prog, "uFxaa");
    uExposure   = glGetUniformLocation(g_Prog, "uExposure");
    uContrast   = glGetUniformLocation(g_Prog, "uContrast");
    uSaturation = glGetUniformLocation(g_Prog, "uSaturation");
    uBloom      = glGetUniformLocation(g_Prog, "uBloom");
    uThresh     = glGetUniformLocation(g_Prog, "uThresh");
    uSharpen    = glGetUniformLocation(g_Prog, "uSharpen");
    uVignette   = glGetUniformLocation(g_Prog, "uVignette");
    uGrain      = glGetUniformLocation(g_Prog, "uGrain");
    uTintStr    = glGetUniformLocation(g_Prog, "uTintStr");
    uTime       = glGetUniformLocation(g_Prog, "uTime");
    uShadowTint = glGetUniformLocation(g_Prog, "uShadowTint");
    uHighTint   = glGetUniformLocation(g_Prog, "uHighTint");

    glGenTextures(1, &g_Tex);
    return true;
}

static void ApplyPostFX(EGLDisplay dpy, EGLSurface surf)
{
    if (!S.enabled || g_Failed) return;

    EGLint w = 0, h = 0;
    eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);
    if (w <= 0 || h <= 0) return;

    if (!g_Ready)
    {
        if (InitGL()) { g_Ready = true; logger->Info("PostFX siap (%dx%d)", w, h); }
        else          { g_Failed = true; return; }
    }

    // ---- simpan state GL milik game ----
    GLint prevProg, prevTex, prevActive, prevArrayBuf, prevElemBuf, prevFBO;
    GLint prevViewport[4];
    GLint prevAttr0 = 0, prevAttr1 = 0, prevAttr2 = 0;
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevCull  = glIsEnabled(GL_CULL_FACE);
    GLboolean prevScis  = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prevStenc = glIsEnabled(GL_STENCIL_TEST);
    GLboolean prevDither = glIsEnabled(GL_DITHER);
    GLboolean prevColorMask[4];

    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActive);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuf);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevElemBuf);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glGetBooleanv(GL_COLOR_WRITEMASK, prevColorMask);
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &prevAttr0);
    glGetVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &prevAttr1);
    glGetVertexAttribiv(2, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &prevAttr2);

    // ---- salin frame ke texture ----
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, g_Tex);
    if (w != g_W || h != g_H)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, w, h, 0);
        g_W = w; g_H = h;
    }
    else
    {
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
    }

    // ---- gambar quad layar penuh dengan shader ----
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DITHER);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glViewport(0, 0, w, h);

    glUseProgram(g_Prog);
    glUniform1i(uTex, 0);
    glUniform2f(uTexel, 1.0f / w, 1.0f / h);
    glUniform1f(uFxaa, S.fxaa ? 1.0f : 0.0f);
    glUniform1f(uExposure, S.exposure);
    glUniform1f(uContrast, S.contrast);
    glUniform1f(uSaturation, S.saturation);
    glUniform1f(uBloom, S.bloomAmount);
    glUniform1f(uThresh, S.bloomThresh);
    glUniform1f(uSharpen, S.sharpen);
    glUniform1f(uVignette, S.vignette);
    glUniform1f(uGrain, S.grain);
    glUniform1f(uTintStr, S.tintStrength);
    glUniform1f(uTime, (float)(g_Frame++ % 997) * 0.37f);
    glUniform3f(uShadowTint, S.shadowTint[0], S.shadowTint[1], S.shadowTint[2]);
    glUniform3f(uHighTint, S.highlightTint[0], S.highlightTint[1], S.highlightTint[2]);

    static const GLfloat quad[] = { -1,-1,  1,-1,  -1,1,  1,1 };
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // ---- kembalikan state game ----
    if (!prevAttr0) glDisableVertexAttribArray(0);
    if (prevAttr1)  glEnableVertexAttribArray(1);
    if (prevAttr2)  glEnableVertexAttribArray(2);
    glBindBuffer(GL_ARRAY_BUFFER, prevArrayBuf);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, prevElemBuf);
    glUseProgram(prevProg);
    glBindTexture(GL_TEXTURE_2D, prevTex);
    glActiveTexture(prevActive);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glColorMask(prevColorMask[0], prevColorMask[1], prevColorMask[2], prevColorMask[3]);
    if (prevBlend)  glEnable(GL_BLEND);
    if (prevDepth)  glEnable(GL_DEPTH_TEST);
    if (prevCull)   glEnable(GL_CULL_FACE);
    if (prevScis)   glEnable(GL_SCISSOR_TEST);
    if (prevStenc)  glEnable(GL_STENCIL_TEST);
    if (prevDither) glEnable(GL_DITHER);
}

static EGLBoolean eglSwapBuffers_hook(EGLDisplay dpy, EGLSurface surf)
{
    ApplyPostFX(dpy, surf);
    return eglSwapBuffers_orig(dpy, surf);
}

ON_MOD_LOAD()
{
    logger->SetTag("GTAVGraphics");
    LoadSettings();
    logger->Info("Plugin dimuat (enabled=%d)", (int)S.enabled);

    void* egl = dlopen("libEGL.so", RTLD_NOW);
    if (!egl) { logger->Error("libEGL.so tidak ketemu"); return; }

    void* swap = dlsym(egl, "eglSwapBuffers");
    if (!swap) { logger->Error("eglSwapBuffers tidak ketemu"); return; }

    aml->Hook(swap, (void*)eglSwapBuffers_hook, (void**)&eglSwapBuffers_orig);
    logger->Info("Hook eglSwapBuffers terpasang");
}
