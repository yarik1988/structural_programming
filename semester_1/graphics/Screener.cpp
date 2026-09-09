#include "glad/glad.h"
#include <SFML/Graphics.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>

#include "imgui-SFML.h"
#include "imgui.h"

typedef std::function<float(float, float)> binOp;
static const binOp ops[] = {
    [](float ic, float jc) {
      return 2 * cos(ic / 20.0f) + 2 * cos(jc / 20.0f);
    },
    [](float ic, float jc) { return (ic * ic + jc * jc) / 5000.0f; },
    [](float ic, float jc) { return cos(ic / 20.0f) * cos(jc / 20.0f); },
    [](float ic, float jc) {
      return (((int)ic * (int)ic / 100) ^ ((int)jc * (int)jc / 100)) / 1000.0f;
    },
    [](float ic, float jc) { return ic * jc / 2000.0f; },

    [](float ic, float jc) {
      return sin(((int)ic | (int)jc - (int)ic & (int)jc) / 100.0f);
    },
    [](float ic, float jc) { return sin(((int)ic ^ (int)jc) / 100.0f); },
    [](float ic, float jc) {
      return 0.5f * sin(jc / 10.0f) + ic / 100.0f +
             0.1f * rand() / (float)RAND_MAX;
    },
    [](float ic, float jc) { return log(fabs(ic * ic / 10.0f + jc) + 1); },
    [](float ic, float jc) { return ic / (10 * sin(jc / 10.0f) + 20.0f); },

    [](float ic, float jc) { return atan(ic / 100.0f) + atan(jc / 100.0f); },
    [](float ic, float jc) { return atan(ic * ic / (jc * jc + 100.0f)); },
    [](float ic, float jc) { return log(fabs(ic * ic / 10.0f + jc) + 1); },
    [](float ic, float jc) { return ic / (10 * sin(jc / 10.0f) + 20.0f); },
    [](float ic, float jc) {
      return exp(std::abs(ic + jc) / 200.0f) + 0.5f * sin(jc / 100.0f) +
             0.5f * cos(ic / 100.0f);
    },
    [](float ic, float jc) {
      // Scale the coordinates to a small range.
      double x = ic / 500.0, y = jc / 500.0;
      double cx = x, cy = y;
      int iter = 0, max_iter = 30;
      double zx = x, zy = y;

      while (zx * zx + zy * zy < 4.0 && iter < max_iter) {
        double xt = zx * zx - zy * zy + cx;
        zy = 2.0 * zx * zy + cy;
        zx = xt;
        iter++;
      }

      if (iter == max_iter) {
        return 1.0f;
      }
      double mag = std::sqrt(zx * zx + zy * zy);
      if (mag <= 0.0)
        mag = 1e-12;
      double mu = iter + 1.0 - std::log(std::log(mag)) / std::log(2.0);
      double smooth = mu / static_cast<double>(max_iter);
      return static_cast<float>(smooth);
    },
};
constexpr size_t ops_count = std::size(ops);
int cur_op = 0;
float speed[3] = {0.007, 0.013, 0.011};

struct Vertex {
  float position[2];
  float texcoord[2];
};

GLuint canvas_vao = 0;
GLuint canvas_vbo = 0;
GLuint canvas_tex = 0;
GLuint canvas_pgm = 0;

const Vertex qdata[6] = {
    {{-1.0f, -1.0f}, {0.0f, 0.0f}}, {{1.0f, -1.0f}, {1.0f, 0.0f}},
    {{-1.0f, 1.0f}, {0.0f, 1.0f}},  {{1.0f, -1.0f}, {1.0f, 0.0f}},
    {{1.0f, 1.0f}, {1.0f, 1.0f}},   {{-1.0f, 1.0f}, {0.0f, 1.0f}}};

const char *gleGetErrorString(GLenum code) {
  switch (code) {
  case GL_NO_ERROR:
    return "No error has been recorded.";
  case GL_INVALID_ENUM:
    return "An enumerated argument is assigned an invalid value.";
  case GL_INVALID_VALUE:
    return "A numeric argument is out of range.";
  case GL_INVALID_OPERATION:
    return "The specified operation is not allowed in the current state.";
  case GL_STACK_OVERFLOW:
    return "The specified operation would cause a stack overflow.";
  case GL_STACK_UNDERFLOW:
    return "The specified operation would cause a stack underflow.";
  case GL_OUT_OF_MEMORY:
    return "The specified operation is out of memory.";
  default:
    return "An unknown error has occurred.";
  }
}

float tr = 0.0f;
float tg = 0.0f;
float tb = 0.0f;

float zoom = 1.0f;
float offsetX = 0.0f;
float offsetY = 0.0f;
bool isDragging = false;
sf::Vector2i lastMousePos;

void init(float *arr, unsigned int DimX, unsigned int DimY) {
  unsigned int numThreads = std::thread::hardware_concurrency();
  if (numThreads == 0)
    numThreads = 4; // Fallback
  std::vector<std::thread> threads;
  unsigned int chunk = DimX / numThreads;

  for (unsigned int t = 0; t < numThreads; ++t) {
    unsigned int start = t * chunk;
    unsigned int end = (t == numThreads - 1) ? DimX : (t + 1) * chunk;
    threads.emplace_back([=]() {
      for (int i = start; i < end; ++i) {
        for (int j = 0; j < DimY; ++j) {
          float x = (i - DimX / 2.0f) / zoom + offsetX;
          float y = (j - DimY / 2.0f) / zoom + offsetY;
          arr[j * DimX + i] = ops[cur_op](x, y);
        }
      }
    });
  }
  for (auto &t : threads)
    t.join();
}

inline float anima(float x) {
  float drob = x > 0 ? x - (int)x : (int)x - x;
  return 2 * std::min(drob, 1 - drob);
}

void display(float *cdata, float *arr, unsigned int DimX, unsigned int DimY) {
  tr += speed[0];
  tg += speed[1];
  tb += speed[2];
  for (int i = 0; i < DimX; i++)
    for (int j = 0; j < DimY; j++) {
      cdata[3 * (j * DimX + i)] = anima(tr + arr[j * DimX + i]);
      cdata[3 * (j * DimX + i) + 1] = anima(tg + arr[j * DimX + i]);
      cdata[3 * (j * DimX + i) + 2] = anima(tb + arr[j * DimX + i]);
    }
  glUseProgram(canvas_pgm);
  glActiveTexture(GL_TEXTURE0);
  glUniform1i(glGetUniformLocation(canvas_pgm, "canvas_tex"), 0);
  glBindTexture(GL_TEXTURE_2D, canvas_tex);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DimX, DimY, GL_RGB, GL_FLOAT, cdata);
  glBindVertexArray(canvas_vao);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
  glUseProgram(0);
}

// load shader program function
GLuint loadShaderProgram(const char *vs_source, const char *fs_source) {
  GLint ok = GL_FALSE;
  typedef unsigned char byte_t;
  GLuint vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vs, 1, &vs_source, 0);
  glCompileShader(vs);
  glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    fprintf(stderr, "Cannot compile vertex shader!\n");
    glDeleteShader(vs);
    return 0;
  }

  GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fs, 1, &fs_source, 0);
  glCompileShader(fs);
  glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    fprintf(stderr, "Cannot compile fragment shader!\n");
    glDeleteShader(vs);
    glDeleteShader(fs);
    return 0;
  }

  GLuint pgm = glCreateProgram();
  glAttachShader(pgm, vs);
  glAttachShader(pgm, fs);
  glLinkProgram(pgm);
  glGetProgramiv(pgm, GL_LINK_STATUS, &ok);
  if (!ok) {
    fprintf(stderr, "Cannot link shader program!\n");
    glDeleteShader(vs);
    glDeleteShader(fs);
    glDeleteProgram(pgm);
    return 0;
  }

  glDeleteShader(vs);
  glDeleteShader(fs);

  return pgm;
}
/************************************************************/
GLenum setupRenderEnvironment(unsigned int DimX, unsigned int DimY) {
  // Use GLSL version 330 core, matching the context we requested
  const char *vertexShaderSource =
      "#version 330 core\n"
      "in vec2 vertex_pos;" // 'in' instead of 'attribute'
      "in vec2 texel_pos;"  // 'in' instead of 'attribute'
      "out vec2 texcoord;"
      "void main()"
      "{"
      "  gl_Position = vec4(vertex_pos, 0.0, 1.0);"
      "  texcoord = texel_pos;"
      "}";

  const char *fragmentShaderSource =
      "#version 330 core\n"
      "out vec4 FragColor;" // Use 'out vec4' for the final color
      "in vec2 texcoord;"
      "uniform sampler2D canvas_tex;"
      "void main()"
      "{"
      // Use texture() function, which is correct for modern GLSL
      "  FragColor = texture(canvas_tex, texcoord);"
      "}";
  canvas_pgm = loadShaderProgram(vertexShaderSource, fragmentShaderSource);
  if (!canvas_pgm)
    return glGetError();

  glGenVertexArrays(1, &canvas_vao);
  glBindVertexArray(canvas_vao);

  glGenBuffers(1, &canvas_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, canvas_vbo);

  glBufferData(GL_ARRAY_BUFFER, sizeof(qdata), qdata, GL_STATIC_DRAW);

  glVertexAttribPointer(glGetAttribLocation(canvas_pgm, "vertex_pos"), 2,
                        GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (const GLvoid *)offsetof(Vertex, position));
  glEnableVertexAttribArray(glGetAttribLocation(canvas_pgm, "vertex_pos"));

  glVertexAttribPointer(glGetAttribLocation(canvas_pgm, "texel_pos"), 2,
                        GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (const GLvoid *)offsetof(Vertex, texcoord));
  glEnableVertexAttribArray(glGetAttribLocation(canvas_pgm, "texel_pos"));

  glGenTextures(1, &canvas_tex);
  glBindTexture(GL_TEXTURE_2D, canvas_tex);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, DimX, DimY, 0, GL_RGB, GL_FLOAT, 0);

  glBindTexture(GL_TEXTURE_2D, 0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
  return glGetError();
}
int main() {
  // 1. Request a specific OpenGL version (this part is the same)
  sf::ContextSettings settings;
  settings.depthBits = 24;
  settings.stencilBits = 8;
  settings.majorVersion = 3;
  settings.minorVersion = 3;
  const auto &modes = sf::VideoMode::getFullscreenModes();
  auto selected_mode = modes.front();
  unsigned int DimX = selected_mode.size.x;
  unsigned int DimY = selected_mode.size.y;
  constexpr auto window_state = SCREENER_WINDOW_MODE == 2
                                  ? sf::State::Fullscreen
                                  : sf::State::Windowed;
  sf::RenderWindow window(selected_mode, "Screener", window_state,
                          settings);
  window.setVerticalSyncEnabled(true);
  window.setFramerateLimit(60);
  if (!gladLoadGLLoader(
          reinterpret_cast<GLADloadproc>(sf::Context::getFunction))) {
    std::cerr << "Failed to initialize GLAD" << std::endl;
    return EXIT_FAILURE;
  }
  std::cout << "Using OpenGL version: " << glGetString(GL_VERSION) << std::endl;
  GLenum res = setupRenderEnvironment(DimX, DimY);
  if (res != GL_NO_ERROR) {
    std::cout << "Error in setupRenderEnvironment: " << gleGetErrorString(res);
    return EXIT_FAILURE;
  }
  // Now init ImGui-SFML (after GL loader)
  bool imgui_init_res = ImGui::SFML::Init(window);
  if (!imgui_init_res) {
    std::cerr << "ImGui-SFML init failed\n";
    return EXIT_FAILURE;
  }
  auto *arr = new float[DimY * DimX];
  auto *cdata = new float[DimY * DimX * 3];
  init(arr, DimX, DimY);
  sf::Clock deltaClock;
  bool needsUpdate = false;
  bool zoomingIn = false;
  bool zoomingOut = false;
  while (window.isOpen()) {
    while (const std::optional event = window.pollEvent()) {
      ImGui::SFML::ProcessEvent(window, *event);
      if (const auto *keyPressed = event->getIf<sf::Event::KeyPressed>()) {
        if (keyPressed->scancode == sf::Keyboard::Scan::Escape)
          window.close();
        if (keyPressed->scancode == sf::Keyboard::Scan::Equal ||
            keyPressed->scancode == sf::Keyboard::Scan::NumpadPlus) {
          zoomingIn = true;
        }
        if (keyPressed->scancode == sf::Keyboard::Scan::Hyphen ||
            keyPressed->scancode == sf::Keyboard::Scan::NumpadMinus) {
          zoomingOut = true;
        }
      }
      if (const auto *keyReleased = event->getIf<sf::Event::KeyReleased>()) {
        if (keyReleased->scancode == sf::Keyboard::Scan::Equal ||
            keyReleased->scancode == sf::Keyboard::Scan::NumpadPlus) {
          zoomingIn = false;
        }
        if (keyReleased->scancode == sf::Keyboard::Scan::Hyphen ||
            keyReleased->scancode == sf::Keyboard::Scan::NumpadMinus) {
          zoomingOut = false;
        }
      }
      if (event->is<sf::Event::Closed>())
        window.close();

      if (const auto *mousePressed =
              event->getIf<sf::Event::MouseButtonPressed>()) {
        if (mousePressed->button == sf::Mouse::Button::Left &&
            !ImGui::GetIO().WantCaptureMouse) {
          isDragging = true;
          lastMousePos = sf::Mouse::getPosition(window);
        }
      }
      if (const auto *mouseReleased =
              event->getIf<sf::Event::MouseButtonReleased>()) {
        if (mouseReleased->button == sf::Mouse::Button::Left) {
          isDragging = false;
        }
      }
      if (const auto *mouseMoved = event->getIf<sf::Event::MouseMoved>()) {
        if (isDragging) {
          sf::Vector2i currentMousePos = sf::Mouse::getPosition(window);
          sf::Vector2i delta = currentMousePos - lastMousePos;
          offsetX -= delta.x / zoom;
          offsetY += delta.y / zoom;
          lastMousePos = currentMousePos;
          needsUpdate = true;
        }
      }
      if (const auto *mouseWheelScrolled =
              event->getIf<sf::Event::MouseWheelScrolled>()) {
        if (mouseWheelScrolled->wheel == sf::Mouse::Wheel::Vertical &&
            !ImGui::GetIO().WantCaptureMouse) {
          if (mouseWheelScrolled->delta > 0)
            zoom *= 1.05f;
          else
            zoom /= 1.05f;
          needsUpdate = true;
        }
      }
    }
    if (zoomingIn) {
      zoom *= 1.02f;
      needsUpdate = true;
    }
    if (zoomingOut) {
      zoom /= 1.02f;
      needsUpdate = true;
    }

    if (needsUpdate) {
      init(arr, DimX, DimY);
      needsUpdate = false;
    }
    ImGui::SFML::Update(window, deltaClock.restart());
    // IMGUI: Create the UI window and sliders
    ImGui::Begin("Controls"); // Create a window

    ImGui::SliderFloat("Red Speed", &speed[0], -0.02f, 0.02f);
    ImGui::SliderFloat("Green Speed", &speed[1], -0.02f, 0.02f);
    ImGui::SliderFloat("Blue Speed", &speed[2], -0.02f, 0.02f);
    if (ImGui::SliderInt("Function", &cur_op, 0, ops_count - 1)) {
      // If the slider was moved, re-initialize the background array.
      init(arr, DimX, DimY);
    }
    ImGui::End(); // End the window
    window.clear();
    display(cdata, arr, DimX, DimY);
    ImGui::SFML::Render(window);
    window.display();
  }
  ImGui::SFML::Shutdown();
  delete[] arr;
  delete[] cdata;
  return EXIT_SUCCESS;
}
