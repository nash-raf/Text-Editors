// includes //

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <string.h>

// define //
#define CTRL_KEY(k) ((k) & 0x1f)
#define KILO_VERSION "0.0.1"

// enum editorKey{
//     arrow_left = 'a',
//     arrow_right ='d',
//     arrow_up = 'w',
//     arrow_down = 's'
// };
enum editorKey
{
    arrow_left = 1000,
    arrow_right,
    arrow_up,
    arrow_down,
    del_key,
    home_key,
    end_key,
    page_up,
    page_down
};

// data //
typedef struct erow
{
    int size;
    char *chars;
} erow;

struct editorConfig
{
    int cx, cy;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    struct termios orig_termios;
};
struct editorConfig E;

void die(const char *s)
{
    write(STDOUT_FILENO, "\x1b[2j", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}
void editorAppendRow(char *s, size_t len){
    E.row = realloc(E.row, sizeof(erow) * E.numrows+1);

    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len+1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] ='\0';
    E.numrows++;
}

// file i/o

void editorOpen(char *filename){
    FILE *fp = fopen(filename, "r");
    if(!fp) {
        die("fopen");
    }

    char *line  = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while((linelen = getline(&line, &linecap, fp)) !=-1)
    {
        while(linelen>0 && (line[linelen-1]=='\n' || line[linelen - 1]== '\r')){
            linelen--;
        }
        editorAppendRow(line, linelen);
    }
    free(line);
    fclose(fp);
}

// append buffer
struct abuf
{
    char *b;
    int len;
};
#define ABUF_INIT \
    {             \
        NULL, 0   \
    }

void abAppend(struct abuf *ab, const char *s, int len)
{
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL)
    {
        return;
    }
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len = ab->len + len;
}
void abfree(struct abuf *ab)
{
    free(ab->b);
}

// terminal / /

void disableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    {
        die("tcsetattr");
    }
}

void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    {
        die("tcgetattr");
    }
    atexit(disableRawMode);
    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_cflag |= (CS8);
    raw.c_oflag &= ~(OPOST);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    {
        die("tcsetattr");
    }
}

int editorReadKey()
{
    int nread;
    char c;

    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN)
        {
            die("read");
        }
    }
    if (c == '\x1b')
    {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1)
        {
            return '\x1b';
        }
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
        {
            return '\x1b';
        }

        if (seq[0] == '[')
        {
            if (seq[1] >= '0' && seq[1] <= '9')
            {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                {
                    return '\x1b';
                }
                if (seq[2] == '~')
                {
                    switch (seq[1])
                    {
                    case '1':
                        return home_key;
                    case '3':
                        return del_key;
                    case '5':
                        return page_up;
                    case '6':
                        return page_down;
                    case '7':
                        return home_key;
                    case '4':
                        return end_key;
                    case '8':
                        return end_key;
                    }
                }
            }
            else
            {
                switch (seq[1])
                {
                case 'A':
                    return arrow_up;
                case 'B':
                    return arrow_down;
                case 'C':
                    return arrow_right;
                case 'D':
                    return arrow_left;
                case 'H':
                    return home_key;
                case 'F':
                    return end_key;
                }
            }
        }
        else if (seq[0] == 'O')
        {
            switch (seq[1])
            {
            case 'H':
                return home_key;
            case 'F':
                return end_key;
            }
        }

        return '\x1b';
    }
    else
    {
        return c;
    }
}

int getCursorPosition(int *rows, int *cols)
{
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    {
        return -1;
    }
    while (i < sizeof(buf) - 1)
    {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
        {
            break;
        }
        if (buf[i] == 'R')
        {
            break;
        }
        i++;
    }
    buf[i] = '\0';
    if (buf[0] != '\x1b' || buf[1] != '[')
    {
        return -1;
    }
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    {
        return -1;
    }

    editorReadKey();
    return -1;
}

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[998B", 12) != 12)
        {
            return -1;
        }

        return getCursorPosition(rows, cols);
    }
    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

// row operations 

// void editorAppendRow(char *s, size_t len){
//     E.row = realloc(E.row, sizeof(E.row) * E.numrows+1);

//     int at = E.numrows;
//     E.row[at].size = len;
//     E.row[at].chars = malloc(len+1);
//     memcpy(E.row[at].chars, s, len);
//     E.row.chars[len] ='\0';
//     E.numrows++;
// }

// output
void editorScroll(){
    if(E.cy <E.rowoff){
        E.rowoff = E.cy;
    }
    if(E.cy >= E.rowoff + E.screenrows){
        E.rowoff = E.cy - E.screenrows+1;
    }
    if(E.cx < E.coloff){
        E.coloff = E.cx;
    }
    if(E.cx >= E.coloff + E.screencols){
        E.coloff = E.cx - E.screencols + 1;
    }
}
void editorDrawRows(struct abuf *ab)
{
    for (int i = 0; i < E.screenrows; i++)
    {
        int filerow = i + E.rowoff;
        if (filerow >= E.numrows)
        {
            if (E.numrows == 0 && i == E.screenrows/3)
            {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome), "kilo editor --version %s", KILO_VERSION);
                if (welcomelen > E.screencols)
                {
                    welcomelen = E.screencols;
                }
                int padding = (E.screencols - welcomelen) / 2;
                if (padding)
                {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--)
                {
                    abAppend(ab, " ", 1);
                }
                abAppend(ab, welcome, welcomelen);
            }
            else
            {
                abAppend(ab, "~", 1);
            }
        }
        else
        {
            int len = E.row[filerow].size;
            if (len > E.screencols)
                len = E.screencols;
            abAppend(ab, E.row[filerow].chars, len);
        }

        abAppend(ab, "\x1b[K", 3);

        if (i < E.screenrows - 1)
        {
            abAppend(ab, "\r\n", 2);
        }
    }
}
void editorRefreshScreen()
{   
    editorScroll();
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
    // abAppend(&ab, "\x1b[2J", 4);
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) +1, (E.cx - E.coloff) +1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);
    write(STDOUT_FILENO, ab.b, ab.len);
    abfree(&ab);
}

// input
void editorMoveCursor(int key)
{
    switch (key)
    {
    case arrow_left:
        if (E.cx != 0)
        {
            E.cx--;
        }
        break;
    case arrow_down:
        if (E.cy < E.numrows)
        {
            E.cy++;
        }
        break;
    case arrow_up:
        if (E.cy != 0)
        {
            E.cy--;
        }
        break;
    case arrow_right:
            E.cx++
        break;
    }
}
void editorProcessKeypress()
{
    int c = editorReadKey();

    switch (c)
    {
    case CTRL_KEY('q'):
        write(STDOUT_FILENO, "\x1b[2j", 4);
        write(STDOUT_FILENO, "\x1b[15;15H", 3);

        exit(0);
        break;

    case page_up:
    case page_down:
    {
        int times = E.screenrows;
        while (times--)
        {
            editorMoveCursor(c == page_up ? arrow_up : arrow_down);
        }
    }
    break;
    case home_key:
        E.cx = 0;
        break;
    case end_key:
        E.cx = E.screencols - 1;
        break;
    case arrow_up:
    case arrow_down:
    case arrow_left:
    case arrow_right:
        editorMoveCursor(c);
        break;
    }
}

// initialize //

void initEditor()
{
    E.cx = 0;
    E.cy = 0;
    E.rowoff =0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    {
        die("getWindowSize");
    }
}

int main(int argc, char *argv[])
{
    enableRawMode();
    initEditor();
    if(argc >=2){
        editorOpen(argv[1]);
    }
    while (1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
