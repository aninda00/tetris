/* Wide-field terminal Tetris in C + ncurses.
 * Build: gcc tetris.c -o tetris -lncurses
 * Run:   ./tetris
 */
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FIELD_W 10
#define FIELD_H 20
#define CELL_W  2
#define BOARD_PX_W (FIELD_W * CELL_W)
#define SIDE_W  22
#define MIN_COLS (BOARD_PX_W + SIDE_W + 4)
#define MIN_ROWS (FIELD_H + 6)

static const int SHAPES[7][4][16] = {
    /* I */
    {
        {0,0,0,0, 1,1,1,1, 0,0,0,0, 0,0,0,0},
        {0,0,1,0, 0,0,1,0, 0,0,1,0, 0,0,1,0},
        {0,0,0,0, 1,1,1,1, 0,0,0,0, 0,0,0,0},
        {0,0,1,0, 0,0,1,0, 0,0,1,0, 0,0,1,0},
    },
    /* O */
    {
        {0,1,1,0, 0,1,1,0, 0,0,0,0, 0,0,0,0},
        {0,1,1,0, 0,1,1,0, 0,0,0,0, 0,0,0,0},
        {0,1,1,0, 0,1,1,0, 0,0,0,0, 0,0,0,0},
        {0,1,1,0, 0,1,1,0, 0,0,0,0, 0,0,0,0},
    },
    /* T */
    {
        {0,1,0,0, 1,1,1,0, 0,0,0,0, 0,0,0,0},
        {0,1,0,0, 0,1,1,0, 0,1,0,0, 0,0,0,0},
        {0,0,0,0, 1,1,1,0, 0,1,0,0, 0,0,0,0},
        {0,1,0,0, 1,1,0,0, 0,1,0,0, 0,0,0,0},
    },
    /* S */
    {
        {0,1,1,0, 1,1,0,0, 0,0,0,0, 0,0,0,0},
        {0,1,0,0, 0,1,1,0, 0,0,1,0, 0,0,0,0},
        {0,1,1,0, 1,1,0,0, 0,0,0,0, 0,0,0,0},
        {0,1,0,0, 0,1,1,0, 0,0,1,0, 0,0,0,0},
    },
    /* Z */
    {
        {1,1,0,0, 0,1,1,0, 0,0,0,0, 0,0,0,0},
        {0,0,1,0, 0,1,1,0, 0,1,0,0, 0,0,0,0},
        {1,1,0,0, 0,1,1,0, 0,0,0,0, 0,0,0,0},
        {0,0,1,0, 0,1,1,0, 0,1,0,0, 0,0,0,0},
    },
    /* J */
    {
        {1,0,0,0, 1,1,1,0, 0,0,0,0, 0,0,0,0},
        {0,1,1,0, 0,1,0,0, 0,1,0,0, 0,0,0,0},
        {0,0,0,0, 1,1,1,0, 0,0,1,0, 0,0,0,0},
        {0,1,0,0, 0,1,0,0, 1,1,0,0, 0,0,0,0},
    },
    /* L */
    {
        {0,0,1,0, 1,1,1,0, 0,0,0,0, 0,0,0,0},
        {0,1,0,0, 0,1,0,0, 0,1,1,0, 0,0,0,0},
        {0,0,0,0, 1,1,1,0, 1,0,0,0, 0,0,0,0},
        {1,1,0,0, 0,1,0,0, 0,1,0,0, 0,0,0,0},
    },
};

typedef struct { int kind, rot, x, y; } Piece;

static int board[FIELD_H][FIELD_W];
static Piece cur;
static int next_kind;
static int score = 0, lines = 0, level = 0, drop_ms = 700;
static int game_over = 0, paused = 0;

static int cell_at(int kind, int rot, int r, int c) {
    return SHAPES[kind][rot][r * 4 + c];
}

static Piece new_piece(int kind) {
    Piece p = { kind, 0, FIELD_W / 2 - 2, 0 };
    return p;
}

static int collides(Piece p) {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (!cell_at(p.kind, p.rot, r, c)) continue;
            int nx = p.x + c, ny = p.y + r;
            if (nx < 0 || nx >= FIELD_W || ny >= FIELD_H) return 1;
            if (ny >= 0 && board[ny][nx]) return 1;
        }
    }
    return 0;
}

static void clear_lines(void) {
    int cleared = 0;
    for (int r = FIELD_H - 1; r >= 0; ) {
        int full = 1;
        for (int c = 0; c < FIELD_W; c++)
            if (!board[r][c]) { full = 0; break; }
        if (full) {
            cleared++;
            for (int rr = r; rr > 0; rr--)
                memcpy(board[rr], board[rr - 1], sizeof(board[rr]));
            memset(board[0], 0, sizeof(board[0]));
        } else {
            r--;
        }
    }
    if (cleared) {
        static const int pts[] = {0, 100, 300, 500, 800};
        lines += cleared;
        score += pts[cleared] * (level + 1);
        level = lines / 10;
        drop_ms = 700 - level * 60;
        if (drop_ms < 100) drop_ms = 100;
    }
}

static void lock_piece(void) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (cell_at(cur.kind, cur.rot, r, c)) {
                int ny = cur.y + r, nx = cur.x + c;
                if (ny >= 0 && ny < FIELD_H && nx >= 0 && nx < FIELD_W)
                    board[ny][nx] = cur.kind + 1;
            }
    clear_lines();
    cur = new_piece(next_kind);
    next_kind = rand() % 7;
    if (collides(cur)) game_over = 1;
}

static int try_move(int dx, int dy) {
    Piece p = cur;
    p.x += dx; p.y += dy;
    if (collides(p)) return 0;
    cur = p;
    return 1;
}

static void try_rotate(void) {
    Piece p = cur;
    p.rot = (p.rot + 1) % 4;
    int kicks[] = {0, -1, 1, -2, 2};
    for (int i = 0; i < 5; i++) {
        Piece np = p;
        np.x += kicks[i];
        if (!collides(np)) { cur = np; return; }
    }
}

static void hard_drop(void) {
    while (try_move(0, 1)) {}
    lock_piece();
}

static void reset_game(void) {
    memset(board, 0, sizeof(board));
    score = lines = level = 0;
    drop_ms = 700;
    game_over = paused = 0;
    cur = new_piece(rand() % 7);
    next_kind = rand() % 7;
}

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void draw(void) {
    erase();
    int cols = getmaxx(stdscr);
    int rows = getmaxy(stdscr);

    if (cols < MIN_COLS || rows < MIN_ROWS) {
        char msg1[] = "Terminal too small";
        char msg2[80];
        snprintf(msg2, sizeof(msg2), "Resize to at least %dx%d (currently %dx%d)",
                 MIN_COLS, MIN_ROWS, cols, rows);
        int my = rows / 2;
        mvaddstr(my - 1, (cols - (int)strlen(msg1)) / 2, msg1);
        mvaddstr(my, (cols - (int)strlen(msg2)) / 2, msg2);
        refresh();
        return;
    }

    int ox = 1, oy = 1;
    char top_bot[BOARD_PX_W + 3];
    top_bot[0] = '+';
    memset(top_bot + 1, '-', BOARD_PX_W);
    top_bot[BOARD_PX_W + 1] = '+';
    top_bot[BOARD_PX_W + 2] = '\0';
    mvaddstr(oy - 1, ox - 1, top_bot);
    mvaddstr(oy + FIELD_H, ox - 1, top_bot);
    for (int r = 0; r < FIELD_H; r++) {
        mvaddstr(oy + r, ox - 1, "|");
        mvaddstr(oy + r, ox + BOARD_PX_W, "|");
    }

    for (int r = 0; r < FIELD_H; r++)
        for (int c = 0; c < FIELD_W; c++) {
            int v = board[r][c];
            if (!v) continue;
            attron(COLOR_PAIR(v));
            mvaddstr(oy + r, ox + c * CELL_W, "[]");
            attroff(COLOR_PAIR(v));
        }

    attron(COLOR_PAIR(cur.kind + 1));
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            if (!cell_at(cur.kind, cur.rot, r, c)) continue;
            int ny = cur.y + r, nx = cur.x + c;
            if (ny < 0 || ny >= FIELD_H || nx < 0 || nx >= FIELD_W) continue;
            mvaddstr(oy + ny, ox + nx * CELL_W, "[]");
        }
    attroff(COLOR_PAIR(cur.kind + 1));

    int px = ox + BOARD_PX_W + 3;
    mvaddstr(oy, px, "WIDE TETRIS");
    mvprintw(oy + 2, px, "Score: %d", score);
    mvprintw(oy + 3, px, "Lines: %d", lines);
    mvprintw(oy + 4, px, "Level: %d", level);
    mvaddstr(oy + 6, px, "Next:");
    attron(COLOR_PAIR(next_kind + 1));
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (cell_at(next_kind, 0, r, c))
                mvaddstr(oy + 7 + r, px + c * CELL_W, "[]");
    attroff(COLOR_PAIR(next_kind + 1));

    mvaddstr(oy + 13, px, "Controls:");
    mvaddstr(oy + 14, px, "Arrows move/rotate");
    mvaddstr(oy + 15, px, "Space  hard drop");
    mvaddstr(oy + 16, px, "ESC    pause/resume");
    mvaddstr(oy + 17, px, "R      restart");
    mvaddstr(oy + 18, px, "Q      quit");

    if (paused)
        mvaddstr(oy + FIELD_H / 2, ox + BOARD_PX_W / 2 - 3, "PAUSED");
    if (game_over)
        mvaddstr(oy + FIELD_H / 2, ox + BOARD_PX_W / 2 - 5, "GAME OVER");

    refresh();
}

int main(void) {
    srand((unsigned)time(NULL));
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);

    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_CYAN, COLOR_BLACK);
        init_pair(2, COLOR_YELLOW, COLOR_BLACK);
        init_pair(3, COLOR_MAGENTA, COLOR_BLACK);
        init_pair(4, COLOR_GREEN, COLOR_BLACK);
        init_pair(5, COLOR_RED, COLOR_BLACK);
        init_pair(6, COLOR_BLUE, COLOR_BLACK);
        init_pair(7, COLOR_WHITE, COLOR_BLACK);
    }

    reset_game();
    long last_drop = now_ms();

    for (;;) {
        int ch = wgetch(stdscr);
        if (ch == 'q' || ch == 'Q') break;

        if (ch == 'r' || ch == 'R') {
            reset_game();
            last_drop = now_ms();
        } else if (!game_over) {
            if (ch == 27) paused = !paused;
            if (!paused) {
                switch (ch) {
                    case KEY_LEFT:  try_move(-1, 0); break;
                    case KEY_RIGHT: try_move(1, 0);  break;
                    case KEY_DOWN:  try_move(0, 1);  break;
                    case KEY_UP:    try_rotate();    break;
                    case ' ':       hard_drop();     break;
                    default: break;
                }
            }
        }

        if (!game_over && !paused) {
            long t = now_ms();
            if (t - last_drop >= drop_ms) {
                if (!try_move(0, 1)) lock_piece();
                last_drop = t;
            }
        }

        draw();
        napms(16);
    }

    endwin();
    return 0;
}
