#include <camera.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/quaternion.hpp>

namespace vkrt {

Camera::Camera()
	: position(glm::vec3(0.0f))
	, direction(glm::vec3(0.0f, 0.0f, 1.0f))
	, up(glm::vec3(0.0f, 1.0f, 0.0f))
	, aspect(1.0f)
	, near(0.1f)
	, far(1000.0f)
	, fov(0.4 * glm::pi<float>())
	, speed(1.0f)
	, sensitivity(0.001f) {}

Camera::Camera(const aiCamera* cam, float width, float height)
	: aspect(static_cast<float>(width) / static_cast<float>(height))
	, near(cam->mClipPlaneNear)
	, far(cam->mClipPlaneNear)
	, fov(cam->mClipPlaneFar)
{
	memcpy(&position, &cam->mPosition, sizeof(glm::vec3));
	auto& d = cam->mLookAt - cam->mPosition;
	memcpy(&direction, &d, sizeof(glm::vec3));
	memcpy(&up, &cam->mUp, sizeof(glm::vec3));
}

void Camera::processKeyInput(GLFWwindow* window, double dt) {
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		position += speed * direction * static_cast<float>(dt);
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		position -= speed * direction * static_cast<float>(dt);
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		position -= speed * glm::normalize(glm::cross(direction, up)) * static_cast<float>(dt);
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		position += speed * glm::normalize(glm::cross(direction, up)) * static_cast<float>(dt);
}

void Camera::cursorPosCallback(double dx, double dy) {
	auto& rotX = glm::angleAxis(static_cast<float>(dx) * sensitivity / (glm::two_pi<float>()), -up);
	auto& rotY = glm::angleAxis(static_cast<float>(dy) * sensitivity / (glm::two_pi<float>()), glm::normalize(glm::cross(direction, up)));
	direction = rotX * direction;
	direction = rotY * direction;
}

}