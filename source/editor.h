
#include "mo_basic.h"

#include "mo_platform.h"
#include "mo_memory_arena.h"
#include "mo_string.h"

#if defined mop_debug

#define editor_enable_sanity_check

#endif

// TODO: 8 MB per file for now
#define editor_buffer_byte_count (8 << 20)

typedef struct
{
    u8 base[31];
    u8 count;
} string31;

array_type(string31_array, string31);

string string31_to_string(string31 text)
{
    return sl(string) { text.base, (usize) text.count };
}

string31 string31_from_string(string text)
{
    string31 result;
    assert(text.count <= carray_count(result.base));

    result.count = (u8) text.count;
    memcpy(result.base, text.base, result.count);

    return result;
}

typedef struct
{
    u8 base[255];
    u8 count;
} string255;

array_type(string255_array, string255);

string string255_to_string(string255 text)
{
    return sl(string) { text.base, (usize) text.count };
}

string255 string255_from_string(string text)
{
    string255 result;
    assert(text.count <= carray_count(result.base));

    result.count = (u8) text.count;
    memcpy(result.base, text.base, result.count);

    return result;
}

typedef struct
{
    u8  base[(64 << 10) - 2];
    u16 count;
} string64k;

array_type(string64k_array, string64k);

string string64k_to_string(string64k text)
{
    return sl(string) { text.base, (usize) text.count };
}

string64k string64k_from_string(string text)
{
    string64k result;
    assert(text.count <= carray_count(result.base));

    result.count = (u8) text.count;
    memcpy(result.base, text.base, result.count);

    return result;
}

typedef struct
{
    u32 i;
} editor_undo;

// all counts and offsets are in bytes unless specified otherwise
typedef struct
{
    string255 title;

    u8         base[editor_buffer_byte_count];
    u32        count;

    u32        cursor_offset;
    u32        selection_start_offset;

    u32 draw_line_offset;
    u32 draw_line_column;

    b8 is_file;
    b8 has_changed;
    b8 file_save_error;
} editor_buffer;

typedef struct
{
    string text;
    u32    total_count;
    u32    cursor_offset;
    u32    selection_start_offset;
    b8     has_changed;
} editor_editable_buffer;

array_type(editor_buffer_array, editor_buffer);

typedef struct
{
    u32 column;
    b8  skipped_line;
} editor_buffer_to_previous_line_end_result;

typedef enum
{
    editor_command_tag_file_open,
    editor_command_tag_buffer_save,
    editor_command_tag_buffer_save_all,
    editor_command_tag_buffer_reload,

    editor_command_tag_focus_buffer,
    editor_command_tag_focus_search,
    editor_command_tag_focus_search_replace,

    editor_command_tag_buffer_next,
    editor_command_tag_buffer_previous,

    editor_command_tag_select_first,
    editor_command_tag_select_last,

    editor_command_tag_select_next,
    editor_command_tag_select_previous,

    editor_command_tag_select_push, // bad names
    editor_command_tag_select_pop,  // bad names

    editor_command_tag_select_accept,
    editor_command_tag_select_accept_all,

    editor_command_tag_select_cancel,

    editor_command_tag_copy,
    editor_command_tag_paste,
    editor_command_tag_paste_cycle,

    editor_command_tag_count,
} editor_command_tag;

// same layout as mop_character.mask
typedef union
{
    u8 value;

    struct
    {
        b8 is_symbol    : 1;
        b8 with_shift   : 1;
        b8 with_alt     : 1;
        b8 with_control : 1;
    };
} editor_character_mask;

const editor_character_mask editor_character_mask_all            = { 0b1111 };
const editor_character_mask editor_character_mask_optional_shift = { 0b1011 };

#define editor_focus_list(macro, ...) \
    macro(buffer, __VA_ARGS__) \
    macro(file_search, __VA_ARGS__) \
    macro(search, __VA_ARGS__) \
    macro(search_replace, __VA_ARGS__) \

mo_enum_list(editor_focus,         editor_focus_list);
mo_string_list(editor_focus_names, editor_focus_list);

typedef struct
{
    u32                   focus_mask;
    editor_command_tag    tag;
    editor_character_mask check_mask;
    mop_character         character;
} editor_command;

typedef struct
{
    string255 text;
    u32 cursor_offset;
    u32 selection_start_offset;
    b8  has_changed;
} editor_buffer255;

typedef struct
{
    mop_file_search_result results[256]; // filepaths are absolute paths
    u32                    result_count;

    u32 selected_index;
    u32 display_offset;
} editor_file_search;

typedef struct
{
    string64k base[16];
    u32       next;
    u32       active;
    u8        count;
    b8        was_used;
    b8        was_used_previously;
} editor_ringbuffer64k;

typedef struct
{
    moma_arena memory;

    // in order of allocation from memory
    string31_array      file_extensions;
    editor_buffer_array buffers;

    u32 active_buffer_index;
    u32 tab_space_count;

    editor_command commands[512];
    u32            command_count;

    editor_ringbuffer64k copy_buffer;

    editor_focus mode;
    editor_focus focus;
    editor_focus previous_focus; // when focusing buffer

    editor_buffer255   file_open_relative_path;
    editor_file_search file_search;

    editor_buffer255 search_buffer;
    editor_buffer255 search_replace_buffer;
    u32              search_start_cursor_offset;

    u32 visible_line_count_top;
    u32 visible_line_count_bottom;

    // from previous frame
    u32 visible_line_count;
    u32 visible_line_character_count;
} editor_state;

typedef struct
{
    u8 buffer[mop_path_max_count];
    string search_directory;
    string pattern;
} editor_file_search_filter;

typedef struct
{
    string suffix;
    string match;
    b8     ok;
} editor_file_search_filter_result;

typedef struct
{
    u32 offset;
    u32 count;
} editor_selection;

#define editor_file_extension_add_signature void editor_file_extension_add(editor_state *editor, string extension)
editor_file_extension_add_signature;

#define editor_file_extension_check_signature b8 editor_file_extension_check(editor_state *editor, string path)
editor_file_extension_check_signature;

#define editor_command_execute_signature void editor_command_execute(editor_state *editor, editor_command_tag command_tag, mop_platform *platform)
editor_command_execute_signature;

#define editor_buffer_text_siganture string editor_buffer_text(editor_state *editor, editor_buffer *buffer)
editor_buffer_text_siganture;

#define editor_buffer_add_signature u32 editor_buffer_add(editor_state *editor, string title, b8 is_file)
editor_buffer_add_signature;

#define editor_buffer_find_signature u32 editor_buffer_find(editor_state *editor, string title, b8 is_file)
editor_buffer_find_signature;

#define editor_buffer_find_or_add_signature u32 editor_buffer_find_or_add(editor_state *editor, string title, b8 is_file)
editor_buffer_find_or_add_signature;

#define  editor_buffer_load_file_signature void editor_buffer_load_file(mop_platform *platform, editor_state *editor, u32 buffer_index)
editor_buffer_load_file_signature;

#define  editor_buffer_save_file_signature void editor_buffer_save_file(mop_platform *platform, editor_state *editor, u32 buffer_index)
editor_buffer_save_file_signature;

#define editor_directory_load_all_files_signature u32 editor_directory_load_all_files(mop_platform *platform, editor_state *editor, string relative_path)
editor_directory_load_all_files_signature;

#define editor_active_buffer_get_signature editor_buffer * editor_active_buffer_get(editor_state *editor)
editor_active_buffer_get_signature;

#define editor_buffer_edit_begin_signature editor_editable_buffer editor_buffer_edit_begin(editor_state *editor, editor_buffer *buffer)
editor_buffer_edit_begin_signature;

#define editor_buffer_edit_end_signature void editor_buffer_edit_end(editor_state *editor, editor_buffer *buffer, editor_editable_buffer editable_buffer)
editor_buffer_edit_end_signature;

#define editor_buffer255_edit_begin_signature editor_editable_buffer editor_buffer255_edit_begin(editor_state *editor, editor_buffer255 *buffer)
editor_buffer255_edit_begin_signature;

#define editor_buffer255_edit_end_signature void editor_buffer255_edit_end(editor_state *editor, editor_buffer255 *buffer, editor_editable_buffer editable_buffer)
editor_buffer255_edit_end_signature;

#define editor_insert_signature void editor_insert(editor_state *editor, editor_editable_buffer *buffer, u32 offset, string text)
editor_insert_signature;

#define editor_remove_signature void editor_remove(editor_state *editor, editor_editable_buffer *buffer, u32 offset, u32 count)
editor_remove_signature;

#define editor_buffer_move_left_signature mos_utf8_result editor_buffer_move_left(editor_state *editor, editor_editable_buffer *buffer)
editor_buffer_move_left_signature;

#define editor_buffer_move_right_signature mos_utf8_result editor_buffer_move_right(editor_state *editor, editor_editable_buffer *buffer)
editor_buffer_move_right_signature;

#define editor_buffer_to_previous_line_end_signature editor_buffer_to_previous_line_end_result editor_buffer_to_previous_line_end(editor_state *editor, editor_editable_buffer *buffer)
editor_buffer_to_previous_line_end_signature;

#define editor_buffer_to_next_line_start_signature void editor_buffer_to_next_line_start(editor_state *editor, editor_editable_buffer *buffer)
editor_buffer_to_next_line_start_signature;

#define editor_buffer_to_line_start_signature u32 editor_buffer_to_line_start(editor_state *editor, editor_editable_buffer *buffer)
editor_buffer_to_line_start_signature;

#define editor_buffer_to_line_end_signature void editor_buffer_to_line_end(editor_state *editor, editor_editable_buffer *buffer)
editor_buffer_to_line_end_signature;

#define editor_buffer_move_right_within_line_signature void editor_buffer_move_right_within_line(editor_state *editor, editor_editable_buffer *buffer, u32 max_move_count)
editor_buffer_move_right_within_line_signature;

#define editor_buffer_count_right_spaces_signature u32 editor_buffer_count_right_spaces(editor_state *editor, editor_editable_buffer *buffer)
editor_buffer_count_right_spaces_signature;

#define editor_file_search_filter_get_signature void editor_file_search_filter_get(editor_file_search_filter *filter, mop_platform *platform, string relative_path)
editor_file_search_filter_get_signature;

#define editor_file_search_filter_check_signature editor_file_search_filter_result editor_file_search_filter_check(editor_file_search_filter filter, mop_file_search_result result)
editor_file_search_filter_check_signature;

#define editor_search_forward_signature string editor_search_forward(editor_state *editor, string text, u32 offset, string pattern)
editor_search_forward_signature;

#define editor_buffer_search_forward_signature void editor_buffer_search_forward(editor_state *editor, editor_editable_buffer *buffer, u32 offset, string pattern)
editor_buffer_search_forward_signature;

#define editor_search_backward_signature string editor_search_backward(editor_state *editor, string text, u32 offset, string pattern)
editor_search_backward_signature;

#define editor_buffer_search_backward_signature void editor_buffer_search_backward(editor_state *editor, editor_editable_buffer *buffer, u32 offset, string pattern)
editor_buffer_search_backward_signature;

#define editor_get_absolute_path_signature string editor_get_absolute_path(u8_array buffer, mop_platform *platform, string relative_path)
editor_get_absolute_path_signature;

#define editor_get_relative_path_signature string editor_get_relative_path(u8_array buffer, mop_platform *platform, string absolute_path)
editor_get_relative_path_signature;

#define editor_command_add_signature editor_command * editor_command_add(editor_state *editor, u32 focus_mask, editor_command_tag tag, u32 symbol_code)
editor_command_add_signature;

#define editor_mode_set_signature void editor_mode_set(editor_state *editor, editor_focus mode)
editor_mode_set_signature;

#define editor_selection_get_signature editor_selection editor_selection_get(editor_state *editor, editor_editable_buffer buffer)
editor_selection_get_signature;

#define editor_selection_remove_signature b8 editor_selection_remove(editor_state *editor, editor_editable_buffer *buffer)
editor_selection_remove_signature;

#define editor_editable_buffer_begin_signature editor_editable_buffer editor_editable_buffer_begin(editor_state *editor)
editor_editable_buffer_begin_signature;

#define editor_editable_buffer_end_signature void editor_editable_buffer_end(editor_state *editor, editor_editable_buffer buffer)
editor_editable_buffer_end_signature;

#define editor_edit_sanity_check_signature void editor_edit_sanity_check(editor_state *editor, editor_editable_buffer buffer)
editor_edit_sanity_check_signature;

const string editor_file_extension_list_path = sc("moed_file_extensions.txt");

void editor_init(editor_state *editor, mop_platform *platform)
{
    moma_arena *memory = &editor->memory;

    moma_create(memory, platform, 1 << 30); // 1 gb

    editor->tab_space_count = 4;

    editor->visible_line_count_top    = 5;
    editor->visible_line_count_bottom = editor->visible_line_count_top;

    {
        usize tmemory_frame = memory->used_count;

        mop_read_file_result result = moma_read_file(platform, memory, editor_file_extension_list_path);

        if (result.ok)
        {
            string iterator = result.data;

            mos_skip_white_space(&iterator);

            while (iterator.count)
            {
                string extension = mos_skip_until_set_or_end(&iterator, s(" \t\n\r"));
                mos_skip_white_space(&iterator);
                assert(extension.count);

                editor_file_extension_add(editor, extension);
            }

            memory->used_count = tmemory_frame;

            // free file result data and move extensions to beginning of allocation
            string31_array file_extensions = { null, editor->file_extensions.count };
            moma_reallocate_array(memory, &file_extensions, string31);
            memcpy(file_extensions.base,  editor->file_extensions.base, sizeof( editor->file_extensions.base[0]) * editor->file_extensions.count);
            editor->file_extensions = file_extensions;
        }
        else
        {
            // add some default common extensions
            editor_file_extension_add(editor, s("h"));
            editor_file_extension_add(editor, s("c"));
            editor_file_extension_add(editor, s("cpp"));
            editor_file_extension_add(editor, s("t"));
            editor_file_extension_add(editor, s("txt"));
            editor_file_extension_add(editor, s("glsl"));
            editor_file_extension_add(editor, s("hlsl"));
            editor_file_extension_add(editor, s("gitignore"));
            editor_file_extension_add(editor, s("bat"));
            editor_file_extension_add(editor, s("py"));
            editor_file_extension_add(editor, s("js"));
            editor_file_extension_add(editor, s("ts"));
            editor_file_extension_add(editor, s("css"));
            editor_file_extension_add(editor, s("html"));
            editor_file_extension_add(editor, s("xml"));

            // save default extensions

            mos_string_buffer builder = mos_buffer_from_memory(memory->base, memory->count - memory->used_count);

            for (u32 i = 0; i < editor->file_extensions.count; i++)
            {
                string extension = string31_to_string(editor->file_extensions.base[i]);
                mos_write(&builder, "%.*s ", fs(extension));
            }
            mos_write(&builder, "\r\n");

            mop_write_file(platform, editor_file_extension_list_path, mos_buffer_to_string(builder));
        }
    }

    // editor_focus_buffer
    {
        editor_command *command = editor_command_add(editor, flag32(editor_focus_buffer), editor_command_tag_buffer_save, 'S');
        command->character.with_control = true;

        command = editor_command_add(editor, flag32(editor_focus_buffer), editor_command_tag_buffer_save_all, 'S');
        command->character.with_shift = true;
        command->character.with_control = true;

        command = editor_command_add(editor, flag32(editor_focus_buffer), editor_command_tag_buffer_reload, 'P');
        command->character.with_shift = true;
        command->character.with_control = true;

        command = editor_command_add(editor, ~0, editor_command_tag_focus_buffer, ' ');
        command->character.with_control = true;

        command = editor_command_add(editor, ~0, editor_command_tag_select_cancel, mop_character_symbol_escape);

        command = editor_command_add(editor, flag32(editor_focus_buffer) | flag32(editor_focus_file_search), editor_command_tag_file_open, 'P');
        command->character.with_control = true;

        command = editor_command_add(editor, flag32(editor_focus_buffer) | flag32(editor_focus_search) | flag32(editor_focus_search_replace), editor_command_tag_focus_search, 'F');
        command->character.with_control = true;

        command = editor_command_add(editor, flag32(editor_focus_buffer) | flag32(editor_focus_search) | flag32(editor_focus_search_replace), editor_command_tag_focus_search_replace, 'R');
        command->character.with_control = true;

        command = editor_command_add(editor, flag32(editor_focus_buffer), editor_command_tag_buffer_next, mop_character_symbol_tabulator);
        command->character.with_control = true;

        command = editor_command_add(editor, flag32(editor_focus_buffer), editor_command_tag_buffer_previous, mop_character_symbol_tabulator);
        command->character.with_shift = true;
        command->character.with_control = true;

        command = editor_command_add(editor, ~0, editor_command_tag_copy, 'C');
        command->character.with_control = true;

        command = editor_command_add(editor, ~0, editor_command_tag_paste, 'V');
        command->character.with_control = true;

        command = editor_command_add(editor, ~0, editor_command_tag_paste_cycle, 'V');
        command->character.with_shift  = true;
        command->character.with_control = true;
    }

    // editor_focus_search and editor_focus_search_replace
    {
        u32 focus_search_or_replace_mask = flag32(editor_focus_search) | flag32(editor_focus_search_replace);

        editor_command *command = editor_command_add(editor, focus_search_or_replace_mask, editor_command_tag_select_next, mop_character_symbol_down);

        command = editor_command_add(editor, focus_search_or_replace_mask, editor_command_tag_select_previous, mop_character_symbol_up);

        command = editor_command_add(editor, focus_search_or_replace_mask, editor_command_tag_select_last, mop_character_symbol_down);
        command->character.with_control = true;

        command = editor_command_add(editor, focus_search_or_replace_mask, editor_command_tag_select_first, mop_character_symbol_up);
        command->character.with_control = true;

        // search only

        command = editor_command_add(editor, flag32(editor_focus_search), editor_command_tag_select_accept, mop_character_symbol_return);

        command = editor_command_add(editor, flag32(editor_focus_search), editor_command_tag_select_accept_all, mop_character_symbol_return);
        command->character.with_control = true;

        // replace only

        command = editor_command_add(editor, flag32(editor_focus_search_replace), editor_command_tag_select_accept, mop_character_symbol_return);

        command = editor_command_add(editor, flag32(editor_focus_search_replace), editor_command_tag_select_accept_all, mop_character_symbol_return);
        command->character.with_control = true;
    }

    // editor_focus_file_search
    {
        editor_command *command = editor_command_add(editor, flag32(editor_focus_file_search), editor_command_tag_select_next, mop_character_symbol_down);

        command = editor_command_add(editor, flag32(editor_focus_file_search), editor_command_tag_select_previous, mop_character_symbol_up);

        command = editor_command_add(editor, flag32(editor_focus_file_search), editor_command_tag_select_push, mop_character_symbol_tabulator);

        command = editor_command_add(editor, flag32(editor_focus_file_search), editor_command_tag_select_pop, mop_character_symbol_tabulator);
        command->character.with_shift = true;

        command = editor_command_add(editor, flag32(editor_focus_file_search), editor_command_tag_select_accept, mop_character_symbol_return);

        command = editor_command_add(editor, flag32(editor_focus_file_search), editor_command_tag_select_accept_all, mop_character_symbol_return);
        command->character.with_control = true;
    }

    editor->active_buffer_index = editor_buffer_add(editor, s("new"), false);
}

void editor_update(editor_state *editor, mop_platform *platform, u32 visible_line_count, u32 visible_line_character_count)
{
    editor->visible_line_count           = visible_line_count;
    editor->visible_line_character_count = visible_line_character_count;

    editor->file_open_relative_path.has_changed = false;
    editor->search_buffer.has_changed = false;

    mop_character_array characters = mop_get_characters(platform);

    for (u32 character_index = 0; character_index < characters.count; character_index++)
    {
        editor_buffer *active_buffer = editor_active_buffer_get(editor);

        mop_character character = characters.base[character_index];

        // TODO: is this still valid?
        assert((editor->mode != editor_focus_buffer) || (editor->mode == editor->focus));

        editor->copy_buffer.was_used = false;

        b8 found_command = false;
        for (u32 command_index = 0; command_index < editor->command_count; command_index++)
        {
            editor_command command = editor->commands[command_index];
            if ((command.focus_mask & flag32(editor->focus)) && (command.character.code == character.code) && ((character.mask & command.check_mask.value) == command.character.mask))
            {
                found_command = true;
                editor_command_execute(editor, command.tag, platform);
                break;
            }
        }

        editor->copy_buffer.was_used_previously = editor->copy_buffer.was_used;

        if (found_command)
            continue;

        editor_editable_buffer buffer = editor_editable_buffer_begin(editor);

        if (!character.is_symbol)
        {
            mos_utf8_encoding encoding = mos_encode_utf8(character.code);
            assert(encoding.count);

            editor_selection_remove(editor, &buffer);
            editor_insert(editor, &buffer, buffer.cursor_offset, sl(string) { encoding.base, encoding.count });
            buffer.cursor_offset += encoding.count;

            buffer.selection_start_offset = buffer.cursor_offset;
        }
        else
        {
            switch (character.code)
            {
                case mop_character_symbol_backspace:
                {
                    if (editor_selection_remove(editor, &buffer))
                        break;

                    u32 cursor_offset = buffer.cursor_offset;

                    // check if cursor is at start of logic line (without leading spaces)
                    // and remove the spaces and new line too
                    {
                        // count line's leading tabs
                        u32 column = editor_buffer_to_line_start(editor, &buffer);

                        u32 space_count = editor_buffer_count_right_spaces(editor, &buffer);
                        u32 tab_count = space_count / editor->tab_space_count;

                        // MAYBE: check for spaces instead of tabs?
                        if (column == tab_count * editor->tab_space_count)
                        {
                            editor_buffer_to_previous_line_end(editor, &buffer);
                            editor_remove(editor, &buffer, buffer.cursor_offset, cursor_offset - buffer.cursor_offset);
                            break;
                        }
                    }

                    buffer.cursor_offset = cursor_offset;

                    mos_utf8_result result = editor_buffer_move_left(editor, &buffer);

                    if (result.utf32_code == '\n')
                    {
                        buffer.cursor_offset += 1;
                        result.byte_count += editor_buffer_count_right_spaces(editor, &buffer);
                        buffer.cursor_offset -= 1;
                    }

                    editor_remove(editor, &buffer, buffer.cursor_offset, result.byte_count);
                } break;

                case mop_character_symbol_delete:
                {
                    if (editor_selection_remove(editor, &buffer))
                        break;

                    if (buffer.cursor_offset >= buffer.text.count)
                        break;

                    mos_utf8_result result = editor_buffer_move_right(editor, &buffer);

                    u32 space_count = 0;
                    if (result.utf32_code == '\n')
                        space_count = editor_buffer_count_right_spaces(editor, &buffer);

                    buffer.cursor_offset -= result.byte_count; // move back left

                    editor_remove(editor, &buffer, buffer.cursor_offset, result.byte_count + space_count);
                } break;

                case mop_character_symbol_return:
                {
                    // count line start tabs

                    u32 cursor_offset = buffer.cursor_offset;

                    editor_buffer_to_line_start(editor, &buffer);
                    u32 space_count = editor_buffer_count_right_spaces(editor, &buffer);
                    u32 tab_count = space_count / editor->tab_space_count;

                    buffer.cursor_offset = cursor_offset;

                    // insert new line

                    editor_selection_remove(editor, &buffer);
                    editor_insert(editor, &buffer, buffer.cursor_offset, s("\n"));
                    buffer.cursor_offset += 1;

                    // indent new line with the same amount of tabs

                    for (u32 i = 0; i < tab_count * editor->tab_space_count; i++)
                        editor_insert(editor, &buffer, buffer.cursor_offset, s(" "));

                    buffer.cursor_offset += tab_count * editor->tab_space_count;

                    buffer.selection_start_offset = buffer.cursor_offset;
                } break;

                case mop_character_symbol_left:
                {
                    editor_buffer_move_left(editor, &buffer);
                } break;

                case mop_character_symbol_right:
                {
                    editor_buffer_move_right(editor, &buffer);
                } break;

                case mop_character_symbol_up:
                {
                    editor_buffer_to_previous_line_end_result result = editor_buffer_to_previous_line_end(editor, &buffer);

                    // move to previous line start
                    editor_buffer_to_line_start(editor, &buffer);

                    // move to previous line column
                    editor_buffer_move_right_within_line(editor, &buffer, result.column);
                } break;

                case mop_character_symbol_down:
                {
                    editor_buffer_to_previous_line_end_result result = editor_buffer_to_previous_line_end(editor, &buffer);

                    // skip previous line
                    if (result.skipped_line)
                        editor_buffer_to_next_line_start(editor, &buffer);

                    // skip current line
                    editor_buffer_to_next_line_start(editor, &buffer);

                    // move to next line column
                    editor_buffer_move_right_within_line(editor, &buffer, result.column);
                } break;

                case mop_character_symbol_page_up:
                {
                    editor_buffer_to_previous_line_end_result result = editor_buffer_to_previous_line_end(editor, &buffer);

                    for (u32 i = 0; i < editor->visible_line_count - 1; i++)
                        editor_buffer_to_previous_line_end(editor, &buffer);

                    // move to previous line start
                    editor_buffer_to_line_start(editor, &buffer);

                    // move to previous line column
                    editor_buffer_move_right_within_line(editor, &buffer, result.column);
                } break;

                case mop_character_symbol_page_down:
                {
                    editor_buffer_to_previous_line_end_result result = editor_buffer_to_previous_line_end(editor, &buffer);

                    // skip previous line
                    if (result.skipped_line)
                        editor_buffer_to_next_line_start(editor, &buffer);

                    // skip current line
                    for (u32 i = 0; i < editor->visible_line_count; i++)
                    editor_buffer_to_next_line_start(editor, &buffer);

                    // move to next line column
                    editor_buffer_move_right_within_line(editor, &buffer, result.column);
                } break;

                case mop_character_symbol_tabulator:
                {
                    u32 previous_cursor_offset = buffer.cursor_offset;

                    editor_buffer_to_line_start(editor, &buffer);

                    u32 line_start_offset = buffer.cursor_offset;

                    u32 space_count = editor_buffer_count_right_spaces(editor, &buffer);
                    u32 tab_count = space_count / editor->tab_space_count;

                    if (character.with_shift)
                    {
                        if (tab_count)
                        {
                            editor_remove(editor, &buffer, line_start_offset, editor->tab_space_count);

                            if (previous_cursor_offset - line_start_offset >= editor->tab_space_count)
                                buffer.cursor_offset = previous_cursor_offset - editor->tab_space_count;
                            else
                                buffer.cursor_offset = line_start_offset;
                        }
                        else
                        {
                            buffer.cursor_offset = previous_cursor_offset;
                        }
                    }
                    else
                    {
                        tab_count += 1;

                        for (u32 i = 0; i < editor->tab_space_count; i++)
                            editor_insert(editor, &buffer, line_start_offset, s(" "));

                        buffer.cursor_offset = previous_cursor_offset + editor->tab_space_count;
                    }
                } break;

                case mop_character_symbol_home:
                {
                    if (character.with_control)
                    {
                        buffer.cursor_offset = 0;
                        break;
                    }

                    u32 previous_cursor_offset = buffer.cursor_offset;

                    editor_buffer_to_line_start(editor, &buffer);

                    u32 line_start_offset = buffer.cursor_offset;

                    u32 space_count = editor_buffer_count_right_spaces(editor, &buffer);
                    u32 tab_count = space_count / editor->tab_space_count;
                    buffer.cursor_offset = line_start_offset + tab_count * editor->tab_space_count;

                    if (buffer.cursor_offset == previous_cursor_offset)
                        buffer.cursor_offset = line_start_offset;
                } break;

                case mop_character_symbol_end:
                {
                    if (character.with_control)
                    {
                        buffer.cursor_offset = buffer.text.count;
                        break;
                    }

                    u32 cursor_offset = buffer.cursor_offset;
                    editor_buffer_to_line_end(editor, &buffer);
                } break;
            }

            if (!character.with_shift)
                buffer.selection_start_offset = buffer.cursor_offset;
        }

        editor_editable_buffer_end(editor, buffer);
    }

    switch (editor->focus)
    {
        case editor_focus_file_search:
        {
            if (!editor->file_open_relative_path.has_changed)
                break;

            editor_file_search *file_search = &editor->file_search;
            mos_string search_path = string255_to_string(editor->file_open_relative_path.text);

            editor_file_search_filter filter;
            editor_file_search_filter_get(&filter, platform, search_path);
            file_search->result_count   = 0;
            file_search->selected_index = 0;
            file_search->display_offset = 0;

            mop_file_search_iterator iterator = mop_file_search_init(platform, filter.search_directory);

            mop_file_search_result result;
            while ((file_search->result_count < carray_count(file_search->results)) && mop_file_search_advance(&result, platform, &iterator))
            {
                if (!result.is_directory && !editor_file_extension_check(editor, result.filepath))
                    continue;

                editor_file_search_filter_result filter_result = editor_file_search_filter_check(filter, result);
                if (!filter_result.ok)
                    continue;

                mop_file_search_result *search_result = &file_search->results[file_search->result_count];
                file_search->result_count += 1;

                *search_result = result;
                search_result->filepath.base = search_result->buffer;
            }
        } break;

        case editor_focus_search:
        {
            if (!editor->search_buffer.has_changed)
                break;

            editor_buffer *active_buffer = editor_active_buffer_get(editor);

            string pattern = string255_to_string(editor->search_buffer.text);
            u32 offset = editor->search_start_cursor_offset;

            editor_editable_buffer buffer = editor_buffer_edit_begin(editor, active_buffer);
            editor_buffer_search_forward(editor, &buffer, offset, pattern);
            editor_buffer_edit_end(editor, active_buffer, buffer);
        } break;
    }

    // scroll buffer view
    {
        editor_buffer *active_buffer = editor_active_buffer_get(editor);
        editor_editable_buffer buffer = editor_buffer_edit_begin(editor, active_buffer);

        u32 line_column = editor_buffer_to_line_start(editor, &buffer);
        u32 line_offset = buffer.cursor_offset;

        {
            b8 skipped_line = false;
            for (u32 i = 0; i < editor->visible_line_count_top; i++)
                skipped_line = editor_buffer_to_previous_line_end(editor, &buffer).skipped_line;

            if (skipped_line)
                editor_buffer_move_right(editor, &buffer);
        }

        if (active_buffer->draw_line_offset > buffer.cursor_offset)
        {
            active_buffer->draw_line_offset = buffer.cursor_offset;
        }
        else if (editor->visible_line_count - editor->visible_line_count_bottom > 0)
        {
            buffer.cursor_offset = line_offset;

            b8 skipped_line = false;
            for (u32 i = 0; i < editor->visible_line_count - editor->visible_line_count_bottom; i++)
                skipped_line = editor_buffer_to_previous_line_end(editor, &buffer).skipped_line;

            if (skipped_line)
                editor_buffer_move_right(editor, &buffer);

            if (active_buffer->draw_line_offset < buffer.cursor_offset)
                active_buffer->draw_line_offset = buffer.cursor_offset;
        }

        assert(!active_buffer->draw_line_offset || (active_buffer->base[active_buffer->draw_line_offset - 1] == '\n'));
        assert(active_buffer->draw_line_offset <= active_buffer->cursor_offset);
    }

}

editor_file_extension_add_signature
{
    assert(!editor->buffers.base); // add all extensions before adding any buffers

    // skip duplicates
    for (u32 i = 0; i < editor->file_extensions.count; i++)
    {
        if (mos_are_equal(extension, string31_to_string(editor->file_extensions.base[i])))
            return;
    }

    editor->file_extensions.count += 1;
    moma_reallocate_array(&editor->memory, &editor->file_extensions, string31);

    editor->file_extensions.base[editor->file_extensions.count - 1] = string31_from_string(extension);
}

editor_file_extension_check_signature
{
    mos_split_path_result split = mos_split_path(path);

    for (u32 i = 0; i < editor->file_extensions.count; i++)
    {
        if (mos_are_equal(split.extension, string31_to_string(editor->file_extensions.base[i])))
            return true;
    }

    return false;
}

editor_command_add_signature
{
    editor_command *command = &editor->commands[editor->command_count];
    *command = sl(editor_command) {0};
    editor->command_count += 1;

    command->check_mask = editor_character_mask_all;
    command->focus_mask = focus_mask;
    command->tag        = tag;

    command->character.is_symbol = true;
    command->character.code      = symbol_code;

    return command;
}

editor_command_execute_signature
{
    // by focus by command
    b8 handled = true;
    switch (editor->focus)
    {
        case editor_focus_buffer:
        {
            handled = false;
        } break;

        case editor_focus_file_search:
        {
            editor_file_search *file_search = &editor->file_search;

            switch (command_tag)
            {
                case editor_command_tag_select_next:
                case editor_command_tag_select_previous:
                {
                    s32 direction = -(command_tag - editor_command_tag_select_next) * 2 + 1;

                    if (file_search->result_count)
                        file_search->selected_index = (file_search->selected_index + file_search->result_count + direction) % file_search->result_count;
                } break;

                case editor_command_tag_select_push:
                {
                    if (!file_search->result_count)
                        break;

                    assert(file_search->selected_index < file_search->result_count);
                    mop_file_search_result result = file_search->results[file_search->selected_index];

                    {
                        string buffer = { editor->file_open_relative_path.text.base, carray_count(editor->file_open_relative_path.text.base) };
                        string relative_path = editor_get_relative_path(buffer, platform, result.filepath);
                        editor->file_open_relative_path.text = string255_from_string(relative_path);
                    }

                    string255 *text = &editor->file_open_relative_path.text;

                    if (!result.is_parent_directory && result.is_directory)
                    {
                        assert(text->count < carray_count(text->base));
                        assert(text->base[text->count - 1] != '/');
                        text->base[text->count] = '/';
                        text->count += 1;
                    }

                    editor->file_open_relative_path.cursor_offset          = text->count;
                    editor->file_open_relative_path.selection_start_offset = editor->file_open_relative_path.cursor_offset;
                    editor->file_open_relative_path.has_changed            = true;
                } break;

                case editor_command_tag_select_pop:
                {
                    editor_buffer255 *buffer = &editor->file_open_relative_path;

                    string path = string255_to_string(buffer->text);
                    mos_split_path_result split = mos_split_path(path);

                    path = split.directory;
                    if (!split.name.count)
                    {
                        while (path.count && (path.base[path.count - 1] != '/'))
                            path.count -= 1;
                    }
                    else if (path.count)
                    {
                        path.count += 1;
                        assert(path.base[path.count - 1] == '/');
                    }

                    buffer->text                   = string255_from_string(path);
                    buffer->cursor_offset          = buffer->text.count;
                    buffer->selection_start_offset = buffer->cursor_offset;
                    buffer->has_changed            = true;
                } break;

                case editor_command_tag_select_accept:
                case editor_command_tag_select_accept_all:
                {
                    string255 relative_path255 = editor->file_open_relative_path.text;
                    if (file_search->result_count)
                    {
                        assert(file_search->selected_index < file_search->result_count);
                        string absolute_path = file_search->results[file_search->selected_index].filepath;

                        string buffer = { relative_path255.base, carray_count(relative_path255.base) };
                        string result = editor_get_relative_path(buffer, platform, absolute_path);
                        relative_path255 = string255_from_string(result);
                    }

                    string relative_path = string255_to_string(relative_path255);

                    if (mop_path_is_directory(platform, relative_path))
                    {
                        if (relative_path.count)
                        {
                            assert(relative_path.base[relative_path.count - 1] != '/');
                            assert(relative_path.count + 1 <= carray_count(relative_path255.base));
                            relative_path.base[relative_path.count] = '/';
                            relative_path.count += 1;
                        }

                        // open all files in directory
                        if (command_tag == editor_command_tag_select_accept_all)
                        {
                            editor->active_buffer_index = editor_directory_load_all_files(platform, editor, relative_path);

                            editor_mode_set(editor, editor_focus_buffer);
                        }
                        else // push into directory
                        {
                            editor_buffer255 *buffer = &editor->file_open_relative_path;

                            buffer->text = string255_from_string(relative_path);
                            buffer->cursor_offset = buffer->text.count;
                            buffer->has_changed = true;
                        }
                    }
                    else // open single file
                    {
                        // TODO: handle creating file with unsupported extension

                        u32 buffer_index = editor_buffer_find_or_add(editor, relative_path, true);
                        editor_buffer_load_file(platform, editor, buffer_index);

                        editor->active_buffer_index = buffer_index;
                        editor_mode_set(editor, editor_focus_buffer);
                    }
                } break;

                default:
                {
                    handled = false;
                }
            }
        } break;

        case editor_focus_search:
        case editor_focus_search_replace:
        {
            string pattern = string255_to_string(editor->search_buffer.text);
            // u32 offset = editor->search_start_cursor_offset;

            editor_buffer *active_buffer = editor_active_buffer_get(editor);
            editor_editable_buffer buffer = editor_buffer_edit_begin(editor, active_buffer);

            switch (command_tag)
            {
                case editor_command_tag_select_first:
                {
                    editor_buffer_search_forward(editor, &buffer, 0, pattern);
                } break;

                case editor_command_tag_select_next:
                {
                    editor_selection selection = editor_selection_get(editor, buffer);
                    editor_buffer_search_forward(editor, &buffer, selection.offset + 1, pattern);
                } break;

                case editor_command_tag_select_previous:
                {
                    editor_selection selection = editor_selection_get(editor, buffer);
                    editor_buffer_search_backward(editor, &buffer, selection.offset, pattern);
                } break;

                case editor_command_tag_select_last:
                {
                    editor_buffer_search_backward(editor, &buffer, buffer.text.count, pattern);
                } break;

                case editor_command_tag_select_accept:
                case editor_command_tag_select_accept_all:
                {
                    if (editor->mode == editor_focus_search)
                    {
                        assert(editor->focus == editor_focus_search);
                        editor_mode_set(editor, editor_focus_buffer);
                        break;
                    }

                    string replacement = string255_to_string(editor->search_replace_buffer.text);

                    // replace all and close mode
                    if (command_tag == editor_command_tag_select_accept_all)
                    {
                        string text = buffer.text;

                        u32 cursor_offset = 0;
                        while (true)
                        {
                            string at = editor_search_forward(editor, buffer.text, cursor_offset, pattern);
                            if (!at.base)
                                break;

                            cursor_offset = (u32) (at.base - buffer.text.base);

                            editor_remove(editor, &buffer, cursor_offset, pattern.count);
                            editor_insert(editor, &buffer, cursor_offset, replacement);

                            cursor_offset += replacement.count;
                        }

                        buffer.cursor_offset          = cursor_offset;
                        buffer.selection_start_offset = buffer.cursor_offset;
                        editor_mode_set(editor, editor_focus_buffer);
                    }
                    else
                    {
                        editor_selection selection = editor_selection_get(editor, buffer);

                        // break if we don't have a match
                        {
                            string at = mos_remaining_substring(buffer.text, selection.offset);
                            if (!mos_try_skip(&at, pattern))
                                break;
                        }

                        editor_remove(editor, &buffer, selection.offset, pattern.count);
                        editor_insert(editor, &buffer, selection.offset, replacement);

                        editor_buffer_search_forward(editor, &buffer, selection.offset + replacement.count, pattern);

                        // HACK: after replace, we can't garantee the start position is still valid
                        editor->search_start_cursor_offset = selection.offset;
                    }
                } break;

                default:
                {
                    handled = false;
                }
            }

            editor_buffer_edit_end(editor, active_buffer, buffer);
        } break;

        cases_complete("focus %.*s", fs(editor_focus_names[editor->mode]));
    }

    if (handled)
        return;

    handled = true;

    // by command
    switch (command_tag)
    {
        case editor_command_tag_select_cancel:
        {
            switch (editor->mode)
            {
                case editor_focus_buffer:
                break;

                case editor_focus_file_search:
                {
                    editor_mode_set(editor, editor_focus_buffer);
                } break;

                case editor_focus_search:
                case editor_focus_search_replace:
                {
                    editor_mode_set(editor, editor_focus_buffer);

                    editor_buffer *active_buffer = editor_active_buffer_get(editor);
                    active_buffer->cursor_offset          = editor->search_start_cursor_offset;
                    active_buffer->selection_start_offset = active_buffer->cursor_offset;
                } break;

                cases_complete("mode %.*s", fs(editor_focus_names[editor->mode]));
            }
        } break;

        case editor_command_tag_focus_buffer:
        {
            if (editor->focus == editor_focus_buffer)
            {
                editor->focus = editor->previous_focus;
            }
            else
            {
                editor->previous_focus = editor->focus;
                editor->focus          = editor_focus_buffer;
            }
        } break;

        case editor_command_tag_file_open:
        {
            if (editor->mode == editor_focus_file_search)
            {
                editor_mode_set(editor, editor_focus_buffer);
            }
            else
            {
                editor_mode_set(editor, editor_focus_file_search);
                editor->file_open_relative_path.has_changed = true; // force a search
                editor->file_open_relative_path.cursor_offset          = 0;
                editor->file_open_relative_path.selection_start_offset = editor->file_open_relative_path.text.count;
            }
        } break;

        case editor_command_tag_focus_search:
        case editor_command_tag_focus_search_replace:
        {
            editor_focus target_focus = (editor_focus) (editor_focus_search + command_tag - editor_command_tag_focus_search);

            if (editor->mode == editor_focus_buffer)
            {
                editor_buffer *active_buffer = editor_active_buffer_get(editor);
                editor->search_start_cursor_offset = active_buffer->cursor_offset;
            }

            if (editor->mode < target_focus)
            {
                editor_mode_set(editor, target_focus);

                if (target_focus == editor_focus_search_replace)
                {
                    editor->search_replace_buffer.cursor_offset          = 0;
                    editor->search_replace_buffer.selection_start_offset = editor->search_replace_buffer.text.count;
                }

                if (editor->mode < editor_focus_search)
                {
                    editor->search_buffer.cursor_offset          = 0;
                    editor->search_buffer.selection_start_offset = editor->search_buffer.text.count;
                }
            }

            editor->focus = target_focus;
            editor->search_buffer.has_changed = true; // force a search
            editor->search_replace_buffer.has_changed = true; // force a search
        } break;

        case editor_command_tag_buffer_next:
        case editor_command_tag_buffer_previous:
        {
            if (editor->mode != editor_focus_buffer)
                break;

            s32 direction = -(command_tag - editor_command_tag_buffer_next) * 2 + 1;

            if (editor->buffers.count)
                editor->active_buffer_index = (editor->active_buffer_index + editor->buffers.count + direction) % editor->buffers.count;
        } break;

        case editor_command_tag_buffer_reload:
        {
            if (editor->mode != editor_focus_buffer)
                break;

            editor_buffer_load_file(platform, editor, editor->active_buffer_index);
        } break;

        case editor_command_tag_buffer_save:
        {
            editor_buffer *active_buffer = editor_active_buffer_get(editor);

            // HACK:
            if (!active_buffer->is_file)
                break;

            editor_buffer_save_file(platform, editor, editor->active_buffer_index);
        } break;

        case editor_command_tag_buffer_save_all:
        {
            for (u32 buffer_index = 0; buffer_index < editor->buffers.count; buffer_index++)
            {
                // HACK:
                if (!editor->buffers.base[buffer_index].is_file)
                    continue;

                editor_buffer_save_file(platform, editor, buffer_index);
            }
        } break;

        case editor_command_tag_copy:
        {
            editor_editable_buffer buffer = editor_editable_buffer_begin(editor);
            editor_selection selection = editor_selection_get(editor, buffer);

            if (!selection.count)
                break;

            string64k *slot = &editor->copy_buffer.base[editor->copy_buffer.next];

            // HACK:
            if (selection.count > carray_count(slot->base))
                break;

            editor->copy_buffer.active = editor->copy_buffer.next;
            editor->copy_buffer.next   = (editor->copy_buffer.next + 1) % carray_count(editor->copy_buffer.base);
            editor->copy_buffer.count  = min(editor->copy_buffer.count + 1, carray_count(editor->copy_buffer.base));

            slot->count = selection.count;
            memcpy(slot->base, buffer.text.base + selection.offset, selection.count);
        } break;

        case editor_command_tag_paste:
        case editor_command_tag_paste_cycle:
        {
            if (!editor->copy_buffer.count)
                break;

            editor_editable_buffer buffer = editor_editable_buffer_begin(editor);
            editor_selection selection = editor_selection_get(editor, buffer);

            if (editor->copy_buffer.was_used_previously && (command_tag == editor_command_tag_paste_cycle))
            {
                u32 active = (editor->copy_buffer.active + editor->copy_buffer.count - 1) % editor->copy_buffer.count;
                assert(editor->copy_buffer.base[active].count);

                string64k slot = editor->copy_buffer.base[editor->copy_buffer.active];
                assert(slot.count);
                buffer.cursor_offset          -= slot.count;
                buffer.selection_start_offset  = buffer.cursor_offset;
                editor_remove(editor, &buffer, buffer.cursor_offset, slot.count);

                editor->copy_buffer.active = active;
            }

            string64k slot = editor->copy_buffer.base[editor->copy_buffer.active];
            assert(slot.count);

            editor_selection_remove(editor, &buffer);
            editor_insert(editor, &buffer, buffer.cursor_offset, string64k_to_string(slot));

            buffer.cursor_offset          += slot.count;
            buffer.selection_start_offset  = buffer.cursor_offset;

            editor_editable_buffer_end(editor, buffer);

            editor->copy_buffer.was_used = true;
        } break;

        cases_complete("command tag %i", command_tag);
    }

    assert(handled);
}

editor_buffer_text_siganture
{
    return sl(string) { buffer->base, buffer->count };
}

editor_buffer_add_signature
{
    assert(editor_buffer_find(editor, title, is_file) == -1);

    u32 buffer_index = editor->buffers.count;
    editor->buffers.count += 1;
    moma_reallocate_array(&editor->memory, &editor->buffers, editor_buffer);

    editor_buffer *buffer = &editor->buffers.base[buffer_index];
    memset(buffer, 0, sizeof(*buffer)); // VS bug with assigning big structs

    buffer->title = string255_from_string(title);
    buffer->is_file = is_file;

    return buffer_index;
}

editor_buffer_find_signature
{
    for (u32 i = 0; i < editor->buffers.count; i++)
    {
        string buffer_title = string255_to_string(editor->buffers.base[i].title);

        if ((editor->buffers.base[i].is_file == is_file) && mos_are_equal(title, buffer_title))
            return i;
    }

    return -1;
}

editor_buffer_find_or_add_signature
{
    u32 buffer_index = editor_buffer_find(editor, title, is_file);

    if (buffer_index == -1)
        buffer_index = editor_buffer_add(editor, title, is_file);

    return buffer_index;
}

editor_active_buffer_get_signature
{
    assert(editor->active_buffer_index < editor->buffers.count);
    editor_buffer *active_buffer = editor->buffers.base + editor->active_buffer_index;

    return active_buffer;
}

editor_buffer_load_file_signature
{
    assert(buffer_index < editor->buffers.count);

    editor_buffer *buffer = &editor->buffers.base[buffer_index];
    assert(buffer->is_file);

    string path = string255_to_string(buffer->title);

    assert(editor_file_extension_check(editor, path));

    string text = { buffer->base, carray_count(buffer->base) };
    mop_read_file_result result = mop_read_file(platform, text, path);
    buffer->has_changed = !result.ok;

    // remove \r
    string iterator = result.data;
    u32 count = 0;
    while (iterator.count)
    {
        mos_utf8_result result = mos_utf8_advance(&iterator);
        if (result.utf32_code != '\r')
        {
            memcpy(buffer->base + count, iterator.base - result.byte_count, result.byte_count);
            count += result.byte_count;
        }
    }

    buffer->count = count;
    buffer->cursor_offset = min(buffer->cursor_offset, buffer->count);
}

editor_buffer_save_file_signature
{
    assert(buffer_index < editor->buffers.count);

    editor_buffer *buffer = &editor->buffers.base[buffer_index];
    assert(buffer->is_file);

    usize tmemory_frame = editor->memory.used_count;

    string text = editor_buffer_text(editor, buffer);

    // save with line endings

    mos_string_buffer builder = mos_buffer_from_memory(editor->memory.base + editor->memory.used_count, editor->memory.count - editor->memory.used_count);

    string iterator = text;
    u32 count = 0;
    while (iterator.count)
    {
        mos_utf8_result result = mos_utf8_advance(&iterator);
        if (result.utf32_code == '\n')
        {
            mos_write(&builder, "\r\n");
        }
        else
        {
            assert(builder.used_count + result.byte_count <= builder.total_count);
            memcpy(builder.base + builder.used_count, iterator.base - result.byte_count, result.byte_count);
            builder.used_count += result.byte_count;
        }
    }

    buffer->file_save_error = !mop_write_file(platform, string255_to_string(buffer->title), mos_buffer_to_string(builder));

    buffer->has_changed &= buffer->file_save_error;

    editor->memory.used_count = tmemory_frame;
}

editor_directory_load_all_files_signature
{
    mop_file_search_iterator iterator = mop_file_search_init(platform, relative_path);

    mop_file_search_result result;

    u32 result_buffer_index = editor->active_buffer_index;

    while (mop_file_search_advance(&result, platform, &iterator))
    {
        if (result.is_directory)
            continue;

        if (!editor_file_extension_check(editor, result.filepath))
            continue;

        u32 buffer_count = editor->buffers.count;
        u32 buffer_index = editor_buffer_find_or_add(editor, result.filepath, true);

        // is new buffer
        if (buffer_index == buffer_count)
        {
            editor_buffer_load_file(platform, editor, buffer_index);
            result_buffer_index = buffer_index;
        }

        buffer_count = editor->buffers.count;
    }

    return result_buffer_index;
}

editor_edit_sanity_check_signature
{
    // sanity check:
#if defined editor_enable_sanity_check
    {
        string text = buffer.text;
        b8 found_cursor_offset          = false;
        b8 found_selection_start_offset = false;

        u32 offset = 0;
        while (text.count)
        {
            found_cursor_offset          |= (offset == buffer.cursor_offset);
            found_selection_start_offset |= (offset == buffer.selection_start_offset);

            mos_utf8_result result = mos_utf8_advance(&text);
            offset += result.byte_count;
        }

        found_cursor_offset          |= (offset == buffer.cursor_offset);
        found_selection_start_offset |= (offset == buffer.selection_start_offset);

        assert(found_cursor_offset && found_selection_start_offset);
    }

    // assert selection is in bounds
    editor_selection_get(editor, buffer);
#endif
}

editor_buffer_edit_begin_signature
{
    editor_editable_buffer result = {0};
    result.text.base              = buffer->base;
    result.text.count             = buffer->count;
    result.total_count            = carray_count(buffer->base);
    result.cursor_offset          = buffer->cursor_offset;
    result.selection_start_offset = buffer->selection_start_offset;

    assert(result.text.count <= carray_count(buffer->base));
    assert(result.cursor_offset <= carray_count(buffer->base));

    return result;
}

editor_buffer_edit_end_signature
{
    assert(editable_buffer.text.count <= carray_count(buffer->base));
    assert(editable_buffer.cursor_offset <= carray_count(buffer->base));

    editor_edit_sanity_check(editor, editable_buffer);

    buffer->count                   = editable_buffer.text.count;
    buffer->cursor_offset           = editable_buffer.cursor_offset;
    buffer->selection_start_offset  = editable_buffer.selection_start_offset;
    buffer->has_changed            |= editable_buffer.has_changed;
}

editor_buffer255_edit_begin_signature
{
    editor_editable_buffer result = {0};
    result.text.base              = buffer->text.base;
    result.text.count             = buffer->text.count;
    result.total_count            = carray_count(buffer->text.base);
    result.cursor_offset          = buffer->cursor_offset;
    result.selection_start_offset = buffer->selection_start_offset;

    assert(result.text.count <= carray_count(buffer->text.base));
    assert(result.cursor_offset <= carray_count(buffer->text.base));

    return result;
}

editor_buffer255_edit_end_signature
{
    assert(editable_buffer.text.count <= carray_count(buffer->text.base));
    assert(editable_buffer.cursor_offset <= carray_count(buffer->text.base));

    editor_edit_sanity_check(editor, editable_buffer);

    buffer->text.count              = editable_buffer.text.count;
    buffer->cursor_offset           = editable_buffer.cursor_offset;
    buffer->selection_start_offset  = editable_buffer.selection_start_offset;
    buffer->has_changed            |= editable_buffer.has_changed;
}

editor_selection_get_signature
{
    editor_selection selection;
    if (buffer.selection_start_offset <= buffer.cursor_offset)
    {
        selection.offset = buffer.selection_start_offset;
        selection.count  = buffer.cursor_offset - buffer.selection_start_offset;
    }
    else
    {
        selection.offset = buffer.cursor_offset;
        selection.count  = buffer.selection_start_offset - buffer.cursor_offset;
    }

    assert(selection.offset                   <= buffer.text.count);
    assert(selection.offset + selection.count <= buffer.text.count);

    return selection;
}

editor_selection_remove_signature
{
    editor_selection selection = editor_selection_get(editor, *buffer);
    editor_remove(editor, buffer, selection.offset, selection.count);

    buffer->cursor_offset          = selection.offset;
    buffer->selection_start_offset = selection.offset;

    return (selection.count > 0);
}

editor_insert_signature
{
    assert(buffer->text.count + text.count <= buffer->total_count);
    assert(offset + text.count <= buffer->total_count);

    memcpy(buffer->text.base + offset + text.count, buffer->text.base + offset, buffer->text.count - offset);
    memcpy(buffer->text.base + offset, text.base, text.count);

    buffer->text.count += text.count;

    buffer->has_changed |= (text.count != 0);
}

editor_remove_signature
{
    assert(count <= buffer->text.count);
    assert(offset + count <= buffer->text.count);

    memcpy(buffer->text.base + offset, buffer->text.base + offset + count, buffer->text.count - offset - count);

    buffer->text.count -= count;

    buffer->has_changed |= (count != 0);
}

editor_buffer_move_left_signature
{
    if (!buffer->cursor_offset)
        return sl(mos_utf8_result) {0};

    mos_utf8_result result = mos_utf8_previous(buffer->text, buffer->cursor_offset);
    assert(result.byte_count);

    assert(result.byte_count <= buffer->cursor_offset);
    buffer->cursor_offset -= result.byte_count;

    return result;
}

editor_buffer_move_right_signature
{
    assert(buffer->cursor_offset <= buffer->text.count);

    if (buffer->cursor_offset == buffer->text.count)
        return sl(mos_utf8_result) {0};

    string text = buffer->text;
    mos_advance(&text, buffer->cursor_offset);

    mos_utf8_result result = mos_utf8_advance(&text);
    assert(result.byte_count);

    buffer->cursor_offset += result.byte_count;

    return result;
}

editor_buffer_to_previous_line_end_signature
{
    editor_buffer_to_previous_line_end_result result = {0};

    do
    {
        mos_utf8_result move_result = editor_buffer_move_left(editor, buffer);
        result.skipped_line |= (move_result.utf32_code == '\n');
        if (!move_result.byte_count || result.skipped_line)
            break;

        result.column += 1;
    } while (true);

    return result;
}

editor_buffer_to_next_line_start_signature
{
    do
    {
        mos_utf8_result result = editor_buffer_move_right(editor, buffer);
        if (!result.byte_count || (result.utf32_code == '\n'))
            break;

    } while (true);
}

editor_buffer_to_line_start_signature
{
    u32 column = 0;

    do
    {
        mos_utf8_result result = editor_buffer_move_left(editor, buffer);
        if (!result.byte_count)
            break;

        // don't skip line
        if (result.utf32_code == '\n')
        {
            buffer->cursor_offset += 1;
            break;
        }

        column += 1;
    } while (true);

    return column;
}

editor_buffer_to_line_end_signature
{
    do
    {
        mos_utf8_result result = editor_buffer_move_right(editor, buffer);
        if (!result.byte_count)
            break;

        // don't skip line
        if (result.utf32_code == '\n')
        {
            buffer->cursor_offset -= 1;
            break;
        }
    } while (true);
}

editor_buffer_move_right_within_line_signature
{
    while (max_move_count)
    {
        mos_utf8_result result = editor_buffer_move_right(editor, buffer);
        if (!result.byte_count)
            break;

        // don't skip line
        if (result.utf32_code == '\n')
        {
            buffer->cursor_offset -= 1;
            break;
        }

        max_move_count -= 1;
    }
}

editor_buffer_count_right_spaces_signature
{
    u32 cursor_offset = buffer->cursor_offset;

    u32 space_count = 0;
    do
    {
        mos_utf8_result result = editor_buffer_move_right(editor, buffer);
        if (!result.byte_count || (result.utf32_code != ' '))
            break;

        space_count += 1;
    } while (true);

     buffer->cursor_offset = cursor_offset;

     return space_count;
}

editor_get_absolute_path_signature
{
    string working_directory = mop_get_working_directory(platform);

    mos_string_buffer builder = mos_buffer_from_memory(buffer.base, buffer.count);

    mos_write(&builder, "%.*s/", fs(working_directory));

    while (relative_path.count)
    {
        if (mos_try_skip(&relative_path, s("..")))
        {
            // remove '/'
            if (builder.used_count)
                builder.used_count -= 1;

            while (builder.used_count && (builder.base[builder.used_count - 1] != '/'))
                builder.used_count -= 1;

            mos_try_skip(&relative_path, s("/"));
        }
        else
        {
            mos_string directory = mos_skip_until_pattern_or_end(&relative_path, s("/"));
            mos_write(&builder, "%.*s", fs(directory));

            if (mos_try_skip(&relative_path, s("/")))
                mos_write(&builder, "/");
        }
    }

    mos_string absolute_path = mos_buffer_to_string(builder);
    return absolute_path;
}

editor_get_relative_path_signature
{
    string working_directory = mop_get_working_directory(platform);

    mos_string_buffer builder = mos_buffer_from_memory(buffer.base, buffer.count);

    string left  = working_directory;
    string right = absolute_path;
    while (left.count && right.count)
    {
        string left_test = left;
        string right_test = right;
        string left_name  = mos_skip_until_pattern_or_end(&left_test, s("/"));
        string right_name = mos_skip_until_pattern_or_end(&right_test, s("/"));

        if (!mos_are_equal(left_name, right_name))
            break;

        if (mos_try_skip(&left_test, s("/")) && right_test.count && !mos_try_skip(&right_test, s("/")))
            break;

        left  = left_test;
        right = right_test;
    }

    if (!left.count && (!right.count || mos_try_skip(&right, s("/"))))
    {
        mos_write(&builder, "%.*s", fs(right));
    }
    else
    {
        while (left.count)
        {
            mos_skip_until_pattern_or_end(&left, s("/"));
            mos_try_skip(&left, s("/"));

            if (builder.used_count)
                mos_write(&builder, "/..");
            else
                mos_write(&builder, "..");
        }

        if (right.count)
        {
            assert(builder.used_count);
            mos_write(&builder, "/%.*s", fs(right));
        }
    }

    mos_string relative_path = mos_buffer_to_string(builder);
    return relative_path;
}

editor_file_search_filter_get_signature
{
    mos_string search_path = editor_get_absolute_path(sl(u8_array) { filter->buffer, carray_count(filter->buffer) }, platform, relative_path);

    mos_split_path_result split = mos_split_path(search_path);

    string pattern = sl(string) { split.name.base, (usize) (search_path.base + search_path.count - split.name.base) };

    filter->search_directory = split.directory;
    filter->pattern = pattern;
}

editor_file_search_filter_check_signature
{
    string suffix;
    if (result.is_parent_directory || result.is_search_directory)
        suffix = string_empty;
    else
    {
        suffix = mos_remaining_substring(result.filepath, filter.search_directory.count);

        if (filter.search_directory.count)
            mos_advance(&suffix, 1);
    }

    string match = mos_contains_pattern(suffix, filter.pattern);
    if (filter.pattern.count && !match.base)
        return sl(editor_file_search_filter_result) { string_empty, string_empty, false };

    return sl(editor_file_search_filter_result) { suffix, match, true };
}

editor_search_forward_signature
{
    string right = mos_remaining_substring(text, offset);
    string at = mos_contains_pattern(right, pattern);
    if (!at.base)
    {
        string left = { text.base, offset };
        at = mos_contains_pattern(left, pattern);
    }

    return at;
}

editor_buffer_search_forward_signature
{
    string at = editor_search_forward(editor, buffer->text, offset, pattern);
    if (at.base)
    {
        buffer->selection_start_offset = (u32) (at.base - buffer->text.base);
        buffer->cursor_offset          = buffer->selection_start_offset + pattern.count;
    }
}

editor_search_backward_signature
{
    string left = { text.base, offset };
    string at = mos_contains_pattern_from_end(left, pattern);
    if (!at.base)
    {
        string right = mos_remaining_substring(text, offset);
        at = mos_contains_pattern_from_end(right, pattern);
    }

    return at;
}

editor_buffer_search_backward_signature
{
    string at = editor_search_backward(editor, buffer->text, offset, pattern);
    if (at.base)
    {
        buffer->selection_start_offset = (u32) (at.base - buffer->text.base);
        buffer->cursor_offset          = buffer->selection_start_offset + pattern.count;
    }
}

editor_mode_set_signature
{
    assert(mode < editor_focus_count);
    if (editor->mode != mode)
    {
        editor->mode = mode;
        editor->focus = mode;
        editor->previous_focus = editor_focus_buffer;
    }
}

editor_editable_buffer_begin_signature
{
    editor_editable_buffer buffer;
    switch (editor->focus)
    {
        case editor_focus_buffer:
        {
            editor_buffer *active_buffer = editor_active_buffer_get(editor);
            buffer = editor_buffer_edit_begin(editor, active_buffer);
        } break;

        case editor_focus_file_search:
        {
            buffer = editor_buffer255_edit_begin(editor, &editor->file_open_relative_path);
        } break;

        case editor_focus_search:
        {
            buffer = editor_buffer255_edit_begin(editor, &editor->search_buffer);
        } break;

        case editor_focus_search_replace:
        {
            buffer = editor_buffer255_edit_begin(editor, &editor->search_replace_buffer);
        } break;

        cases_complete("%.*s", fs(editor_focus_names[editor->focus]));
    }

    return buffer;
}

editor_editable_buffer_end_signature
{
    switch (editor->focus)
    {
        case editor_focus_buffer:
        {
            editor_buffer *active_buffer = editor_active_buffer_get(editor);
            editor_buffer_edit_end(editor, active_buffer, buffer);
        } break;

        case editor_focus_file_search:
        {
            editor_buffer255_edit_end(editor, &editor->file_open_relative_path, buffer);
        } break;

        case editor_focus_search:
        {
            editor_buffer255_edit_end(editor, &editor->search_buffer, buffer);
        } break;

        case editor_focus_search_replace:
        {
            editor_buffer255_edit_end(editor, &editor->search_replace_buffer, buffer);
        } break;

        cases_complete("%.*s", fs(editor_focus_names[editor->focus]));
    }
}