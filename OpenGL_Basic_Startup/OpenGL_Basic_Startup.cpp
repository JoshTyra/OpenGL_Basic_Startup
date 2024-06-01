#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <vector>
#include "Camera.h"
#include "FileSystemUtils.h"

Camera camera(glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, 0.0f, 6.0f, 0.1f, 45.0f);

GLuint loadTexture(const char* path);
GLuint loadCubemap(std::vector<std::string> faces);
void mouseCallback(GLFWwindow* window, double xpos, double ypos);
void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);

// Water Vertex Shader source code
const char* waterVertex = R"(
#version 430 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

out vec2 TexCoord;
out vec3 WorldPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    WorldPos = vec3(model * vec4(aPos, 1.0));
    gl_Position = projection * view * vec4(WorldPos, 1.0);
    TexCoord = aTexCoord;
}
)";

const char* waterFragment = R"(
#version 430 core
out vec4 FragColor;

in vec2 TexCoord;
in vec3 WorldPos;

uniform vec3 eyePos;
uniform float time;
layout(location = 0) uniform samplerCube cubeTextureSampler;
layout(location = 1) uniform sampler2D normalTextureSampler1;
layout(location = 2) uniform sampler2D normalTextureSampler2;

// Uniforms to control the strength of each normal map
uniform float strength1;
uniform float strength2;
uniform float strength3;
uniform float strength4;

// Uniform to control the color tint of the water
uniform vec3 waterColorTint;

void main()
{
    vec3 viewDir = normalize(WorldPos - eyePos);
    
    // Calculate the scrolling effect
    float scrollSpeed = 0.1;
    vec2 scrollOffset1 = vec2(time * scrollSpeed, time * scrollSpeed);
    vec2 scrollOffset2 = vec2(time * scrollSpeed * 0.5, time * scrollSpeed * 0.5);
    
    // Sample the normal maps with different scales and offsets
    vec2 texCoord1 = TexCoord * vec2(1.0, 1.0) + scrollOffset1;
    vec2 texCoord2 = TexCoord * vec2(6.0, 4.0) - scrollOffset1;
    vec2 texCoord3 = TexCoord * vec2(1.0, 1.0) + scrollOffset2;
    vec2 texCoord4 = TexCoord * vec2(3.0, 3.0) - scrollOffset2;
    
    vec3 normal1 = texture(normalTextureSampler1, texCoord1).xyz * 2.0 - 1.0;
    vec3 normal2 = texture(normalTextureSampler2, texCoord2).xyz * 2.0 - 1.0;
    vec3 normal3 = texture(normalTextureSampler1, texCoord3).xyz * 2.0 - 1.0;
    vec3 normal4 = texture(normalTextureSampler2, texCoord4).xyz * 2.0 - 1.0;
    
    // Blend the normals with different strengths
    vec3 blendedNormal = normalize(
        normal1 * strength1 + 
        normal2 * strength2 + 
        normal3 * strength3 + 
        normal4 * strength4
    );
    
    vec3 cubeTexCoords = reflect(WorldPos - eyePos, blendedNormal);
    vec3 cubeTex = texture(cubeTextureSampler, cubeTexCoords).rgb;
    
    vec3 refractColor = texture(cubeTextureSampler, refract(viewDir, blendedNormal, 0.7)).rgb;
    
    // Calculate the Fresnel term using Schlick's approximation
    float fresnelTerm = pow(1.0 - max(dot(viewDir, blendedNormal), 0.0), 5.0);
    fresnelTerm = mix(0.02, 1.0, fresnelTerm);
    
    // Calculate the final color by mixing refraction and reflection based on the Fresnel term
    vec3 finalColor = mix(refractColor, cubeTex, fresnelTerm);
    
    // Apply the color tint to the final color
    finalColor *= waterColorTint;
    
    FragColor = vec4(finalColor, 1.0);
}
)";

// Skybox Vertex Shader
const char* skyboxVertex = R"(
    #version 430 core
    layout (location = 0) in vec3 aPos;
    out vec3 TexCoords;

    uniform mat4 view;
    uniform mat4 projection;

    void main()
    {
        TexCoords = aPos;
        mat4 rotView = mat4(mat3(view)); // remove translation part of the view matrix
        gl_Position = projection * rotView * vec4(aPos, 1.0);
    }
)";

// Skybox Fragment Shader
const char* skyboxFragment = R"(
    #version 430 core
    out vec4 FragColor;

    in vec3 TexCoords;

    uniform samplerCube skybox;

    void main()
    {    
        FragColor = texture(skybox, TexCoords);
    }
)";

int main() {
    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    // Create a GLFW window
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwSwapInterval(1); // Enable VSync to cap frame rate to monitor's refresh rate
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(2560, 1080, "OpenGL Basic Application", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    // Define the viewport dimensions
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);

    glEnable(GL_DEPTH_TEST);

    glfwMakeContextCurrent(window);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetScrollCallback(window, scrollCallback);

    // Initialize GLEW
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        return -1;
    }

    // Define the viewport dimensions
    glViewport(0, 0, 2560, 1080);

    // Load normal textures
    GLuint normalTextureId1 = loadTexture(FileSystemUtils::getAssetFilePath("textures/water_bump1.tga").c_str());
    GLuint normalTextureId2 = loadTexture(FileSystemUtils::getAssetFilePath("textures/water_bump2.tga").c_str());

    // Load cubemap
    std::vector<std::string> faces = {
        FileSystemUtils::getAssetFilePath("textures/cubemaps/snow_right.tga").c_str(),
        FileSystemUtils::getAssetFilePath("textures/cubemaps/snow_left.tga").c_str(),
        FileSystemUtils::getAssetFilePath("textures/cubemaps/snow_up.tga").c_str(),
        FileSystemUtils::getAssetFilePath("textures/cubemaps/snow_down.tga").c_str(),
        FileSystemUtils::getAssetFilePath("textures/cubemaps/snow_front.tga").c_str(),
        FileSystemUtils::getAssetFilePath("textures/cubemaps/snow_back.tga").c_str()
    };

    GLuint cubeTextureId = loadCubemap(faces);

    // Build and compile the shader program
    // Vertex Shader
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &waterVertex, NULL);
    glCompileShader(vertexShader);

    // Check for shader compile errors
    int success;
    char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        std::cerr << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
    }

    // Fragment Shader
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &waterFragment, NULL);
    glCompileShader(fragmentShader);

    // Check for shader compile errors
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        std::cerr << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
    }

    // Link shaders
    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    // Check for linking errors
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cerr << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
    }
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Set up vertex data (and buffer(s)) and configure vertex attributes
    float vertices[] = {
        // positions          // normals           // texture coords
        -0.5f, -0.5f, 0.0f,   0.0f, 0.0f, 1.0f,   0.0f, 0.0f,
         0.5f, -0.5f, 0.0f,   0.0f, 0.0f, 1.0f,   1.0f, 0.0f,
         0.5f,  0.5f, 0.0f,   0.0f, 0.0f, 1.0f,   1.0f, 1.0f,
         0.5f,  0.5f, 0.0f,   0.0f, 0.0f, 1.0f,   1.0f, 1.0f,
        -0.5f,  0.5f, 0.0f,   0.0f, 0.0f, 1.0f,   0.0f, 1.0f,
        -0.5f, -0.5f, 0.0f,   0.0f, 0.0f, 1.0f,   0.0f, 0.0f
    };

    GLuint VBO, VAO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Build and compile the skybox shader program
    // Skybox Vertex Shader
    GLuint skyboxVertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(skyboxVertexShader, 1, &skyboxVertex, NULL);
    glCompileShader(skyboxVertexShader);

    // Skybox Fragment Shader
    GLuint skyboxFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(skyboxFragmentShader, 1, &skyboxFragment, NULL);
    glCompileShader(skyboxFragmentShader);

    // Link shaders
    GLuint skyboxShaderProgram = glCreateProgram();
    glAttachShader(skyboxShaderProgram, skyboxVertexShader);
    glAttachShader(skyboxShaderProgram, skyboxFragmentShader);
    glLinkProgram(skyboxShaderProgram);

    // Clean up
    glDeleteShader(skyboxVertexShader);
    glDeleteShader(skyboxFragmentShader);

    // Set up vertex data and buffers for the skybox
    float skyboxVertices[] = {
        // positions          
        -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,

        -1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,

         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,

        -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,

        -1.0f,  1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f, -1.0f,

        -1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f,  1.0f
    };

    GLuint skyboxVBO, skyboxVAO;
    glGenVertexArrays(1, &skyboxVAO);
    glGenBuffers(1, &skyboxVBO);

    glBindVertexArray(skyboxVAO);

    glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), skyboxVertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    float deltaTime = 0.0f;
    float lastFrame = 0.0f;

    // Render loop
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // Input
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        // Render
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Camera movement
        const float cameraSpeed = 2.5f * deltaTime;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            camera.processKeyboardInput(GLFW_KEY_W, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            camera.processKeyboardInput(GLFW_KEY_S, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            camera.processKeyboardInput(GLFW_KEY_A, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            camera.processKeyboardInput(GLFW_KEY_D, deltaTime);

        glm::mat4 view = camera.getViewMatrix();
        glm::mat4 projection = camera.getProjectionMatrix(2560.0f / 1080.0f);

        // Draw the skybox
        glDepthFunc(GL_LEQUAL);
        glUseProgram(skyboxShaderProgram);

        glm::mat4 skyboxView = glm::mat4(glm::mat3(camera.getViewMatrix()));
        glUniformMatrix4fv(glGetUniformLocation(skyboxShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(skyboxView));
        glUniformMatrix4fv(glGetUniformLocation(skyboxShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

        glBindVertexArray(skyboxVAO);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubeTextureId);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
        glDepthFunc(GL_LESS);

        // Draw the triangle
        glUseProgram(shaderProgram);

        // Create a model matrix for the water plane
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(0.0f, 0.0f, 0.0f)); // Adjust the position as needed
        model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)); // Rotate by -90 degrees around the x-axis
        model = glm::scale(model, glm::vec3(100.0f, 100.0f, 100.0f)); // Adjust the scale as needed

        // Set the model matrix uniform in the shader program
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));

        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

        // Set uniform variables
        glUniform3fv(glGetUniformLocation(shaderProgram, "eyePos"), 1, glm::value_ptr(camera.getPosition()));
        glUniform1f(glGetUniformLocation(shaderProgram, "textureLerp"), 0.5f);

        float time = glfwGetTime();
        glUniform1f(glGetUniformLocation(shaderProgram, "time"), time);

        glUniform1f(glGetUniformLocation(shaderProgram, "strength1"), 0.05f);
        glUniform1f(glGetUniformLocation(shaderProgram, "strength2"), 0.1f);
        glUniform1f(glGetUniformLocation(shaderProgram, "strength3"), 0.05f);
        glUniform1f(glGetUniformLocation(shaderProgram, "strength4"), 0.1f);
        glUniform3f(glGetUniformLocation(shaderProgram, "waterColorTint"), 0.8f, 0.8f, 0.85f); // Adjust the tint as needed

        // Bind textures
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubeTextureId);
        glUniform1i(glGetUniformLocation(shaderProgram, "cubeTextureSampler"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, normalTextureId1);
        glUniform1i(glGetUniformLocation(shaderProgram, "normalTextureSampler1"), 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, normalTextureId2);
        glUniform1i(glGetUniformLocation(shaderProgram, "normalTextureSampler2"), 2);

        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Swap buffers and poll IO events
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Clean up
    glDeleteTextures(1, &normalTextureId1);
    glDeleteTextures(1, &normalTextureId2);
    glDeleteTextures(1, &cubeTextureId);

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);

    glDeleteVertexArrays(1, &skyboxVAO);
    glDeleteBuffers(1, &skyboxVBO);
    glDeleteProgram(skyboxShaderProgram);

    glfwTerminate();
    return 0;
}

GLuint loadTexture(const char* path)
{
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    // Set texture wrapping and filtering options
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Load and generate the texture
    int width, height, nrChannels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path, &width, &height, &nrChannels, 0);
    if (data)
    {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    else
    {
        std::cout << "Failed to load texture: " << path << std::endl;
    }
    stbi_image_free(data);

    return textureID;
}

unsigned int loadCubemap(std::vector<std::string> faces) {
    unsigned int textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

    int width, height, nrChannels;
    stbi_set_flip_vertically_on_load(false); // Ensure images are not flipped
    for (unsigned int i = 0; i < faces.size(); i++) {
        unsigned char* data = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);
        if (data) {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
            stbi_image_free(data);
        }
        else {
            std::cerr << "Cubemap texture failed to load at path: " << faces[i] << std::endl;
            stbi_image_free(data);
        }
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    return textureID;
}

void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    static bool firstMouse = true;
    static float lastX = 2560.0f / 2.0;
    static float lastY = 1080.0 / 2.0;

    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos;

    lastX = xpos;
    lastY = ypos;

    camera.processMouseMovement(xoffset, yoffset);
}

void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    camera.processMouseScroll(yoffset);
}
