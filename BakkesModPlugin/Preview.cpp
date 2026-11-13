#include "RewardTesterPlugin.h"
#include "imgui/imgui.h"
#include <windows.h>
#include <GL/gl.h>
#include <chrono>
#include <cstddef>
#include <windowsx.h>

void RewardTesterPlugin::LoadPreview() {
    client_ = std::make_unique<RewardClient>();
    client_->connected_ = true;
    client_->count_ = 3;
    client_->latest_ = {true, 0.08f, {"Ball Touch", "Speed Toward Ball", "Boost Use"},
                        {1.0f, 0.3f, -0.2f}, {0.05f, 0.2f, 0.15f}, {0.05f, 0.06f, -0.03f}};
    totalWeightedReward = 0.08f;
    rollingAvgValues = {0.2f, 0.28f, -0.15f};
    termHistory = {{0, 0, 1, 0, 0, 1}, {0.1f, 0.2f, 0.3f, 0.35f, 0.3f},
                   {-0.1f, -0.15f, -0.2f, -0.15f}};
    client_->tasks_ = {
        {"wall", "Wall pinch", "Pinch the ball against the side wall.", "Freeplay", "wall", 3, 1, 1},
        {"ground", "Ground pinch", "Pinch the ball against the floor.", "Freeplay", "ground", 3, 0, 0}};
    client_->lastAttempt_ = {1, "scored"};
    client_->clip_.folder = "Sample clips";
    client_->clip_.clips = {"generic/warmup.rlpr", "tasks/wall/attempt_1.rlpr"};
    watch.inReplay = true;
    watch.followedName = "Blue player";
    watch.followedIndex = 0;
    watch.frame = 390;
    watch.fps = 30;
    watch.cars = 2;
    watch.samples = 780;
}

namespace {

LRESULT CALLBACK WindowProc(HWND window, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_CLOSE) {
        PostQuitMessage(0);
        return 0;
    }
    auto& io = ImGui::GetIO();
    switch (msg) {
    case WM_MOUSEMOVE: io.MousePos = ImVec2(GET_X_LPARAM(l), GET_Y_LPARAM(l)); return 0;
    case WM_LBUTTONDOWN: SetCapture(window); io.MouseDown[0] = true; return 0;
    case WM_LBUTTONUP: io.MouseDown[0] = false; ReleaseCapture(); return 0;
    case WM_MOUSEWHEEL: io.MouseWheel += GET_WHEEL_DELTA_WPARAM(w) / 120.0f; return 0;
    case WM_KEYDOWN:
    case WM_KEYUP: if (w < 512) io.KeysDown[w] = msg == WM_KEYDOWN; return 0;
    case WM_CHAR: io.AddInputCharacter(static_cast<unsigned>(w)); return 0;
    case WM_KILLFOCUS:
        for (auto& key : io.KeysDown) key = false;
        io.MouseDown[0] = false;
        break;
    }
    return DefWindowProcW(window, msg, w, l);
}

void Draw(const ImDrawData& data) {
    glViewport(0, 0, static_cast<int>(data.DisplaySize.x), static_cast<int>(data.DisplaySize.y));
    glClearColor(.10f, .10f, .12f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, data.DisplaySize.x, data.DisplaySize.y, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_SCISSOR_TEST);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    for (int i = 0; i < data.CmdListsCount; ++i) {
        const auto& list = *data.CmdLists[i];
        for (const auto& cmd : list.CmdBuffer) {
            if (cmd.UserCallback) continue;
            const auto* vertices = reinterpret_cast<const char*>(list.VtxBuffer.Data + cmd.VtxOffset);
            glVertexPointer(2, GL_FLOAT, sizeof(ImDrawVert), vertices + offsetof(ImDrawVert, pos));
            glTexCoordPointer(2, GL_FLOAT, sizeof(ImDrawVert), vertices + offsetof(ImDrawVert, uv));
            glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(ImDrawVert), vertices + offsetof(ImDrawVert, col));
            const auto& r = cmd.ClipRect;
            glScissor(static_cast<int>(r.x), static_cast<int>(data.DisplaySize.y - r.w),
                      static_cast<int>(r.z - r.x), static_cast<int>(r.w - r.y));
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(reinterpret_cast<uintptr_t>(cmd.TextureId)));
            glDrawElements(GL_TRIANGLES, cmd.ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                           list.IdxBuffer.Data + cmd.IdxOffset);
        }
    }
    glDisable(GL_SCISSOR_TEST);
}

}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
    ImGui::CreateContext();
    WNDCLASSW cls{};
    cls.style = CS_OWNDC;
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
    cls.lpszClassName = L"RewardTesterPreview";
    if (!RegisterClassW(&cls)) return 1;
    const HWND window = CreateWindowW(cls.lpszClassName, L"Reward Tester Preview (sample data)",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1000, 820, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    const HDC dc = GetDC(window);
    PIXELFORMATDESCRIPTOR format{};
    format.nSize = sizeof(format);
    format.nVersion = 1;
    format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    format.iPixelType = PFD_TYPE_RGBA;
    format.cColorBits = 32;
    if (!SetPixelFormat(dc, ChoosePixelFormat(dc, &format), &format)) return 1;
    const HGLRC context = wglCreateContext(dc);
    if (!context || !wglMakeCurrent(dc, context)) return 1;

    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.KeyMap[ImGuiKey_Tab] = VK_TAB;
    io.KeyMap[ImGuiKey_LeftArrow] = VK_LEFT;
    io.KeyMap[ImGuiKey_RightArrow] = VK_RIGHT;
    io.KeyMap[ImGuiKey_UpArrow] = VK_UP;
    io.KeyMap[ImGuiKey_DownArrow] = VK_DOWN;
    io.KeyMap[ImGuiKey_Home] = VK_HOME;
    io.KeyMap[ImGuiKey_End] = VK_END;
    io.KeyMap[ImGuiKey_Delete] = VK_DELETE;
    io.KeyMap[ImGuiKey_Backspace] = VK_BACK;
    io.KeyMap[ImGuiKey_Enter] = VK_RETURN;
    io.KeyMap[ImGuiKey_Escape] = VK_ESCAPE;
    io.KeyMap[ImGuiKey_A] = 'A';
    io.KeyMap[ImGuiKey_Z] = 'Z';

    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    GLuint font;
    glGenTextures(1, &font);
    glBindTexture(GL_TEXTURE_2D, font);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    io.Fonts->TexID = reinterpret_cast<void*>(static_cast<uintptr_t>(font));
    RewardTesterPlugin preview;
    preview.LoadPreview();
    ShowWindow(window, SW_SHOW);
    auto previous = std::chrono::steady_clock::now();
    MSG msg{};
    while (msg.message != WM_QUIT) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (msg.message == WM_QUIT) break;
        RECT rect{};
        GetClientRect(window, &rect);
        io.DisplaySize = ImVec2(static_cast<float>(rect.right), static_cast<float>(rect.bottom));
        if (!IsIconic(window) && rect.right > 0 && rect.bottom > 0) {
            const auto now = std::chrono::steady_clock::now();
            io.DeltaTime = std::chrono::duration<float>(now - previous).count();
            previous = now;
            io.KeyCtrl = io.KeysDown[VK_CONTROL];
            io.KeyShift = io.KeysDown[VK_SHIFT];
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            preview.Render();
            ImGui::Render();
            Draw(*ImGui::GetDrawData());
            SwapBuffers(dc);
        }
        Sleep(15);
    }
    glDeleteTextures(1, &font);
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(context);
    ReleaseDC(window, dc);
    DestroyWindow(window);
    ImGui::DestroyContext();
    return 0;
}
