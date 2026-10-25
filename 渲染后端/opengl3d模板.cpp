// 文语言（wen）· WEN-LANG-0.1
// 3D 渲染：OpenGL 4.6 core + GLSL 4.60 + 深度缓冲
#include <windows.h>
#include <GL/gl.h>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

typedef ptrdiff_t GLsizeiptr;
typedef char GLchar;
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW 0x88E0
#endif
#ifndef GL_DEPTH_TEST
#define GL_DEPTH_TEST 0x0B71
#endif

typedef GLuint(WINAPI* PFNGLCREATESHADERPROC)(GLenum);
typedef void(WINAPI* PFNGLSHADERSOURCEPROC)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void(WINAPI* PFNGLCOMPILESHADERPROC)(GLuint);
typedef void(WINAPI* PFNGLGETSHADERIVPROC)(GLuint, GLenum, GLint*);
typedef void(WINAPI* PFNGLGETSHADERINFOLOGPROC)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef GLuint(WINAPI* PFNGLCREATEPROGRAMPROC)(void);
typedef void(WINAPI* PFNGLATTACHSHADERPROC)(GLuint, GLuint);
typedef void(WINAPI* PFNGLLINKPROGRAMPROC)(GLuint);
typedef void(WINAPI* PFNGLGETPROGRAMIVPROC)(GLuint, GLenum, GLint*);
typedef void(WINAPI* PFNGLGETPROGRAMINFOLOGPROC)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void(WINAPI* PFNGLUSEPROGRAMPROC)(GLuint);
typedef void(WINAPI* PFNGLGENVERTEXARRAYSPROC)(GLsizei, GLuint*);
typedef void(WINAPI* PFNGLBINDVERTEXARRAYPROC)(GLuint);
typedef void(WINAPI* PFNGLGENBUFFERSPROC)(GLsizei, GLuint*);
typedef void(WINAPI* PFNGLBINDBUFFERPROC)(GLenum, GLuint);
typedef void(WINAPI* PFNGLBUFFERDATAPROC)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void(WINAPI* PFNGLVERTEXATTRIBPOINTERPROC)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void(WINAPI* PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint);
typedef GLint(WINAPI* PFNGLGETUNIFORMLOCATIONPROC)(GLuint, const GLchar*);
typedef void(WINAPI* PFNGLUNIFORMMATRIX4FVPROC)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void(WINAPI* PFNGLUNIFORM3FPROC)(GLint, GLfloat, GLfloat, GLfloat);

static PFNGLCREATESHADERPROC pglCreateShader;
static PFNGLSHADERSOURCEPROC pglShaderSource;
static PFNGLCOMPILESHADERPROC pglCompileShader;
static PFNGLGETSHADERIVPROC pglGetShaderiv;
static PFNGLGETSHADERINFOLOGPROC pglGetShaderInfoLog;
static PFNGLCREATEPROGRAMPROC pglCreateProgram;
static PFNGLATTACHSHADERPROC pglAttachShader;
static PFNGLLINKPROGRAMPROC pglLinkProgram;
static PFNGLGETPROGRAMIVPROC pglGetProgramiv;
static PFNGLGETPROGRAMINFOLOGPROC pglGetProgramInfoLog;
static PFNGLUSEPROGRAMPROC pglUseProgram;
static PFNGLGENVERTEXARRAYSPROC pglGenVertexArrays;
static PFNGLBINDVERTEXARRAYPROC pglBindVertexArray;
static PFNGLGENBUFFERSPROC pglGenBuffers;
static PFNGLBINDBUFFERPROC pglBindBuffer;
static PFNGLBUFFERDATAPROC pglBufferData;
static PFNGLVERTEXATTRIBPOINTERPROC pglVertexAttribPointer;
static PFNGLENABLEVERTEXATTRIBARRAYPROC pglEnableVertexAttribArray;
static PFNGLGETUNIFORMLOCATIONPROC pglGetUniformLocation;
static PFNGLUNIFORMMATRIX4FVPROC pglUniformMatrix4fv;
static PFNGLUNIFORM3FPROC pglUniform3f;

struct Face3 {
    std::vector<std::array<float, 3>> pts;
    float r = 0.3f, g = 0.6f, b = 1.0f;
};

static std::vector<Face3> faces = {
    // __SCENE3D__
};

static const int W = @W@;
static const int H = @H@;
static const float CAM = @CAM@;
static GLuint program = 0, vao = 0, vbo = 0;
static GLint locMVP = -1, locColor = -1;

template <typename T>
static T loadGL(const char* name) {
    T p = (T)wglGetProcAddress(name);
    if (!p) {
        HMODULE m = GetModuleHandleA("opengl32.dll");
        p = (T)GetProcAddress(m, name);
    }
    return p;
}

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = pglCreateShader(type);
    pglShaderSource(s, 1, &src, nullptr);
    pglCompileShader(s);
    GLint ok = 0;
    pglGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        pglGetShaderInfoLog(s, sizeof(log), nullptr, log);
        MessageBoxA(nullptr, log, "Shader compile failed", MB_OK);
        ExitProcess(1);
    }
    return s;
}

static void initGL() {
    pglCreateShader = loadGL<PFNGLCREATESHADERPROC>("glCreateShader");
    pglShaderSource = loadGL<PFNGLSHADERSOURCEPROC>("glShaderSource");
    pglCompileShader = loadGL<PFNGLCOMPILESHADERPROC>("glCompileShader");
    pglGetShaderiv = loadGL<PFNGLGETSHADERIVPROC>("glGetShaderiv");
    pglGetShaderInfoLog = loadGL<PFNGLGETSHADERINFOLOGPROC>("glGetShaderInfoLog");
    pglCreateProgram = loadGL<PFNGLCREATEPROGRAMPROC>("glCreateProgram");
    pglAttachShader = loadGL<PFNGLATTACHSHADERPROC>("glAttachShader");
    pglLinkProgram = loadGL<PFNGLLINKPROGRAMPROC>("glLinkProgram");
    pglGetProgramiv = loadGL<PFNGLGETPROGRAMIVPROC>("glGetProgramiv");
    pglGetProgramInfoLog = loadGL<PFNGLGETPROGRAMINFOLOGPROC>("glGetProgramInfoLog");
    pglUseProgram = loadGL<PFNGLUSEPROGRAMPROC>("glUseProgram");
    pglGenVertexArrays = loadGL<PFNGLGENVERTEXARRAYSPROC>("glGenVertexArrays");
    pglBindVertexArray = loadGL<PFNGLBINDVERTEXARRAYPROC>("glBindVertexArray");
    pglGenBuffers = loadGL<PFNGLGENBUFFERSPROC>("glGenBuffers");
    pglBindBuffer = loadGL<PFNGLBINDBUFFERPROC>("glBindBuffer");
    pglBufferData = loadGL<PFNGLBUFFERDATAPROC>("glBufferData");
    pglVertexAttribPointer = loadGL<PFNGLVERTEXATTRIBPOINTERPROC>("glVertexAttribPointer");
    pglEnableVertexAttribArray = loadGL<PFNGLENABLEVERTEXATTRIBARRAYPROC>("glEnableVertexAttribArray");
    pglGetUniformLocation = loadGL<PFNGLGETUNIFORMLOCATIONPROC>("glGetUniformLocation");
    pglUniformMatrix4fv = loadGL<PFNGLUNIFORMMATRIX4FVPROC>("glUniformMatrix4fv");
    pglUniform3f = loadGL<PFNGLUNIFORM3FPROC>("glUniform3f");

    const char* vsSrc = R"GLSL(#version 460 core
layout(location=0) in vec3 aPos;
uniform mat4 uMVP;
void main() { gl_Position = uMVP * vec4(aPos, 1.0); }
)GLSL";
    const char* fsSrc = R"GLSL(#version 460 core
uniform vec3 uColor;
out vec4 FragColor;
void main() { FragColor = vec4(uColor, 1.0); }
)GLSL";
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
    program = pglCreateProgram();
    pglAttachShader(program, vs);
    pglAttachShader(program, fs);
    pglLinkProgram(program);
    GLint ok = 0;
    pglGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        pglGetProgramInfoLog(program, sizeof(log), nullptr, log);
        MessageBoxA(nullptr, log, "Shader link failed", MB_OK);
        ExitProcess(1);
    }
    pglUseProgram(program);
    locMVP = pglGetUniformLocation(program, "uMVP");
    locColor = pglGetUniformLocation(program, "uColor");
    pglGenVertexArrays(1, &vao);
    pglGenBuffers(1, &vbo);
    pglBindVertexArray(vao);
    pglBindBuffer(GL_ARRAY_BUFFER, vbo);
}

static void mul(const float* a, const float* b, float* out) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + r] * b[c * 4 + k];
            out[c * 4 + r] = s;
        }
}

static void perspective(float* m, float fovy, float aspect, float zn, float zf) {
    float f = 1.0f / std::tan(fovy * 0.5f);
    for (int i = 0; i < 16; ++i) m[i] = 0;
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zf + zn) / (zn - zf);
    m[11] = -1;
    m[14] = (2 * zf * zn) / (zn - zf);
}

static void drawFace(const Face3& f, const float* mvp) {
    std::vector<float> data;
    data.reserve(f.pts.size() * 3);
    for (const auto& p : f.pts) {
        data.push_back(p[0]);
        data.push_back(p[1]);
        data.push_back(p[2]);
    }
    pglUniformMatrix4fv(locMVP, 1, GL_FALSE, mvp);
    pglUniform3f(locColor, f.r, f.g, f.b);
    pglBindVertexArray(vao);
    pglBindBuffer(GL_ARRAY_BUFFER, vbo);
    pglBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(data.size() * sizeof(float)), data.data(),
                  GL_STREAM_DRAW);
    pglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    pglEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_FAN, 0, (GLsizei)f.pts.size());
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

int main() {
    WNDCLASSA wc{};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "WenGL3D";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("WenGL3D", "Wen Render - 3D OpenGL", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              100, 100, W + 16, H + 39, nullptr, nullptr, wc.hInstance, nullptr);
    HDC dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    int pf = ChoosePixelFormat(dc, &pfd);
    SetPixelFormat(dc, pf, &pfd);
    HGLRC dummy = wglCreateContext(dc);
    wglMakeCurrent(dc, dummy);
    auto createCtx = (HGLRC(WINAPI*)(HDC, HGLRC, const int*))wglGetProcAddress("wglCreateContextAttribsARB");
    HGLRC rc = nullptr;
    if (createCtx) {
        const int attribs[] = {0x2091, 4, 0x2092, 6, 0};
        rc = createCtx(dc, nullptr, attribs);
    }
    if (!rc) rc = wglCreateContext(dc);
    if (!rc) {
        MessageBoxA(nullptr, "Failed to create OpenGL 4.6 context", "Wen Render", MB_OK);
        return 1;
    }
    wglMakeCurrent(dc, rc);
    if (dummy) wglDeleteContext(dummy);
    glViewport(0, 0, W, H);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearColor(0.05f, 0.07f, 0.10f, 1.0f);
    initGL();
    float proj[16], view[16], model[16], tmp[16], mvp[16];
    perspective(proj, 55.0f * 3.14159265f / 180.0f, (float)W / (float)H, 0.1f, 200.0f);
    float angle = 0;
    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        pglUseProgram(program);

        float cy = std::cos(angle), sy = std::sin(angle);
        float ry[16] = {cy, 0, -sy, 0, 0, 1, 0, 0, sy, 0, cy, 0, 0, 0, 0, 1};
        float cx = std::cos(angle * 0.37f), sx = std::sin(angle * 0.37f);
        float rx[16] = {1, 0, 0, 0, 0, cx, sx, 0, 0, -sx, cx, 0, 0, 0, 0, 1};
        mul(rx, ry, model);
        float tr[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -CAM, 1};
        mul(tr, model, view);
        mul(proj, view, tmp);
        mvp[0] = tmp[0]; mvp[1] = tmp[1]; mvp[2] = tmp[2]; mvp[3] = tmp[3];
        mvp[4] = tmp[4]; mvp[5] = tmp[5]; mvp[6] = tmp[6]; mvp[7] = tmp[7];
        mvp[8] = tmp[8]; mvp[9] = tmp[9]; mvp[10] = tmp[10]; mvp[11] = tmp[11];
        mvp[12] = tmp[12]; mvp[13] = tmp[13]; mvp[14] = tmp[14]; mvp[15] = tmp[15];

        for (const auto& f : faces) drawFace(f, mvp);
        SwapBuffers(dc);
        angle += 0.012f;
        Sleep(16);
    }
    return 0;
}
