#include <iostream>
#include <array>
#include <vector>
#include <fstream>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>

#include <glad/glad.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/ext.hpp>
#include <glm/gtx/hash.hpp>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// Function prototypes
void error_callback(int error, const char* description);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
void cursor_position_callback(GLFWwindow* window, double x, double y);
void scroll_callback(GLFWwindow* window, double x, double y);

void checkShader(GLuint shader);
void checkProgram(GLuint program);
GLuint createShader(std::string_view source, GLenum shaderType);
std::tuple<GLuint, GLuint> createShaderProgram(std::array<std::string_view, 2> const& source);
GLuint createTexture2D(GLenum internalformat, GLsizei width, GLsizei height, GLenum format, void* data = nullptr,
	GLenum minFilter = GL_LINEAR, GLenum magFilter = GL_LINEAR, GLenum wrapMode = GL_REPEAT);
using stb_comp_t = decltype(STBI_default);
GLuint loadTexture(std::string_view filename, stb_comp_t comp = STBI_rgb_alpha);
void loadModel(const std::string& filename);
glm::mat4 camera(float zoom, const glm::vec2& rotate);

constexpr int WIDTH{1920};
constexpr int HEIGHT{1080};
glm::vec2 rotation = glm::vec2(0.0f, 0.0f);
float zoom = 40.0f;
double cursorX;
double cursorY;

struct alignas(16) Vertex
{
	glm::vec4 position;
	glm::vec4 color;
	glm::vec2 texcoord;
	bool operator==(const Vertex& other) const {
		return position == other.position && color == other.color && texcoord == other.texcoord;
	}
};

namespace std {
	template<> struct hash<Vertex> {
		size_t operator()(Vertex const& vertex) const {
			return ((hash<glm::vec3>()(vertex.position) ^
				(hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^
				(hash<glm::vec2>()(vertex.texcoord) << 1);
		}
	};
}

std::vector<Vertex> vertices;
std::vector<uint32_t> indices;

struct UniformBufferObject
{
	glm::mat4 MVP;
};

namespace buffer
{
	enum type
	{
		VERTEX,
		ELEMENT,
		TRANSFORM,
		MAX
	};
}

const char* const vs_source = R"(
#version 460 core

layout(binding = 1) uniform UniformBufferObject {
    mat4 MVP;
} ubo;

struct Vertex
{
    vec4 position;
    vec4 color;
    vec2 texcoord;
};

layout(std430, binding = 0) buffer Mesh
{
    Vertex vertex[];
} mesh;

out gl_PerVertex
{
    vec4 gl_Position;
};

out block
{
    vec4 Color;
    vec2 Texcoord;
} Out;

void main()
{
    gl_Position = ubo.MVP * mesh.vertex[gl_VertexID].position;
    Out.Color = mesh.vertex[gl_VertexID].color;
    Out.Texcoord = mesh.vertex[gl_VertexID].texcoord;
}
)";

const char* const fs_source = R"(
#version 460 core

layout(binding = 1) uniform sampler2D tex;

in block
{
    vec4 Color;
    vec2 Texcoord;
} In;

layout(location = 0) out vec4 color;

void main()
{
    color = texture(tex, In.Texcoord);
}
)";


int main()
{
	if (!glfwInit())
		return -1;

	glfwSetErrorCallback(error_callback);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

	GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Rabbit", nullptr, nullptr);
	if (!window)
	{
		std::cout << "Failed to create GLFW window\n";
		glfwTerminate();
		return -1;
	}

	glfwMakeContextCurrent(window);

	glfwSetKeyCallback(window, key_callback);
	glfwSetMouseButtonCallback(window, mouse_button_callback);
	glfwSetCursorPosCallback(window, cursor_position_callback);
	glfwSetScrollCallback(window, scroll_callback);

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		std::cout << "Failed to initialize OpenGL context" << std::endl;
		return -1;
	}

	if (GLAD_GL_VERSION_4_6) {
		std::cout << "We support at least OpenGL version 4.6" << std::endl;
	}

	int width, height;
	glfwGetFramebufferSize(window, &width, &height);
	glViewport(0, 0, width, height);

	const auto [program, pipeline] = createShaderProgram({ vs_source, fs_source });

	loadModel("model/rabbit.obj");

	GLint alignment = 0;
	glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &alignment);
	GLint blockSize = glm::max(GLint(sizeof(UniformBufferObject)), alignment);

	std::array<GLuint, buffer::MAX> buffers{};
	glCreateBuffers(buffer::MAX, buffers.data());
	glNamedBufferStorage(buffers[buffer::VERTEX], vertices.size() * sizeof(Vertex), vertices.data(), 0);
	glNamedBufferStorage(buffers[buffer::ELEMENT], indices.size() * sizeof(uint32_t), indices.data(), 0);
	glNamedBufferStorage(buffers[buffer::TRANSFORM], blockSize, nullptr, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
	
	GLuint vao = 0;
	glCreateVertexArrays(1, &vao);
	glVertexArrayElementBuffer(vao, buffers[buffer::ELEMENT]);
	
	GLuint tex = loadTexture("model/rabbit.jpg");
	
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	
	// time management
	float currentFrame = (float)glfwGetTime(), deltaTime = 0.0f, lastFrame = 0.0f;
	float time = 0.0f;
	GLuint  fps = 0;
	
	while (!glfwWindowShouldClose(window))
	{
		// - calculate time spent on last frame
		currentFrame = (float)glfwGetTime();
		deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;
		// - periodcally display the FPS the game is running in
		time += deltaTime;
		++fps;
		if (time >= 1.0f)
		{
			time -= 1.0f;
			glfwSetWindowTitle(window, std::string("FPS: " + std::to_string(fps)).c_str());
			fps = 0;
		}

		{
			auto Pointer = static_cast<UniformBufferObject*>(glMapNamedBufferRange(buffers[buffer::TRANSFORM], 0,
				blockSize, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT));
			Pointer->MVP = camera(zoom, rotation);
			glUnmapNamedBuffer(buffers[buffer::TRANSFORM]);
		}

		glClearBufferfv(GL_COLOR, 0, &glm::vec4(0.26f, 0.33f, 0.46f, 1.0f)[0]);
		glClearBufferfv(GL_DEPTH, 0, &glm::vec4(1.0f)[0]);
		
		glBindProgramPipeline(pipeline);
		glBindVertexArray(vao);
		glBindTextureUnit(1, tex);
		glBindBufferBase(GL_UNIFORM_BUFFER, 1, buffers[buffer::TRANSFORM]);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffers[buffer::VERTEX]);

		glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, nullptr, 1);
		
		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	glDeleteProgramPipelines(1, &pipeline);
	glDeleteProgram(program);
	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(buffer::MAX, buffers.data());
	glDeleteTextures(1, &tex);

	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}

void error_callback(int error, const char* description)
{
	std::cerr << "Error (" << error << "): " << description << "\n";
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
		glfwSetWindowShouldClose(window, GL_TRUE);
}

//========================================================================
// Callback function for mouse button events
//========================================================================
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
	if (button != GLFW_MOUSE_BUTTON_LEFT)
		return;
	if (action == GLFW_PRESS)
	{
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		glfwGetCursorPos(window, &cursorX, &cursorY);
	}
	else
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

//========================================================================
// Callback function for cursor motion events
//========================================================================
void cursor_position_callback(GLFWwindow* window, double x, double y)
{
	if (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
	{
		rotation.x += (GLfloat)(x - cursorX) / 10.f;
		rotation.y += (GLfloat)(y - cursorY) / 10.f;
		cursorX = x;
		cursorY = y;
	}
}

//========================================================================
// Callback function for scroll events
//========================================================================
void scroll_callback(GLFWwindow* window, double x, double y)
{
	zoom += (float)y / 4.f;
	if (zoom < 0)
		zoom = 0;
}

void loadModel(const std::string& filename)
{
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn;
	std::string err;

	bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename.c_str(), "");

	if (!warn.empty()) {
		std::cout << "WARN: " << warn << std::endl;
	}

	if (!err.empty()) {
		std::cerr << err << std::endl;
	}

	if (!ret) {
		std::cerr << "Failed to load: " << filename << std::endl;
	}

	std::unordered_map<Vertex, uint32_t> uniqueVertices{};

	for (const auto& shape : shapes) {
		for (const auto& index : shape.mesh.indices) {
			Vertex vertex{};

			vertex.position = {
				attrib.vertices[3 * index.vertex_index + 0],
				attrib.vertices[3 * index.vertex_index + 1],
				attrib.vertices[3 * index.vertex_index + 2],
				1.0f
			};

			vertex.texcoord = {
				attrib.texcoords[2 * index.texcoord_index + 0],
				attrib.texcoords[2 * index.texcoord_index + 1]
			};

			vertex.color = { 1.0f, 1.0f, 1.0f, 1.0f };

			if (uniqueVertices.count(vertex) == 0) {
				uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
				vertices.push_back(vertex);
			}

			indices.push_back(uniqueVertices[vertex]);
		}
	}
}

GLuint createTexture2D(GLenum internalformat,
	GLsizei width,
	GLsizei height,
	GLenum format,
	void* data /*= nullptr*/,
	GLenum minFilter /*= GL_LINEAR*/,
	GLenum magFilter /*= GL_LINEAR*/,
	GLenum wrapMode /*= GL_REPEAT*/)
{
	GLuint textureId = 0;
	glCreateTextures(GL_TEXTURE_2D, 1, &textureId);
	glTextureStorage2D(textureId, 1, internalformat, width, height);

	// set texture filtering parameters
	glTextureParameteri(textureId, GL_TEXTURE_MIN_FILTER, minFilter);
	glTextureParameteri(textureId, GL_TEXTURE_MAG_FILTER, magFilter);

	// set the texture wrapping parameters
	glTextureParameteri(textureId, GL_TEXTURE_WRAP_S, wrapMode);
	glTextureParameteri(textureId, GL_TEXTURE_WRAP_T, wrapMode);

	if (data)
	{
		glTextureSubImage2D(textureId, 0, 0, 0, width, height, format, GL_UNSIGNED_BYTE, data);
	}

	glGenerateTextureMipmap(textureId);

	return textureId;
}

GLuint loadTexture(std::string_view filename, stb_comp_t comp /*= STBI_rgb_alpha*/)
{
	stbi_set_flip_vertically_on_load(true);
	int w, h, c;
	const auto data = stbi_load(filename.data(), &w, &h, &c, comp);
	if (!data)
	{
		std::cout << "Failed to load texture: " << std::string(filename) << '\n';
	}
	
	auto const [in, ex] = [comp]() {
		switch (comp)
		{
		case STBI_rgb_alpha:    return std::make_pair(GL_RGBA8, GL_RGBA);
		case STBI_rgb:            return std::make_pair(GL_RGB8, GL_RGB);
		case STBI_grey:            return std::make_pair(GL_R8, GL_RED);
		case STBI_grey_alpha:    return std::make_pair(GL_RG8, GL_RG);
		default: std::cerr << "Invalid format\n";
			return std::make_pair(GL_RGBA8, GL_RGBA);
		}
	}();

	const auto name = createTexture2D(in, w, h, ex, data);

	stbi_image_free(data);

	return name;
}

GLuint createShader(std::string_view source, GLenum shaderType)
{
	const auto csrc = source.data();

	GLuint shader = glCreateShader(shaderType);
	glShaderSource(shader, 1, &csrc, nullptr);
	glCompileShader(shader);
	checkShader(shader);

	return shader;
}

std::tuple<GLuint, GLuint> createShaderProgram(std::array<std::string_view, 2> const& source)
{
	const auto vs = createShader(source[0].data(), GL_VERTEX_SHADER);
	const auto fs = createShader(source[1].data(), GL_FRAGMENT_SHADER);
	static const std::array<GLuint, 2>& shaders{ vs, fs };

	GLuint program = glCreateProgram();
	glProgramParameteri(program, GL_PROGRAM_SEPARABLE, GL_TRUE);

	for (const auto& shader : shaders)
	{
		glAttachShader(program, shader);
	}

	glLinkProgram(program);
	checkProgram(program);

	for (const auto& shader : shaders)
	{
		glDetachShader(program, shader);
		glDeleteShader(shader);
	}

	GLuint pipeline = 0;
	glCreateProgramPipelines(1, &pipeline);
	glUseProgramStages(pipeline, GL_VERTEX_SHADER_BIT | GL_FRAGMENT_SHADER_BIT, program);

	return std::make_tuple(program, pipeline);
}

void checkShader(GLuint shader)
{
	GLint isCompiled = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);

	GLint maxLength{};
	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

	if (maxLength > 0
#ifdef NDEBUG
		&& isCompiled == GL_FALSE
#endif // NDEBUG
		)
	{
		std::vector<char> buffer(maxLength);
		glGetShaderInfoLog(shader, maxLength, nullptr, buffer.data());
		glDeleteShader(shader);

		std::cout << "Error compiled:\n" << buffer.data() << '\n';
	}
}

void checkProgram(GLuint program)
{
	GLint isLinked = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &isLinked);

	GLint maxLength{};
	glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

	if (maxLength > 0
#ifdef NDEBUG
		&& isLinked == GL_FALSE
#endif // NDEBUG
		)
	{
		std::vector<char> buffer(maxLength);
		glGetProgramInfoLog(program, maxLength, nullptr, buffer.data());
		glDeleteProgram(program);

		std::cout << "Error linking:\n" << buffer.data() << '\n';
	}
}

glm::mat4 camera(float zoom, const glm::vec2& rotate)
{
	auto aspectRatio = static_cast<float>(WIDTH) / static_cast<float>(HEIGHT);
	glm::mat4 Projection = glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 100.0f);
	glm::mat4 View = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -zoom));
	View = glm::rotate(View, glm::radians(rotate.y), glm::vec3(1.0f, 0.0f, 0.0f));
	View = glm::rotate(View, glm::radians(rotate.x), glm::vec3(0.0f, 1.0f, 0.0f));
	glm::mat4 Model = glm::mat4(1.0f);

	return Projection * View * Model;
}
