#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <assimp/camera.h>

namespace vkrt {

class Camera {
public:
	Camera();
	Camera(const aiCamera* cam, float width, float height);
	glm::mat4 getView() { return glm::lookAt(position, position + direction, up); }
	glm::mat4 getViewInv() { return glm::inverse(glm::lookAt(position, position + direction, up)); }
	glm::mat4 getProjection() { return glm::perspective(fov, aspect, near, far); }
	glm::mat4 getProjectionInv() { return glm::inverse(glm::perspective(fov, aspect, near, far)); }

	void processKeyInput(GLFWwindow* window, double dt);
	void cursorPosCallback(double dx, double dy);

private:
	glm::vec3 position, direction, up;
	float near, far, fov, aspect;
	float speed, sensitivity;

};

}