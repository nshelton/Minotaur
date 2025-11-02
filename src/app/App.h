#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <string>
#include <vector>

class IScreen;

class App {
public:
    App(int width, int height, const char* title);
    ~App();

    void run(IScreen& screen);

    GLFWwindow* window() const { return m_window; }
    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    GLFWwindow* m_window{nullptr};
    int m_width{0};
    int m_height{0};
    IScreen* m_activeScreen{nullptr};

    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void charCallback(GLFWwindow* window, unsigned int c);
    static void dropCallback(GLFWwindow* window, int count, const char** paths);
};
