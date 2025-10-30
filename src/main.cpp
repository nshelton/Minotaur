#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <vector>

static void framebufferSizeCallback(GLFWwindow* /*window*/, int width, int height) {
	glViewport(0, 0, width, height);
}

static GLuint compileShader(GLenum type, const char* source) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, nullptr);
	glCompileShader(shader);
	GLint success = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success) {
		GLint logLen = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
		std::vector<char> log(static_cast<size_t>(logLen));
		glGetShaderInfoLog(shader, logLen, nullptr, log.data());
		std::cerr << "Shader compilation failed: " << log.data() << std::endl;
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

static GLuint linkProgram(GLuint vert, GLuint frag) {
	GLuint program = glCreateProgram();
	glAttachShader(program, vert);
	glAttachShader(program, frag);
	glLinkProgram(program);
	GLint success = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &success);
	if (!success) {
		GLint logLen = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
		std::vector<char> log(static_cast<size_t>(logLen));
		glGetProgramInfoLog(program, logLen, nullptr, log.data());
		std::cerr << "Program link failed: " << log.data() << std::endl;
		glDeleteProgram(program);
		return 0;
	}
	return program;
}

int main() {
	if (!glfwInit()) {
		std::cerr << "Failed to initialize GLFW" << std::endl;
		return 1;
	}

	// Request OpenGL 3.3 core profile
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	GLFWwindow* window = glfwCreateWindow(800, 600, "Minotaur Triangle", nullptr, nullptr);
	if (!window) {
		std::cerr << "Failed to create GLFW window" << std::endl;
		glfwTerminate();
		return 1;
	}
	glfwMakeContextCurrent(window);
	glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
		std::cerr << "Failed to initialize GLAD" << std::endl;
		glfwDestroyWindow(window);
		glfwTerminate();
		return 1;
	}

	// Vertex and fragment shaders (GLSL 330 core)
	const char* vertexShaderSrc = R"(\
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
out vec3 vColor;
void main() {
    vColor = aColor;
    gl_Position = vec4(aPos, 1.0);
}
)";

	const char* fragmentShaderSrc = R"(\
#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(vColor, 1.0);
}
)";

	GLuint vert = compileShader(GL_VERTEX_SHADER, vertexShaderSrc);
	GLuint frag = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSrc);
	if (vert == 0 || frag == 0) {
		glfwDestroyWindow(window);
		glfwTerminate();
		return 1;
	}
	GLuint program = linkProgram(vert, frag);
	glDeleteShader(vert);
	glDeleteShader(frag);
	if (program == 0) {
		glfwDestroyWindow(window);
		glfwTerminate();
		return 1;
	}

	// Triangle geometry: positions and colors
	float vertices[] = {
		// positions        // colors
		 0.0f,  0.5f, 0.0f,  1.0f, 0.3f, 0.3f,
		-0.5f, -0.5f, 0.0f,  0.3f, 1.0f, 0.3f,
		 0.5f, -0.5f, 0.0f,  0.3f, 0.3f, 1.0f
	};

	GLuint vao = 0, vbo = 0;
	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);

	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
	glfwSwapInterval(1); // vsync

	while (!glfwWindowShouldClose(window)) {
		glClear(GL_COLOR_BUFFER_BIT);

		glUseProgram(program);
		glBindVertexArray(vao);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(1, &vbo);
	glDeleteProgram(program);
	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}


