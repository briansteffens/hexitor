#include <stdlib.h>

#include <ncurses.h>

#define SOURCE_LEN 256

typedef struct
{
    WINDOW* window;
    int left;
    int top;
    int width;
    int height;
} pane;

pane hex_pane;
pane detail_pane;

unsigned char source[SOURCE_LEN];

int cursor_byte;
int cursor_nibble;

int scroll_start;

void setup_pane(pane* pane)
{
    if (pane->window)
    {
        delwin(pane->window);
    }

    pane->window = newwin(pane->height, pane->width, pane->top, pane->left);
}

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

int bytes_per_line()
{
    return hex_pane.width / 3;
}

int byte_in_line(int byte_offset)
{
    return byte_offset / bytes_per_line();
}

int byte_in_column(int byte_offset)
{
    return byte_offset % bytes_per_line() * 3;
}

int first_byte_in_line(int line_index)
{
    return line_index * bytes_per_line();
}

int last_byte_in_line(int line_index)
{
    return first_byte_in_line(line_index + 1) - 1;
}

int first_visible_byte()
{
    return first_byte_in_line(scroll_start);
}

int last_visible_line()
{
    return scroll_start + hex_pane.height - 1;
}

int last_visible_byte()
{
    int ret = last_byte_in_line(last_visible_line());

    return ret < SOURCE_LEN ? ret : SOURCE_LEN - 1;
}

void render_details()
{
    WINDOW* w = detail_pane.window;
    wclear(w);

    unsigned char* cursor_start = source + cursor_byte;

    mvwprintw(w, 1, 1, "Offset: %d", cursor_byte);
    mvwprintw(w, 2, 1, "Int8:   %d", *(int8_t*)cursor_start);
    mvwprintw(w, 3, 1, "Uint8:  %d", *(uint8_t*)cursor_start);
    mvwprintw(w, 4, 1, "Int16:  %d", *(int16_t*)cursor_start);
    mvwprintw(w, 5, 1, "Uint16: %d", *(uint16_t*)cursor_start);
    mvwprintw(w, 6, 1, "Int32:  %d", *(int32_t*)cursor_start);
    mvwprintw(w, 7, 1, "Uint32: %d", *(uint32_t*)cursor_start);
    mvwprintw(w, 8, 1, "Int64:  %ld", *(int64_t*)cursor_start);
    mvwprintw(w, 9, 1, "UInt64: %ld", *(uint64_t*)cursor_start);

    box(w, 0, 0);
}

void handle_sizing()
{
    static int last_max_x = -1;
    static int last_max_y = -1;

    int max_x;
    int max_y;

    getmaxyx(stdscr, max_y, max_x);

    if (max_y == last_max_y && max_x == last_max_x)
    {
        return;
    }

    // Terminal resized
    last_max_x = max_x;
    last_max_y = max_y;

    hex_pane.width = max_x - 20;
    hex_pane.height = max_y - 20;

    hex_pane.width = max_x - 20;
    //hex_pane.height = max_y - 20;
    hex_pane.height = 5;
    setup_pane(&hex_pane);

    detail_pane.top = hex_pane.top + hex_pane.height + 3;
    detail_pane.width = max_x - 20;
    detail_pane.height = max_y - hex_pane.height - hex_pane.top - 5;
    setup_pane(&detail_pane);
}

void handle_event(int event)
{
    int temp;

    switch (event)
    {
        case KEY_LEFT:
            if (cursor_nibble == 1)
            {
                cursor_nibble = 0;
            }
            else
            {
                cursor_nibble = 1;
                cursor_byte--;
            }

            break;

        case KEY_RIGHT:
            if (cursor_nibble == 0)
            {
                cursor_nibble = 1;
            }
            else
            {
                cursor_nibble = 0;
                cursor_byte++;
            }

            break;

        case KEY_UP:
            temp = cursor_byte - bytes_per_line();

            if (temp >= 0)
            {
                cursor_byte = temp;
            }

            break;

        case KEY_DOWN:
            temp = cursor_byte + bytes_per_line();

            if (temp < SOURCE_LEN)
            {
                cursor_byte = temp;
            }

            break;
    }
}

void clamp_scrolling()
{
    // Clamp to start of buffer
    if (cursor_byte < 0)
    {
        cursor_byte = 0;
        cursor_nibble = 0;
    }

    // Clamp to end of buffer
    if (cursor_byte >= SOURCE_LEN)
    {
        cursor_byte = SOURCE_LEN - 1;
        cursor_nibble = 1;
    }

    // Scroll up if cursor has left viewport
    while (cursor_byte < first_visible_byte())
    {
        scroll_start--;
    }

    // Scroll down if cursor has left viewport
    while (cursor_byte > last_visible_byte())
    {
        scroll_start++;
    }

    // Clamp scroll start to beginning of buffer
    if (scroll_start < 0)
    {
        scroll_start = 0;
    }
}

void render_hex()
{
    wclear(hex_pane.window);

    char hex[2];

    for (int i = first_visible_byte(); i <= last_visible_byte(); i++)
    {
        byte_to_hex(source[i], hex);

        int out_y = byte_in_line(i) - scroll_start;
        int out_x = byte_in_column(i);

        mvwprintw(hex_pane.window, out_y, out_x, "%c%c ", hex[0], hex[1]);
    }
}

void place_cursor()
{
    int render_cursor_x = hex_pane.left +
        byte_in_column(cursor_byte) + cursor_nibble;
    int render_cursor_y = hex_pane.top +
        byte_in_line(cursor_byte) - scroll_start;

    move(render_cursor_y, render_cursor_x);
}

void flush_output()
{
    wnoutrefresh(hex_pane.window);
    wnoutrefresh(detail_pane.window);

    doupdate();
}

void update(int event)
{
    handle_sizing();
    handle_event(event);
    clamp_scrolling();
    render_hex();
    render_details();
    place_cursor();
    flush_output();
}

int main()
{
    cursor_byte = 0;
    cursor_nibble = 0;
    scroll_start = 0;

    hex_pane.left = 10;
    hex_pane.top = 3;

    detail_pane.left = 10;

    for (int i = 0; i < SOURCE_LEN; i++)
    {
        source[i] = (unsigned char)i;
    }

    initscr();
    cbreak();
    keypad(stdscr, TRUE);

    int event;

    while ((event = getch()) != KEY_F(1))
    {
        update(event);
    }
}
