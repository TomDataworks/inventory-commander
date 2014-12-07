#include <ncurses.h>

int main(void)
{
    int mx=0, my=0;

    initscr();
    getmaxyx(stdscr, my, mx);
    endwin();

    printf("%d %d\n", mx, my);

    return 0;
}
