// 文语言（wen）· WEN-LANG-0.1
// 由「文」渲染库生成：OpenGL 4.6 core profile 窗口（Win32/WGL + GLSL 4.60）
// 编译：g++ -std=c++17 -O2 本文件.cpp -o 本文件.exe -lopengl32 -lgdi32
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
#ifndef GL_PROGRAM_POINT_SIZE
#define GL_PROGRAM_POINT_SIZE 0x8642
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
typedef void(WINAPI* PFNGLUNIFORM1IPROC)(GLint, GLint);
typedef void(WINAPI* PFNGLUNIFORM2FPROC)(GLint, GLfloat, GLfloat);

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
static PFNGLUNIFORM1IPROC pglUniform1i;
static PFNGLUNIFORM2FPROC pglUniform2f;

static const int W = @W@;
static const int H = @H@;
static GLuint program = 0, vao = 0, vbo = 0;
static GLint locSize = -1, locPoint = -1;

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
    pglUniform1i = loadGL<PFNGLUNIFORM1IPROC>("glUniform1i");
    pglUniform2f = loadGL<PFNGLUNIFORM2FPROC>("glUniform2f");

    const char* vsSrc = R"GLSL(#version 460 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec4 aColor;
uniform vec2 uSize;
out vec4 vColor;
void main() {
    vec2 p = vec2(aPos.x / uSize.x * 2.0 - 1.0, 1.0 - aPos.y / uSize.y * 2.0);
    gl_Position = vec4(p, 0.0, 1.0);
    gl_PointSize = 9.0;
    vColor = aColor;
}
)GLSL";
    const char* fsSrc = R"GLSL(#version 460 core
in vec4 vColor;
uniform int uPoint;
out vec4 FragColor;
void main() {
    if (uPoint == 1) {
        vec2 d = gl_PointCoord - vec2(0.5);
        if (dot(d, d) > 0.25) discard;
    }
    FragColor = vColor;
}
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
    locSize = pglGetUniformLocation(program, "uSize");
    locPoint = pglGetUniformLocation(program, "uPoint");
    pglUniform2f(locSize, (float)W, (float)H);
    pglUniform1i(locPoint, 0);
    pglGenVertexArrays(1, &vao);
    pglGenBuffers(1, &vbo);
    pglBindVertexArray(vao);
    pglBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnable(GL_PROGRAM_POINT_SIZE);
}

static void drawData(const std::vector<float>& data, GLenum mode, bool points) {
    if (data.empty()) return;
    pglUseProgram(program);
    pglUniform1i(locPoint, points ? 1 : 0);
    pglBindVertexArray(vao);
    pglBindBuffer(GL_ARRAY_BUFFER, vbo);
    pglBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(data.size() * sizeof(float)), data.data(),
                  GL_STREAM_DRAW);
    pglVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    pglEnableVertexAttribArray(0);
    pglVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                           (void*)(2 * sizeof(float)));
    pglEnableVertexAttribArray(1);
    glDrawArrays(mode, 0, (GLsizei)(data.size() / 6));
}

static void push(std::vector<float>& v, float x, float y, float r, float g, float b, float a) {
    v.push_back(x);
    v.push_back(y);
    v.push_back(r);
    v.push_back(g);
    v.push_back(b);
    v.push_back(a);
}

static float bboxT(const std::vector<std::array<float, 2>>& p, float x, float y, int dir) {
    float minx = p[0][0], maxx = p[0][0], miny = p[0][1], maxy = p[0][1];
    for (const auto& q : p) {
        minx = std::fmin(minx, q[0]);
        maxx = std::fmax(maxx, q[0]);
        miny = std::fmin(miny, q[1]);
        maxy = std::fmax(maxy, q[1]);
    }
    if (dir == 0) return (maxx > minx) ? (x - minx) / (maxx - minx) : 0.0f;
    if (dir == 1) return (maxy > miny) ? (y - miny) / (maxy - miny) : 0.0f;
    float tx = (maxx > minx) ? (x - minx) / (maxx - minx) : 0.0f;
    float ty = (maxy > miny) ? (y - miny) / (maxy - miny) : 0.0f;
    return 0.5f * (tx + ty);
}

static void drawGradient(const std::vector<std::array<float, 2>>& p, float r1, float g1, float b1,
                         float r2, float g2, float b2, int dir) {
    std::vector<float> data;
    for (size_t i = 1; i + 1 < p.size(); ++i) {
        for (int k = 0; k < 3; ++k) {
            const auto& q = p[k == 0 ? 0 : (k == 1 ? i : i + 1)];
            float t = bboxT(p, q[0], q[1], dir);
            push(data, q[0], q[1], r1 + (r2 - r1) * t, g1 + (g2 - g1) * t,
                 b1 + (b2 - b1) * t, 1.0f);
        }
    }
    drawData(data, GL_TRIANGLES, false);
}

static void drawLight(const std::vector<std::array<float, 2>>& p, float lx, float ly,
                      float strength, bool volumetric) {
    std::vector<float> data;
    for (size_t i = 1; i + 1 < p.size(); ++i) {
        for (int k = 0; k < 3; ++k) {
            const auto& q = p[k == 0 ? 0 : (k == 1 ? i : i + 1)];
            float d = std::sqrt((q[0] - lx) * (q[0] - lx) + (q[1] - ly) * (q[1] - ly));
            float scale = volumetric ? 220.0f : 120.0f;
            float a = strength * std::exp(-d / scale);
            if (volumetric) a += 0.35f * strength * std::exp(-d / (scale * 2.2f));
            if (a > 1.0f) a = 1.0f;
            push(data, q[0], q[1], 0.06f + a, 0.09f + a, 0.14f + a, 1.0f);
        }
    }
    drawData(data, GL_TRIANGLES, false);
}

static void drawDoodle(const std::vector<std::array<float, 2>>& p,
                       const std::vector<std::array<float, 2>>& dots) {
    std::vector<float> face;
    for (size_t i = 1; i + 1 < p.size(); ++i) {
        for (int k = 0; k < 3; ++k) {
            const auto& q = p[k == 0 ? 0 : (k == 1 ? i : i + 1)];
            push(face, q[0], q[1], 0.98f, 0.98f, 0.98f, 1.0f);
        }
    }
    drawData(face, GL_TRIANGLES, false);
    std::vector<float> pts;
    for (const auto& q : dots) push(pts, q[0], q[1], 0.15f, 0.15f, 0.15f, 1.0f);
    drawData(pts, GL_POINTS, true);
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
    wc.lpszClassName = "WenGL46";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("WenGL46", "Wen Render - OpenGL 4.6", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              100, 100, W + 16, H + 39, nullptr, nullptr, wc.hInstance, nullptr);
    HDC dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
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
    glClearColor(0.98f, 0.98f, 0.98f, 1.0f);
    initGL();
    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        glClear(GL_COLOR_BUFFER_BIT);
        // __SCENE__
        SwapBuffers(dc);
        Sleep(16);
    }
    return 0;
}
