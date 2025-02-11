#include <logging.h>
#include <raytracer.h>
#include <args.hxx>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>

glm::uvec2 defaultResolution(800u, 600u);
glm::uvec2 uvec2Zero(0u);
glm::vec3 vec3Zero(0.0f);
glm::vec3 vec3One(1.0f);
glm::vec3 cameraDefaultPos(0.0f, 1.0f, 3.0f);
glm::vec3 cameraDefaultDir(0.0f, 0.0f, -1.0f);

template<glm::uvec2* defaultValue = &uvec2Zero>
struct UVec2Reader {
	void operator()(const std::string& name, const std::string& value, glm::uvec2& v)
	{
		if (value == "d") {
			v = *defaultValue;
			return;
		}
		size_t readPos = 0;
		try {
			size_t separatorPos = 0;
			v.x = std::stoul(value, &separatorPos);
			readPos += separatorPos + 1;
			v.y = std::stoul(std::string(value, readPos), &separatorPos);
		} catch (std::out_of_range e) {
			char error[100];
			std::snprintf(error, sizeof(error), "%s - must be 'd' or provide 2 positive integers", name.c_str());
			throw args::ParseError(error);
		} catch (std::exception e) {
			char error[200];
			std::istringstream is(std::string(value, readPos));
			std::string badSubstr;
			is >> badSubstr;
			std::snprintf(error, sizeof(error), "%s - could not parse value '%s' at position %d: %s", name.c_str(), badSubstr.c_str(), readPos, e.what());
			throw args::ParseError(error);
		}
	}
};

template<glm::vec3* defaultValue = &vec3Zero>
struct Vec3Reader {
	void operator()(const std::string& name, const std::string& value, glm::vec3& v) {
		if (value == "d") {
			v = *defaultValue;
			return;
		}
		size_t readPos = 0;
		try {
			size_t separatorPos = 0;
			v.x = std::stof(value, &separatorPos);
			readPos += separatorPos + 1;
			v.y = std::stof(std::string(value, readPos), &separatorPos);
			readPos += separatorPos + 1;
			v.z = std::stof(std::string(value, readPos));
		} catch (std::out_of_range e) {
			char error[100];
			std::snprintf(error, sizeof(error), "%s - must be 'd' or provide 3 real values", name.c_str());
			throw args::ParseError(error);
		} catch (std::exception e) {
			char error[200];
			std::istringstream is(std::string(value, readPos));
			std::string badSubstr;
			is >> badSubstr;
			std::snprintf(error, sizeof(error), "%s - could not parse value '%s' at position %d: %s", name.c_str(), badSubstr.c_str(), readPos, e.what());
			throw args::ParseError(error);
		}
	}
};

glm::quat quatIdentity = glm::identity<glm::quat>();
template<glm::quat* defaultValue = &quatIdentity>
struct QuaternionReader {
	void operator()(const std::string& name, const std::string& value, glm::quat& q) {
		if (value == "d") {
			q = *defaultValue;
			return;
		}
		size_t readPos = 0;
		try {
			size_t separatorPos = 0;
			q.w = std::stof(value, &separatorPos);
			readPos += separatorPos + 1;
			q.x = std::stof(std::string(value, readPos), &separatorPos);
			readPos += separatorPos + 1;
			q.y = std::stof(std::string(value, readPos), &separatorPos);
			readPos += separatorPos + 1;
			q.z = std::stof(std::string(value, readPos));
		} catch (std::out_of_range e) {
			char error[100];
			std::snprintf(error, sizeof(error), "%s - must be 'd' or provide 4 real values", name.c_str());
			throw args::ParseError(error);
		} catch (std::exception e) {
			char error[200];
			std::istringstream is(std::string(value, readPos));
			std::string badSubstr;
			is >> badSubstr;
			std::snprintf(error, sizeof(error), "%s - could not parse value '%s' at position %d: %s", name.c_str(), badSubstr.c_str(), readPos, e.what());
			throw args::ParseError(error);
		}
	}
};

using TranslationReader = Vec3Reader<>;
using ScaleReader = Vec3Reader<&vec3One>;
using ResolutionReader = UVec2Reader<&defaultResolution>;

int main(int argc, char** argv) {
	args::ArgumentParser parser("Vulkan raytracer - a glTF path tracer.\n\n"
								"[WASD] - move around the scene\n"
								"[LEFT MOUSE] - pan camera\n"
								"[RIGHT MOUSE] - adjust fov\n"
	);
	args::HelpFlag help(parser, "help", "Display this help menu", { 'h', "help" });

	args::ImplicitValueFlag<glm::uvec2, ResolutionReader> resolution(parser, "resolution", "Resolution [w,h]", { 'r', "resolution" }, defaultResolution, args::Options::Single);

	args::Group pathTracingSettings(parser, "Path tracing settings");
	args::ImplicitValueFlag<uint32_t> maxRayDepth(pathTracingSettings, "maxRayDepth", "Max ray depth", { 'b', "max-ray-depth" }, 5u, args::Options::Single);

	args::ValueFlagList<std::string> models(parser, "models", "glTF model file(s)", { 'm', "models" });

	args::Group transform(parser, "Transform modifiers - the n:th transform modifier will affect the transform of n:th model provided. Use comma separated list to specify values or \'d\' to use default value.");
	args::ValueFlagList <glm::vec3, std::vector, TranslationReader> translations(transform, "translations", "Model translation(s) [x,y,z]", { 't', "translations" });
	args::ValueFlagList<glm::quat, std::vector, QuaternionReader<>> rotations(transform, "rotations", "Model rotation(s) [w,x,y,z]", { 'o', "rotations" });
	args::ValueFlagList<glm::vec3, std::vector, ScaleReader> scales(transform, "scales", "Model scale(s) [x,y,z]", { 's', "scales" });

	args::Group camera(parser, "Initial camera settings");
	args::ValueFlag<glm::vec3, Vec3Reader<&cameraDefaultPos>> cameraPos(camera, "cameraPos", "Camera position [x,y,z]", { 'c', "camera-position" }, args::Options::Single);
	args::ValueFlag<glm::vec3, Vec3Reader<&cameraDefaultDir>> cameraDir(camera, "cameraDir", "Camera direction [x,y,z]", { 'd', "camera-direction" }, args::Options::Single);

	args::Group skyboxParams(parser, "Skybox settings");
	args::ImplicitValueFlag<std::string> skybox(skyboxParams, "skybox", "Skybox file", { "skybox" }, "hilly_terrain_01_4k.hdr", args::Options::Single);
	args::ImplicitValueFlag<float> skyboxStrength(skyboxParams, "skyboxStrength", "Skybox strength multiplier", { "skybox-strength" }, 1.0f, args::Options::Single);

	try {
		parser.ParseCLI(argc, argv);
	} catch (args::Help) {
		fprintf(stdout, parser.Help().c_str());
		return 0;
	} catch (args::ParseError e) {
		LOG_ERROR("%s", e.what());
		fprintf(stdout, parser.Help().c_str());
		return 1;
	} catch (args::ValidationError e) {
		LOG_ERROR("%s", e.what());
		fprintf(stdout, parser.Help().c_str());
		return 1;
	}

	std::vector modelFiles = !models || models.Get().empty() ? std::vector<std::string>{ "CornellBox.gltf" } : models.Get();
	std::vector<glm::mat4> transforms;
	transforms.reserve(modelFiles.size());
	for (int i = 0; i < modelFiles.size(); i++) {
		glm::mat4 transform(1.0f);
		if (scales && i < scales.Get().size()) transform = glm::scale(scales.Get()[i]) * transform;
		if (rotations && i < rotations.Get().size()) transform = glm::mat4(rotations.Get()[i]) * transform;
		if (translations && i < translations.Get().size()) transform = glm::translate(translations.Get()[i]) * transform;
		transforms.push_back(transform);
	}

	auto rt = vkrt::Raytracer(resolution.Get().x, resolution.Get().y, maxRayDepth.Get(), modelFiles, transforms, cameraPos ? cameraPos.Get() : cameraDefaultPos, cameraDir ? cameraDir.Get() : cameraDefaultDir, skybox.Get(), skyboxStrength.Get());
	rt.renderLoop();
}