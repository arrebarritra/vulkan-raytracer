#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace vkrt {

class Camera {
public:
	Camera();
	glm::mat4 getView() { return glm::lookAt(position, position + direction, up); }
	glm::mat4 getViewInv() { return glm::inverse(getView()); }
	glm::mat4 getProjection() { return glm::perspective(fov, aspect, near, far); }
	glm::mat4 getProjectionInv() { return glm::inverse(getProjection()); }

	void processKeyInput(GLFWwindow* window, double dt);
	void cursorPosCallback(GLFWwindow* window, double dx, double dy);

	glm::vec3 position, direction, up;
	float near, far, fov, aspect;
	float speed, sensitivity;
	bool positionChanged = false, directionChanged = false;
};

}