#include <stdlib.h>

#include <ncurses.h>

typedef struct
{
    WINDOW* window;
    int left;
    int top;
    int width;
    int height;
} pane;

void setup_pane(pane* pane)
{
    if (pane->window)
    {
        delwin(pane->window);
    }

    pane->window = newwin(pane->height, pane->width, pane->top, pane->left);
}

#define SOURCE_LEN 256
#define MAX_OUTPUT_WIDTH 256
#define MAX_OUTPUT_HEIGHT 256

typedef struct
{
    unsigned char source[SOURCE_LEN];

    int cursor_byte;
    int cursor_nibble;

    int hex_width;
    int hex_height;

    int grouping;

    int scroll_start;
} state;

char nibble_to_hex(unsigned char nibble)
{
    if (nibble < 10)
    {
        return '0' + nibble;
    }

    return 'a' + nibble - 10;
}

void byte_to_hex(unsigned char byte, char* hex)
{
    unsigned char first = byte >> 4;
    unsigned char second = byte & 0x0f;

    hex[0] = nibble_to_hex(first);
    hex[1] = nibble_to_hex(second);
}

void render_details(state* state, pane* pane)
{
    wclear(pane->window);

    unsigned char* cursor_start = state->source + state->cursor_byte;

    mvwprintw(pane->window, 1, 1, "Offset: %d", state->cursor_byte);
    mvwprintw(pane->window, 2, 1, "Int8:   %d", *(int8_t*)cursor_start);
    mvwprintw(pane->window, 3, 1, "Uint8:  %d", *(uint8_t*)cursor_start);
    mvwprintw(pane->window, 4, 1, "Int16:  %d", *(int16_t*)cursor_start);
    mvwprintw(pane->window, 5, 1, "Uint16: %d", *(uint16_t*)cursor_start);
    mvwprintw(pane->window, 6, 1, "Int32:  %d", *(int32_t*)cursor_start);
    mvwprintw(pane->window, 7, 1, "Uint32: %d", *(uint32_t*)cursor_start);
    mvwprintw(pane->window, 8, 1, "Int64:  %ld", *(int64_t*)cursor_start);
    mvwprintw(pane->window, 9, 1, "UInt64: %ld", *(uint64_t*)cursor_start);

    box(pane->window, 0, 0);
}

int bytes_per_line(state* state)
{
    return state->hex_width / (state->grouping * 2 + 1);
}

int byte_in_line(state* state, int byte_offset)
{
    return byte_offset / bytes_per_line(state);
}

int byte_in_column(state* state, int byte_offset)
{
    return byte_offset % bytes_per_line(state) * (state->grouping * 2 + 1);
}

int first_byte_in_line(state* state, int line_index)
{
    return line_index * bytes_per_line(state);
}

int last_byte_in_line(state* state, int line_index)
{
    return first_byte_in_line(state, line_index + 1) - 1;
}

int first_visible_byte(state* state)
{
    return first_byte_in_line(state, state->scroll_start);
}

int last_visible_line(state* state)
{
    return state->scroll_start + state->hex_height - 1;
}

int last_visible_byte(state* state)
{
    int ret = last_byte_in_line(state, last_visible_line(state));

    return ret < SOURCE_LEN ? ret : SOURCE_LEN - 1;
}

int main()
{
    state state;
    state.cursor_byte = 0;
    state.cursor_nibble = 0;
    state.grouping = 1;
    state.scroll_start = 0;

    for (int i = 0; i < SOURCE_LEN; i++)
    {
        state.source[i] = (unsigned char)i;
    }

    initscr();
    cbreak();
    keypad(stdscr, TRUE);
    refresh();

    pane hex_pane;
    hex_pane.left = 10;
    hex_pane.top = 3;

    pane detail_pane;
    detail_pane.left = 10;

    int last_max_x = -1;
    int last_max_y = -1;
    int max_x;
    int max_y;

    getmaxyx(stdscr, max_y, max_x);

    int ch;
    while ((ch = getch()) != KEY_F(1))
    {
        getmaxyx(stdscr, max_y, max_x);

        if (max_y != last_max_y || max_x != last_max_x)
        {
            // Terminal resized
            last_max_x = max_x;
            last_max_y = max_y;

            hex_pane.width = max_x - 20;
            hex_pane.height = max_y - 20;

            hex_pane.width = max_x - 20;
            //hex_pane.height = max_y - 20;
            hex_pane.height = 5;
            setup_pane(&hex_pane);

            state.hex_width = hex_pane.width;
            state.hex_height = hex_pane.height;

            detail_pane.top = hex_pane.top + hex_pane.height + 3;
            detail_pane.width = max_x - 20;
            detail_pane.height = max_y - hex_pane.height - hex_pane.top - 5;
            setup_pane(&detail_pane);
        }

        int temp;

        switch (ch)
        {
            case KEY_LEFT:
                if (state.cursor_nibble == 1)
                {
                    state.cursor_nibble = 0;
                }
                else
                {
                    state.cursor_nibble = 1;
                    state.cursor_byte--;
                }

                break;

            case KEY_RIGHT:
                if (state.cursor_nibble == 0)
                {
                    state.cursor_nibble = 1;
                }
                else
                {
                    state.cursor_nibble = 0;
                    state.cursor_byte++;
                }

                break;

            case KEY_UP:
                temp = state.cursor_byte - bytes_per_line(&state);

                if (temp >= 0)
                {
                    state.cursor_byte = temp;
                }

                break;

            case KEY_DOWN:
                temp = state.cursor_byte + bytes_per_line(&state);

                if (temp < SOURCE_LEN)
                {
                    state.cursor_byte = temp;
                }

                break;

            case KEY_F(2):
                state.scroll_start++;
                break;
        }

        // Clamp to start of buffer
        if (state.cursor_byte < 0)
        {
            state.cursor_byte = 0;
            state.cursor_nibble = 0;
        }

        // Clamp to end of buffer
        if (state.cursor_byte >= SOURCE_LEN)
        {
            state.cursor_byte = SOURCE_LEN - 1;
            state.cursor_nibble = 1;
        }

        // Scroll up if cursor has left viewport
        while (state.cursor_byte < first_visible_byte(&state))
        {
            state.scroll_start--;
        }

        // Scroll down if cursor has left viewport
        while (state.cursor_byte > last_visible_byte(&state))
        {
            state.scroll_start++;
        }

        // Clamp scroll start to beginning of buffer
        if (state.scroll_start < 0)
        {
            state.scroll_start = 0;
        }

        // Render hex data
        wclear(hex_pane.window);
        char hex[2];
        for (int i = first_visible_byte(&state);
             i <= last_visible_byte(&state); i++)
        {
            byte_to_hex(state.source[i], hex);

            int out_y = byte_in_line(&state, i) - state.scroll_start;
            int out_x = byte_in_column(&state, i);

            mvwprintw(hex_pane.window, out_y, out_x, "%c%c ", hex[0], hex[1]);
        }

        render_details(&state, &detail_pane);

        // Place cursor
        int render_cursor_x = hex_pane.left +
            byte_in_column(&state, state.cursor_byte) + state.cursor_nibble;
        int render_cursor_y = hex_pane.top +
            byte_in_line(&state, state.cursor_byte) - state.scroll_start;

        move(render_cursor_y, render_cursor_x);

        // Flush output
        wnoutrefresh(hex_pane.window);
        wnoutrefresh(detail_pane.window);

        doupdate();
    }
}
