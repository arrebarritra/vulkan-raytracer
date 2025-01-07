#include <camera.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/quaternion.hpp>

namespace vkrt {

Camera::Camera()
	: position(glm::vec3(0.0f, 1.0f, 0.0f))
	, direction(glm::vec3(0.0f, 0.0f, 1.0f))
	, up(glm::vec3(0.0f, 1.0f, 0.0f))
	, aspect(1.0f)
	, near(0.1f)
	, far(1000.0f)
	, fov(70.0 * (glm::pi<float>() / 180.0))
	, speed(2.0f)
	, sensitivity(0.01f) {}

void Camera::processKeyInput(GLFWwindow* window, double dt) {
	float speedMul;
	if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
		speedMul = 3.0f;
	else
		speedMul = 1.0f;

	positionChanged = false;
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
		position += speedMul * speed * direction * static_cast<float>(dt);
		positionChanged = true;
	}
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
		position -= speedMul * speed * direction * static_cast<float>(dt);
		positionChanged = true;
	}
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
		position -= speedMul * speed * glm::normalize(glm::cross(direction, up)) * static_cast<float>(dt);
		positionChanged = true;
	}
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
		position += speedMul * speed * glm::normalize(glm::cross(direction, up)) * static_cast<float>(dt);
		positionChanged = true;
	}
}

void Camera::cursorPosCallback(GLFWwindow* window, double dx, double dy) {
	directionChanged = false;
	if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
		auto& rotX = glm::angleAxis(static_cast<float>(dx) * sensitivity / (glm::two_pi<float>()), -up);
		auto& rotY = glm::angleAxis(static_cast<float>(dy) * sensitivity / (-glm::two_pi<float>()), glm::normalize(glm::cross(direction, up)));
		direction = rotX * direction;
		direction = rotY * direction;
		if (dx != 0.0 && dy != 0.0) directionChanged = true;
	}
}

}