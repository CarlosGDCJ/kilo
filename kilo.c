/** Includes **/
// these feature test macros must come before the inputs
// because they are used by the header files
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <fcntl.h>
#include <stdarg.h>
#include <time.h>
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
#define ABUF_INIT {NULL, 0}
#define KILO_TABSTOP 8
#define KILO_QUIT_TIMES 3

/** Data **/
typedef struct erow {
    char *chars;
    int size;
    char *render;
    int rsize;
} erow;

struct editorConfig {
    struct termios original_termios;
    int screenrows;
    int screencols;
    int cx, cy;
    int rx;
    int numrows;
    int rowoff;
    int coloff;
    int dirty;
    erow *row;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
};

struct editorConfig E;

struct abuf {
    char *b;
    int len;
};

enum editorKey {
    BACKSPACE = 127,
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

/** prototypes **/
void editorSetStatusMessage(const char *fmt, ...);


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
int editorRowCxToRx(erow *row, int cx)
{
    int i, rx = 0;
    for (i = 0; i < cx; i++)
    {
        if (row->chars[i] == '\t')
            rx += (KILO_TABSTOP - 1) - (rx % KILO_TABSTOP);
        rx++;
    }

    return rx;
}
void editorUpdateRow(erow *row)
{
    // this function transforms the chars into what they look like
    int tabs = 0;
    // this pass is necessary to know the amount of memory to allocate
    int j;
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == '\t')
            tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs * (KILO_TABSTOP - 1) + 1);

    int idx = 0;
    for (j = 0; j < row->size; j++)
    {
        if (row->chars[j] == '\t')
        {
            row->render[idx++] = ' ';

            while (idx % KILO_TABSTOP != 0)
                row->render[idx++] = ' ';
        }
        else
        {
            row->render[idx++] = row->chars[j];
        }
    }

    row->render[idx] = '\0';
    row->rsize = idx;


}

void editorAppendRow(char *s, ssize_t len)
{
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);

    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].render = NULL;
    E.row[at].rsize = 0;
    // copy stuff to render and size
    editorUpdateRow(&E.row[at]);

    // only increase after everything is done (?)
    E.numrows++;
    E.dirty++;

}

void editorRowInsertChar(erow *row, int at, int c)
{
    // our char is an int (?)
    if (at < 0 || at > row->size)
        at = row->size;
    row->chars = realloc(row->chars, row->size + 2); // 1 for the new char, 1 for '\0' (row->size doesn't count '\0')
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->chars[at] = c;
    row->size++;
    editorUpdateRow(row);

    E.dirty++;

}

/** Editor operations */
void editorInsertChar(int c)
{
    if (E.cy == E.numrows)
        editorAppendRow("", 0);
    
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;

}
/** File i/o **/
char *editorRowsToString(int *buflen)
{
    // creates a string with all rows
    int j;
    int totlen = 0;

    // calculate the amount of memory
    for (j = 0; j < E.numrows; j++)
        totlen += E.row[j].size + 1; // 1 for the \n at every line

    *buflen = totlen;
    
    char *buf = malloc(totlen);
    char *p = buf; //we will advance p, but buf will remain at the start

    for (j = 0; j < E.numrows; j++)
    {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;

    }

    return buf;
}
void editorOpen(char *filename)
{
    free(E.filename);
    E.filename = strdup(filename);

    FILE *fp = fopen(filename, "r");

    if (!fp)
        die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    E.filename = filename;

    while((linelen = getline(&line, &linecap, fp)) != -1)
    {
        while(linelen > 0 && (line[linelen - 1] == '\r' || line[linelen - 1] == '\n'))
        {
            linelen--;
        }

        editorAppendRow(line, linelen);

    }

    E.dirty = 0;
    free(line);
    fclose(fp);

}

void editorSave()
{
    if (E.filename == NULL)
        return;

    int len;
    char *buf = editorRowsToString(&len); // free this later

    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1)
    {
        if (ftruncate64(fd, len) != -1)
        {
            if (write(fd, buf, len) == len)
            {
                close(fd);
                free(buf);
                editorSetStatusMessage("%d bytes written to disk", len);
                E.dirty = 0;
                return;
            }

        }

        close(fd);
    }
    free(buf);
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
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
void editorSetStatusMessage(const char *fmt, ...)
{
    // the va functions come from stdarg and are used to
    // handle variadic functions
    va_list ap;
    va_start(ap, fmt); // we pass the argument before the ... as a point of reference
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL); // passing NULL gives current time

}

void editorScroll()
{
    E.rx = 0;
    if (E.cy < E.numrows)
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    if (E.cy < E.rowoff)
        E.rowoff = E.cy;
    else if (E.cy >= E.rowoff + E.screenrows)
        E.rowoff = E.cy - E.screenrows + 1; // go back 1 screen from E.cy, so that it is now in the middle
    
    if (E.rx < E.coloff)
        E.coloff = E.rx;
    else if (E.rx >= E.coloff + E.screencols)
        E.coloff = E.rx - E.screencols + 1;

}


void editorDrawStatusBar(struct abuf *ab)
{
    abAppend(ab, "\x1b[7m", 4);

    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", E.filename ? E.filename : "[No name]", E.numrows, (E.dirty > 0) ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);

    if (len > E.screencols)
        len = E.screencols;

    abAppend(ab, status, len);

    while(len < E.screencols)
    {
        if (E.screencols - len == rlen)
        {
            abAppend(ab, rstatus, rlen);
            break;
        }
        else
        {
            abAppend(ab, " ", 1);
            len++;
        }
    }

    // abAppend(ab, E.filename)

    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);

}

void editorDrawMessageBar(struct abuf *ab)
{
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screencols)
        msglen = E.screencols;
    if (msglen && time(NULL) - E.statusmsg_time < 5)
        abAppend(ab, E.statusmsg, msglen);

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
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0)
                len = 0;
            if (len > E.screencols)
                len = E.screencols;
            abAppend(ab, &E.row[filerow].render[E.coloff], len);
        }


        abAppend(ab, "\x1b[K", 3); // clear rest of line
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
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy - E.rowoff + 1, E.rx - E.coloff + 1); // position cursor
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); // show cursor
    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/** Input **/
void editorMoveCursor(int c)
{
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    switch (c)
    {
        case ARROW_UP:
            if (E.cy > 0)
                E.cy--;
            break;
        case ARROW_LEFT:
            if (E.cx > 0)
                E.cx--;
            else if (E.cy > 0){
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows - 1)
                E.cy++;
            break;
        case ARROW_RIGHT:
            if (row && E.cx < row->size)
                E.cx++;
            else if (row && E.cx == row->size)
            {
                E.cy++;
                E.cx = 0;
            }
            break;
    }

    // we have to theck for the row again after we increase/decreased x/y
    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;

    if (E.cx > rowlen)
        E.cx = rowlen;
}
void editorProcessKeypress()
{
    static int quit_times = KILO_QUIT_TIMES; // static files get initialized only once
    int key = editorReadKey();

    switch (key)
    {
        case '\r':
            // TODO
            break;

        case CTRL_KEY('q'):
            if (E.dirty && quit_times > 0)
            {
                editorSetStatusMessage("WARNING!!! File has unsaved changes. "
                    "Press Ctrl-Q %d more times to quit.", quit_times);
                quit_times--;
                return;
                // this return makes so it doesn't read the line further below
                // that increases assigns quit_times to KILO_QUIT_TIMES

            }
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[1;1H", 6);
            exit(0);
            break;
        
        case CTRL_KEY('s'):
            editorSave();
            break;
        
        case BACKSPACE:
        case CTRL_KEY('h'):
        case DELETE_KEY:
            // TODO
            break;
        
        case CTRL_KEY('l'):
        case '\x1b':
            break;

        case PAGE_UP:
        case PAGE_DOWN:
        {
            if (key == PAGE_UP)
                E.cy = E.rowoff;
            else if (key == PAGE_DOWN)
            {
                E.cy = E.rowoff + E.screenrows - 1;
                if (E.cy > E.numrows)
                    E.cy = E.numrows;

            }
            // need braces to declare the integer
            int times = E.screenrows;
            while (times--)
                editorMoveCursor(key == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            break;
        }

        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            if (E.cy < E.numrows)
                E.cx = E.row[E.cy].size;
            break;

        case ARROW_UP:
        case ARROW_LEFT:
        case ARROW_DOWN:
        case ARROW_RIGHT:
            editorMoveCursor(key);
            break;
        
        default:
            editorInsertChar(key);
            break;
    }

    quit_times = KILO_QUIT_TIMES;
}


/** Init **/
void initEditor()
{
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.numrows = 0;
    E.rowoff = 0;
    E.row = NULL;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    E.dirty = 0;
    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
        die("getWindowSize");
    
    E.screenrows -= 2; // One of the lines is reserved as the status bar

}
int main(int argc, char *argv[])
{
    enableRawMode();
    initEditor();
    if (argc > 1)
        editorOpen(argv[1]);

    editorSetStatusMessage("HELP: Ctrl-S = Save | Ctrl-Q = Quit");
    while(1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}