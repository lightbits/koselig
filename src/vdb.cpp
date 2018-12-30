#define assert_LEVEL 2
#include "SDL.h"
#include "glad/glad.c"
#ifdef _WIN32
#include <winuser.h> // for Windows' SetWindowPos (allows you to set topmost)
#undef WIN32_LEAN_AND_MEAN // defined by glad
#endif
#include "SDL_syswm.h"
#include "imgui/imgui.cpp"
#include "imgui/imgui_draw.cpp"
#include "imgui/imgui_demo.cpp"
#include "imgui/imgui_impl_sdl_gl3.h"
#include "imgui/imgui_impl_sdl_gl3.cpp"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"
#include "vdb.h"
#include "vdb_config.h"
#include "_gl_state.cpp"
#include "_gl_error.cpp"
#include "_settings.cpp"
#include "_framegrab.cpp"
#include "_source_sans_pro.cpp"
#include "_texture.cpp"
#include "_shader.cpp"
#include "_sketch_mode.cpp"
#include "_ruler_mode.cpp"

#include "vdb_keyboard.cpp"
#include "vdb_mouse.cpp"

#include "_window.cpp"
#include "_uistuff.cpp"

struct vdb_globals_t
{
    const char *active_label;
    bool initialized;
    bool is_first_frame;
};

static vdb_globals_t vdb = {0};

#include "vdb_render_texture.cpp"
#include "vdb_transform.cpp"
#include "vdb_camera.cpp"
#include "vdb_immediate.cpp"
#include "vdb_immediate_util.cpp"
#include "vdb_shader.cpp"
#include "vdb_points.cpp"
#include "vdb_image.cpp"
#include "vdb_filter.cpp"

bool vdbIsFirstFrame()
{
    return vdb.is_first_frame;
}

void vdbDetachGLContext()
{
    window::DetachGLContext();
}

bool vdbBeginFrame(const char *label)
{
    static const char *skip_label = NULL;
    // static const char *prev_label = NULL;
    static bool is_first_frame = true;
    vdb.is_first_frame = is_first_frame;
    // prev_label = label;
    if (skip_label == label)
        return false;
    is_first_frame = false; // todo: first frame detection is janky.
                            // consider e.g. a for loop with single-stepping

    if (!vdb.initialized)
    {
        settings::LoadOrDefault(VDB_SETTINGS_FILENAME);
        window::Open(settings::window_x, settings::window_y, settings::window_w, settings::window_h);
        ImGui::CreateContext();
        ImGui_ImplSdlGL3_Init(window::sdl_window);
        ImGui::StyleColorsDark();
        ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF((const char*)source_sans_pro_compressed_data, source_sans_pro_compressed_size, VDB_FONT_HEIGHT);
        ImGui::GetStyle().WindowBorderSize = 0.0f;
        vdb.initialized = true;

        // Assuming user uploads images that are one-byte packed?
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        CheckGLError();
    }

    window::EnsureGLContextIsCurrent();
    window::PollEvents();

    static int save_settings_counter = VDB_SAVE_SETTINGS_PERIOD;
    if (save_settings_counter >= 0)
    {
        save_settings_counter--;
    }
    else
    {
        save_settings_counter = VDB_SAVE_SETTINGS_PERIOD;
        settings::Save(VDB_SETTINGS_FILENAME);
    }

    if (keys::pressed[SDL_SCANCODE_F10])
    {
        settings::Save(VDB_SETTINGS_FILENAME);
        is_first_frame = true;
        return false;
    }
    if (keys::pressed[SDL_SCANCODE_F5])
    {
        settings::Save(VDB_SETTINGS_FILENAME);
        is_first_frame = true;
        skip_label = label;
        return false;
    }
    if (window::should_quit)
    {
        settings::Save(VDB_SETTINGS_FILENAME);
        window::Close();
        exit(0);
    }

    transform::Reset();
    mouse_over::Reset();
    uistuff::escape_eaten = false;
    immediate_util::note_index = 0;
    transform::viewport_left = 0;
    transform::viewport_bottom = 0;
    transform::viewport_width = window::framebuffer_width;
    transform::viewport_height = window::framebuffer_height;
    mouse::ndc = vdbWindowToNDC((float)mouse::x, (float)mouse::y);

    ImGui_ImplSdlGL3_NewFrame(window::sdl_window);
    CheckGLError();

    if (VDB_HOTKEY_SKETCH_MODE) uistuff::sketch_mode_active = !uistuff::sketch_mode_active;

    if (uistuff::sketch_mode_active)
    {
        if (keys::pressed[SDL_SCANCODE_ESCAPE])
        {
            uistuff::sketch_mode_active = false;
            uistuff::escape_eaten = true;
        }
        bool undo = vdbIsKeyDown(VDB_KEY_LCTRL) && vdbWasKeyPressed(VDB_KEY_Z);
        bool redo = vdbIsKeyDown(VDB_KEY_LCTRL) && vdbWasKeyPressed(VDB_KEY_Y);
        bool clear = vdbWasKeyPressed(VDB_KEY_D);
        bool click = vdbIsMouseLeftDown();
        bool brightness = vdbIsKeyDown(VDB_KEY_LALT);
        float x = vdbGetMousePos().x;
        float y = vdbGetMousePos().y;
        vdbSketchMode(undo, redo, clear, brightness, click, x, y);
        ImGui::GetIO().WantCaptureKeyboard = true;
        ImGui::GetIO().WantCaptureMouse = true;
    }

    if (VDB_HOTKEY_RULER_MODE) uistuff::ruler_mode_active = !uistuff::ruler_mode_active;

    if (uistuff::ruler_mode_active)
    {
        if (keys::pressed[SDL_SCANCODE_ESCAPE])
        {
            uistuff::ruler_mode_active = false;
            uistuff::escape_eaten = true;
        }
        vdbRulerMode(vdbIsMouseLeftDown(), vdbGetMousePos());
        ImGui::GetIO().WantCaptureKeyboard = true;
        ImGui::GetIO().WantCaptureMouse = true;
    }

    glDisable(GL_COLOR_LOGIC_OP);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE);
    glLineWidth(1.0f);
    glPointSize(1.0f);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_TEXTURE_2D);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_DEPTH_TEST);
    glViewport(0, 0, window::framebuffer_width, window::framebuffer_height);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    CheckGLError();

    return true;
}

void vdbEndFrame()
{
    if (filter::taa_begun) vdbEndTAA();
    if (filter::tss_begun) vdbEndTSS();

    if (uistuff::sketch_mode_active)
        vdbSketchModePresent();

    if (uistuff::ruler_mode_active)
        vdbRulerModePresent();

    if (framegrab::active)
    {
        framegrab_options_t opt = framegrab::options;

        if (opt.draw_imgui)
        {
            ImGui::Render();
            ImGui_ImplSdlGL3_RenderDrawData(ImGui::GetDrawData());
        }

        GLenum format = opt.alpha_channel ? GL_RGBA : GL_RGB;
        int channels = opt.alpha_channel ? 4 : 3;
        int width = window::framebuffer_width;
        int height = window::framebuffer_height;
        unsigned char *data = (unsigned char*)malloc(width*height*channels);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadBuffer(GL_BACK);
        glReadPixels(0, 0, width, height, format, GL_UNSIGNED_BYTE, data);

        if (!opt.draw_imgui)
        {
            ImGui::Render();
            ImGui_ImplSdlGL3_RenderDrawData(ImGui::GetDrawData());
        }

        framegrab::SaveFrame(data, width, height, channels, format);

        free(data);

        if (keys::pressed[SDL_SCANCODE_ESCAPE])
        {
            framegrab::StopRecording();
            uistuff::escape_eaten = true;
        }
        else if (VDB_HOTKEY_FRAMEGRAB)
        {
            framegrab::StopRecording();
        }
    }
    else
    {
        uistuff::WindowSizeDialog();
        uistuff::FramegrabDialog();
        uistuff::ExitDialog();
        ImGui::Render();
        ImGui_ImplSdlGL3_RenderDrawData(ImGui::GetDrawData());
    }

    window::SwapBuffers(1.0f/60.0f);
    CheckGLError();
}
