#pragma once

struct GLFWwindow;

// Minimal GLFW + Dear ImGui + OpenGL host (mirrors the Scene Editor's App), but drives
// the ConverterUI instead of the full editor layout. No renderer, no DotNetHost.
class App {
public:
    App();
    ~App();

    bool Init(const char* title, int width, int height);
    void Run();

private:
    void BeginFrame();
    void EndFrame();
    void ApplyStyle();

    GLFWwindow* m_window = nullptr;
};
