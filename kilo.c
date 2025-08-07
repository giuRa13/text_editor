// feature test macros ( getline() )
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
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
#define KILO_QUIT_TIMES 1
// mirrors what ctrl_key does in terminal : sets the upper 3 bit to 0 (0001.1111 = 0x1f)
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey 
{
    BACKSPACE = 127,
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
    int dirty;
    char *filename;
    char statusMsg[80];
    time_t statusMsg_time;
    struct termios orig_termios;
};

struct editorConfig E;


/*************************************************************************/
/********* Prototypes ****************************************************/
void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));


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

int editorRowRxToCx(erow *row, int rx)
{
    int cur_rx = 0;
    int cx;
    for (cx = 0; cx < row->size; cx++)
    {
	if (row->chars[cx] == '\t')
	    cur_rx += (KILO_TAB_STOP - 1) - (cur_rx % KILO_TAB_STOP);

	cur_rx++;
	if (cur_rx > rx) { return cx; }
    }
    return cx;
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

void editorInsertRow(int at, char *s, size_t len)
{
    if (at < 0 || at > E.numTextRows) { return; }

    E.row = realloc(E.row, sizeof(erow) * (E.numTextRows + 1)); //add 1 line space
    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numTextRows - at));

    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rendersize = 0;
    E.row[at].render = NULL;
    editorUpdateRow(&E.row[at]);

    E.numTextRows++;
    E.dirty++;
}

void editorFreeRow(erow *row)
{
    // allocated with "malloc()"
    free(row->render); 
    free(row->chars);
}

void editorDeleteRow(int at)
{
    if (at < 0 || at >= E.numTextRows) { return; }

    editorFreeRow(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numTextRows -at - 1));
    E.numTextRows--;
    E.dirty++;
}

// for when Delete at the begin of line: append the content of current line to the previous line + remove the current line
void editorRowAppendString(erow *row, char *s, size_t len)
{
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len); // copy to end of line
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c)
{
    if (at < 0 || at > row->size) { at = row->size; }
    row->chars = realloc(row->chars, row->size + 2);

    // "memmove" is like "memcpy" but safer if src and dest overlap
    // copy from [at] to end, and move by 1 (shift by 1)
    memmove(&row->chars[at+1], &row->chars[at], row->size - at + 1);//(+1 is the [at] itself) row->size - at = from at+1 to end)  
    row->size++;
    row->chars[at] = c; // now at [at] insert c
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowDeleteChar(erow *row, int at)
{
    if (at < 0 || at >= row->size) { return; }

    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    editorUpdateRow(row);
    E.dirty++;
}

/*************************************************************************/
/********* Editor operations *********************************************/
void editorInsertChar(int c)
{
    if (E.cy == E.numTextRows) { editorInsertRow(E.numTextRows, "", 0); } // on ~ line

    editorRowInsertChar(&E.row[E.cy], E.cx, c); // insert char at cursor pos
    E.cx++;
}

void editorDeleteChar()
{
    if (E.cy == E.numTextRows) { return; }
    if (E.cx == 0 && E.cy == 0) { return; }

    erow *row = &E.row[E.cy];
    if (E.cx  > 0)
    {
	editorRowDeleteChar(row, E.cx - 1);
	E.cx--;
    }
    else 
    {
	E.cx = E.row[E.cy - 1].size;
	editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
	editorDeleteRow(E.cy);
	E.cy--;
    }
}

void editorInsertNewLine()
{
    if (E.cx == 0)
    {
	editorInsertRow(E.cy, "", 0);
    }
    else 
    {
	erow *row = &E.row[E.cy];
	// insert new row from current row[cursor pos] to end row (in the next line) 
	editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx); //new row
	
	// stop current row at cursor pos
	row = &E.row[E.cy];
	row->size = E.cx;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
    }
    E.cy++;
    E.cx = 0;
}


/*************************************************************************/
/********* File I/O ******************************************************/
char *editorRowsToString(int *bufLen)
{
    // total bytes to write to file
    int totLen = 0;
    int j;
    for (j = 0; j < E.numTextRows; j++)
    {
	totLen += E.row[j].size + 1; // + space for '\n'
    }
    *bufLen = totLen;

    // copy line by line
    char *buf = malloc(totLen);
    char *p = buf;
    for (j = 0; j < E.numTextRows; j++)
    {
	memcpy(p, E.row[j].chars, E.row[j].size);
	p += E.row[j].size;
	*p = '\n'; // '\n' at end of each line
	p++;
    }

    return buf;
    // return lenght, and a pointer to buf 
}

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

	editorInsertRow(E.numTextRows ,line, linelen);
    }
    free(line);
    fclose(fp);
    E.dirty = 0; //cause called editorInsertRow()
}

void editorSave()
{
    if (E.filename == NULL) 
    { 
	E.filename = editorPrompt("Save as: %s  (ESC to cancel)", NULL);
	if (E.filename == NULL)
	{
	    editorSetStatusMessage("Not saved");
	    return;
	}
    }

    int len;
    char *buf = editorRowsToString(&len);

    // O_CREAT = create if doesn't exists      // O_RDWR = open for read and write
    // 0644 = standard permission for text files( Owner permission read/write, others read only)
    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1)
    { 
	if (ftruncate(fd, len) != -1)// set file size, ( safer then O_TRUNC ) 
	{
	    if ( write(fd, buf, len) == len)
	    {
		close(fd);
		free(buf);
		E.dirty = 0; //cause called editorInsertRow()
		editorSetStatusMessage("%d bytes written to disk", len);
		return;
	    }
	}
	close(fd);
    }
    free(buf);
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}


/********* Find *********************************************************/
void editorFindCallback(char *query, int key)
{
    static int last_match = -1; // row index of last match
    static int direction = 1;   // 1 = forward, -1 = backward 

    if (key == '\r' || key == '\x1b')
    {
	last_match = -1;
	direction = 1;
	return; 
    }
    else if (key == ARROW_RIGHT || key == ARROW_DOWN) 
    {
	direction = 1;
    }
    else if (key == ARROW_LEFT || key == ARROW_UP) 
    {
	direction = -1;
    }
    else 
    {
	last_match = -1;
	direction = 1;
    }

    if (last_match == -1) { direction = 1; }
    int current = last_match; // index of current row searched

    int i;
    for (i = 0; i < E.numTextRows; i++)
    {
	current += direction;
	// go from end of file to beginning and vice versa. 
	if (current == -1) { current = E.numTextRows - 1; }
	else if (current == E.numTextRows) { current = 0; }

	erow *row = &E.row[current];
	// pointer to first occurrence of substring in string
	char *match = strstr(row->render, query);
	if (match)
	{
	    last_match = current; // when find match, set last to current 
	    E.cy = current;
	    E.cx = editorRowRxToCx(row, match - row->render); 
	    E.rowOffset = E.numTextRows;
	    break;
	}
    }
}

void editorFind()
{
    // save cursor pos before run query
    int saved_cx = E.cx;
    int saved_cy = E.cy;
    int saved_colOff = E.colOffset;
    int saved_rowOff = E.rowOffset;

    char *query = editorPrompt("Search: %s  (Use ESC/Arrows/Enter)", editorFindCallback);

    if (query)  
    { 
	free(query); 
    }
    else // press ESSC )
    {
	E.cx = saved_cx;
	E.cy = saved_cy;
	E.colOffset = saved_colOff;
	E.rowOffset = saved_rowOff;
    }
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
char *editorPrompt(char *prompt, void (*callback)(char *, int))
{
    size_t bufSize = 128;
    char *buf = malloc(bufSize);

    size_t bufLen = 0;
    buf[0] = '\0';

    // read filename from user input key after key. And show in status message 
    while (1)
    {
	editorSetStatusMessage(prompt, buf);
	editorRefreshScreen();

	int c = editorReadKey();

	if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE)
	{
	    if (bufLen != 0) { buf[--bufLen] = '\0'; }
	}
	else if (c == '\x1b')
	{
	    editorSetStatusMessage("");
	    if (callback) { callback(buf, c); }
	    free(buf);
	    return NULL;
	}
	else if (c == '\r') // <Enter>
	{
	    if (bufLen != 0)
	    {
		editorSetStatusMessage("");
		if (callback) { callback(buf, c); }
		return buf;
	    }
	}
	else if (!iscntrl(c) && c < 128) // input key not one of the special keys in the editorKey enum ( < 128 )
	{
	    if (bufLen == bufSize - 1)
	    {   
		// when buffer reach max size realloc (double size) 
		bufSize *= 2;
		buf = realloc(buf, bufSize);
	    }

	    buf[bufLen++] = c;
	    buf[bufLen] = '\0';
	}

	if (callback)  { callback(buf, c); } // ( now call every character )
	// if allow to pass NULL for the callback
    }
}

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
    static int quit_times = KILO_QUIT_TIMES;

    int c = editorReadKey();

    switch (c) 
    {
	case '\r': // <Enter>
	{
	    editorInsertNewLine();
	}
	break;
	case CTRL_KEY('q'):
	{
	    if (E.dirty && quit_times > 0)
	    {
		editorSetStatusMessage("Warning!!! File has unsaved changes. " 
			"Press Ctrl-Q %d more times to quit.", quit_times);
		quit_times--;
		return;
	    }
	    write(STDOUT_FILENO, "\x1b[2J", 4);
	    write(STDOUT_FILENO, "\x1b[H", 3);
	    exit(0);
	    break;
	}
	case CTRL_KEY('s'):
	{
	    editorSave();
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
	case CTRL_KEY('f'):
	{
	    editorFind();
	}
	break;
	case BACKSPACE:
	case CTRL_KEY('h'):
	case DEL_KEY:
	{
	    if (c == DEL_KEY) { editorMoveCursor(ARROW_RIGHT); }
	    editorDeleteChar();
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
	case CTRL_KEY('l'):
	    // - Ctrl-L default: refresh terminal screen. Dont need cause kilo refresh after every key press
	case '\x1b':
	    // - ignore Escape key, cause in editorReadKey() not mapped key(f1, f2,...)will be equivalent to <esc>
	    break;

	default:
	    editorInsertChar(c);
	    break;
    }

    quit_times = KILO_QUIT_TIMES;
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
			" %.20s - %d lines %s", 
			E.filename ? E.filename : "[No Name]", 
			E.numTextRows,
			E.dirty ? " (modified)" : "");
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
    E.dirty = 0;
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

    editorSetStatusMessage("HELP: Ctrl-S = Save | Ctrl-Q = Quit | Ctrl-F = Find");

    while(1) 
    {
	editorRefreshScreen();
	editorProcessKeypress();
    }

    return 0;
}
