#include "App.h"
#include "Screen.h"

#include <iostream>
#include <string>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glog/logging.h>

#include "core/Core.h"

App::App(int width, int height, const char *title)
    : m_width(width), m_height(height)
{
    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        throw std::runtime_error("GLFW init failed");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Append build configuration to window title
#if defined(_DEBUG) || !defined(NDEBUG)
    constexpr const char *kBuildCfg = "Debug";
#else
    constexpr const char *kBuildCfg = "Release";
#endif
    std::string finalTitle = std::string(title) + " - " + kBuildCfg;
    m_window = glfwCreateWindow(width, height, finalTitle.c_str(), nullptr, nullptr);
    if (!m_window)
    {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        throw std::runtime_error("Window creation failed");
    }

    glfwMakeContextCurrent(m_window);
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferSizeCallback);
    glfwSetMouseButtonCallback(m_window, mouseButtonCallback);
    glfwSetCursorPosCallback(m_window, cursorPosCallback);
    glfwSetScrollCallback(m_window, scrollCallback);
    glfwSetKeyCallback(m_window, keyCallback);
    glfwSetCharCallback(m_window, charCallback);
    glfwSetDropCallback(m_window, dropCallback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        glfwDestroyWindow(m_window);
        glfwTerminate();
        throw std::runtime_error("GLAD init failed");
    }

    glfwSwapInterval(1);
    glViewport(0, 0, width, height);
    glClearColor(0.1f, 0.1f, 0.12f, 1.0f);

    // ImGui setup
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(m_window, false);
    ImGui_ImplOpenGL3_Init("#version 330 core");
}

App::~App()
{
    // ImGui shutdown
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (m_window)
    {
        glfwDestroyWindow(m_window);
    }
    glfwTerminate();
}

void App::run(IScreen &screen)
{
    m_activeScreen = &screen;
    screen.onAttach(*this);
    screen.onResize(m_width, m_height);

    double lastTime = glfwGetTime();
    while (!glfwWindowShouldClose(m_window))
    {
        double now = glfwGetTime();
        double dt = now - lastTime;
        lastTime = now;

        // New ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        screen.onUpdate(dt);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        screen.onRender();
        screen.onGui();

        // Render ImGui
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(m_window);
        glfwPollEvents();

        ImGuiIO &io = ImGui::GetIO();
        if (!io.WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            glfwSetWindowShouldClose(m_window, GLFW_TRUE); // or your platform’s close call
        }
    }

    screen.onDetach();
    m_activeScreen = nullptr;
}

void App::framebufferSizeCallback(GLFWwindow *window, int width, int height)
{
    LOG(INFO) << "Framebuffer resized to " << width << "x" << height;
    auto *app = reinterpret_cast<App *>(glfwGetWindowUserPointer(window));
    if (!app)
        return;
    app->m_width = width;
    app->m_height = height;
    app->m_activeScreen->onResize(width, height);
}

void App::mouseButtonCallback(GLFWwindow *window, int button, int action, int mods)
{
    auto *app = reinterpret_cast<App *>(glfwGetWindowUserPointer(window));
    if (!app)
        return;
    ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
    if (!app->m_activeScreen)
        return;
    ImGuiIO &io = ImGui::GetIO();
    if (io.WantCaptureMouse)
        return;
    double x = 0.0, y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    app->m_activeScreen->onMouseButton(button, action, mods, Vec2{static_cast<float>(x), static_cast<float>(y)});
}

void App::cursorPosCallback(GLFWwindow *window, double xpos, double ypos)
{
    auto *app = reinterpret_cast<App *>(glfwGetWindowUserPointer(window));
    if (!app || !app->m_activeScreen)
        return;
    ImGuiIO &io = ImGui::GetIO();
    if (io.WantCaptureMouse)
        return;
    app->m_activeScreen->onCursorPos(Vec2{static_cast<float>(xpos), static_cast<float>(ypos)});
}

void App::scrollCallback(GLFWwindow *window, double xoffset, double yoffset)
{
    auto *app = reinterpret_cast<App *>(glfwGetWindowUserPointer(window));
    if (!app)
        return;
    ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
    if (!app->m_activeScreen)
        return;
    ImGuiIO &io = ImGui::GetIO();
    if (io.WantCaptureMouse)
        return;
    double x = 0.0, y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    app->m_activeScreen->onScroll(xoffset, yoffset, Vec2{static_cast<float>(x), static_cast<float>(y)});
}

void App::keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    auto *app = reinterpret_cast<App *>(glfwGetWindowUserPointer(window));
    (void)app;
    ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
}

void App::charCallback(GLFWwindow *window, unsigned int c)
{
    auto *app = reinterpret_cast<App *>(glfwGetWindowUserPointer(window));
    (void)app;
    ImGui_ImplGlfw_CharCallback(window, c);
}

void App::dropCallback(GLFWwindow *window, int count, const char **paths)
{
    auto *app = reinterpret_cast<App *>(glfwGetWindowUserPointer(window));
    if (!app || !app->m_activeScreen)
        return;
    std::vector<std::string> files;
    files.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        if (paths[i]) files.emplace_back(paths[i]);
    }
    app->m_activeScreen->onFilesDropped(files);
}
