#include "MeshPreviewWidget.h"
#include <cstring>
#include <iostream>

// ---------------------------------------------------------------------------
// Mat4 helpers (self-contained, no dependency on core/Mat3)
// ---------------------------------------------------------------------------

static constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;

MeshPreviewWidget::Mat4 MeshPreviewWidget::Mat4::identity()
{
	Mat4 r;
	std::memset(r.m, 0, sizeof(r.m));
	r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
	return r;
}

MeshPreviewWidget::Mat4 MeshPreviewWidget::Mat4::perspective(float fovDeg, float aspect, float zn, float zf)
{
	Mat4 r;
	std::memset(r.m, 0, sizeof(r.m));
	float f = 1.0f / std::tan(fovDeg * kDeg2Rad * 0.5f);
	r.m[0]  = f / aspect;
	r.m[5]  = f;
	r.m[10] = (zf + zn) / (zn - zf);
	r.m[11] = -1.0f;
	r.m[14] = (2.0f * zf * zn) / (zn - zf);
	return r;
}

MeshPreviewWidget::Mat4 MeshPreviewWidget::Mat4::ortho(float halfExtent, float aspect, float zn, float zf)
{
	Mat4 r;
	std::memset(r.m, 0, sizeof(r.m));
	float right = halfExtent * aspect;
	float top   = halfExtent;
	r.m[0]  = 1.0f / right;
	r.m[5]  = 1.0f / top;
	r.m[10] = -2.0f / (zf - zn);
	r.m[14] = -(zf + zn) / (zf - zn);
	r.m[15] = 1.0f;
	return r;
}

MeshPreviewWidget::Mat4 MeshPreviewWidget::Mat4::rotationX(float deg)
{
	Mat4 r = identity();
	float c = std::cos(deg * kDeg2Rad);
	float s = std::sin(deg * kDeg2Rad);
	r.m[5]  =  c; r.m[9]  = -s;
	r.m[6]  =  s; r.m[10] =  c;
	return r;
}

MeshPreviewWidget::Mat4 MeshPreviewWidget::Mat4::rotationY(float deg)
{
	Mat4 r = identity();
	float c = std::cos(deg * kDeg2Rad);
	float s = std::sin(deg * kDeg2Rad);
	r.m[0]  =  c; r.m[8]  =  s;
	r.m[2]  = -s; r.m[10] =  c;
	return r;
}

MeshPreviewWidget::Mat4 MeshPreviewWidget::Mat4::rotationZ(float deg)
{
	Mat4 r = identity();
	float c = std::cos(deg * kDeg2Rad);
	float s = std::sin(deg * kDeg2Rad);
	r.m[0] =  c; r.m[4] = -s;
	r.m[1] =  s; r.m[5] =  c;
	return r;
}

MeshPreviewWidget::Mat4 MeshPreviewWidget::Mat4::translation(float x, float y, float z)
{
	Mat4 r = identity();
	r.m[12] = x;
	r.m[13] = y;
	r.m[14] = z;
	return r;
}

MeshPreviewWidget::Mat4 MeshPreviewWidget::Mat4::operator*(const Mat4 &rhs) const
{
	Mat4 r;
	for (int c = 0; c < 4; ++c)
		for (int row = 0; row < 4; ++row)
		{
			float sum = 0;
			for (int k = 0; k < 4; ++k)
				sum += m[k * 4 + row] * rhs.m[c * 4 + k];
			r.m[c * 4 + row] = sum;
		}
	return r;
}

// ---------------------------------------------------------------------------
// GL resource management
// ---------------------------------------------------------------------------

bool MeshPreviewWidget::ensureShader()
{
	if (m_program) return true;

	const char *vsSrc = R"(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

	const char *fsSrc = R"(
#version 330 core
uniform vec4 uColor;
out vec4 FragColor;
void main() {
    FragColor = uColor;
}
)";

	auto compile = [](GLenum type, const char *src) -> GLuint {
		GLuint s = glCreateShader(type);
		glShaderSource(s, 1, &src, nullptr);
		glCompileShader(s);
		GLint ok = 0;
		glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
		if (!ok)
		{
			GLint len = 0;
			glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
			std::string log(static_cast<size_t>(len), '\0');
			glGetShaderInfoLog(s, len, nullptr, log.data());
			std::cerr << "MeshPreview shader error: " << log << std::endl;
			glDeleteShader(s);
			return 0;
		}
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

	m_uMVP   = glGetUniformLocation(m_program, "uMVP");
	m_uColor = glGetUniformLocation(m_program, "uColor");

	glGenVertexArrays(1, &m_vao);
	glGenBuffers(1, &m_vbo);

	glBindVertexArray(m_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), nullptr);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	return true;
}

bool MeshPreviewWidget::ensureFBO(int w, int h)
{
	if (w <= 0 || h <= 0) return false;
	if (m_fbo && m_fboW == w && m_fboH == h) return true;

	// Delete old if resizing
	if (m_fbo)
	{
		glDeleteFramebuffers(1, &m_fbo);
		glDeleteTextures(1, &m_colorTex);
		glDeleteRenderbuffers(1, &m_depthRbo);
		m_fbo = 0;
	}

	glGenFramebuffers(1, &m_fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

	glGenTextures(1, &m_colorTex);
	glBindTexture(GL_TEXTURE_2D, m_colorTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTex, 0);

	glGenRenderbuffers(1, &m_depthRbo);
	glBindRenderbuffer(GL_RENDERBUFFER, m_depthRbo);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthRbo);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		glDeleteFramebuffers(1, &m_fbo);
		glDeleteTextures(1, &m_colorTex);
		glDeleteRenderbuffers(1, &m_depthRbo);
		m_fbo = 0; m_colorTex = 0; m_depthRbo = 0;
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		return false;
	}

	m_fboW = w;
	m_fboH = h;
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	return true;
}

void MeshPreviewWidget::shutdown()
{
	if (m_fbo) { glDeleteFramebuffers(1, &m_fbo); m_fbo = 0; }
	if (m_colorTex) { glDeleteTextures(1, &m_colorTex); m_colorTex = 0; }
	if (m_depthRbo) { glDeleteRenderbuffers(1, &m_depthRbo); m_depthRbo = 0; }
	if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
	if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
	if (m_program) { glDeleteProgram(m_program); m_program = 0; }
	m_fboW = m_fboH = 0;
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void MeshPreviewWidget::render(int width, int height)
{
	if (!m_mesh || m_mesh->edges.empty()) return;
	if (!ensureShader()) return;
	if (!ensureFBO(width, height)) return;

	// Save current GL state we'll modify
	GLint prevFBO = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
	GLint prevViewport[4];
	glGetIntegerv(GL_VIEWPORT, prevViewport);

	glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
	glViewport(0, 0, width, height);

	glClearColor(0.12f, 0.12f, 0.12f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);

	// Build MVP: projection * view * model
	float aspect = static_cast<float>(width) / static_cast<float>(height);
	Mat4 proj  = Mat4::ortho(1.2f, aspect, 0.01f, 100.0f);
	Mat4 view  = Mat4::translation(0, 0, -3.0f);
	Mat4 model = Mat4::rotationX(m_rotX) * Mat4::rotationY(m_rotY) * Mat4::rotationZ(m_rotZ);
	Mat4 mvp   = proj * view * model;

	// Upload edge vertices
	std::vector<Vtx> verts;
	verts.reserve(m_mesh->edges.size() * 2);
	for (const auto &edge : m_mesh->edges)
	{
		const auto &a = m_mesh->vertices[edge.a];
		const auto &b = m_mesh->vertices[edge.b];
		verts.push_back({a.x, a.y, a.z});
		verts.push_back({b.x, b.y, b.z});
	}

	glUseProgram(m_program);
	glUniformMatrix4fv(m_uMVP, 1, GL_FALSE, mvp.m);
	glUniform4f(m_uColor, 0.8f, 0.8f, 0.8f, 1.0f);

	glBindVertexArray(m_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	glBufferData(GL_ARRAY_BUFFER,
		static_cast<GLsizeiptr>(verts.size() * sizeof(Vtx)),
		verts.data(), GL_DYNAMIC_DRAW);

	glLineWidth(1.0f);
	glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(verts.size()));

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
	glDisable(GL_DEPTH_TEST);

	// Restore previous GL state
	glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFBO));
	glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}
