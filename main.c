#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>

#include <ncurses.h>

#define BUFFER_SIZE 16 * 1024
#define MAX_COMMAND_LEN 256

#define KEY_ESC 27
#define KEY_RETURN 10
#define KEY_DELETE 127

typedef struct
{
    WINDOW* window;
    int left;
    int top;
    int width;
    int height;
} pane;

pane hex_pane;
pane ascii_pane;
pane detail_pane;

unsigned char* source = NULL;
int source_len;

char* original_filename;

int cursor_byte;
int cursor_nibble;

int scroll_start;

int max_x;
int max_y;

char command[MAX_COMMAND_LEN];
int command_len;
bool command_entering = false;

FILE* log_file;

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

    int data_width = max_x - 20;

    hex_pane.width = data_width * 0.75;
    //hex_pane.height = max_y - 20;
    hex_pane.height = 5;
    setup_pane(&hex_pane);

    ascii_pane.left = hex_pane.left + hex_pane.width;
    ascii_pane.top = hex_pane.top;
    ascii_pane.width = data_width * 0.25;
    ascii_pane.height = 5;
    setup_pane(&ascii_pane);

    detail_pane.top = hex_pane.top + hex_pane.height + 3;
    detail_pane.width = max_x - 20;
    detail_pane.height = max_y - hex_pane.height - hex_pane.top - 5;
    setup_pane(&detail_pane);
}

void handle_start_command()
{
    command_entering = true;
    command[0] = ':';
    command_len = 1;
}

void unrender_command()
{
    move(max_y - 1, 0);

    for (int i = 0; i < max_x; i++)
    {
        addch(' ');
    }
}

void handle_cancel_command()
{
    command_entering = false;
    unrender_command();
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

    // Write file to disk
    FILE* file = fopen(filename, "w");

    if (!file)
    {
        // TODO: better things
        printf("Error opening file. File not found / permissions problem?\n");
        exit(3);
    }

    size_t bytes_written = 0;
    size_t bytes_left = source_len;

    while (bytes_left > 0)
    {
        size_t size = BUFFER_SIZE < bytes_left ? BUFFER_SIZE : bytes_left;
        size_t written = fwrite(source + bytes_written, 1, size, file);

        if (written != size)
        {
            // TODO: better things
            printf("Encountered error while writing file; may be corrupt.\n");
            exit(4);
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

void handle_submit_command()
{
    command[command_len] = 0;
    command_entering = false;
    unrender_command();

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

void handle_event(int event)
{
    if (command_entering)
    {
        handle_command_event(event);
        return;
    }

    switch (event)
    {
        case 'h':
        case 'H':
        case KEY_LEFT:  handle_key_left();       break;

        case 'l':
        case 'L':
        case KEY_RIGHT: handle_key_right();      break;

        case 'k':
        case 'K':
        case KEY_UP:    handle_key_up();         break;

        case 'j':
        case 'J':
        case KEY_DOWN:  handle_key_down();       break;

        case ':':       handle_start_command();  break;

        default:        handle_overwrite(event); break;
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

void render_ascii()
{
    wclear(ascii_pane.window);

    for (int i = first_visible_byte(); i <= last_visible_byte(); i++)
    {
        int out_y = byte_in_line(i) - scroll_start;
        int out_x = i % bytes_per_line();

        char output = '.';

        if (source[i] >= ' ' && source[i] <= '~')
        {
            output = source[i];
        }

        mvwprintw(ascii_pane.window, out_y, out_x, "%c", output);
    }
}

void render_details()
{
    WINDOW* w = detail_pane.window;
    wclear(w);

    unsigned char* cursor_start = source + cursor_byte;

    mvwprintw(w, 1, 1, "Offset: %d", cursor_byte);
    mvwprintw(w, 2, 1, "Int8:   %d", *(int8_t*)cursor_start);
    mvwprintw(w, 3, 1, "UInt8:  %d", *(uint8_t*)cursor_start);
    mvwprintw(w, 4, 1, "Int16:  %d", *(int16_t*)cursor_start);
    mvwprintw(w, 5, 1, "UInt16: %d", *(uint16_t*)cursor_start);
    mvwprintw(w, 6, 1, "Int32:  %d", *(int32_t*)cursor_start);
    mvwprintw(w, 7, 1, "UInt32: %d", *(uint32_t*)cursor_start);
    mvwprintw(w, 8, 1, "Int64:  %ld", *(int64_t*)cursor_start);
    mvwprintw(w, 9, 1, "UInt64: %ld", *(uint64_t*)cursor_start);

    box(w, 0, 0);
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
        render_cursor_x = hex_pane.left + byte_in_column(cursor_byte) +
                          cursor_nibble;
        render_cursor_y = hex_pane.top + byte_in_line(cursor_byte) -
                          scroll_start;
    }

    move(render_cursor_y, render_cursor_x);
}

void flush_output()
{
    wnoutrefresh(hex_pane.window);
    wnoutrefresh(ascii_pane.window);
    wnoutrefresh(detail_pane.window);

    doupdate();
}

void update(int event)
{
    handle_sizing();
    handle_event(event);
    clamp_scrolling();
    render_hex();
    render_ascii();
    render_details();
    render_command();
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

    log_file = fopen("logfile", "w");

    cursor_byte = 0;
    cursor_nibble = 0;
    scroll_start = 0;

    hex_pane.left = 10;
    hex_pane.top = 3;

    detail_pane.left = 10;

    initscr();
    cbreak();
    keypad(stdscr, TRUE);
    refresh();

    update(-1);

    int event;

    while ((event = getch()) != KEY_F(1))
    {
        update(event);
    }

    fclose(log_file);
}
