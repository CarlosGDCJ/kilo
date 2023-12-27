#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios original_termios;

void cleanUp(void)
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
}

void enableRawMode(void)
{
    tcgetattr(STDIN_FILENO, &original_termios);
    atexit(cleanUp);
    struct termios raw = original_termios;

    raw.c_lflag &= ~(ECHO);

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

}

int main(void)
{
    enableRawMode();
    char c;
    // read 1 byte from stdin and write to c
    while(read(STDIN_FILENO, &c, 1) == 1 && c != 'q')
    {
        ;
    }
    return 0;
}