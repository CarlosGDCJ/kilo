/** Includes **/
// these feature test macros must come before the inputs
// because they are used by the header files
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <sys/types.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>

/** Defines * */
#define KILO_VERSION "0.0.1"
#define CTRL_KEY(a) ((a) & 0x1f)
#define ABUF_INIT {NULL, 0};

/** Data **/
typedef struct erow {
    char *chars;
    int size;
} erow;

struct editorConfig {
    struct termios original_termios;
    int screenrows;
    int screencols;
    int cx, cy;
    int numrows;
    int rowoff;
    erow *row;
};

struct editorConfig E;

struct abuf {
    char *b;
    int len;
};


enum editorKey {
    ARROW_UP = 1000,
    ARROW_DOWN,
    ARROW_RIGHT,
    ARROW_LEFT,
    DELETE_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN,
};



/** Terminal **/
void die(const char *e)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[1;1H", 6);
    perror(e);
    exit(1);
}

void disableRawMode(void)
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.original_termios) == -1)
        die("tcsetattr");
}

void enableRawMode(void)
{
    if (tcgetattr(STDIN_FILENO, &E.original_termios) == -1)
        die("tcgetattr");

    atexit(disableRawMode);
    struct termios raw = E.original_termios;

    // disabling flags (and (&) with negation)
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag &= ~(CS8);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_cc[VMIN] = 0; // bytes before read can return
    raw.c_cc[VTIME] = 1; // time to wait for read

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");

}

int getCursorPosition(int *rows, int *cols)
{
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;
    
    while (i < sizeof(buf) - 1)
    {
        if(read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if (buf[i] == 'R')
            break;
        i++;
    }

    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;

    return 0;

}

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;
        return getCursorPosition(rows, cols);
    }
    else
    {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
        return 0;
    }
}

// wait for a keypress and return it
int editorReadKey()
{
    int nread;
    char c;

    // althogh we return an int, we use a char to read
    // because read sets only 1 byte (8 bits), so our int would have
    // leftover garbage at the end and that would mess up the equality
    // checks. One alternative would be to use a int initialized to zero
    // but we opted to use a char and it gets extended with zeros
    // when it is returned
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if(nread == -1 && errno != EAGAIN)
            die("read");
    }

    if (c == '\x1b')
    {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';
        if (seq[0] == '[')
        {
            if (seq[1] > '0' && seq[1] <= '9')
            {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';
                
                if (seq[2] == '~')
                {
                    switch(seq[1])
                    {
                        case '1':
                            return HOME_KEY;
                        case '3':
                            return DELETE_KEY;
                        case '4':
                            return END_KEY;
                        case '5':
                            return PAGE_UP;
                        case '6':
                            return PAGE_DOWN;
                        case '7':
                            return HOME_KEY;
                        case '8':
                            return END_KEY;
                    }
                }

            }
            else
            {
                switch(seq[1])
                {
                    case 'A':
                        return ARROW_UP;
                    case 'B':
                        return ARROW_DOWN;
                    case 'C':
                        return ARROW_RIGHT;
                    case 'D':
                        return ARROW_LEFT;
                    case 'H':
                        return HOME_KEY;
                    case 'F':
                        return END_KEY;
                }
            }

        }
        else if (seq[0] == '0')
        {
            switch(seq[1])
            {
                case 'H':
                    return HOME_KEY;
                case 'F':
                    return END_KEY;
            }

        }
        return '\x1b';
    }
    else
    {
        return c;
    }

}

/** Row ops*/
void appendRow(char *s, ssize_t len)
{
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);

    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    // only increase after everything is done (?)
    E.numrows++;

}
/** File i/o **/
void editorOpen(char *filename)
{
    FILE *fp = fopen(filename, "r");

    if (!fp)
        die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;

    while((linelen = getline(&line, &linecap, fp)) != -1)
    {
        while(linelen > 0 && (line[linelen - 1] == '\r' || line[linelen - 1] == '\n'))
        {
            linelen--;
        }

        appendRow(line, linelen);

    }

    free(line);
    fclose(fp);

}

/** Append buffer */
void abAppend(struct abuf *ab, char *s, int len)
{
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL)
        return;
    
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab)
{
    free(ab->b);
}

/** Output **/
void editorScroll()
{
    if (E.cy < E.rowoff)
        E.rowoff = E.cy;
    else if (E.cy >= E.rowoff + E.screenrows)
        E.rowoff = E.cy - E.screenrows + 1; // go back 1 screen from E.cy, so that it is now in the middle

}

void editorDrawRows(struct abuf *ab)
{
    int y;
    for (y = 0; y < E.screenrows; y++)
    {
        int filerow = E.rowoff + y;
        // E.numrows = rows in current file
        // so we only print the default stuff (~)
        // after we printed the whole file
        if (filerow >= E.numrows)
        {
            if (E.numrows == 0 && filerow == E.screenrows / 3)
            {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);

                if (welcomelen > E.screencols)
                    welcomelen = E.screencols;

                int padding = (E.screencols - welcomelen) / 2;
                if (padding)
                {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--)
                    abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);

            }
            else
            {
                abAppend(ab, "~", 1);
            }
        }
        else
        {
            // we use this variable (instead of changing
            // E.row.size directly) to not lose the original
            // value of E.row.size
            int len = E.row[filerow].size;
            if (len > E.screencols)
                len = E.screencols;
            abAppend(ab, E.row[filerow].chars, len);
        }


        abAppend(ab, "\x1b[K", 3); // clear rest of line

        if (y < E.screenrows - 1)
            abAppend(ab, "\r\n", 2);
    }
}

void editorRefreshScreen()
{
    editorScroll();
    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[?25l", 6); // hide cursor
    abAppend(&ab, "\x1b[H", 3); // go to start

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy - E.rowoff + 1, E.cx + 1); // position cursor
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); // show cursor
    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/** Input **/
void editorMoveCursor(int c)
{
    switch (c)
    {
        case ARROW_UP:
            if (E.cy > 0)
                E.cy--;
            break;
        case ARROW_LEFT:
            if (E.cx > 0)
                E.cx--;
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows - 1)
                E.cy++;
            break;
        case ARROW_RIGHT:
            if (E.cx < E.screencols - 1)
                E.cx++;
            break;

    }
}
void editorProcessKeypress()
{
    int c = editorReadKey();

    switch (c)
    {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[1;1H", 6);
            exit(0);
            break;

        case PAGE_UP:
        case PAGE_DOWN:
        {
            // need braces to declare the integer
            int times = E.screenrows;
            while (times--)
                editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            break;
        }

        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            E.cx = E.screencols - 1;
            break;

        case ARROW_UP:
        case ARROW_LEFT:
        case ARROW_DOWN:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}


/** Init **/
void initEditor()
{
    E.cx = 0;
    E.cy = 0;
    E.numrows = 0;
    E.row = NULL;
    E.rowoff = 0;
    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
        die("getWindowSize");

}
int main(int argc, char *argv[])
{
    enableRawMode();
    initEditor();
    if (argc > 1)
        editorOpen(argv[1]);

    while(1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}