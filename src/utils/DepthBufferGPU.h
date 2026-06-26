#pragma once

#include <glad/glad.h>
#include <vector>

// GPU-accelerated depth buffer for hidden line removal.
// Takes pre-projected 2D triangle positions with Z depth values,
// renders them to a depth-only FBO, and reads back the depth buffer.
//
// This replaces the O(pixels * triangles) software rasterizer with
// a single GPU draw call + readback, enabling million-triangle HLR.
//
// Usage:
//   DepthBufferGPU gpu;
//   gpu.render(triangleVerts, numTris, minX, minY, maxX, maxY, minZ, maxZ);
//   // gpu.depthData() now contains the DBUF_SIZE x DBUF_SIZE depth buffer

class DepthBufferGPU
{
public:
	static constexpr int DBUF_SIZE = 1024;

	DepthBufferGPU() = default;
	~DepthBufferGPU() { shutdown(); }

	DepthBufferGPU(const DepthBufferGPU &) = delete;
	DepthBufferGPU &operator=(const DepthBufferGPU &) = delete;

	struct TriVert
	{
		float x, y, z; // x,y = projected 2D position, z = view-space depth
	};

	// Render triangles to depth buffer and read back.
	// triVerts: array of TriVert, 3 per triangle (numTris * 3 total).
	// Bounds define the projection window in 2D space.
	// zMin/zMax define the depth range (view-space Z).
	// After this call, depthData() contains the readback.
	bool render(const std::vector<TriVert> &triVerts,
	            float minX, float minY, float maxX, float maxY,
	            float zMin, float zMax)
	{
		if (triVerts.empty()) return false;
		if (!ensureResources()) return false;

		// Save GL state
		GLint prevFBO = 0;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
		GLint prevViewport[4];
		glGetIntegerv(GL_VIEWPORT, prevViewport);
		GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
		GLint prevDepthFunc;
		glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);

		glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
		glViewport(0, 0, DBUF_SIZE, DBUF_SIZE);

		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glClearDepth(1.0);
		glClear(GL_DEPTH_BUFFER_BIT);

		// Upload triangle data
		glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
		glBufferData(GL_ARRAY_BUFFER,
			static_cast<GLsizeiptr>(triVerts.size() * sizeof(TriVert)),
			triVerts.data(), GL_STREAM_DRAW);

		// Set up orthographic projection mapping:
		//   x: [minX, maxX] -> [-1, 1]
		//   y: [minY, maxY] -> [-1, 1]
		//   z: [zMin, zMax] -> [0, 1] (zMax = closest to camera -> smallest depth)
		// Note: in our convention, higher Z = closer to camera.
		// GL depth buffer: smaller = closer. So we map zMax -> 0 (near), zMin -> 1 (far).
		glUseProgram(m_program);
		float scaleX = 2.0f / (maxX - minX);
		float scaleY = 2.0f / (maxY - minY);
		float zRange = zMax - zMin;
		if (zRange < 1e-6f) zRange = 1.0f;
		float scaleZ = -1.0f / zRange; // negate: high Z -> low depth
		float offX = -(maxX + minX) / (maxX - minX);
		float offY = -(maxY + minY) / (maxY - minY);
		float offZ = zMax / zRange; // zMax maps to 0

		glUniform3f(m_uScale, scaleX, scaleY, scaleZ);
		glUniform3f(m_uOffset, offX, offY, offZ);

		glBindVertexArray(m_vao);
		glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(triVerts.size()));

		// Read back depth buffer
		m_depthData.resize(DBUF_SIZE * DBUF_SIZE);
		glReadPixels(0, 0, DBUF_SIZE, DBUF_SIZE, GL_DEPTH_COMPONENT, GL_FLOAT, m_depthData.data());

		// Restore GL state
		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFBO));
		glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
		if (!prevDepthTest) glDisable(GL_DEPTH_TEST);
		glDepthFunc(prevDepthFunc);

		m_zMin = zMin;
		m_zMax = zMax;
		return true;
	}

	// Access the readback depth data. Values are in [0, 1] where 0 = zMax (closest).
	// Convert back to view-space Z: viewZ = zMax - depthValue * (zMax - zMin)
	const std::vector<float> &depthData() const { return m_depthData; }

	// Convert a GL depth value back to view-space Z
	float depthToViewZ(float glDepth) const
	{
		return m_zMax - glDepth * (m_zMax - m_zMin);
	}

	float zMin() const { return m_zMin; }
	float zMax() const { return m_zMax; }

	void shutdown()
	{
		if (m_fbo) { glDeleteFramebuffers(1, &m_fbo); m_fbo = 0; }
		if (m_depthTex) { glDeleteTextures(1, &m_depthTex); m_depthTex = 0; }
		if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
		if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
		if (m_program) { glDeleteProgram(m_program); m_program = 0; }
	}

private:
	GLuint m_fbo{0};
	GLuint m_depthTex{0};
	GLuint m_program{0};
	GLuint m_vao{0};
	GLuint m_vbo{0};
	GLuint m_uScale{0};
	GLuint m_uOffset{0};

	std::vector<float> m_depthData;
	float m_zMin{0}, m_zMax{0};

	bool ensureResources()
	{
		if (m_fbo && m_program) return true;

		// Create depth-only FBO
		if (!m_fbo)
		{
			glGenFramebuffers(1, &m_fbo);
			glGenTextures(1, &m_depthTex);

			glBindTexture(GL_TEXTURE_2D, m_depthTex);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F,
				DBUF_SIZE, DBUF_SIZE, 0,
				GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

			glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
				GL_TEXTURE_2D, m_depthTex, 0);
			glDrawBuffer(GL_NONE);
			glReadBuffer(GL_NONE);

			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			{
				glDeleteFramebuffers(1, &m_fbo);
				glDeleteTextures(1, &m_depthTex);
				m_fbo = 0; m_depthTex = 0;
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				return false;
			}
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}

		// Create shader
		if (!m_program)
		{
			const char *vsSrc = R"(
#version 330 core
layout(location=0) in vec3 aPos;
uniform vec3 uScale;
uniform vec3 uOffset;
void main() {
    gl_Position = vec4(
        aPos.x * uScale.x + uOffset.x,
        aPos.y * uScale.y + uOffset.y,
        aPos.z * uScale.z + uOffset.z,
        1.0);
}
)";
			const char *fsSrc = R"(
#version 330 core
void main() {}
)";

			auto compile = [](GLenum type, const char *src) -> GLuint {
				GLuint s = glCreateShader(type);
				glShaderSource(s, 1, &src, nullptr);
				glCompileShader(s);
				GLint ok = 0;
				glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
				if (!ok) { glDeleteShader(s); return 0; }
				return s;
			};

			GLuint vs = compile(GL_VERTEX_SHADER, vsSrc);
			if (!vs) return false;
			GLuint fs = compile(GL_FRAGMENT_SHADER, fsSrc);
			if (!fs) { glDeleteShader(vs); return false; }

			m_program = glCreateProgram();
			glAttachShader(m_program, vs);
			glAttachShader(m_program, fs);
			glLinkProgram(m_program);
			glDeleteShader(vs);
			glDeleteShader(fs);

			GLint ok = 0;
			glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
			if (!ok)
			{
				glDeleteProgram(m_program);
				m_program = 0;
				return false;
			}

			m_uScale  = glGetUniformLocation(m_program, "uScale");
			m_uOffset = glGetUniformLocation(m_program, "uOffset");

			glGenVertexArrays(1, &m_vao);
			glGenBuffers(1, &m_vbo);

			glBindVertexArray(m_vao);
			glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
			glEnableVertexAttribArray(0);
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TriVert), nullptr);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindVertexArray(0);
		}

		return true;
	}
};
