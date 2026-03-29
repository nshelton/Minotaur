#pragma once

#include <glad/glad.h>
#include <cmath>
#include <vector>
#include "utils/ObjLoader.h"

// Self-contained 3D wireframe preview rendered to an FBO.
// Designed to be drawn inside an ImGui panel. All GL resources are owned
// by the widget and cleaned up on destruction.
//
// Usage:
//   widget.setMesh(&mesh);
//   widget.setRotation(rx, ry, rz);
//   widget.render(width, height);       // render to internal FBO
//   ImGui::Image((ImTextureID)widget.texture(), ...);
//
// The widget is completely decoupled from the rest of the rendering pipeline.

class MeshPreviewWidget
{
public:
	MeshPreviewWidget() = default;
	~MeshPreviewWidget() { shutdown(); }

	// Non-copyable, movable
	MeshPreviewWidget(const MeshPreviewWidget &) = delete;
	MeshPreviewWidget &operator=(const MeshPreviewWidget &) = delete;

	// Point to mesh data (not owned). Set to nullptr to clear.
	void setMesh(const ObjMesh *mesh) { m_mesh = mesh; }

	// Euler rotation in degrees
	void setRotation(float rx, float ry, float rz)
	{
		m_rotX = rx; m_rotY = ry; m_rotZ = rz;
	}

	// Render the wireframe into the internal FBO at the given resolution.
	// Creates/resizes GL resources as needed. Safe to call every frame.
	void render(int width, int height);

	// GL texture ID for ImGui::Image. Returns 0 if not yet rendered.
	GLuint texture() const { return m_colorTex; }

	// Has GL resources been initialized?
	bool isInitialized() const { return m_fbo != 0; }

	void shutdown();

private:
	const ObjMesh *m_mesh{nullptr};
	float m_rotX{0}, m_rotY{0}, m_rotZ{0};

	// FBO resources
	GLuint m_fbo{0};
	GLuint m_colorTex{0};
	GLuint m_depthRbo{0};
	int m_fboW{0}, m_fboH{0};

	// Shader + VAO
	GLuint m_program{0};
	GLuint m_vao{0};
	GLuint m_vbo{0};
	GLuint m_uMVP{0};
	GLuint m_uColor{0};

	bool ensureFBO(int w, int h);
	bool ensureShader();

	struct Vtx { float x, y, z; };

	// 4x4 matrix helpers (column-major, local to this widget)
	struct Mat4
	{
		float m[16];
		static Mat4 identity();
		static Mat4 perspective(float fovDeg, float aspect, float zn, float zf);
		static Mat4 ortho(float halfExtent, float aspect, float zn, float zf);
		static Mat4 rotationX(float deg);
		static Mat4 rotationY(float deg);
		static Mat4 rotationZ(float deg);
		static Mat4 translation(float x, float y, float z);
		Mat4 operator*(const Mat4 &rhs) const;
	};
};
