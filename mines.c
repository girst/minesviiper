/*******************************************************************************
 minesviiper 0.3.145926
 By Tobias Girstmair, 2015 - 2018

 ./minesviiper 16x16x40
 (see ./minesviiper -h for full list of options)

 MOUSE MODE: - left click to open and choord
             - right click to flag/unflag
 VI MODE:    - hjkl to move cursor left/down/up/right
             - o/space to open and choord
             - i to flag/unflag
             - (see `./minesviiper -h' for all keybindings)

 GNU GPL v3, see LICENSE or https://www.gnu.org/licenses/gpl-3.0.txt
*******************************************************************************/


#define _POSIX_C_SOURCE 2 /*for getopt, sigaction in c99*/
#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "mines.h"
#include "schemes.h"

#define LINE_OFFSET 3
#define LINES_AFTER 2
#define COL_OFFSET 2
#define BIG_MOVE 5
#define MOUSE_MAX 231

#define MIN(a,b) (a>b?b:a)
#define MAX(a,b) (a>b?a:b)
#define CLAMP(a,m,M) (a<m?m:(a>M?M:a))
#define printm(n, s) for (int _loop = 0; _loop < n; _loop++) fputs (s, stdout)
#define print(str) fputs (str?str:"", stdout)
#define EMOT(e) op.scheme->emoticons[EMOT_ ## e]
#define BORDER(l, c) op.scheme->border[B_ ## l][B_ ## c]
#define CW op.scheme->cell_width /* for brevity */
#define CTRL_ 0x1F &

#define CELL f.c[ROW][COL] /* helper for AROUND() */
#define AROUND(l, c) /* gives us loop-variables ROW, COL, CELL */ \
	for (int ROW = MAX(l-1, 0); ROW <= MIN(l+1, f.h-1); ROW++) \
		for (int COL = MAX(c-1, 0); COL <= MIN(c+1, f.w-1); COL++) \
			if (!(ROW == l && COL == c)) /* skip itself */

struct minefield f;
struct game g;
struct opt op;

int main (int argc, char** argv) {
	/* defaults: */
	f.w = 30;
	f.h = 16;
	f.m = 99;
	op.scheme = &symbols_mono;
	op.mode = FLAG;

	int optget;
	opterr = 0; /* don't print message on unrecognized option */
	while ((optget = getopt (argc, argv, "+hnfqcdx")) != -1) {
		switch (optget) {
		case 'n': op.mode    = NOFLAG; break;
		case 'f': op.mode    = FLAG; break; /*default*/
		case 'q': op.mode    = QUESM; break;
		case 'c': op.scheme  = &symbols_col1; break;
		case 'd': op.scheme  = &symbols_doublewidth; break;
		case 'h':
		default: 
			fprintf (stderr, SHORTHELP LONGHELP, argv[0]);
			return !(optget=='h');
		}
	} if (optind < argc) { /* parse Fieldspec */
		if (parse_fieldspec (argv[optind])) {
			fprintf (stderr, "FIELDSPEC: WxH[xM]"
			" (width 'x' height 'x' mines)\n");
			return 1;
		}
	}

	signal_setup();
	screen_setup(1);
	atexit (*quit);

	/* check boundaries */
	if (f.m > (f.w-1) * (f.h-1)) {
		move_ph (LINE_OFFSET+f.h+LINES_AFTER-1,0);
		f.m = (f.w-1) * (f.h-1);
		fprintf (stdout, "too many mines. reduced to %d.\r\n", f.m); //TODO: should disappear after a timeout (stomp-hi?)
	}
	/* end check */

newgame:
	switch (minesviiper()) {
	case GAME_NEW: goto newgame;
	case GAME_WON: g.o = GAME_WON; break;
	case GAME_LOST:g.o = GAME_LOST;break;
	case GAME_QUIT:goto quit;
	}

	timer_setup(0); /* stop timer */
	show_minefield (SHOWMINES);
	for(;;) {
		switch(getch_wrapper()) {
		case WRAPPER_EMOTICON:
		case 'r':
			free_field ();
			goto newgame;
		case 'q': goto quit;
		}
	}

quit:
	return 0;
}

int minesviiper(void) {
	f.c = alloc_array (f.h, f.w); /* TODO: just memset it */
	g = (const struct game){0}; /* reset all game-specific parameters */

	show_minefield (NORMAL);

	for(;;) {
		switch (getch_wrapper()) {
		case ' ':
			if (g.s == MODE_OPEN || f.c[g.p[0]][g.p[1]].o == OPENED)
				goto open_cell;
			if (g.s == MODE_FLAG)
				goto flag_cell;
			if (g.s ==  MODE_QUESM)
				goto quesm_cell;
			break;
		case 'a':
			g.s = (g.s+1)%(op.mode+1);
			show_minefield (g.c?SHOWMINES:NORMAL);
			break;
		open_cell:
		case CTRSEQ_MOUSE_LEFT:
		case 'o':
			switch (do_uncover(&(g.n))) {
				case GAME_LOST: return GAME_LOST;
				case GAME_WON:  return GAME_WON;
			}
			break;
		flag_cell:
		case CTRSEQ_MOUSE_RIGHT:
		case 'i': flag_square (g.p[0], g.p[1]); break;
		quesm_cell:
		case '?':quesm_square (g.p[0], g.p[1]); break;
		case CTRSEQ_CURSOR_LEFT:
		case 'h': move_hi (g.p[0],   g.p[1]-1); break;
		case CTRSEQ_CURSOR_DOWN:
		case 'j': move_hi (g.p[0]+1, g.p[1]  ); break;
		case CTRSEQ_CURSOR_UP:
		case 'k': move_hi (g.p[0]-1, g.p[1]  ); break;
		case CTRSEQ_CURSOR_RIGHT:
		case 'l': move_hi (g.p[0],   g.p[1]+1); break;
		case 'w': to_next_boundary (g.p[0], g.p[1], '>'); break;
		case 'b': to_next_boundary (g.p[0], g.p[1], '<'); break;
		case 'u': to_next_boundary (g.p[0], g.p[1], '^'); break;
		case 'd': to_next_boundary (g.p[0], g.p[1], 'v'); break;
		case '0': /* fallthrough */
		case '^': move_hi (g.p[0],    0        ); break;
		case '$': move_hi (g.p[0],    f.w-1    ); break;
		case 'g': move_hi (0,         g.p[1]   ); break;
		case 'G': move_hi (f.h-1,     g.p[1]   ); break;
		case 'z': move_hi (f.h/2, f.w/2); break;
		case 'm': set_mark(); break;
		case'\'': /* fallthrough */
		case '`': jump_mark(); break;
		case WRAPPER_EMOTICON:
		case 'r': return GAME_NEW;
		case 'q': return GAME_QUIT;
		case CTRL_'L': show_minefield (NORMAL); break;
		case '\\':
			if (g.n == GAME_NEW) break; /* must open a cell first */
			show_minefield (g.c?NORMAL:SHOWMINES);
			g.c = !g.c;
			break;
		}
	}

}

void quit (void) {
	screen_setup(0);
	free_field ();
}

/* I haven't won as long as a cell exists, that
    - I haven't opened, and
    - is not a mine */
int everything_opened (void) {
	for (int row = 0; row < f.h; row++)
		for (int col = 0; col < f.w; col++)
			if (f.c[row][col].o == CLOSED &&
			    f.c[row][col].m == NO_MINE  ) return 0;
	return 1;
}

int wait_mouse_up (int l, int c) { /* TODO: should not take minefield-coords but absolute ones */
	unsigned char mouse2[3];
	int level = 1;
	int l2, c2;

	/* show :o face */
	move_ph (1, field2screen_c (f.w/2)-1); print (EMOT(OHH));

	if (!(l < 0 || l >= f.h || c < 0 || c >= f.w)) {
		/* show a pushed-in button if cursor is on minefield */
		move_ph (l+LINE_OFFSET, field2screen_c(c));
		fputs (op.scheme->mouse_highlight, stdout);
	}

	while (level > 0) {
		if (getctrlseq (mouse2) == CTRSEQ_MOUSE) {
			/* ignore mouse wheel events: */
			if (mouse2[0] & 0x40) continue;

			else if (mouse2[0]&3 == 3) level--; /* release event */
			else level++; /* another button pressed */
		}
	}

	move_ph (1, field2screen_c (f.w/2)-1); print (get_emoticon());

	if (!(l < 0 || l >= f.h || c < 0 || c >= f.w)) {
		partial_show_minefield (l, c, NORMAL);
	}
	c2 = screen2field_c(mouse2[1]);
	l2 = screen2field_l(mouse2[2]);
	return ((l2 == l) && (c2 == c));
}

int choord_square (int line, int col) {
	if (uncover_square (line, col)) return 1;
	AROUND(line,col)
		if (CELL.f != FLAG) {
			if (uncover_square (ROW, COL))
				 return 1;
		}

	return 0;
}

int uncover_square (int l, int c) {
	if (f.c[l][c].o == OPENED)
		return 0; /* nothing to do */

	f.c[l][c].o = OPENED;
	f.c[l][c].f = NOFLAG; /*must not be QUESM, otherwise rendering issues*/
	partial_show_minefield (l, c, NORMAL);

	if (f.c[l][c].m) {
		f.c[l][c].m = DEATH_MINE;
		return 1;
	}

	/* check for chording */
	if (f.c[l][c].n == 0) {
		AROUND(l, c)
			if (uncover_square (ROW, COL)) return 1;
	}

	return 0;
}

void flag_square (int l, int c) {
	static char modechar[] = {'*', '!', '?'};

	if (f.c[l][c].o != CLOSED) return;
	/* cycle through flag/quesm/noflag: */
	f.c[l][c].f = (f.c[l][c].f + 1) % (op.mode + 1);
	if (f.c[l][c].f==FLAG) g.f++;
	else if ((op.mode==FLAG  && f.c[l][c].f==NOFLAG) ||
	         (op.mode==QUESM && f.c[l][c].f==QUESM)) g.f--;
	partial_show_minefield (l, c, NORMAL);
	move_ph (1, op.scheme->cell_width);
	printf ("[%03d%c]", f.m - g.f, modechar[g.s]);
}

void quesm_square (int l, int c) {
	/* toggle question mark / none. won't turn flags into question marks.
	unlike flag_square, this function doesn't respect `-q'. */
	if      (f.c[l][c].o != CLOSED) return;
	else if (f.c[l][c].f == NOFLAG) f.c[l][c].f = QUESM;
	else if (f.c[l][c].f == QUESM)  f.c[l][c].f = NOFLAG;
	partial_show_minefield (l, c, NORMAL);
}

int do_uncover (int* is_newgame) {
	if (*is_newgame == GAME_NEW) {
		*is_newgame = GAME_INPROGRESS;
		fill_minefield (g.p[0], g.p[1]);
		timer_setup(1);
	}

	if (f.c[g.p[0]][g.p[1]].f == FLAG  ) return GAME_INPROGRESS;
	if (f.c[g.p[0]][g.p[1]].o == CLOSED) {
		if (uncover_square (g.p[0], g.p[1])) return GAME_LOST;
	} else if (get_neighbours (g.p[0], g.p[1], 1) == 0) {
		if (choord_square (g.p[0], g.p[1])) return GAME_LOST;
	} else {
		/* show the stomp size if we aren't fully flagged */
		AROUND(g.p[0], g.p[1])
			if (CELL.o == CLOSED && CELL.f !=FLAG)
				partial_show_minefield (ROW, COL, HIGHLIGHT); //TODO: hide after timeout
			/* save the highlight position, timeout-end, and setup a timer. if another tiemout comes in before the timeout is over, the old one shall be removed and the timer cancelled before drawing the new one. if we hit any key it shall remove itself aswell */
	}
	if (everything_opened()) return GAME_WON;

	return GAME_INPROGRESS;
}

void set_mark(void) {
	int mark = tolower(getch_wrapper());
	if (mark < 'a' || mark > 'z') return; /*out of bound*/

	g.m[mark-'a'].l = g.p[0];
	g.m[mark-'a'].c = g.p[1];
	g.m[mark-'a'].s = 1;
}

void jump_mark(void) {
	int mark = tolower(getch_wrapper());
	/* check bounds and if set: */
	if (mark < 'a' || mark > 'z' || !g.m[mark-'a'].s) return;
	move_hi (g.m[mark-'a'].l, g.m[mark-'a'].c);
}

void fill_minefield (int l, int c) {
	srand (time(0));
	int mines_set = f.m;
	while (mines_set) {
		int line = rand() % f.h;
		int col = rand() % f.w;

		if (f.c[line][col].m) {
		/* skip if field already has a mine */
			continue;
		} else if ((line == l) && (col == c)) {
		/* don't put a mine on the already opened (first click) field */
			continue;
		} else {
			mines_set--;
			f.c[line][col].m = STD_MINE;
		}
	}

	/* precalculate neighbours */
	for (int l=0; l < f.h; l++)
		for (int c=0; c < f.w; c++)
			f.c[l][c].n = get_neighbours (l, c, NORMAL);
}

void move_ph (int line, int col) {
	/* move printhead to zero-indexed position */
	printf ("\033[%d;%dH", line+1, col+1);
}

void move_hi (int l, int c) {
	/* move cursor and highlight to absolute coordinates */

	partial_show_minefield (g.p[0], g.p[1], NORMAL);
	/* update g.p */
	g.p[0] = CLAMP(l, 0, f.h-1);
	g.p[1] = CLAMP(c, 0, f.w-1);
	move_ph (g.p[0]+LINE_OFFSET, field2screen_c(g.p[1]));

	print("\033[7m"); /* reverse video */
	partial_show_minefield (g.p[0], g.p[1], HIGHLIGHT);
	print("\033[0m"); /* un-invert */
}

/* to_next_boundary(): move into the supplied direction until a change in open-
state or flag-state is found and move there. falls back to BIG_MOVE. */
#define FIND_NEXT(X, L, C, L1, C1, MAX, OP) do {\
	new_ ## X OP ## = BIG_MOVE;\
	for (int i = X OP 1; i > 0 && i < f.MAX-1; i OP ## OP)\
		if (((f.c[L ][C ].o<<2) + f.c[L ][C ].f) \
		 != ((f.c[L1][C1].o<<2) + f.c[L1][C1].f)) {\
			new_ ## X = i OP 1;\
			break;\
		}\
	} while(0)
void to_next_boundary (int l, int c, char direction) {
	int new_l = l;
	int new_c = c;
	switch (direction) {
	case '>': FIND_NEXT(c, l, i, l, i+1, w, +); break;
	case '<': FIND_NEXT(c, l, i, l, i-1, w, -); break;
	case '^': FIND_NEXT(l, i, c, i-1, c, h, -); break;
	case 'v': FIND_NEXT(l, i, c, i+1, c, h, +); break;
	}

	move_hi (new_l, new_c);
}
#undef FIND_NEXT

char* cell2schema (int l, int c, int mode) {
	struct minecell cell = f.c[l][c];

	if (mode == SHOWMINES) return (
		cell.f == FLAG &&  cell.m ? op.scheme->field_flagged:
		cell.f == FLAG && !cell.m ? op.scheme->mine_wrongf:
		cell.m == STD_MINE        ? op.scheme->mine_normal:
		cell.m == DEATH_MINE      ? op.scheme->mine_death:
		cell.o == CLOSED          ? op.scheme->field_closed:
		/*.......................*/ op.scheme->number[f.c[l][c].n]);
	 else return (
		cell.f == FLAG            ? op.scheme->field_flagged:
		cell.f == QUESM           ? op.scheme->field_question:
		cell.o == CLOSED          ? op.scheme->field_closed:
		cell.m == STD_MINE        ? op.scheme->mine_normal:
		cell.m == DEATH_MINE      ? op.scheme->mine_death:
		/*.......................*/ op.scheme->number[f.c[l][c].n]);
}

void partial_show_minefield (int l, int c, int mode) {
	if (mode == HIGHLIGHT) printf ("\033[7m"); /* reverse video */
	move_ph (l+LINE_OFFSET, field2screen_c(c));
	print (cell2schema(l, c, mode));
	if (mode == HIGHLIGHT) printf ("\033[0m"); /* reset all */
}

char* get_emoticon(void) {
	return g.o==GAME_WON ? EMOT(WON):
	       g.o==GAME_LOST? EMOT(DEAD):
		EMOT(SMILE);
}

/* https://zserge.com/blog/c-for-loop-tricks.html */
#define print_line(which) \
	for (int _break = (printf("%s", BORDER(which,LEFT)), 1); _break; \
		_break = 0, printf("%s\r\n", BORDER(which,RIGHT)))
#define print_border(which, width) \
	print_line(which) printm (width, BORDER(which,MIDDLE))
void show_minefield (int mode) {
	int dtime = difftime (time(NULL), g.t)*!!g.t;
	int half_spaces = f.w*op.scheme->cell_width/2;
	int left_spaces = MAX(0,half_spaces-7-(f.m-g.f>999));
	int right_spaces = MAX(0,half_spaces-6-(dtime>999));
	static char modechar[] = {'*', '!', '?'};

	move_ph (0,0);

	print_border(TOP, f.w);
	print_line(STATUS) {
		printf("[%03d%c]%*s%s%*s[%03d]",
		  /* [ */ f.m - g.f, modechar[g.s], /* ] */
		  left_spaces,"", get_emoticon(), right_spaces,"",
		  /* [ */ dtime /* ] */);
	}
	print_border(DIVIDER, f.w);
	/* main body */
	for (int l = 0; l < f.h; l++) print_line(FIELD)
		printm (f.w, cell2schema(l, _loop, mode));
	print_border(BOTTOM, f.w);
}

int get_neighbours (int line, int col, int reduced_mode) {
	/* counts mines surrounding a square
	   modes: 0=normal; 1=reduced */

	int count = 0;
	AROUND(line, col) {
		count += !!CELL.m;
		count -= reduced_mode * CELL.f==FLAG;
	}
	return count;
}

struct minecell** alloc_array (int lines, int cols) {
	free_field (); /* TODO: just memset */
	struct minecell** a = malloc (lines * sizeof(struct minecell*));
	if (a == NULL) return NULL;
	for (int l = 0; l < lines; l++) {
		a[l] = calloc (cols, sizeof(struct minecell));
		if (a[l] == NULL) goto unalloc;
	}

	return a;
unalloc:
	for (int l = 0; l < lines; l++)
		free (a[l]);
	return NULL;
}

void free_field (void) {
	if (f.c == NULL) return;
	for (int l = 0; l < f.h; l++) {
		free (f.c[l]);
	}

	free (f.c);
	f.c = NULL;
}

int screen2field_l (int l) {
	return (l-LINE_OFFSET) - 1;
}
int screen2field_c (int c) {
	/* this depends on the cell width and only works when it is 1 or 2. */
	return (c-COL_OFFSET+1 - 2*(CW%2))/2 - CW/2;
}
int field2screen_c (int c) {
	return (CW*c+COL_OFFSET - (CW%2));
}
int clicked_emoticon (unsigned char* mouse) {
	return (mouse[2] == LINE_OFFSET-1 && (
		mouse[1] == f.w+COL_OFFSET ||
		mouse[1] == f.w+COL_OFFSET+1));
}

enum esc_states {
	START,
	ESC_SENT,
	CSI_SENT,
	MOUSE_EVENT,
};
int getctrlseq (unsigned char* buf) {
	int c;
	int state = START;
	int offset = 0x20; /* never sends control chars as data */
	while ((c = getchar()) != EOF) {
		switch (state) {
		case START:
			switch (c) {
			case '\033': state=ESC_SENT; break;
			default: return c;
			}
			break;
		case ESC_SENT:
			switch (c) {
			case '[': state=CSI_SENT; break;
			default: return CTRSEQ_INVALID;
			}
			break;
		case CSI_SENT:
			switch (c) {
			case 'A': return CTRSEQ_CURSOR_UP;
			case 'B': return CTRSEQ_CURSOR_DOWN;
			case 'C': return CTRSEQ_CURSOR_RIGHT;
			case 'D': return CTRSEQ_CURSOR_LEFT;
			case 'M': state=MOUSE_EVENT; break;
			default: return CTRSEQ_INVALID;
			}
			break;
		case MOUSE_EVENT:
			buf[0] = c - offset;
			buf[1] = getchar() - offset;
			buf[2] = getchar() - offset;
			return CTRSEQ_MOUSE;
		default:
			return CTRSEQ_INVALID;
		}
	}
	return 2;
}

int getch(unsigned char* buf) {
/* returns a character, EOF, or constant for an escape/control sequence - NOT
compatible with the ncurses implementation of same name */
	int action = getctrlseq(buf);
	int l, c;
	switch (action) {
	case CTRSEQ_MOUSE:
		l = screen2field_l (buf[2]);
		c = screen2field_c (buf[1]);

		if (buf[0] > 3) break; /* ignore all but left/middle/right/up */
		int success = wait_mouse_up(l, c);

		/* mouse moved while pressed: */
		if (!success) return CTRSEQ_INVALID;

		switch (buf[0]) {
		case 0: return CTRSEQ_MOUSE_LEFT;
		case 1: return CTRSEQ_MOUSE_MIDDLE;
		case 2: return CTRSEQ_MOUSE_RIGHT;
		}
	}

	return action;
}

int getch_wrapper (void) {
	unsigned char mouse[3];
	int c = getch(mouse);

	if (c == CTRSEQ_MOUSE_LEFT || c == CTRSEQ_MOUSE_RIGHT) {
		if (clicked_emoticon(mouse))
			return WRAPPER_EMOTICON;

		if (screen2field_c (mouse[1]) < 0
		 || screen2field_c (mouse[1]) >= f.w
		 || screen2field_l (mouse[2]) < 0
		 || screen2field_l (mouse[2]) >= f.h)
			return CTRSEQ_INVALID;

		g.p[0] = screen2field_l (mouse[2]);
		g.p[1] = screen2field_c (mouse[1]);

		return c; /* CTRSEQ_MOUSE_LEFT || CTRSEQ_MOUSE_RIGHT */
	}

	return c;
}

int parse_fieldspec(char* str) {
	/* parses the FIELDSPEC (WxHxM); returns 1 on error */
	int n = sscanf (str, "%dx%dx%d", &f.w, &f.h, &f.m);
	struct winsize w;
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

	/* clamp field size to terminal size and mouse maximum: */
	if (COL_OFFSET + f.w*CW + COL_OFFSET > w.ws_col)
		f.w = w.ws_col/CW - (COL_OFFSET+COL_OFFSET);
	if (LINE_OFFSET + f.h + LINES_AFTER > w.ws_row)
		f.h = w.ws_row - (LINE_OFFSET+LINES_AFTER);
	if (COL_OFFSET + f.w*CW > MOUSE_MAX)
		f.w = MOUSE_MAX/CW - COL_OFFSET;
	if (LINE_OFFSET + f.h > MOUSE_MAX)
		f.h = MOUSE_MAX - LINE_OFFSET;

	if (n < 2) {
		return 1; /* error */
	} else if (n == 2) {
		if (f.w < 30) f.m = f.w*f.h*.15625;
		else f.m = f.w*f.h*.20625;
	}

	return 0;
}

void timer_setup (int enable) {
	static struct itimerval tbuf;
	tbuf.it_interval.tv_sec  = 1;
	tbuf.it_interval.tv_usec = 0;

	if (enable) {
		g.t = time(NULL);
		tbuf.it_value.tv_sec  = 1;
		tbuf.it_value.tv_usec = 0;
		if (setitimer(ITIMER_REAL, &tbuf, NULL) == -1) {
			perror("setitimer");
			exit(1);
		}
	} else {
		tbuf.it_value.tv_sec  = 0;
		tbuf.it_value.tv_usec = 0;
		if ( setitimer(ITIMER_REAL, &tbuf, NULL) == -1 ) {
			perror("setitimer");
			exit(1);
		}
	}

}

void signal_setup (void) {
	struct sigaction saction;

	saction.sa_handler = signal_handler;
	sigemptyset(&saction.sa_mask);
	saction.sa_flags   = 0;
	if (sigaction(SIGALRM, &saction, NULL) < 0 ) {
		perror("SIGALRM");
		exit(1);
	}

	if (sigaction(SIGINT, &saction, NULL) < 0 ) {
		perror ("SIGINT");
		exit (1);
	}
}

void signal_handler (int signum) {
	int dtime;
	switch (signum) {
	case SIGALRM:
		dtime = difftime (time(NULL), g.t);
		move_ph (1, f.w*CW-(CW%2)-3-(dtime>999));
		printf ("[%03d]", g.t?dtime:0);
		break;
	case SIGINT:
		exit(128+SIGINT);
	}
}

void screen_setup (int enable) {
	if (enable) {
		raw_mode(1);
		printf ("\033[s\033[?47h"); /* save cursor, alternate screen */
		printf ("\033[H\033[J"); /* reset cursor, clear screen */
		printf ("\033[?1000h\033[?25l"); /* enable mouse, hide cursor */
		print (op.scheme->init_seq); /* swich charset, if necessary */
	} else {
		print (op.scheme->reset_seq); /* reset charset, if necessary */
		printf ("\033[?9l\033[?25h"); /* disable mouse, show cursor */
		printf ("\033[?47l\033[u"); /* primary screen, restore cursor */
		raw_mode(0);
	}
}

/* http://users.csc.calpoly.edu/~phatalsk/357/lectures/code/sigalrm.c */
void raw_mode(int enable) {
	static struct termios saved_term_mode;
	struct termios raw_term_mode;

	if (enable) {
		tcgetattr(STDIN_FILENO, &saved_term_mode);
		raw_term_mode = saved_term_mode;
		raw_term_mode.c_lflag &= ~(ICANON | ECHO);
		raw_term_mode.c_cc[VMIN] = 1 ;
		raw_term_mode.c_cc[VTIME] = 0;
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_term_mode);
	} else {
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_term_mode);
	}
}