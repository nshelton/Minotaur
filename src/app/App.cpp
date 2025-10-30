#include "App.h"
#include "Screen.h"

#include <iostream>

App::App(int width, int height, const char* title)
    : m_width(width), m_height(height) {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        throw std::runtime_error("GLFW init failed");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    m_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!m_window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        throw std::runtime_error("Window creation failed");
    }

    glfwMakeContextCurrent(m_window);
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferSizeCallback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        glfwDestroyWindow(m_window);
        glfwTerminate();
        throw std::runtime_error("GLAD init failed");
    }

    glfwSwapInterval(1);
    glViewport(0, 0, width, height);
    glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
}

App::~App() {
    if (m_window) {
        glfwDestroyWindow(m_window);
    }
    glfwTerminate();
}

void App::run(IScreen& screen) {
    m_activeScreen = &screen;
    screen.onAttach(*this);
    screen.onResize(m_width, m_height);

    double lastTime = glfwGetTime();
    while (!glfwWindowShouldClose(m_window)) {
        double now = glfwGetTime();
        double dt = now - lastTime;
        lastTime = now;

        screen.onUpdate(dt);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        screen.onRender();

        glfwSwapBuffers(m_window);
        glfwPollEvents();
    }

    screen.onDetach();
    m_activeScreen = nullptr;
}

void App::framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    auto* app = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));
    if (!app) return;
    app->m_width = width;
    app->m_height = height;
    if (app->m_activeScreen) {
        app->m_activeScreen->onResize(width, height);
    }
}

