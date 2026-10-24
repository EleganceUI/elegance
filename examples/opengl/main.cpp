// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// OpenGL: a spinning cube drawn by OpenGL on its own thread, shown in an Elegance window with
// controls over it. OpenGL draws straight into a GpuImage's shared textures (no copies) and
// signals its shared fence when each frame is done. SDL (which Elegance already uses) provides
// the OpenGL context. Windows only for now, like GpuImage.

#include <elg/elegance.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <type_traits>

using namespace elg;

namespace {

constexpr int kSize = 512; // pixels, square
constexpr int kSamples = 4;

// OpenGL beyond 1.1 is loaded at run time: framebuffer objects, and the extensions that import
// the image's Direct3D textures (GL_EXT_memory_object_win32) and fence (GL_EXT_semaphore_win32).
struct GlFunctions {
    PFNGLGENFRAMEBUFFERSPROC gen_framebuffers;
    PFNGLBINDFRAMEBUFFERPROC bind_framebuffer;
    PFNGLFRAMEBUFFERRENDERBUFFERPROC framebuffer_renderbuffer;
    PFNGLFRAMEBUFFERTEXTURE2DPROC framebuffer_texture_2d;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC check_framebuffer_status;
    PFNGLBLITFRAMEBUFFERPROC blit_framebuffer;
    PFNGLDELETEFRAMEBUFFERSPROC delete_framebuffers;
    PFNGLGENRENDERBUFFERSPROC gen_renderbuffers;
    PFNGLBINDRENDERBUFFERPROC bind_renderbuffer;
    PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC renderbuffer_storage_multisample;
    PFNGLDELETERENDERBUFFERSPROC delete_renderbuffers;
    PFNGLCREATEMEMORYOBJECTSEXTPROC create_memory_objects;
    PFNGLMEMORYOBJECTPARAMETERIVEXTPROC memory_object_parameter;
    PFNGLIMPORTMEMORYWIN32HANDLEEXTPROC import_memory;
    PFNGLTEXSTORAGEMEM2DEXTPROC tex_storage_mem_2d;
    PFNGLDELETEMEMORYOBJECTSEXTPROC delete_memory_objects;
    // Optional: without them, frames finish with glFinish().
    PFNGLGENSEMAPHORESEXTPROC gen_semaphores;
    PFNGLIMPORTSEMAPHOREWIN32HANDLEEXTPROC import_semaphore;
    PFNGLSEMAPHOREPARAMETERUI64VEXTPROC semaphore_parameter;
    PFNGLSIGNALSEMAPHOREEXTPROC signal_semaphore;
    PFNGLDELETESEMAPHORESEXTPROC delete_semaphores;

    bool load() {
        const auto get = [](auto& fn, const char* name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(SDL_GL_GetProcAddress(name));
            return fn != nullptr;
        };
        get(gen_semaphores, "glGenSemaphoresEXT");
        get(import_semaphore, "glImportSemaphoreWin32HandleEXT");
        get(semaphore_parameter, "glSemaphoreParameterui64vEXT");
        get(signal_semaphore, "glSignalSemaphoreEXT");
        get(delete_semaphores, "glDeleteSemaphoresEXT");
        return get(gen_framebuffers, "glGenFramebuffers") && get(bind_framebuffer, "glBindFramebuffer") &&
               get(framebuffer_renderbuffer, "glFramebufferRenderbuffer") &&
               get(framebuffer_texture_2d, "glFramebufferTexture2D") &&
               get(check_framebuffer_status, "glCheckFramebufferStatus") &&
               get(blit_framebuffer, "glBlitFramebuffer") && get(delete_framebuffers, "glDeleteFramebuffers") &&
               get(gen_renderbuffers, "glGenRenderbuffers") && get(bind_renderbuffer, "glBindRenderbuffer") &&
               get(renderbuffer_storage_multisample, "glRenderbufferStorageMultisample") &&
               get(delete_renderbuffers, "glDeleteRenderbuffers") &&
               get(create_memory_objects, "glCreateMemoryObjectsEXT") &&
               get(memory_object_parameter, "glMemoryObjectParameterivEXT") &&
               get(import_memory, "glImportMemoryWin32HandleEXT") &&
               get(tex_storage_mem_2d, "glTexStorageMem2DEXT") &&
               get(delete_memory_objects, "glDeleteMemoryObjectsEXT");
    }

    bool has_semaphores() const {
        return gen_semaphores && import_semaphore && semaphore_parameter && signal_semaphore && delete_semaphores;
    }
};

void draw_cube(float angle) {
    // Each face: a color, then its four corners.
    struct Face {
        float r, g, b;
        float corners[4][3];
    };
    static constexpr Face faces[] = {
        {1.00f, 0.37f, 0.43f, {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}}},     // front
        {0.28f, 0.81f, 0.45f, {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}}}, // back
        {0.24f, 0.65f, 1.00f, {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}}},     // top
        {1.00f, 0.76f, 0.44f, {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}}}, // bottom
        {0.64f, 0.42f, 1.00f, {{1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}}},     // right
        {0.95f, 0.95f, 0.97f, {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}}}, // left
    };

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.5, 0.5, -0.5, 0.5, 1, 20);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0, 0, -6);
    glRotatef(angle * 0.7f, 1, 0, 0);
    glRotatef(angle, 0, 1, 0);

    glBegin(GL_QUADS);
    for (const Face& face : faces) {
        for (int i = 0; i < 4; ++i) {
            // A little darker toward two corners, so the faces read as surfaces.
            const float shade = i < 2 ? 0.78f : 1.f;
            glColor3f(face.r * shade, face.g * shade, face.b * shade);
            glVertex3fv(face.corners[i]);
        }
    }
    glEnd();
}

// Renders the cube into `image` on its own thread, with its own context, until destroyed. Make it
// after the App (which starts SDL) and destroy it before.
class CubeRenderer {
public:
    explicit CubeRenderer(GpuImage image) : image_(std::move(image)) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        // The window is never shown; it only gives the context something to belong to.
        window_ = SDL_CreateWindow("OpenGL", 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        context_ = window_ ? SDL_GL_CreateContext(window_) : nullptr;
        if (!context_) {
            std::fprintf(stderr, "OpenGL context: %s\n", SDL_GetError());
            return;
        }
        SDL_GL_MakeCurrent(window_, nullptr); // the thread takes it
        thread_ = std::thread([this] { run(); });
    }

    ~CubeRenderer() {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
        if (context_) {
            SDL_GL_DestroyContext(context_);
        }
        if (window_) {
            SDL_DestroyWindow(window_);
        }
    }

    CubeRenderer(const CubeRenderer&) = delete;
    CubeRenderer& operator=(const CubeRenderer&) = delete;

    // Degrees per second (any thread).
    std::atomic<float> speed{60};

private:
    // One of the image's buffers, imported: a texture over its memory, and a framebuffer.
    struct Target {
        GLuint memory = 0;
        GLuint texture = 0;
        GLuint framebuffer = 0;
    };

    void run() {
        SDL_GL_MakeCurrent(window_, context_);
        GlFunctions gl{};
        if (!gl.load()) {
            std::fprintf(stderr, "OpenGL: GL_EXT_memory_object_win32 isn't supported\n");
            return;
        }

        // Draw multisampled, then resolve into the image's buffer.
        GLuint buffers[2];
        gl.gen_renderbuffers(2, buffers);
        gl.bind_renderbuffer(GL_RENDERBUFFER, buffers[0]);
        gl.renderbuffer_storage_multisample(GL_RENDERBUFFER, kSamples, GL_RGBA8, kSize, kSize);
        gl.bind_renderbuffer(GL_RENDERBUFFER, buffers[1]);
        gl.renderbuffer_storage_multisample(GL_RENDERBUFFER, kSamples, GL_DEPTH_COMPONENT24, kSize, kSize);
        GLuint samples = 0;
        gl.gen_framebuffers(1, &samples);
        gl.bind_framebuffer(GL_FRAMEBUFFER, samples);
        gl.framebuffer_renderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, buffers[0]);
        gl.framebuffer_renderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, buffers[1]);

        std::array<Target, GpuImage::kBuffers> targets{};
        GLuint semaphore = 0;
        float angle = 0;
        auto last = std::chrono::steady_clock::now();
        while (running_) {
            const auto now = std::chrono::steady_clock::now();
            angle += speed * std::chrono::duration<float>(now - last).count();
            last = now;

            // Nothing until a window has drawn (the image is made on the renderer's GPU).
            const std::optional<GpuImage::Frame> frame = image_.begin_frame();
            if (!frame) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            Target& target = targets[frame->buffer];
            if (!target.texture) {
                const GLint dedicated = GL_TRUE; // required for Direct3D 11 textures
                gl.create_memory_objects(1, &target.memory);
                gl.memory_object_parameter(target.memory, GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
                gl.import_memory(target.memory, GLuint64{frame->width} * frame->height * 4,
                                 GL_HANDLE_TYPE_D3D11_IMAGE_EXT, frame->texture);
                glGenTextures(1, &target.texture);
                glBindTexture(GL_TEXTURE_2D, target.texture);
                gl.tex_storage_mem_2d(GL_TEXTURE_2D, 1, GL_RGBA8, static_cast<GLsizei>(frame->width),
                                      static_cast<GLsizei>(frame->height), target.memory, 0);
                gl.gen_framebuffers(1, &target.framebuffer);
                gl.bind_framebuffer(GL_FRAMEBUFFER, target.framebuffer);
                gl.framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0);
                if (gl.check_framebuffer_status(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                    std::fprintf(stderr, "OpenGL: can't draw into the image's texture\n");
                    break;
                }
            }
            if (!semaphore && gl.has_semaphores()) {
                gl.gen_semaphores(1, &semaphore);
                gl.import_semaphore(semaphore, GL_HANDLE_TYPE_D3D12_FENCE_EXT, frame->fence);
            }

            gl.bind_framebuffer(GL_FRAMEBUFFER, samples);
            glViewport(0, 0, kSize, kSize);
            glClearColor(0.07f, 0.08f, 0.11f, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
            draw_cube(angle);

            gl.bind_framebuffer(GL_READ_FRAMEBUFFER, samples);
            gl.bind_framebuffer(GL_DRAW_FRAMEBUFFER, target.framebuffer);
            gl.blit_framebuffer(0, 0, kSize, kSize, 0, 0, static_cast<GLint>(frame->width),
                                static_cast<GLint>(frame->height), GL_COLOR_BUFFER_BIT, GL_NEAREST);

            if (semaphore) {
                // The fence reaches fence_value when the GPU has drawn the frame; Elegance's GPU
                // waits for it. No waiting here.
                gl.semaphore_parameter(semaphore, GL_D3D12_FENCE_VALUE_EXT, &frame->fence_value);
                const GLenum layout = GL_LAYOUT_COLOR_ATTACHMENT_EXT;
                gl.signal_semaphore(semaphore, 0, nullptr, 1, &target.texture, &layout);
                glFlush();
                image_.end_frame(*frame);
            } else {
                glFinish();
                image_.end_frame(*frame, GpuImage::Sync::finished);
            }

            std::this_thread::sleep_until(now + std::chrono::microseconds(16'667));
        }

        for (const Target& target : targets) {
            if (target.texture) {
                gl.delete_framebuffers(1, &target.framebuffer);
                glDeleteTextures(1, &target.texture);
                gl.delete_memory_objects(1, &target.memory);
            }
        }
        if (semaphore) {
            gl.delete_semaphores(1, &semaphore);
        }
        gl.delete_framebuffers(1, &samples);
        gl.delete_renderbuffers(2, buffers);
        SDL_GL_MakeCurrent(window_, nullptr);
    }

    GpuImage image_;
    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    std::atomic<bool> running_{true};
    std::thread thread_;
};

} // namespace

int main() {
    App app;
    // OpenGL draws the bottom row first.
    const GpuImage image({.width = kSize, .height = kSize, .origin = GpuImage::Origin::bottom_left});
    CubeRenderer cube(image);

    app.window({.title = "OpenGL", .width = 640, .height = 640}, [&] {
        const State<float> speed(cube.speed.load());
        const Binding<float> binding([=] { return speed(); },
                                     [=, &cube](float value) {
                                         speed = value;
                                         cube.speed = value;
                                     });
        return ZStack({.alignment = Alignment::bottom}, GpuImageView(image, ContentMode::fill),
                      HStack({.spacing = 12}, Text("Speed"), Slider(binding, {.min = -360, .max = 360}).frame({.width = 220}),
                             Text([=] { return std::to_string(static_cast<int>(speed())) + "°/s"; })
                                 .frame({.width = 64}))
                          .padding(12)
                          .background(Material::regular, Capsule())
                          .padding(20));
    });
    return app.run();
}
