#pragma once

#include <glad/glad.h>

class ViewportController {
public:
    void setSize(int width, int height) {
        m_width = width;
        m_height = height;
        glViewport(0, 0, width, height);
    }
    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    int m_width{0};
    int m_height{0};
};

