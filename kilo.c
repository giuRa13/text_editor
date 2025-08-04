#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>

#define KILO_VERSION "0.0.1"
// mirrors what ctrl_key does in terminal : sets the upper 3 bit to 0 (0001.1111 = 0x1f)
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey 
{
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY, // laptop fn + backspace
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/************************/
/********* Data *********/
struct editorConfig 
{
    int cx, cy;
    int screenRows;
    int screenCols;
    struct termios orig_termios;
};

struct editorConfig E;


/****************************/
/********* Terminal *********/
void die(const char *s) 
{
    // Escape Sequence always starts with 27[ ( "\x1b" is the escape character (27)(<esc>) )
    // J : Clear dispay, params 0, 1, 2 (2 = all)
    // H : Cursor Position "<esc>[12;30H"  <esc>[(n°row);(n°col)H
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    // most C lib functions that fail set the global "errno" to indicate what the error was 
    perror(s); // look at the global "errno" var and prints an error message 
    exit(1);
}

void disableRawMode()
{
    if ( tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1 ) { die("tcsetattr"); }
}

void enableRawMode() 
{
    struct termios raw;
    atexit(disableRawMode);

    if ( tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) { die("tcgetattr"); }
    raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT |ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); 
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1; 
    if ( tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) { die("tcsetattr"); }
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
 
    // CONTROL CHARACTERS :
    // VMIN : sets the minimum number of bytes of input needed before read() can return ( can return also if 0 byte readed )
    // VTIME :  sets the maximum amount of time to wait before read() returns ( set to 1/10 of a second, or 100 milliseconds )
}

int editorReadKey()
{
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1) 
    {
	// in Cygwin, when read() times out it returns -1 with an errno of EAGAIN, instead of just returning 0 
	if (nread == -1 && errno != EAGAIN) { die("read"); }
    }

    // Arrows Escape Sequence as a single press
    if (c == '\x1b')
    {
	char seq[3];

	if (read(STDIN_FILENO, &seq[0], 1) != 1) { return '\x1b'; }
	if (read(STDIN_FILENO, &seq[1], 1) != 1) { return '\x1b'; }

	if (seq[0] == '[')
	{
	    if (seq[1] >= '0' && seq[1] <= '9')
	    {
		if (read(STDIN_FILENO, &seq[2], 1) != 1) { return '\x1b'; }
		if (seq[2] == '~')
		{
		    switch (seq[1])
		    {
			case '1': return HOME_KEY;   // \x1b[1~  <esc>[7~  <esc>[H  <esc>OH
			case '3': return DEL_KEY;    // \x1b[3~
			case '4': return END_KEY;    // \x1b[4~  <esc>[8~  <esc>[F  <esc>OF
			case '5': return PAGE_UP;    // \x1b[5~
			case '6': return PAGE_DOWN;  // \x1b[6~
			case '7': return HOME_KEY;      
			case '8': return END_KEY;    
		    }
		}
	    }
	    else 
	    {
		switch (seq[1]) 
		{
		    case 'A' : return ARROW_UP;     // <esc>[A
		    case 'B' : return ARROW_DOWN;   // <esc>[B
		    case 'C' : return ARROW_RIGHT;
		    case 'D' : return ARROW_LEFT;
		    case 'H' : return HOME_KEY;
		    case 'F' : return END_KEY;
		}
	    }
	}
	else if (seq[0] == 'O')
	{
	    switch (seq[1])
	    {
		case 'H': return HOME_KEY;
		case 'F': return END_KEY;
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

    // n = Device Status Report, 6 = cursor Position
    // return another Escape Sequence(so need to parse it)with the parameters as response ( 53rows, 60cols = "[53;60R" )
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) { return -1; }

    while(i < sizeof(buf) - 1)
    {
	if(read(STDIN_FILENO, &buf[i], 1) != 1) { break; }
	if (buf[i] == 'R') { break; }
	i++;
    }
    buf[i] = '\0'; 
    //printf() expects strings to end with a 0 byte, so we make sure to assign '\0' to the final byte of buf

    if (buf[0] != '\x1b' || buf[1] != '[') { return -1; }
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) { return -1; } // buf[2] skip '\x1b' and '['

    return 0;
}

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;

    // T.erminal I.nput O.utput C.ontrol G.et WIN.ndow S.ize
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
	//ioctl() isn’t guaranteed to be able to request the window size on all systems
	if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)  { return -1; }
	// C = Cursor Forward, B = Cursor Down (move all down all right) 
	// C and B specifically documented to stop the cursor from going past the edge of the screen
	return getCursorPosition(rows, cols);
    }
    else 
    {
	*cols = ws.ws_col;
	*rows = ws.ws_row;
	return 0;
    }
}

/*********************************/
/********* Append Buffer *********/
struct abuf
{
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len)
{
    char *newb = realloc(ab->b, ab->len + len);

    if (newb == NULL)  { return; } 
    memcpy(&newb[ab->len], s, len);
    ab->b = newb;
    ab->len += len;
}

void abFreee(struct abuf *ab)
{
    free(ab->b);
}


/*************************/
/********* Input *********/    
void editorMoveCursor(int key)
{
    switch (key)
    {
	case ARROW_LEFT:
	{
	    if (E.cx != 0) { E.cx--; }
	    break;
	}
	case ARROW_RIGHT:
	{
	    if (E.cx != E.screenCols -1) { E.cx++; }
	    break;
	}
        case ARROW_UP:
	{
	    if (E.cy != 0) { E.cy--; }
	    break;
	}
	case ARROW_DOWN:
	{
	    if (E.cy != E.screenRows -1) { E.cy++; }
	    break;
	}
    }
}

void editorProcessKeypress()
{
    int c = editorReadKey();

    switch (c) 
    {
	case CTRL_KEY('q'):
	{
	    write(STDOUT_FILENO, "\x1b[2J", 4);
	    write(STDOUT_FILENO, "\x1b[H", 3);
	    exit(0);
	    break;
	}
	case HOME_KEY:
	{
	    E.cx = 0;
	    break;
	}
	case END_KEY:
	{  
	    E.cx = E.screenCols - 1;
	    break;
	}
	case PAGE_UP:
	case PAGE_DOWN:
	{
	    int times = E.screenRows;
	    while (times--)
		editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
	}
	break;
	case ARROW_UP:
	case ARROW_DOWN:
	case ARROW_LEFT:
	case ARROW_RIGHT:
	    editorMoveCursor(c);
	    break;
    }
}


/*************************/
/******** Output *********/
void editorDrawRows(struct abuf *ab)
{
    int y;
    for (y = 0; y < E.screenRows; y++)
    {
	if(y == E.screenRows / 3)
	{
	    char welcome[80];
	    int welcomeLen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);
	    if(welcomeLen > E.screenCols) { welcomeLen = E.screenCols; }
	    
	    int padding = (E.screenCols - welcomeLen) / 2;
	    if (padding)
	    {
		abAppend(ab, "~", 1);
		padding--;
	    }
	    while (padding--) { abAppend(ab, " ", 1); }
	    abAppend(ab, welcome, welcomeLen);
	}
	else 
	{ 
	    abAppend(ab, "~", 1);
	}
	
	// K = erase part of line, params same as J, (0 erase part right to the cursor ) 
	// refresh each line instead of all "[2J"
	abAppend(ab, "\x1b[K", 3);

	if (y < E.screenRows -1)
	    abAppend(ab, "\r\n", 2);
    }
}

void editorRefreshScreen()
{
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6); // h, l = turn on/turn of features(?25 cursor)
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);
   
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));
    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFreee(&ab);
}


/***********************/
/********* Init ********/
void initEditor() 
{
    E.cx = 0;
    E.cy = 0;

    if (getWindowSize(&E.screenRows, &E.screenCols) == -1) { die("getWindowSize"); }
}


int main()
{
    enableRawMode();
    initEditor();
    
    while(1) 
    {
	editorRefreshScreen();
	editorProcessKeypress();
    }

    return 0;
}
