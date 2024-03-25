
#if defined _DEBUG
#define mop_debug
#endif

#include "mo_basic.h"

#include "mo_platform.h"
#include "mo_memory_arena.h"
#include "mo_audio.h"

#define STBTT_assert(x) STBTT_assert_wrapper(x)
#include "stb_truetype.h"

#include "stb_image.h"

#define moui_gl_implementation
#include "mo_ui.h"

#define mop_implementation
#include "mo_platform.h"

#define moa_implementation
#include "mo_audio.h"

#include "mo_math.h"

void STBTT_assert_wrapper(b8 condition)
{
    assert(condition);
}

#include "editor.h"

#define moma_implementation
#include "mo_memory_arena.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define moui_implementation
#include "mo_ui.h"

#define mos_implementation
#include "mo_string.h"

#include "mo_random_pcg.h"

typedef struct
{
    u32 max_file_display_count;
    b8  show_white_space;
} editor_settings;

typedef struct
{
    moma_arena         memory;
    mop_window         window;

    moui_default_state ui;
    moui_simple_font   font;
    // box2               ui_letterbox;

    editor_state    editor;
    editor_settings settings;

    random_pcg random;

    usize memory_ui_reset_count;
    usize memory_frame_reset_count;
} program_state;

program_state global_program;

typedef struct
{
    u8 *text_base;
    f32 x_distance;
    f32 y_distance;
} draw_text_closest_hit;

const rgba background_color = { 0.1f, 0.1f, 0.1f, 1.0f };
const rgba text_color       = { 0.9f, 0.9f, 0.9f, 1.0f };
const rgba text_color_match = { 1.0f, 0.4f, 0.1f, 1.0f };
const rgba caption_color    = { 0.3f, 0.3f, 1.0f, 1.0f };
const rgba caret_color = { 0.2f, 1.0f, 0.2f, 0.5f };

typedef enum
{
    draw_layer_background,
    draw_layer_caption,
    draw_layer_caption_inset,
    draw_layer_text,
    draw_layer_caret,

    draw_layer_count,
} draw_layer;

#define draw_single_line_text_signature void draw_single_line_text(moui_state *ui, moui_simple_font font, moui_text_cursor *draw_cursor, editor_editable_buffer *buffer, editor_settings settings)
draw_single_line_text_signature;

#define draw_text_advance_signature void draw_text_advance(moui_state *ui, editor_settings settings, rgba color, moui_simple_text_iterator *iterator, draw_text_closest_hit *closest_hit)
draw_text_advance_signature;

#if 1
int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
#else
int main(int argument_count, char *arguments[])
#endif
{
    mop_platform         platform = {0};
    mop_hot_reload_state hot_reload_state = {0};
    program_state *program = &global_program;

    mop_init(&platform);
    mop_window_init(&platform, &program->window, "moed", 1280, 720);

    moui_default_init(&program->ui);
    moui_default_window ui_window = moui_get_default_platform_window(&program->ui, &platform, program->window);
    moui_default_window_init(&program->ui, &ui_window);

    moma_create(&program->memory, &platform, ((usize) 2) << 30);

    program->random = random_from_win23();

    // WARNING: all memory allocation from this line on is frame temporary

    program->memory_ui_reset_count = program->memory.used_count;

    // allocate font
    s32 pixel_height = 22;
    s32 thickness    = 2;
    // program->font = moui_load_outlined_font_file(&platform, &program->memory, s("C:/windows/fonts/consola.ttf"), 1024, 1024, pixel_height, ' ', 96, thickness, moui_rgba_white, moui_rgba_black);
    program->font = moui_load_font_file(&platform, &program->memory, s("C:/windows/fonts/consola.ttf"), 1024, 1024, pixel_height, ' ', 96);

    program->ui.base.renderer.quad_count = 1 << 20;
    program->ui.base.renderer.texture_count = 64;
    program->ui.base.renderer.command_count = 1024;
    moui_resize_buffers(&program->ui.base, &program->memory);

    program->memory_frame_reset_count = program->memory.used_count;

    editor_init(&program->editor, &platform);
    program->settings.max_file_display_count = 16;
    program->settings.show_white_space = true;

    {
        editor_state *editor = &program->editor;
        mop_command_line_info info = mop_get_command_line_info(&platform);

        mop_string text_buffer = {0};
        text_buffer.count = info.text_byte_count;
        moma_reallocate_array(&program->memory, &text_buffer, u8);
        string *arguments = moma_allocate_array(&program->memory, string, info.argument_count);

        mop_get_command_line_arguments(text_buffer, info.argument_count, arguments);

        // skip first arguments, the exe name
        for (u32 i = 1; i < info.argument_count; i++)
        {
            if (mop_path_is_directory(&platform, arguments[i]))
            {
                string255 path = string255_from_string(arguments[i]);
                assert(path.count < carray_count(path.base));
                path.base[path.count] = '/';
                path.count += 1;
                editor->active_buffer_index = editor_directory_load_all_files(&platform, editor, string255_to_string(path));
            }
            else
            {
                u32 buffer_count = editor->buffers.count;
                u32 buffer_index = editor_buffer_find_or_add(editor, arguments[i], true);
                if (buffer_count == buffer_index)
                {
                    editor_buffer_load_file(&platform, editor, buffer_index);
                    editor->active_buffer_index = buffer_index;
                }
            }
        }
    }

    mop_u64 realtime_counter = mop_get_realtime_counter(&platform);
    platform.delta_seconds = 0.0f;
    platform.last_realtime_counter = realtime_counter;

    while (true)
    {
        mop_handle_messages(&platform);

        b8 did_reload = mop_hot_reload(&platform, &hot_reload_state, s("hot"));

        hot_reload_state.hot_update(&platform, sl(u8_array) { (u8 *) program, sizeof(*program) }, did_reload);

        // give update a chance to catch and override quit
        if (platform.do_quit)
            break;

        moui_default_render_begin(&program->ui, &ui_window);

        // moui_default_render_prepare_execute_viewport(&program->ui, program->ui_letterbox);
        moui_default_render_prepare_execute(&program->ui);

        moui_execute(&program->ui.base);
        moui_resize_buffers(&program->ui.base, &program->memory);

        moui_default_render_end(&program->ui, &ui_window, true);
    }

    return 0;
}

mop_hot_update_signature
{
    program_state *program = (program_state *) data.base;
    moui_state *ui = &program->ui.base;
    f32 delta_seconds = platform->delta_seconds;

    #if defined mop_debug
    if (did_reload)
    {

    }
    #endif

    mop_window_info window_info = mop_window_get_info(platform, &program->window);
    vec2 ui_size = { (f32) window_info.size.x, (f32) window_info.size.y };
    vec2 mouse_position = { (f32) window_info.relative_mouse_position.x, (f32) window_info.relative_mouse_position.y };

    #if 0
    const f32 target_width_over_height = 16.0f / 9;
    f32 width_over_height = ui_size.x / ui_size.y;

    box2 letterbox;
    if (width_over_height > target_width_over_height)
    {
        f32 width = ui_size.y * target_width_over_height;
        letterbox.min.x = floorf((ui_size.x - width) * 0.5f);
        letterbox.max.x = ui_size.x - letterbox.min.x;
        letterbox.min.y = 0;
        letterbox.max.y = ui_size.y;

        ui_size.x = width;
    }
    else
    {
        f32 height = ui_size.x / target_width_over_height;
        letterbox.min.x = 0;
        letterbox.max.x = ui_size.x;
        letterbox.min.y = floorf((ui_size.y - height) * 0.5f);
        letterbox.max.y = ui_size.y - letterbox.min.y;

        ui_size.y = height;
    }

    program->ui_letterbox = letterbox;

    mouse_position = vec2_sub(mouse_position, letterbox.min);
    #endif

    const f32 target_height = 360;

    // auto resize font depending on resolution
    if (false)
    {
        s32 pixel_height = ceilf(16 * ui_size.y / target_height);

        if (program->font.height != pixel_height)
        {
            program->memory.used_count = program->memory_ui_reset_count;

            if (program->font.texture.handle)
            {
                u32 handle = (u32)(usize)program->font.texture.handle;
                glDeleteTextures(1, &handle);
            }

            s32 thickness = ceilf(2 * ui_size.y / target_height);

            program->font = moui_load_outlined_font_file(platform, &program->memory, s("assets/fonts/steelfis.ttf"), 1024, 1024, pixel_height, ' ', 96, thickness, moui_rgba_white, moui_rgba_black);

            ui->renderer.quads = null;
            moui_resize_buffers(ui, &program->memory);

            program->memory_frame_reset_count = program->memory.used_count;
        }
    }

    moui_simple_font font = program->font;

    program->memory.used_count = program->memory_frame_reset_count;

    u32 visible_line_count           = (u32) ceilf(ui_size.y / font.line_spacing);
    u32 visible_line_character_count = (u32) ceilf(ui_size.x / (font.left_margin + font.right_margin));

    editor_state *editor = &program->editor;
    editor_update(editor, platform, visible_line_count, visible_line_character_count);

    if (platform->do_quit || window_info.requested_close)
    {
        b8 can_close = true;
        for (u32 buffer_index = 0; buffer_index < editor->buffers.count; buffer_index++)
        {
            editor_buffer *buffer = &editor->buffers.base[buffer_index];
            if (buffer->has_changed)
            {
                can_close = false;
            }
            else
            {
                editor->buffers.count -= 1;
                memcpy(&editor->buffers.base[buffer_index], &editor->buffers.base[editor->buffers.count], sizeof(editor_buffer));
                buffer_index -= 1;
            }
        }

        moma_reallocate_array(&editor->memory, &editor->buffers, editor_buffer);

        if (editor->active_buffer_index >= editor->buffers.count)
            editor->active_buffer_index = 0;

        platform->do_quit = can_close;
        if (platform->do_quit)
            return;
    }

    {
        b8 left_is_active = platform->keys[mop_key_mouse_left].is_active || mop_key_was_pressed(platform, mop_key_mouse_left);
        moui_frame(ui, ui_size, mouse_position, left_is_active);
    }

    moui_box(ui, draw_layer_background, moui_to_quad_colors(background_color), sl(box2) { 0, 0, ui_size });

    f32 inset = 4;
    moui_text_cursor draw_cursor = moui_text_cursor_at_top(font, sl(moui_vec2) { inset + font.left_margin, ui_size.y - inset });
    // moui_printf(ui, font, 0, sl(moui_rgba) { 1.0f, 1.0f, 1.0f, 1.0f }, &draw_cursor, "fps: %f\n", 1.0f / platform->delta_seconds);

    editor_buffer *active_buffer = editor_active_buffer_get(editor);

    {
        moui_box2 used_box = moui_used_box_begin(ui);

        draw_cursor.position.x += inset;
        draw_cursor.line_start_x = draw_cursor.position.x;

        string title = string255_to_string(active_buffer->title);
        moui_printf(ui, font, 2, sl(moui_rgba) { 1.0f, 1.0f, 1.0f, 1.0f }, &draw_cursor, "%.*s", fs(title));

        if (active_buffer->file_save_error)
            moui_printf(ui, font, draw_layer_text, sl(moui_rgba) { 1.0f, 0.5f, 0.1f, 1.0f }, &draw_cursor, " !");

        if (active_buffer->has_changed)
            moui_printf(ui, font, draw_layer_text, sl(moui_rgba) { 1.0f, 0.5f, 0.1f, 1.0f }, &draw_cursor, " *");

        moui_printf(ui, font, draw_layer_text, sl(moui_rgba) { 1.0f, 1.0f, 1.0f, 1.0f }, &draw_cursor, "\n");

        switch (editor->mode)
        {
            case editor_focus_file_search:
            {
                moui_printf(ui, font, draw_layer_text, sl(moui_rgba) { 1.0f, 1.0f, 1.0f, 1.0f }, &draw_cursor, "Open File: ");

                f32 min_x = draw_cursor.position.x;
                moui_box2 edit_box = moui_used_box_begin(ui);

                editor_editable_buffer buffer = editor_buffer255_edit_begin(editor, &editor->file_open_relative_path);

                draw_single_line_text(ui, font,  &draw_cursor, &buffer, program->settings);

                editor_buffer255_edit_end(editor, &editor->file_open_relative_path, buffer);

                moui_printf(ui, font, draw_layer_text, sl(moui_rgba) { 1.0f, 1.0f, 1.0f, 1.0f }, &draw_cursor, "\n");

                edit_box = moui_used_box_end(ui, edit_box);
                edit_box.min.x = min_x - inset; // some room for caret
                edit_box.max.x = ui_size.x - inset * 2;
                edit_box = box2_grow(edit_box, 2);
                moui_rounded_box(ui, draw_layer_caption_inset, moui_to_quad_colors(background_color), edit_box, 4);

                ui->renderer.used_box.min.y -= inset - 2;
                draw_cursor.position.y -= inset;

                editor_file_search *file_search  = &editor->file_search;
                editor_file_search_filter filter;
                editor_file_search_filter_get(&filter, platform, buffer.text);

                assert(file_search->result_count >= file_search->display_offset);

                u32 display_count = min(program->settings.max_file_display_count, file_search->result_count - file_search->display_offset);

                for (u32 i = 0; i < display_count; i++)
                {
                    mop_file_search_result result = file_search->results[i + file_search->display_offset];
                    editor_file_search_filter_result filter_result = editor_file_search_filter_check(filter, result);
                    assert(filter_result.ok);

                    string suffix = filter_result.suffix;
                    string match  = filter_result.match;

                    string left  = { suffix.base, (usize) (match.base - suffix.base) };
                    string right = { match.base + match.count, (usize) (suffix.base + suffix.count - match.base - match.count) };

                    vec2 start = draw_cursor.position;

                    if (result.is_parent_directory || result.is_search_directory)
                        moui_printf(ui, font, draw_layer_text, text_color_match, &draw_cursor, "%.*s/", fs(result.filepath));
                    else if (filter.search_directory.count)
                        moui_printf(ui, font, draw_layer_text, text_color_match, &draw_cursor, "%.*s/", fs(filter.search_directory));

                    moui_printf(ui, font, draw_layer_text, text_color,       &draw_cursor, "%.*s", fs(left));
                    moui_printf(ui, font, draw_layer_text, text_color_match, &draw_cursor, "%.*s", fs(match));
                    moui_printf(ui, font, draw_layer_text, text_color,       &draw_cursor, "%.*s", fs(right));

                    if (result.is_directory && !(result.is_parent_directory || result.is_search_directory))
                        moui_printf(ui, font, draw_layer_text, text_color, &draw_cursor, "/\n");
                    else
                        moui_printf(ui, font, draw_layer_text, text_color, &draw_cursor, "\n");

                    if (i + file_search->display_offset == file_search->selected_index)
                    {
                        box2 box;
                        box.min.x = inset * 2; // some room for caret
                        box.max.x = ui_size.x - inset * 2;
                        box.min.y = start.y - font.bottom_to_line;
                        box.max.y = start.y + font.line_to_top;
                        box = box2_grow(box, 2);
                        moui_rounded_box(ui, draw_layer_caption_inset, moui_to_quad_colors(background_color), box, 4);
                    }
                }
            } break;

            case editor_focus_search:
            {
                moui_printf(ui, font, draw_layer_text, sl(moui_rgba) { 1.0f, 1.0f, 1.0f, 1.0f }, &draw_cursor, "Search: ");

                f32 min_x = draw_cursor.position.x;
                moui_box2 edit_box = moui_used_box_begin(ui);

                editor_editable_buffer buffer = editor_buffer255_edit_begin(editor, &editor->search_buffer);

                draw_single_line_text(ui, font,  &draw_cursor, &buffer, program->settings);

                editor_buffer255_edit_end(editor, &editor->search_buffer, buffer);

                moui_printf(ui, font, draw_layer_text, sl(moui_rgba) { 1.0f, 1.0f, 1.0f, 1.0f }, &draw_cursor, "\n");

                edit_box = moui_used_box_end(ui, edit_box);
                edit_box.min.x = min_x - inset; // some room for caret
                edit_box.max.x = ui_size.x - inset * 2;
                edit_box = box2_grow(edit_box, 2);
                moui_rounded_box(ui, draw_layer_caption_inset, moui_to_quad_colors(background_color), edit_box, 4);

                ui->renderer.used_box.min.y -= inset - 2;
                draw_cursor.position.y -= inset;
            } break;
        }

        used_box = moui_used_box_end(ui, used_box);
        used_box.min.x = inset;
        used_box.max.x = ui_size.x - inset;
        used_box = box2_grow(used_box, 2);
        moui_rounded_box(ui, draw_layer_caption, moui_to_quad_colors(caption_color), used_box, 4);

        draw_cursor.position.x -= inset;
        draw_cursor.line_start_x = draw_cursor.position.x;
        draw_cursor.position.y -= inset;
    }

    {
        editor_editable_buffer editable_buffer = editor_buffer_edit_begin(editor, active_buffer);

        editable_buffer.text = mos_remaining_substring(editable_buffer.text, active_buffer->draw_line_offset);

        assert(editable_buffer.cursor_offset >= active_buffer->draw_line_offset);
        u32 cursor_offset = editable_buffer.cursor_offset - active_buffer->draw_line_offset;

        editable_buffer.cursor_offset = 0;

        for (u32 i = 0; i < editor->visible_line_count; i++)
            editor_buffer_to_next_line_start(editor, &editable_buffer);

        editable_buffer.text.count = editable_buffer.cursor_offset;
        editable_buffer.cursor_offset = cursor_offset;

        draw_single_line_text(ui, font,  &draw_cursor, &editable_buffer, program->settings);

        active_buffer->cursor_offset = editable_buffer.cursor_offset + active_buffer->draw_line_offset;

        // editor_buffer_edit_end(editor, active_buffer, editable_buffer);
    }
}

draw_single_line_text_signature
{
    string text = buffer->text;
    assert(buffer->cursor_offset <= text.count);

    string left = { text.base, buffer->cursor_offset };

    moui_simple_text_iterator text_iterator = { font, *draw_cursor, left };

    moui_set_command_texture(ui, draw_layer_text, font.texture);

    draw_text_closest_hit closest_hit = {0};
    closest_hit.x_distance = 1000000.0f;
    closest_hit.y_distance = 1000000.0f;

    while (text_iterator.text.count)
    {
        draw_text_advance(ui, settings, text_color, &text_iterator, &closest_hit);
    }

    box2 caret_box;
    caret_box.min = text_iterator.cursor.position;
    caret_box.min.x -= 2;
    caret_box.min.y -= font.bottom_to_line;
    caret_box.max = vec2_add(caret_box.min, sl(vec2) { 5, (f32) font.line_height });

    // right
    text_iterator.text = sl(string) { text.base + buffer->cursor_offset, text.count - buffer->cursor_offset };
    while (text_iterator.text.count)
    {
        draw_text_advance(ui, settings, text_color, &text_iterator, &closest_hit);
    }

    if (closest_hit.text_base)
    {
        buffer->cursor_offset = (u32) (closest_hit.text_base - buffer->text.base);
        assert(buffer->cursor_offset <= text.count);
    }

    moui_rounded_box(ui, draw_layer_caret, moui_to_quad_colors(caret_color), caret_box, 2);

    *draw_cursor = text_iterator.cursor;
}

draw_text_advance_signature
{
    if (!iterator->text.count)
        return;

    u8 *left_base = iterator->text.base;

    mos_utf8_result result = mos_utf8_advance(&iterator->text);

    u8 *right_base = iterator->text.base;

    moui_u32 render_code = result.utf32_code;
    if (render_code == '\n')
    {
        if (settings.show_white_space)
        {
            render_code = 'n';
            *(vec3 *) &color = vec3_scale(*(vec3 *) &color, 0.5f);
        }
        else
        {
            render_code = ' ';
        }

        right_base = left_base; // new line bounding boxes should always point to offset befor the glyph
    }

    if ((render_code == ' ') && settings.show_white_space)
    {
        render_code = '.';
        *(vec3 *) &color = vec3_scale(*(vec3 *) &color, 0.5f);
    }

    moui_vec2 texture_scale = { 1.0f / iterator->font.texture.width, 1.0f / iterator->font.texture.height };

    moui_f32 bottom_to_line = iterator->font.bottom_to_line;
    moui_f32 line_to_top    = iterator->font.line_to_top;

    // out_glyph->code = result.utf32_code;
    assert(iterator->font.glyph_count);
    moui_u32 glyph_index = render_code - iterator->font.glyphs[0].code;

    f32 cursor_y = iterator->cursor.position.y;

    if (glyph_index >= iterator->font.glyph_count)
    {
        glyph_index = '?' - iterator->font.glyphs[0].code;
        color = sl(rgba) { 1.0f, 0.5f, 0.1f, 1.0f };
    }

    moui_quad_colors colors = moui_to_quad_colors(color);

    {
        moui_font_glyph glyph = iterator->font.glyphs[glyph_index];

        box2 box;
        s32 width  = glyph.texture_box.max.x - glyph.texture_box.min.x;
        s32 height = glyph.texture_box.max.y - glyph.texture_box.min.y;

        box.min.x = iterator->cursor.position.x + glyph.offset.x;
        box.min.y = iterator->cursor.position.y + glyph.offset.y;
        box.max.x = box.min.x + width;
        box.max.y = box.min.y + height;

        moui_add_texture_quad(ui, texture_scale, colors, box, glyph.texture_box);

        if (ui->input.cursor_active_mask & 1)
        {
            // we want to use the glyph bounding box, not the glyph draw box for layouting
            box2 bounding_box;
            bounding_box.min.x = box.min.x;
            bounding_box.max.x = box.max.x;
            bounding_box.min.y = cursor_y - bottom_to_line;
            bounding_box.max.y = cursor_y + line_to_top;
            //moui_quad_colors bounds_colors = moui_to_quad_colors(sl(rgba) { 1.0f, 0.1f, 0.1f, 0.5f });
            //moui_add_texture_quad(ui, texture_scale, bounds_colors, bounding_box, glyph.texture_box);

            f32 y_distance = 0;
            if (ui->input.cursor.y < bounding_box.min.y)
                y_distance = bounding_box.min.y - ui->input.cursor.y;
            else if (ui->input.cursor.y >= bounding_box.max.y)
                y_distance = ui->input.cursor.y - bounding_box.max.y;

            u8 *text_base = left_base;
        #if 1
            f32 x_center = (bounding_box.min.x + bounding_box.max.x) * 0.5f;
            f32 x_distance;
            if (ui->input.cursor.x < x_center)
                x_distance = x_center - ui->input.cursor.x;
            else
            {
                x_distance = ui->input.cursor.x - x_center;
                text_base = right_base;
            }
        #else
            f32 x_distance = 0;
            if (ui->input.cursor.x < bounding_box.min.x)
                x_distance = bounding_box.min.x - ui->input.cursor.x;
            else if (ui->input.cursor.x >= bounding_box.max.x)
                x_distance = ui->input.cursor.x - bounding_box.max.x;
        #endif

            if (y_distance < closest_hit->y_distance)
            {
                closest_hit->text_base = text_base;
                closest_hit->y_distance = y_distance;
                closest_hit->x_distance = x_distance;
            }
            else if ((y_distance == closest_hit->y_distance) && (x_distance < closest_hit->x_distance))
            {
                closest_hit->text_base = text_base;
                closest_hit->x_distance = x_distance;
            }
        }

        //if ((ui->input.cursor_active_mask & 1) && moui_box_is_hot(ui, bounding_box))
            //*selected_text_base = text_base;
        // box2 ignored;
        // moui_scissor(ui, &bounding_box, &ignored);

        iterator->cursor.position.x += glyph.x_advance;
    }

    if (result.utf32_code == '\n')
        moui_text_advance_line(iterator);
}

