#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios original_termios;

void die(const char *e)
{
    perror(e);
    exit(1);
}

void cleanUp(void)
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios) == -1)
        die("tcsetattr");
}

void enableRawMode(void)
{
    if (tcgetattr(STDIN_FILENO, &original_termios) == -1)
        die("tcgetattr");

    atexit(cleanUp);
    struct termios raw = original_termios;

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

int main(void)
{
    // make read return
    enableRawMode();
    // read 1 byte from stdin and write to c
    while(1)
    {
        char c = '\0';
        // cygwin sometimes raise eagain when read fails
        if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN)
            die("read");
        // is control character (non-printable)
        if(iscntrl(c))
        {
            printf("%d\r\n", c);
        }
        else
        {
            // print byte and character
            printf("%d (%c)\r\n", c, c);
        }

        if (c == 'q')
            break;
    }
    return 0;
}