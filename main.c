#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>

#include <ncurses.h>

#define BUFFER_SIZE 16 * 1024
#define MAX_COMMAND_LEN 256
#define MAX_ERROR_LEN 64

#define KEY_ESC 27
#define KEY_RETURN 10
#define KEY_DELETE 127

#define STYLE_ERROR 13
#define STYLE_CURSOR 14

#define CHARS_PER_BYTE 3

#define ESCAPE_SEQUENCE_MAX_TIME_MS 50

typedef struct
{
    WINDOW* window;
    int left;
    int top;
    int width;
    int height;
} pane;

#define right(pane) (pane.left + pane.width)
#define bottom(pane) (pane.top + pane.height)

#define PANES_LEN 3

#define PANE_HEX 0
#define PANE_ASCII 1
#define PANE_DETAIL 2

pane panes[PANES_LEN];

typedef struct
{
    int x;
    int y;
} point;

unsigned char* source = NULL;
int source_len;

char* original_filename;

int cursor_byte = 0;
int cursor_nibble = 0;

int scroll_start = 0;

int max_x;
int max_y;

char command[MAX_COMMAND_LEN];
int command_len;
bool command_entering = false;

// 2 hex digits plus space
#define MAX_SEARCH_TERM_LEN MAX_COMMAND_LEN / 3
unsigned char search_term[MAX_SEARCH_TERM_LEN];
int search_term_len;

char error_text[MAX_ERROR_LEN];
bool error_displayed = false;

void set_error(const char* text)
{
    error_displayed = true;
    strncpy(error_text, text, MAX_ERROR_LEN);
}

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

unsigned char hex_to_nibble(char hex)
{
    if (hex >= 'a')
    {
        return hex - 'a' + 10;
    }

    return hex - '0';
}

unsigned char first_nibble(unsigned char byte)
{
    return byte >> 4;
}

unsigned char second_nibble(unsigned char byte)
{
    return byte & 0x0f;
}

void byte_to_hex(unsigned char byte, char* hex)
{
    hex[0] = nibble_to_hex(first_nibble(byte));
    hex[1] = nibble_to_hex(second_nibble(byte));
}

unsigned char nibbles_to_byte(unsigned char n0, unsigned char n1)
{
    return n0 << 4 | n1;
}

int bytes_per_line()
{
    return panes[PANE_HEX].width / CHARS_PER_BYTE;
}

int byte_in_line(int byte_offset)
{
    return byte_offset / bytes_per_line();
}

int byte_in_column(int byte_offset)
{
    return byte_offset % bytes_per_line() * CHARS_PER_BYTE;
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
    return scroll_start + panes[PANE_HEX].height - 1;
}

int last_visible_byte()
{
    int ret = last_byte_in_line(last_visible_line());

    return ret < source_len ? ret : source_len - 1;
}

void handle_sizing()
{
    static int last_max_x = -1;
    static int last_max_y = -1;

    getmaxyx(stdscr, max_y, max_x);

    if (max_y == last_max_y && max_x == last_max_x)
    {
        return;
    }

    // Terminal resized
    last_max_x = max_x;
    last_max_y = max_y;

    panes[PANE_DETAIL].height = 7;

    panes[PANE_HEX].left = 1;
    panes[PANE_HEX].top = 0;
    panes[PANE_HEX].width = (max_x - 1) * 0.75;
    panes[PANE_HEX].height = max_y - panes[PANE_DETAIL].height;
    setup_pane(&panes[PANE_HEX]);

    panes[PANE_ASCII].left = panes[PANE_HEX].left + panes[PANE_HEX].width;
    panes[PANE_ASCII].top = panes[PANE_HEX].top;
    panes[PANE_ASCII].width = (max_x - 1) * 0.25;
    panes[PANE_ASCII].height = max_y - panes[PANE_DETAIL].height;
    setup_pane(&panes[PANE_ASCII]);

    panes[PANE_DETAIL].left = 0;
    panes[PANE_DETAIL].top = max_y - panes[PANE_DETAIL].height;
    panes[PANE_DETAIL].width = max_x;
    setup_pane(&panes[PANE_DETAIL]);
}

void handle_start_command(char first_char)
{
    command_entering = true;
    command[0] = first_char;
    command_len = 1;
}

void handle_cancel_command()
{
    command_entering = false;
}

void quit()
{
    if (source != NULL)
    {
        free(source);
    }

    endwin();
    exit(0);
}

void handle_write()
{
    char* subcommand = command + 2;
    int remaining = command_len - 2;

    // Check for quit
    bool also_quit = false;
    if (remaining > 0 && subcommand[0] == 'q')
    {
        also_quit = true;
        subcommand++;
        remaining--;
    }

    // Check for filename in command
    char* filename = original_filename;
    if (remaining > 0 && subcommand[0] == ' ')
    {
        filename = subcommand + 1;
    }

    // Write buffer to disk
    FILE* file = fopen(filename, "w");

    if (!file)
    {
        set_error("Error opening file: path not found or permissions?");
        return;
    }

    size_t bytes_written = 0;
    size_t bytes_left = source_len;

    while (bytes_left > 0)
    {
        size_t size = BUFFER_SIZE < bytes_left ? BUFFER_SIZE : bytes_left;
        size_t written = fwrite(source + bytes_written, 1, size, file);

        if (written != size)
        {
            set_error("Encountered error while writing file; may be corrupt.");
            return;
        }

        bytes_written += written;
        bytes_left -= written;
    }

    fclose(file);

    if (also_quit)
    {
        quit();
    }
}

void handle_jump_offset()
{
    // Make sure the command is an ASCII integer
    for (int i = 1; i < command_len; i++)
    {
        if (command[i] < '0' || command[i] > '9')
        {
            return;
        }
    }

    // Move cursor to requested offset
    cursor_byte = atoi(command + 1);
    cursor_nibble = 0;
}

bool is_hex_digit(char c)
{
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

void set_search_term(char* hex_ascii, int len)
{
    int cur = 0;
    search_term_len = 0;

    while (true)
    {
        // Skip any whitespace
        while (cur < len && isspace(hex_ascii[cur]))
        {
            cur++;
        }

        if (cur >= len)
        {
            break;
        }

        unsigned char first = hex_ascii[cur++];
        unsigned char second = hex_ascii[cur++];

        if (!is_hex_digit(first) || !is_hex_digit(second))
        {
            search_term_len = 0;
            set_error("Invalid search term format");
            return;
        }

        if (search_term_len >= MAX_SEARCH_TERM_LEN)
        {
            search_term_len = 0;
            set_error("Search term storage overflow");
            return;
        }

        search_term[search_term_len++] = nibbles_to_byte(
                hex_to_nibble(first), hex_to_nibble(second));
    }
}

void handle_search_next()
{
    if (!search_term_len)
    {
        return;
    }

    int cur = cursor_byte + 1;

    while (cur != cursor_byte)
    {
        if (cur + search_term_len >= source_len)
        {
            cur = 0;
        }

        bool match = true;

        if (cur + search_term_len >= source_len)
        {
            continue;
        }

        for (int i = 0; i < search_term_len; i++)
        {
            if (source[cur + i] != search_term[i])
            {
                match = false;
                break;
            }
        }

        if (match)
        {
            cursor_byte = cur;
            cursor_nibble = 0;
            return;
        }

        cur++;
    }

    set_error("Search term not found");
}

void handle_search_previous()
{
    if (!search_term_len)
    {
        return;
    }

    int cur = cursor_byte - 1;

    while (cur != cursor_byte)
    {
        if (cur < 0)
        {
            cur = source_len - search_term_len;
        }

        bool match = true;

        for (int i = 0; i < search_term_len; i++)
        {
            if (source[cur + i] != search_term[i])
            {
                match = false;
                break;
            }
        }

        if (match)
        {
            cursor_byte = cur;
            cursor_nibble = 0;
            return;
        }

        cur--;
    }

    set_error("Search term not found");
}

void handle_submit_command()
{
    command[command_len] = 0;
    command_entering = false;

    if (command[0] == '/')
    {
        set_search_term(&command[1], command_len - 1);
        handle_search_next();
        return;
    }

    if (strncmp(command, ":q", MAX_COMMAND_LEN) == 0)
    {
        quit();
        return;
    }

    if (command[1] == 'w')
    {
        handle_write();
        return;
    }

    handle_jump_offset();
}

void handle_backspace_command()
{
    if (command_len == 1)
    {
        handle_cancel_command();
        return;
    }

    command_len--;
}

void handle_add_to_command(int event)
{
    if (event < 32 || event > '~')
    {
        return;
    }

    if (command_len >= MAX_COMMAND_LEN)
    {
        return;
    }

    command[command_len++] = event;
}

void handle_command_event(int event)
{
    switch (event)
    {
        case KEY_ESC:    handle_cancel_command();      break;
        case KEY_RETURN: handle_submit_command();      break;
        case KEY_DELETE: handle_backspace_command();   break;
        default:         handle_add_to_command(event); break;
    }
}

void render_command()
{
    if (!command_entering)
    {
        return;
    }

    int start = 0;
    int len = command_len;

    // Scroll command horizontally
    if (command_len >= max_x - 1)
    {
        start = command_len - max_x + 1;
        len = max_x - 1;
    }

    move(max_y - 1, 0);

    // Print command to screen
    for (int i = 0; i < len; i++)
    {
        addch(command[start + i]);
    }

    // Clear the rest of the command line
    for (int i = len; i < max_x; i++)
    {
        addch(' ');
    }
}

void handle_key_left()
{
    if (cursor_nibble == 1)
    {
        cursor_nibble = 0;
    }
    else
    {
        cursor_nibble = 1;
        cursor_byte--;
    }
}

void handle_key_right()
{
    if (cursor_nibble == 0)
    {
        cursor_nibble = 1;
    }
    else
    {
        cursor_nibble = 0;
        cursor_byte++;
    }
}

void handle_key_up()
{
    int temp = cursor_byte - bytes_per_line();

    if (temp >= 0)
    {
        cursor_byte = temp;
    }
}

void handle_key_down()
{
    int temp = cursor_byte + bytes_per_line();

    if (temp < source_len)
    {
        cursor_byte = temp;
    }
}

void handle_previous_byte()
{
    cursor_byte--;
}

void handle_next_byte()
{
    cursor_byte++;
}

void handle_overwrite(int event)
{
    // Convert A-F to lower case
    if (event >= 'A' && event <= 'F')
    {
        event += 'a' - 'A';
    }

    // Only process 0-9 and a-f
    if (!((event >= '0' && event <= '9') || (event >= 'a' && event <= 'f')))
    {
        return;
    }

    unsigned char* byte = &source[cursor_byte];

    unsigned char first = first_nibble(*byte);
    unsigned char second = second_nibble(*byte);

    unsigned char* nibble = cursor_nibble ? &second : &first;
    *nibble = hex_to_nibble(event);

    *byte = nibbles_to_byte(first, second);

    handle_key_right();
}

void handle_page_up()
{
    cursor_byte -= panes[PANE_HEX].height * bytes_per_line();
}

void handle_page_down()
{
    cursor_byte += panes[PANE_HEX].height * bytes_per_line();
    scroll_start += panes[PANE_HEX].height;
}

void handle_end_of_buffer()
{
    cursor_byte = source_len - 1;
    cursor_nibble = 1;
}

// Handle compound commands that start with g (ex: gg).
// Returns true if the event was handled.
bool handle_g_chord(int event)
{
    static bool g_pressed = false;

    if (!g_pressed)
    {
        // If g was pressed, remember that for next key in the chord.
        // Otherwise, don't consume the event.
        g_pressed = event == 'g';
        return g_pressed;
    }

    // Handle second key in the chord.
    switch (event)
    {
        case 'g':
            // Chord 'gg' - move to beginning of buffer
            cursor_byte = 0;
            cursor_nibble = 0;
            break;
    }

    g_pressed = false;
    return true;
}

// Given an x,y location, find the pane at that position.
// Return -1 if there is no pane at that position.
int get_pane_under_coords(int x, int y)
{
    for (int pane = 0; pane < PANES_LEN; pane++)
    {
        if (x >= panes[pane].left && x <= right(panes[pane]) &&
            y >= panes[pane].top && y <= bottom(panes[pane]))
        {
            return pane;
        }
    }

    return -1;
}

// Convert screen-space coords to pane-space coords
point screen_to_pane(pane* pane, int x, int y)
{
    point ret;

    ret.x = x - pane->left;
    ret.y = y - pane->top;

    return ret;
}

void handle_mouse_pressed(MEVENT mouse_event)
{
    int pane = get_pane_under_coords(mouse_event.x, mouse_event.y);

    if (pane != PANE_HEX && pane != PANE_ASCII)
    {
        return;
    }

    point coords = screen_to_pane(&panes[pane], mouse_event.x, mouse_event.y);

    if (pane == PANE_HEX)
    {
        if (coords.x >= bytes_per_line() * CHARS_PER_BYTE - 1)
        {
            return;
        }

        cursor_byte = first_byte_in_line(coords.y + scroll_start) +
                      coords.x / CHARS_PER_BYTE;

        cursor_nibble = coords.x % CHARS_PER_BYTE;

        if (cursor_nibble == 2)
        {
            cursor_byte++;
            cursor_nibble = 0;
        }
    }
    else if (pane == PANE_ASCII)
    {
        if (coords.x >= bytes_per_line())
        {
            return;
        }

        cursor_byte = first_byte_in_line(coords.y + scroll_start) + coords.x;
        cursor_nibble = 0;
    }
}

void handle_key_home()
{
    cursor_byte = first_byte_in_line(byte_in_line(cursor_byte));
    cursor_nibble = 0;
}

void handle_key_end()
{
    cursor_byte = last_byte_in_line(byte_in_line(cursor_byte));
    cursor_nibble = 1;
}

// Calculate the milliseconds elapsed between start and end
unsigned ms_taken(struct timespec start, struct timespec end)
{
    return ((end.tv_sec - start.tv_sec) * 1000) +
           ((end.tv_nsec - start.tv_nsec) / 1000000);
}

bool handle_escape_sequence(int event)
{
    #define FULL_SEQUENCE_LEN 4

    static char sequence[FULL_SEQUENCE_LEN];
    static int sequence_len = 0;

    static struct timespec start;
    static struct timespec end;

    // Sequence must start with an escape key. If sequence is unstarted then
    // the only valid key is the escape key.
    if (!sequence_len && event != KEY_ESC)
    {
        return false;
    }

    // Start the timer on the first key of the sequence.
    if (!sequence_len)
    {
        clock_gettime(CLOCK_REALTIME, &start);
    }
    // Check the timer on subsequent keys of the sequence.
    else
    {
        clock_gettime(CLOCK_REALTIME, &end);

        // Reset the sequence and don't consume the keypress if the sequence
        // has taken too long to complete.
        if (ms_taken(start, end) > ESCAPE_SEQUENCE_MAX_TIME_MS)
        {
            sequence_len = 0;
            return false;
        }
    }

    sequence[sequence_len++] = event;

    // Sequence is not yet complete
    if (sequence_len != FULL_SEQUENCE_LEN)
    {
        return true;
    }

    // Handle sequence and reset
    switch (sequence[2])
    {
        case 49:
            handle_key_home();
            break;

        case 52:
            handle_key_end();
            break;
    }

    sequence_len = 0;
    return true;
}

void handle_event(int event)
{
    if (command_entering)
    {
        handle_command_event(event);
        return;
    }

    if (handle_g_chord(event))
    {
        return;
    }

    if (handle_escape_sequence(event))
    {
        return;
    }

    switch (event)
    {
        case 'h':
        case 'H':
        case KEY_LEFT:
            handle_key_left();
            break;

        case 'l':
        case 'L':
        case KEY_RIGHT:
            handle_key_right();
            break;

        case 'k':
        case 'K':
        case KEY_UP:
            handle_key_up();
            break;

        case 'j':
        case 'J':
        case KEY_DOWN:
            handle_key_down();
            break;

        case 'q':
        case 'Q':
            handle_previous_byte();
            break;

        case 'w':
        case 'W':
            handle_next_byte();
            break;

        case 'G':
            handle_end_of_buffer();
            break;

        case KEY_PPAGE:
            handle_page_up();
            break;

        case KEY_NPAGE:
            handle_page_down();
            break;

        case ':':
        case '/':
            handle_start_command(event);
            break;

        case 'n':
            handle_search_next();
            break;

        case 'N':
            handle_search_previous();
            break;

        default:
            handle_overwrite(event);
            break;
    }

    MEVENT mouse_event;
    if (getmouse(&mouse_event) == OK)
    {
        if (mouse_event.bstate & BUTTON1_PRESSED)
        {
            handle_mouse_pressed(mouse_event);
            return;
        }
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
    if (cursor_byte >= source_len)
    {
        cursor_byte = source_len - 1;
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
    wclear(panes[PANE_HEX].window);

    char hex[2];

    for (int i = first_visible_byte(); i <= last_visible_byte(); i++)
    {
        byte_to_hex(source[i], hex);

        int out_y = byte_in_line(i) - scroll_start;
        int out_x = byte_in_column(i);

        mvwprintw(panes[PANE_HEX].window, out_y, out_x, "%c%c ", hex[0],
                  hex[1]);
    }
}

void render_ascii()
{
    wclear(panes[PANE_ASCII].window);

    for (int i = first_visible_byte(); i <= last_visible_byte(); i++)
    {
        int out_y = byte_in_line(i) - scroll_start;
        int out_x = i % bytes_per_line();

        char output = '.';

        if (source[i] >= ' ' && source[i] <= '~')
        {
            output = source[i];
        }

        if (i == cursor_byte)
        {
            wattron(panes[PANE_ASCII].window, COLOR_PAIR(STYLE_CURSOR));
        }

        mvwprintw(panes[PANE_ASCII].window, out_y, out_x, "%c", output);

        if (i == cursor_byte)
        {
            wattroff(panes[PANE_ASCII].window, COLOR_PAIR(STYLE_CURSOR));
        }
    }
}

// Turn something like "1234" to "1,234". str must have enough space to add
// the commas (plus one for null termination): (len - 1) / 3 + 1
void add_commas(char* str, int len)
{
    // Skip dash if negative integer
    if (str[0] == '-')
    {
        str++;
        len--;
    }

    int commas = (len - 1) / 3;

    int source = len - 1;
    int target = source + commas;

    str[target + 1] = 0;

    while (source >= 0)
    {
        int digits_processed = len - source - 1;

        if (digits_processed && digits_processed % 3 == 0)
        {
            str[target--] = ',';
        }

        str[target--] = str[source--];
    }
}

void byte_to_binary_string(unsigned char src, char* output)
{
    char* out = output + 8;
    *out = 0;

    while (src != 0)
    {
        *--out = src & 1 ? '1' : '0';
        src >>= 1;
    }

    while (output != out)
    {
        *--out = '0';
    }
}

// Max value of uint64 with commas and null char
#define MAX_RENDERED_INT 27

#define render_int(y, x, label, cast, format) ({ \
    char rendered_int[MAX_RENDERED_INT]; \
    int len = snprintf(rendered_int, MAX_RENDERED_INT, format, \
                       *(cast*)(source + cursor_byte)); \
    add_commas(rendered_int, len); \
    mvwprintw(panes[PANE_DETAIL].window, y, x, "%s %s", label, \
              rendered_int); \
    }) \

void render_details()
{
    WINDOW* w = panes[PANE_DETAIL].window;
    wclear(w);

    mvwprintw(w, 1, 1, "Offset: %d", cursor_byte);

    char binary[9];
    byte_to_binary_string(*(int8_t*)(source + cursor_byte), binary);
    mvwprintw(w, 1, 30, "Binary: %s", binary);

    render_int(2, 1, "Int8:  ", int8_t, "%d");
    render_int(3, 1, "UInt8: ", uint8_t, "%d");
    render_int(4, 1, "Int16: ", int16_t, "%d");
    render_int(5, 1, "UInt16:", uint16_t, "%d");

    render_int(2, 30, "Int32: ", int32_t, "%d");
    render_int(3, 30, "UInt32:", uint32_t, "%d");
    render_int(4, 30, "Int64: ", int64_t, "%ld");
    render_int(5, 30, "UInt64:", uint64_t, "%ld");

    box(w, 0, 0);
}

void render_error()
{
    if (!error_displayed)
    {
        return;
    }

    attron(COLOR_PAIR(STYLE_ERROR));
    mvprintw(max_y - 1, 0, "%s", error_text);
    attroff(COLOR_PAIR(STYLE_ERROR));
}

void place_cursor()
{
    int render_cursor_x;
    int render_cursor_y;

    if (command_entering)
    {
        render_cursor_y = max_y - 1;
        render_cursor_x = command_len >= max_x ? max_x : command_len;
    }
    else
    {
        render_cursor_x = panes[PANE_HEX].left + byte_in_column(cursor_byte) +
                          cursor_nibble;
        render_cursor_y = panes[PANE_HEX].top + byte_in_line(cursor_byte) -
                          scroll_start;
    }

    move(render_cursor_y, render_cursor_x);
}

void flush_output()
{
    wnoutrefresh(panes[PANE_HEX].window);
    wnoutrefresh(panes[PANE_ASCII].window);
    wnoutrefresh(panes[PANE_DETAIL].window);

    doupdate();
}

void update(int event)
{
    error_displayed = false;

    handle_sizing();
    handle_event(event);
    clamp_scrolling();
    render_hex();
    render_ascii();
    render_details();
    render_command();
    render_error();
    place_cursor();
    flush_output();
}

void open_file(char* filename)
{
    // Get filesize
    struct stat st;
    stat(filename, &st);
    source_len = st.st_size;
    int count = st.st_size * sizeof(unsigned char);

    // Allocate or reallocate memory
    if (source == NULL)
    {
        source = malloc(count);
    }
    else
    {
        source = realloc(source, count);
    }

    // Read file into memory
    FILE* file = fopen(filename, "r");

    if (!file)
    {
        printf("Error opening file. File not found / permissions problem?\n");
        exit(2);
    }

    unsigned char* target = source;
    size_t bytes_read;

    while ((bytes_read = fread(target, 1, BUFFER_SIZE, file)) > 0)
    {
        target += bytes_read;
    }

    fclose(file);

    original_filename = filename;
}

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        printf("Usage: hexitor <filename>\n");
        return 1;
    }

    open_file(argv[1]);

    initscr();
    start_color();
    cbreak();
    keypad(stdscr, TRUE);

    mouseinterval(0);
    mousemask(ALL_MOUSE_EVENTS, NULL);

    init_pair(STYLE_ERROR, COLOR_BLACK, COLOR_RED);
    init_pair(STYLE_CURSOR, COLOR_BLACK, COLOR_WHITE);

    refresh();

    update(-1);

    int event;

    while ((event = getch()) != KEY_F(1))
    {
        update(event);
    }
}
