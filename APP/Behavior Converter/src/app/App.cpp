#include "app/App.h"
#include "ui/ConverterUI.h"

#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <cstdio>

static void GlfwErrorCallback(int error, const char* desc) {
    fprintf(stderr, "GLFW error %d: %s\n", error, desc);
}

App::App()  = default;
App::~App() {
    if (!m_window) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

bool App::Init(const char* title, int width, int height) {
    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit()) return false;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    m_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!m_window) { glfwTerminate(); return false; }

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1); // vsync

    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
        fprintf(stderr, "gladLoadGL failed\n");
        glfwDestroyWindow(m_window);
        glfwTerminate();
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename  = nullptr;   // a single-window tool needs no layout persistence

    ApplyStyle();

    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 450");
    return true;
}

void App::Run() {
    ConverterUI ui;
    while (!glfwWindowShouldClose(m_window) && !ui.WantsQuit()) {
        glfwPollEvents();
        BeginFrame();
        ui.Draw();
        EndFrame();
    }
}

void App::BeginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    int w, h;
    glfwGetFramebufferSize(m_window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.08f, 0.08f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void App::EndFrame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(m_window);
}

void App::ApplyStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding    = 4.0f;
    s.FrameRounding     = 3.0f;
    s.ScrollbarRounding = 3.0f;
    s.GrabRounding      = 3.0f;
    s.WindowBorderSize  = 1.0f;
    s.FramePadding      = ImVec2(8.0f, 5.0f);
    s.ItemSpacing       = ImVec2(8.0f, 6.0f);
    s.ScrollbarSize     = 12.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]       = ImVec4(0.13f, 0.14f, 0.15f, 1.00f);
    c[ImGuiCol_ChildBg]        = ImVec4(0.10f, 0.11f, 0.12f, 1.00f);
    c[ImGuiCol_FrameBg]        = ImVec4(0.18f, 0.19f, 0.22f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.26f, 0.30f, 1.00f);
    c[ImGuiCol_Button]         = ImVec4(0.22f, 0.28f, 0.42f, 1.00f);
    c[ImGuiCol_ButtonHovered]  = ImVec4(0.30f, 0.40f, 0.60f, 1.00f);
    c[ImGuiCol_ButtonActive]   = ImVec4(0.36f, 0.50f, 0.78f, 1.00f);
    c[ImGuiCol_Separator]      = ImVec4(0.28f, 0.30f, 0.35f, 0.80f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.40f, 0.72f, 0.35f);
}
