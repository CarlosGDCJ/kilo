#include <ctype.h>
#include <stdio.h>
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

    // disabling flags (and (&) with negation)
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag &= ~(CS8);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_cc[VMIN] = 0; // bytes before read can return
    raw.c_cc[VTIME] = 1; // time to wait for read

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

}

int main(void)
{
    // make read return
    enableRawMode();
    // read 1 byte from stdin and write to c
    while(1)
    {
        char c = '\0';
        read(STDIN_FILENO, &c, 1);
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