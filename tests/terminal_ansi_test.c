#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kernel/terminal.h"
void fd_readiness_wake(void) {}
static void send(const char *text) { terminal_write(text, strlen(text)); }
static unsigned char cell(unsigned int x, unsigned int y)
{ return terminal_cell_at(0, x, y).character; }
int main(void)
{
    assert(terminal_init(80, 24, NULL, NULL));
    /* ncurses initialization, split across writes like real stdout. */
    send("\033[?1049h\033[22;0;0t\033[1;24r\033(B\033[m\033[4l\033[?7h");
    send("\033[?12"); send("l\033[?25h\033[?2004h");
    send("\033[2J\033[H\033(B\033[0;7mnano\033[m");
    assert(cell(0,0)=='n' && cell(3,0)=='o' && cell(4,0)==' ');
    struct terminal_cell title = terminal_cell_at(0,0,0);
    assert(title.foreground == 0 && title.background == 7);
    send("\r\033[2dhello\033[40Gworld");
    assert(cell(0,1)=='h' && cell(39,1)=='w');
    send("\033[24;1H^X Exit\033[2;1H");
    send("\033[?25l\033[?12l\033[?25h");
    assert(cell(0,1)=='h' && cell(0,23)=='^');
    send("\033[1K"); assert(cell(0,1)==' ' && cell(1,1)=='e');
    send("\033[3X"); assert(cell(3,1)=='l');
    send("\033[3;1H-\033[4b");
    for(unsigned int i=0;i<5;i++) assert(cell(i,2)=='-');
    send("\033[2A\033[2D!"); assert(cell(3,0)=='!');
    send("\033]0;ignored title\007\033]2;ignored\033\\");
    assert(cell(4,0)==' ');
    send("\033[4;1Habcdef\033[4;3H\033[J");
    assert(cell(0,3)=='a' && cell(1,3)=='b' && cell(2,3)==' ' && cell(0,23)==' ');
    /* Unsupported private modes and excess parameters must stay invisible. */
    send("\033[?1;2;3;4;5;6;7;8;9;10;11;12;13;14;15;16;17hZ");
    assert(cell(2,3)=='Z' && cell(3,3)==' ');
    puts("Terminal ncurses ANSI parsing and screen layout passed");
    return 0;
}
