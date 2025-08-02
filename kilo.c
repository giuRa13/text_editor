#include <asm-generic/errno-base.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>

// mirrors what ctrl_key does in terminal : sets the upper 3 bit to 0 (0001.1111 = 0x1f)
#define CTRL_KEY(k) ((k) & 0x1f)


/********* Data *********/
struct termios orig_termios;


/********* Terminal *********/
void die(const char *s) 
{
    // most C lib functions that fail set the global "errno" to indicate what the error was 
    perror(s); // look at the global "errno" var and prints an error message 
    exit(1);
}

void disableRawMode()
{
    if ( tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1 ) die("tcsetattr");
}

void enableRawMode() 
{
    struct termios raw;
    atexit(disableRawMode);

    if ( tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
    raw = orig_termios;
    raw.c_iflag &= ~(BRKINT |ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); 
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1; 
    if ( tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}
// FLAGS (Input, Output, Control, Local) :
// ICANON : Canonical Mode: reading input byte-by-byte instead of line-by-line
// ISIG : stop of Signals (Ctrl-C SIGNINT)(terminate) (Ctrl-Z SIGTSTP)(suspend)
// IXON : stop Ctrl-S, Ctrl-Q (Ctrl-S stops data to be transmitted to terminal until Ctrl-Q) 
// IEXTEN : stop Ctrl-V (on some systems the terminal waits for you to type another character and then sends that character literally)
// ICRNL : fix Ctrl-M : default return 10 but shuld return 13. Terminal translate any carriage return ("\r", 13) input by user to newline ("\n", 10)
// OPOST : turn off the default "\n" to "\r\n" translation in the Output so need to put carriage return("\r") in the "printf()" 
// 		or it will not return to the left/begin of the new line 
// CS8 : sets the character size (CS) to 8 bits per byte (Not a Flag, a BitMask we Add)	(probably already set)	
// BRKINT, SIGINT, INPCK, ISTRIP : been conservative (probably already turned off) 
// 
// CONTROL CHARACTERS :
// VMIN : sets the minimum number of bytes of input needed before read() ( can return also if 0 byte readed )
// VTIME :  sets the maximum amount of time to wait before read() returns ( set to 1/10 of a second, or 100 milliseconds )

char editorReadKey()
{
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1) 
    {
	// in Cygwin, when read() times out it returns -1 with an errno of EAGAIN, instead of just returning 0 
	if (nread == -1 && errno != EAGAIN) die("read");
    }
    return c;
}


/********* Input *********/
void inputDebug()
{
    char c = '\0';
    if ( read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN ) die("read");

    if(iscntrl(c))
    {
	printf("%d\r\n", c);
    }
    else 
    {
	printf("%d ('%c')\r\n", c, c);
    }
    
    if (c == CTRL_KEY('q')) exit(0);
}

void editorProcessKeypress()
{
    char c = editorReadKey();

    switch (c) 
    {
	case CTRL_KEY('q'): 
	{
	    exit(0);
	    break;
	}
    }
}


/********* Init ********/
int main()
{
    printf("Hello Kilo\n");
    enableRawMode();
    
    while(1) 
    {
	editorProcessKeypress();
    }

    return 0;
}
