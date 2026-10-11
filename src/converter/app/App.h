#pragma once

struct GLFWwindow;

// Minimal GLFW + Dear ImGui + OpenGL host that drives the ConverterUI.
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
