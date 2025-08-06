// feature test macros ( getline() )
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <termios.h>
#include <time.h>

#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8
// mirrors what ctrl_key does in terminal : sets the upper 3 bit to 0 (0001.1111 = 0x1f)
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey 
{
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,  // fn + backspace
    HOME_KEY, // fn + left
    END_KEY,  // fn + right
    PAGE_UP,
    PAGE_DOWN
};


/*************************************************************************/
/********* Data **********************************************************/ 
typedef struct erow {
    int size;
    int rendersize;
    char *chars;    // "/t"
    char *render;   // "    "
} erow;

struct editorConfig 
{
    int cx, cy;
    int rx;
    int rowOffset; // first visible row
    int colOffset;
    int screenRows;
    int screenCols;
    int numTextRows;
    erow *row;
    char *filename;
    char statusMsg[80];
    time_t statusMsg_time;
    struct termios orig_termios;
};

struct editorConfig E;


/*************************************************************************/
/********* Terminal ******************************************************/
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

    while (i < sizeof(buf) - 1)
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



/*************************************************************************/
/********* Row operation *************************************************/
int editorRowCxToRx(erow *row, int cx)
{
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++)
    {
	if (row->chars[j] == '\t')
	    rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);

	rx++;
    }

    return rx;
}

void editorUpdateRow(erow *row) // from chars to render (proper content VS render mode)
{
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j++)
    {
	if (row->chars[j] == '\t') { tabs++; }
    }

    free(row->render);
    row->render = malloc(row->size + tabs*(KILO_TAB_STOP -1) + 1);

    int idx = 0;
    for (j = 0; j < row->size; j++)
    {
	if (row->chars[j] == '\t')
	{
	    row->render[idx++] = ' ';
	    while (idx % KILO_TAB_STOP != 0)
		row->render[idx++] = ' ';
	}
	else 
	{
	    // copy row->chars in row->render
	    row->render[idx++] = row->chars[j];
	}
    }

    row->render[idx] = '\0';
    row->rendersize = idx;
}

void editorAppendRow(char *s, size_t len)
{
    E.row = realloc(E.row, sizeof(erow) * (E.numTextRows + 1)); //add 1 line space

    int at = E.numTextRows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rendersize = 0;
    E.row[at].render = NULL;
    editorUpdateRow(&E.row[at]);

    E.numTextRows++;
}


/*************************************************************************/
/********* File I/O ******************************************************/
void editorOpen(char *filename)
{
    free(E.filename);
    E.filename = strdup(filename); 
    //makes a copy of the string, allocating the memory and assuming you will free() that memory.

    FILE *fp = fopen(filename, "r");
    if (!fp) { die("fopen"); }

    char *line = NULL;
    size_t linecap = 0; // buffer size (pointed to &line)
    ssize_t linelen;    // n° of chars readed

    // getline() return -1 when there's no more lines to read
    while ((linelen = getline(&line, &linecap, fp)) != -1) // getline() allocs memory needed
    {
	while (linelen > 0 && ( line[linelen -1] == '\n' || line[linelen -1] == '\r' ))
	    linelen--; // remove "\n", "\r"

	editorAppendRow(line, linelen);
    }
    free(line);
    fclose(fp);
}


/*************************************************************************/
/********* Append Buffer *************************************************/
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


/*************************************************************************/
/********* Input *********************************************************/   
void editorMoveCursor(int key)
{
    erow *row = (E.cy >= E.numTextRows) ? NULL : &E.row[E.cy]; // current row

    switch (key)
    {
	case ARROW_LEFT:
	{
	    if (E.cx != 0) 
	    { 
		E.cx--; 
	    }
	    else if (E.cy > 0) 
	    {
		// from begin line to end previous line 
		E.cy--;
		E.cx = E.row[E.cy].size;
	    }
	    break;
	}
	case ARROW_RIGHT:
	{
	    if (row && E.cx < row->size) 
	    { 
		E.cx++; 
	    }
	    else if (row && E.cx == row->size)
	    {
		// from end line to begin of next line
		E.cy++;
		E.cx = 0;

	    }
	    break;
	}
        case ARROW_UP:
	{
	    if (E.cy != 0) 
	    { 
		E.cy--; 
	    }
	    break;
	}
	case ARROW_DOWN:
	{
	    if (E.cy < E.numTextRows) 
	    { 
		E.cy++; 
	    }
	    break;
	}
    }

    // stop cursor on end of line (for every line) 
    row = (E.cy >= E.numTextRows) ? NULL : &E.row[E.cy];
    int rowLen  = row ? row->size : 0;
    if (E.cx > rowLen) { E.cx = rowLen; }
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
	}
	break;
	case HOME_KEY:
	{
	    E.cx = 0;
	}
	break;
	case END_KEY:
	{
	    if (E.cy < E.numTextRows)
		E.cx = E.row[E.cy].size;
	}
        break;
	case PAGE_UP:
	case PAGE_DOWN:
	{
	    if (c == PAGE_UP)
	    {
		E.cy = E.rowOffset;
	    }
	    else if (c == PAGE_DOWN)
            {
		E.cy = E.rowOffset + E.screenRows -1;
		if (E.cy > E.numTextRows) { E.cy = E.numTextRows; }
	    }

	    int times = E.screenRows;
	    while (times--)
		editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
	}
	break;
	case ARROW_UP:
	case ARROW_DOWN:
	case ARROW_LEFT:
	case ARROW_RIGHT:
	{
	    editorMoveCursor(c);
	}
	break;
    }
}


/*************************************************************************/
/******** Output *********************************************************/
void editorScroll()
{
    E.rx = 0;
    if (E.cy < E.numTextRows)
	E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);

    if (E.cy < E.rowOffset)
	E.rowOffset = E.cy;

    if (E.cy >= E.rowOffset + E.screenRows)
	E.rowOffset = E.cy - E.screenRows +1;

    if (E.rx < E.colOffset)
	E.colOffset = E.rx;

    if (E.rx >= E.colOffset + E.screenCols)
	E.colOffset = E.rx - E.screenCols +1;
}

void editorDrawRows(struct abuf *ab)
{
    int y;
    for (y = 0; y < E.screenRows; y++)
    {
	int filerow = y + E.rowOffset;
	if (filerow >= E.numTextRows)
	{
	    if (E.numTextRows == 0 && y == E.screenRows / 3)
	    {
		char welcome[80];
		int welcomeLen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);
		if (welcomeLen > E.screenCols) { welcomeLen = E.screenCols; }
	    
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
	}
	else 
	{
	    int len = E.row[filerow].rendersize - E.colOffset;
	    if (len < 0) { len = 0; }
	    if (len > E.screenCols) { len = E.screenCols; }
	    abAppend(ab, &E.row[filerow].render[E.colOffset], len);
	}
	
	// K = erase part of line, params same as J, (0 erase part right to the cursor ) 
	// refresh each line instead of all "[2J"
	abAppend(ab, "\x1b[K", 3);

	//if (y < E.screenRows -1)
	    abAppend(ab, "\r\n", 2);
    }
}

void editoeDrawStatusBar(struct abuf *ab)
{
    // "<esc>[1;4;5m" = Select Graphic Rendition, text printed after with various attrs
    // ( 1 = bold, 4 = underscore, 5 = blink, 7 = inverted colors) "<esc>[m" = go to default
    abAppend(ab, "\x1b[7m", 4);
    
    char status[80], rstatus[80]; // left, right
    int len = snprintf( status, 
			sizeof(status), 
			" %.20s - %d lines", 
			E.filename ? E.filename : " [No Name]", 
			E.numTextRows);
    int rlen = snprintf(rstatus, 
			sizeof(rstatus), 
			"%d/%d ", 
			E.cy + 1, 
			E.numTextRows);

    if (len > E.screenCols) { len = E.screenCols; }
    abAppend(ab, status, len);

    while (len < E.screenCols)
    {
	if (E.screenCols - len == rlen)
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

    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab)
{
    abAppend(ab, "\x1b[K", 3); // clear the message bar
			       
    int msglen = strlen(E.statusMsg);
    if (msglen > E.screenCols) { msglen = E.screenCols; }
    if (msglen && time(NULL) - E.statusMsg_time < 5) // if less than 5 seconds old (and key pressed)
    {
	abAppend(ab, " ", 1);
	abAppend(ab, E.statusMsg, msglen);
    }
}

void editorSetStatusMessage(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusMsg, sizeof(E.statusMsg), fmt, ap); // va_arg()
    va_end(ap);

    E.statusMsg_time = time(NULL); // get current time (unix time)
}

void editorRefreshScreen()
{
    editorScroll();

    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6); // h, l = turn on/turn of features(?25 cursor)
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);
    editoeDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);
   
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowOffset)+1, (E.rx - E.colOffset)+1);
    abAppend(&ab, buf, strlen(buf));
    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFreee(&ab);
}


/*************************************************************************/
/********* Init **********************************************************/
void initEditor() 
{
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowOffset = 0;
    E.colOffset = 0;
    E.numTextRows = 0;
    E.row = NULL;
    E.filename = NULL;
    E.statusMsg[0] = '\0';
    E.statusMsg_time = 0;

    if (getWindowSize(&E.screenRows, &E.screenCols) == -1) { die("getWindowSize"); }

    E.screenRows -= 2; // editorDrawRow() not at last 2 line
}


int main(int argc, char *argv[])
{
    enableRawMode();
    initEditor();
    if (argc >= 2)
	editorOpen(argv[1]);

    editorSetStatusMessage("HELP: Ctrl-Q = quit");

    while(1) 
    {
	editorRefreshScreen();
	editorProcessKeypress();
    }

    return 0;
}
