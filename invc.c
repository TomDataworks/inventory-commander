#include <sqlite3.h>
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>

#define ARRLEN(rr) (sizeof(rr)/sizeof(rr[0]))

#define BY_NAME  0
#define BY_ABOUT 1

#define BUFF_SIZE 64

struct win_properties_t {
    int view_limit;
    int main_height;
    int main_width;
    int number_actions;
    int action_length;
    int panel_left;
    int panel_right;
    int data_width;
    int data_width_tab;
    int int_length;
} win_props;

struct entry_t {
    int id;
    int parent;
    const char* name;
    const char* about;
    int count;
};

struct path_t {
    struct path_t* next;
    int id;
    int offset;
    const char* name;
};

struct panel_t {
    const char* title;
    struct path_t* path;
    WINDOW* win;
    struct entry_t* entries;
    int offset;
    int count;
    int parent;
    bool loaded;
};

struct search_panel_t {
    const char* query;
    WINDOW* win;
    struct entry_t* entries;
    struct entry_t* current;
    int offset;
    int count;
    int type;
    int is_closing;
};

struct action_source_t {
    struct entry_t* entry;
    WINDOW* window;
};

struct action_t {
    int key;
    bool show_on_bar;
    const char* keyname;
    const char* name;
    const char* description;
    int (*function)(struct action_source_t);
};

struct button_t {
    const char* name;
    int (*function)(char*);
};

WINDOW *bar;
WINDOW *current_window;
sqlite3 *db;
sqlite3_stmt *insert_stmt;
sqlite3_stmt *select_stmt;
sqlite3_stmt *count_stmt;
sqlite3_stmt *item_count_stmt;
sqlite3_stmt *item_parent_stmt;
sqlite3_stmt *update_count_stmt;
sqlite3_stmt *rename_stmt;
sqlite3_stmt *redescribe_stmt;
sqlite3_stmt *description_stmt;
sqlite3_stmt *move_stmt;
sqlite3_stmt *delete_stmt;
sqlite3_stmt *by_about_stmt;
sqlite3_stmt *by_name_stmt;
sqlite3_stmt *count_by_about_stmt;
sqlite3_stmt *count_by_name_stmt;
int panel;

int show_modal_help();
int show_modal_open();
int show_modal_count();
int show_modal_add();
int show_modal_rename();
int show_modal_editor();
int show_modal_search();
int show_modal_error(char* error);
int editor_save();
int panel_descend();
int panel_ascend();
int open_database();
int switch_panels();
int draw_panel(struct panel_t* p);
int update_dataview(struct panel_t* panel, int reload);
int panel_offset_inc();
int panel_offset_pgdn();
int panel_offset_dec();
int panel_offset_pgup();
int move_item();
int delete_item();
int item_search_by_name(char* name);
int item_search_by_about(char* about);
int search_offset_dec();
int search_offset_inc();
int search_offset_pgup();
int search_offset_pgdn();
int search_goto();
int search_goto_parent();

struct panel_t panels[] = {
    {"Left", NULL, NULL, 0, 0, 0, FALSE},
    {"Right", NULL, NULL, 0, 0, 0, FALSE}
};

struct search_panel_t search_panel;

struct action_t actions[] = {
    // {key, show key on bar, key name, action name, action help, function}
    {KEY_F(1), TRUE, "F1", "Help", "Display this help screen", show_modal_help},
    {KEY_F(2), TRUE, "F2", "Open", "Create a new database or open an existing one", show_modal_open},
    {KEY_F(3), TRUE, "F3", "Add", "Create a new container or item", show_modal_add},
    {KEY_F(4), TRUE, "F4", "Rename", "Rename the item", show_modal_rename},
    {KEY_F(5), TRUE, "F5", "Text", "Give this item a text description", show_modal_editor},
    {KEY_F(6), TRUE, "F6", "Move", "Move this item to a new location", move_item},
    {KEY_F(7), TRUE, "F7", "Delete", "Delete this item", delete_item},
    {KEY_F(8), TRUE, "F8", "Count", "Set the number of this item", show_modal_count},
    {KEY_F(9), TRUE, "F9", "Search", "Search for an item by name or description", show_modal_search},
    {KEY_F(10), TRUE, "F10", "Quit", "Quit the program", NULL},
    {'\t', FALSE, "Tab", "Switch", "Switch between panels", switch_panels},
    {KEY_UP, FALSE, "Up", "GoUp", "Navigate listing up", panel_offset_dec},
    {KEY_DOWN, FALSE, "Down", "GoDown", "Navigate listing down", panel_offset_inc},
    {KEY_PPAGE, FALSE, "PgUp", "PageUp", "Navigate listing up a page", panel_offset_pgup},
    {KEY_NPAGE, FALSE, "PgUp", "PageUp", "Navigate listing up a page", panel_offset_pgdn},
    {KEY_LEFT, FALSE, "Left", "GoBack", "Go back", panel_ascend},
    {'\n', FALSE, "Enter", "GoInto", "Navigate into item", panel_descend},
    {0, FALSE, NULL, NULL, NULL, NULL}
};

struct action_t editor_actions[] = {
    {KEY_F(2), TRUE, "F2", "Save", "Save description", editor_save},
    {KEY_F(3), TRUE, "F3", "Leave", "Leave description editor", NULL},
    {0, FALSE, NULL, NULL, NULL, NULL}
};

struct action_t search_actions[] = {
    {KEY_F(1), TRUE, "F1", "Go To", "Go to item in database", search_goto},
    {KEY_F(2), TRUE, "F2", "Parent", "Go to item's parent in database", search_goto_parent},
    {KEY_F(3), TRUE, "F3", "Leave", "Leave search results", NULL},
    {KEY_UP, TRUE, "KPU", "Up", "Navigate listing up", search_offset_dec},
    {KEY_DOWN, TRUE, "KPD", "Down", "Navigate listing up", search_offset_inc},
    {KEY_PPAGE, TRUE, "PGU", "PgUp", "Navigate listing up", search_offset_pgup},
    {KEY_NPAGE, TRUE, "PGD", "PgDown", "Navigate listing up", search_offset_pgdn},
    {0, FALSE, NULL, NULL, NULL, NULL}
};

int setup_window_properties() {
    getmaxyx(stdscr, win_props.main_height, win_props.main_width);
    win_props.number_actions = 10;
    win_props.action_length = win_props.main_width / win_props.number_actions;
    win_props.panel_left = 0;
    win_props.panel_right = 1;
    win_props.data_width = win_props.main_width / 2 - 2;
    win_props.data_width_tab = win_props.data_width / 3 + 1;
    // View limit is the main size minus the bottom border and action bar
    win_props.view_limit = win_props.main_height - 4;
    win_props.int_length = 5;
}

struct entry_t* current_entry() {
    return &panels[panel].entries[panels[panel].offset % win_props.view_limit];
}

int move_item() {
    if(panels[panel].loaded == FALSE) {
        show_modal_error("No database loaded.");
        return 1;
    }

    struct path_t* path;
    struct entry_t* entry = current_entry();
    int new_parent;
    if(panel == win_props.panel_left) {
        path = panels[win_props.panel_right].path;
        new_parent = panels[win_props.panel_right].parent;
    } else if(panel == win_props.panel_right) {
        path = panels[win_props.panel_left].path;
        new_parent = panels[win_props.panel_left].parent;
    }
    int allow_move = TRUE;
    while (path != NULL) {
        if(path->id == entry->id) allow_move = FALSE;
        path = path->next;
    }
    if(allow_move) {
        //gmvwprintw(panels[panel].win, 23, 10, "ID: %d PAR: %d", entry->id, new_parent);
        wrefresh(panels[panel].win);

        if(new_parent == 0) {
            sqlite3_bind_null(move_stmt, 1);
        } else {
            sqlite3_bind_int(move_stmt, 1, new_parent);
        }
        sqlite3_bind_int(move_stmt, 2, entry->id);
        int s;
        while ((s = sqlite3_step(move_stmt)) != SQLITE_DONE) { }
        sqlite3_reset(move_stmt);
        update_dataview(&panels[win_props.panel_left], TRUE);
        update_dataview(&panels[win_props.panel_right], TRUE);
    }
}

int delete_item() {
    if(panels[panel].loaded == FALSE) {
        show_modal_error("No database loaded.");
        return 1;
    }

    struct entry_t* entry = current_entry();
    sqlite3_bind_int(delete_stmt, 1, entry->id);
    int s;
    while ((s = sqlite3_step(delete_stmt)) != SQLITE_DONE) { }
    if(panels[panel].offset == panels[panel].count - 1) {
        panels[panel].offset--;
    }
    sqlite3_reset(delete_stmt);
    if(panels[win_props.panel_left].parent == panels[win_props.panel_right].parent) {
        for(int i = 0; i < ARRLEN(panels); i++) {
            update_dataview(&panels[i], TRUE);
        }
    } else {
        update_dataview(&panels[panel], TRUE);
    }
}

int panel_offset_inc() {
    if(panels[panel].offset < panels[panel].count - 1) {
        panels[panel].offset++;
        update_dataview(&panels[panel], FALSE);
    }
}

int panel_offset_dec() {
    if(panels[panel].offset > 0) {
        panels[panel].offset--;
        update_dataview(&panels[panel], FALSE);
    }
}

int panel_offset_pgdn() {
    if((panels[panel].offset / win_props.view_limit) * win_props.view_limit + win_props.view_limit < panels[panel].count) {
        panels[panel].offset = (panels[panel].offset / win_props.view_limit) * win_props.view_limit + win_props.view_limit;
        update_dataview(&panels[panel], TRUE);
    }
}

int panel_offset_pgup() {
    if(panels[panel].offset >= win_props.view_limit) {
        panels[panel].offset = (panels[panel].offset / win_props.view_limit) * win_props.view_limit - win_props.view_limit;
        update_dataview(&panels[panel], TRUE);
    }
}

int panel_descend() {
    if(panels[panel].count > 0) {
        struct entry_t* entry = current_entry();
        int id = entry->id;
        panels[panel].parent = id;
        if(panels[panel].path == NULL) {
            panels[panel].path = malloc(sizeof(struct path_t));
            panels[panel].path->id = id;
            if(entry->name != NULL) {
                char* buf = malloc(strlen(entry->name) + 1);
                strcpy(buf, entry->name);
                panels[panel].path->name = buf;
            } else {
                panels[panel].path->name = NULL;
            }
            panels[panel].path->offset = panels[panel].offset;
            panels[panel].path->next = NULL;
        } else {
            struct path_t* p = panels[panel].path;
            while(p != NULL) {
                if(p->next == NULL) {
                    p->next = malloc(sizeof(struct path_t));
                    p->next->id = id;
                    if(entry->name != NULL) {
                        char* buf = malloc(strlen(entry->name) + 1);
                        strcpy(buf, entry->name);
                        p->next->name = buf;
                    } else {
                        p->next->name = NULL;
                    }
                    p->next->offset = panels[panel].offset;
                    p->next->next = NULL;
                    break;
                }
                p = p->next;
            }
        }
        panels[panel].offset = 0;
        draw_panel(&panels[panel]);
        update_dataview(&panels[panel], FALSE);
    }
}

int panel_ascend() {
    int id = panels[panel].parent;
    struct path_t* p = panels[panel].path;
    panels[panel].offset = 0;
    if(p != NULL) {
        if(p->next == NULL) {
            panels[panel].offset = p->offset;
            free(p);
            panels[panel].path = NULL;
            panels[panel].parent = 0;
        } else {
            while(p != NULL) {
                 if(p->next != NULL && p->next->next == NULL) {
                      panels[panel].offset = p->next->offset;
                      free(p->next);
                      p->next = NULL;
                      break;
                 }
                 p = p->next;
            }
            //fprintf(stderr, "%s %d\n", p->name, p->id);
            panels[panel].parent = p->id;
        }
        draw_panel(&panels[panel]);
        update_dataview(&panels[panel], TRUE);
    }
}

struct button_t file_open_buttons[] = {
    {"Open Database", open_database},
    {"Cancel", NULL},
    {NULL, NULL}
};

struct button_t item_search_buttons[] = {
    {"By Name", item_search_by_name},
    {"By Description", item_search_by_about},
    {"Cancel", NULL},
    {NULL, NULL}
};

int init_colors_midnight() {
    start_color();
    init_pair(1, COLOR_WHITE, COLOR_BLACK);
    init_pair(2, COLOR_BLACK, COLOR_CYAN);
    init_pair(3, COLOR_WHITE, COLOR_BLUE);
    init_pair(4, COLOR_YELLOW, COLOR_BLACK);
    init_pair(5, COLOR_YELLOW, COLOR_RED);
    init_pair(6, COLOR_YELLOW, COLOR_BLUE);
}

int select_window(WINDOW *win) {
    wmove(win, 1, 1);
    wrefresh(win);
}

static int database_callback(void *NotUsed, int argc, char **argv, char **azColName){
   int i;
   for(i=0; i<argc; i++){
      printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
   }
   printf("\n");
   return 0;
}

int update_dataview(struct panel_t* panel, int reload) {
    //reload = TRUE;
    select_window(panel->win);
    if(panel->loaded == FALSE) return 1;
    if(panel->path == NULL) {
        sqlite3_bind_null(count_stmt, 1);
        sqlite3_bind_null(select_stmt, 1);
    } else {
        sqlite3_bind_int(count_stmt, 1, panel->parent);
        sqlite3_bind_int(select_stmt, 1, panel->parent);
    }
    sqlite3_bind_int(select_stmt, 2, win_props.view_limit);
    sqlite3_bind_int(select_stmt, 3, (panel->offset / win_props.view_limit) * win_props.view_limit);
    int s, cnt;
    while ((s = sqlite3_step(count_stmt)) != SQLITE_DONE) {
        if(s == SQLITE_ROW) {
            cnt = sqlite3_column_int(count_stmt, 0);
        }
    }
    panel->count = cnt;
    int i = 0;
    int name_length = (win_props.main_width / 2) - 4 - win_props.int_length * 2;
    mvwaddch(panel->win, 1, 1 + win_props.int_length, ACS_VLINE);
    mvwaddch(panel->win, 0, 2 + win_props.int_length + name_length, ACS_TTEE);
    mvwaddch(panel->win, 1, 2 + win_props.int_length + name_length, ACS_VLINE);
    mvwaddch(panel->win, win_props.main_height - 2, 2 + win_props.int_length + name_length, ACS_BTEE);
    wattron(panel->win, WA_BOLD);
    wattron(panel->win, COLOR_PAIR(6));
    mvwprintw(panel->win, 1, 1, "Id");
    wattroff(panel->win, COLOR_PAIR(6));
    wattron(panel->win, COLOR_PAIR(6));
    mvwprintw(panel->win, 1, 2 + win_props.int_length, "Name");
    wattroff(panel->win, COLOR_PAIR(6));
    wattron(panel->win, COLOR_PAIR(6));
    mvwprintw(panel->win, 1, 3 + win_props.int_length + name_length, "Qty");
    wattroff(panel->win, COLOR_PAIR(6));
    wattroff(panel->win, WA_BOLD);
    if((panel->offset % win_props.view_limit) == 0 || (panel->offset % win_props.view_limit) == win_props.view_limit - 1 || reload == TRUE) {
        while ((s = sqlite3_step(select_stmt)) != SQLITE_DONE) {
            if(s == SQLITE_ROW) {
                panel->entries[i].id = sqlite3_column_int(select_stmt, 0);
                if(panel->entries[i].name != NULL) { free((void*)(panel->entries[i].name)); panel->entries[i].name = NULL; }
                int bytes = sqlite3_column_bytes(select_stmt, 1);
                if(bytes > 0) {
                    char* buf = malloc(bytes + 1);
                    panel->entries[i].name = strcpy(buf, sqlite3_column_text(select_stmt, 1));
                } else {
                    panel->entries[i].name = NULL;
                }
                panel->entries[i].about = NULL;
                panel->entries[i].count = sqlite3_column_int(select_stmt, 3);
                i++;
            }
        }
        for(; i < win_props.view_limit; i++) {
            panel->entries[i].id = 0;
            panel->entries[i].name = NULL;
        }
    }
    for(i = 0; i < win_props.view_limit; i++) {
        if(panel->entries[i].id != 0) {
            if(i == ((panel->offset)% win_props.view_limit)) {
                wattron(panel->win, WA_STANDOUT);
            }
            mvwhline(panel->win, 2+i, 1, ' ', win_props.data_width);
            mvwaddch(panel->win, 2 + i, 1 + win_props.int_length, ACS_VLINE);
            mvwaddch(panel->win, 2 + i, 2 + win_props.int_length + name_length, ACS_VLINE);
            //mvwhline(panel->win, 2 + i, 1, ' ', win_props.data_width);
            //mvwhline(panel->win, 2+i, 1, ' ', win_props.int_length);
            //mvwhline(panel->win, 2+i, 1, ' ', win_props.int_length);
            mvwprintw(panel->win, 2 + i, 1, "%d", panel->entries[i].id);
            //mvwaddch(panel->win, 2 + i, win_props.data_width_tab * 1, ACS_VLINE);
            if(panel->entries[i].name != NULL) {
                mvwprintw(panel->win, 2 + i, 2 + win_props.int_length * 1, "%.*s", name_length, panel->entries[i].name);
            }
            //mvwaddch(panel->win, 2 + i, win_props.data_width_tab * 2, ACS_VLINE);
            mvwprintw(panel->win, 2 + i, 3 + win_props.int_length * 1 + name_length, "%d", panel->entries[i].count);
            wattroff(panel->win, WA_STANDOUT);
        } else {
            mvwhline(panel->win, 2+i, 1, ' ', win_props.data_width);
            mvwaddch(panel->win, 2 + i, 1 + win_props.int_length, ACS_VLINE);
            mvwaddch(panel->win, 2 + i, 2 + win_props.int_length + name_length, ACS_VLINE);
        }
    }
    sqlite3_reset(select_stmt);
    sqlite3_reset(count_stmt);
    //int id = current_entry()->id;
    //mvwprintw(panel->win, 23, 1, "ID: %d OFF: %d PAR: %d", id, panel->offset % win_props.view_limit, panel->parent);
    wrefresh(panel->win);
}

int switch_panels() {
    panel = (panel + 1) % ARRLEN(panels);
    select_window(panels[panel].win);
}

int draw_command_bar(WINDOW *command, struct action_t* bar) {
    int n = 0;
    for(int i = 0; bar[i].key != 0; i++) {
        if(bar[i].show_on_bar) {
            wattron(command, COLOR_PAIR(1));
            mvwprintw(command, 0, n * win_props.action_length, "%s", bar[i].keyname);
            wattron(command, COLOR_PAIR(2));
            waddstr(command, bar[i].name);
            int len = win_props.action_length - strlen(bar[i].name);
            for(int j = 0; j < len; j++) {
                waddch(command, ' ');
            }
            n++;
        }
    }
    wrefresh(command);
}

int draw_panel(struct panel_t* p) {
    WINDOW* win = p->win;
    wbkgd(win, COLOR_PAIR(3));
    box(win, 0, 0);
    if(p->loaded == FALSE) {
        wattron(win, WA_STANDOUT);
        mvwaddstr(win, 0, 2, "No Database Loaded");
        wattroff(win, WA_STANDOUT);
    } else {
        mvwaddch(p->win, win_props.main_height - 2, 1 + win_props.int_length, ACS_BTEE);
        wattron(win, WA_STANDOUT);
        if(p->path == NULL) {
            mvwaddstr(win, 0, 2, "/ (root)");
        } else {
            const char* name;
            struct path_t* path = p->path;
            mvwaddstr(win, 0, 2, "/");
            while(path != NULL) {
                wprintw(p->win, "%d/", path->id);
                name = path->name;
                path = path->next;
            }
            if(name != NULL) {
                int name_length = (win_props.main_width / 2) - 4 - win_props.int_length * 2;
                mvwprintw(p->win, win_props.main_height - 2, 2, "%s", name);
            }
        }
	wattroff(win, WA_STANDOUT);
    }
    wrefresh(win);
}

int redraw() {
    current_window = NULL;
    draw_command_bar(bar, actions);
    for(int i = 0; i < ARRLEN(panels); i++) {
      draw_panel(&panels[i]);
    }
    for(int i = 0; i < ARRLEN(panels); i++) {
        update_dataview(&panels[i], TRUE);
    }
}

int show_modal_help() {
    int ch;
    int width = win_props.main_width - 6;
    WINDOW *modal = current_window = newwin(win_props.main_height - 4, width, 2, 3);
    const char* title = "Using Inventory Commander";
    box(modal, 0, 0);
    wattron(modal, WA_STANDOUT);
    mvwprintw(modal, 0, (width - strlen(title))/2, title);
    wattroff(modal, WA_STANDOUT);
    for(int i = 0; actions[i].key != 0; i++) {
        wattron(modal, COLOR_PAIR(1));
        mvwaddstr(modal, i + 1, 1, actions[i].keyname);
        waddstr(modal, " (");
        wattron(modal, COLOR_PAIR(4) | WA_BOLD);
        waddstr(modal, actions[i].name);
        wattroff(modal, WA_BOLD);
        wattron(modal, COLOR_PAIR(1));
        waddstr(modal, ") ");
        waddstr(modal, actions[i].description);
    }
    mvwaddstr(modal, 18, 1, "NOTE: Any changes made will be committed immediately.");
    mvwaddstr(modal, 19, 1, "Hit 'F1' to close this help message");
    wrefresh(modal);
    while ((ch = getch()) != KEY_F(1)) { }
    delwin(modal);
    redraw();
}

int show_modal_error(char* message) {
    int ch;
    int width = win_props.main_width - 6;
    WINDOW *modal = newwin(win_props.main_height - 16, width, 8, 3);
    const char* title = "An Error Has Occurred";
    box(modal, 0, 0);
    wbkgd(modal, ' ' | COLOR_PAIR(5));
    wattron(modal, WA_STANDOUT);
    mvwprintw(modal, 0, (width - strlen(title))/2, title);
    wattroff(modal, WA_STANDOUT);
    wattron(modal, COLOR_PAIR(5));
    mvwaddstr(modal, 2, 2, message);
    mvwaddstr(modal, 6, 2, "Hit 'x' to close this error message");
    wattroff(modal, COLOR_PAIR(5));
    wrefresh(modal);
    while ((ch = getch()) != 'x') { }
    delwin(modal);
    if(current_window == NULL) {
        redraw();
    } else {
        redrawwin(current_window);
    }
}

int chomp(char* buffer) {
    int len = strlen(buffer);
    for(int j = len - 1; j >= 0; j--) {
        if(buffer[j-1] != ' ' || j == 0) {
            buffer[j] = 0;
            break;
        }
    }
    len = strlen(buffer);
    return len;
}

int editor_save(struct action_source_t source) {
    int len = 0;
    int y, x;
    int max;
    getyx(source.window, y, x);
    char* blob = malloc(win_props.main_width * (win_props.view_limit + 1));
    blob[0] = 0;
    int width = win_props.main_width;
    char* buffer = malloc(sizeof(char) * width);
    for(int i = win_props.view_limit + 1; i >= 1; i--) {
        mvwinnstr(source.window, i, 1, buffer, width - 2);
        len = chomp(buffer);
        if(len > 0) {
            max = i;
            break;
        }
    }
    for(int i = 1; i <= max; i++) {
        mvwinnstr(source.window, i, 1, buffer, width - 2);
        len = strlen(buffer);
        for(int j = len - 1; j >= 0; j--) {
            if(buffer[j-1] != ' ' || j == 0) {
                buffer[j] = 0;
                break;
            }
        }
        strcat(blob, buffer);
        strcat(blob, "\n");
    }
    free(buffer);
    len = strlen(blob);
    blob[len - 1] = 0;
    wmove(source.window, y, x);
    wrefresh(source.window);

    sqlite3_bind_text(redescribe_stmt, 1, blob, strlen(blob), SQLITE_STATIC);
    sqlite3_bind_int(redescribe_stmt, 2, source.entry->id);
    if (sqlite3_step(redescribe_stmt) != SQLITE_DONE) {
        show_modal_error("Error in saving data to database.");
        return 1;
    }
    sqlite3_reset(redescribe_stmt);

    free(blob);
}

int show_modal_editor() {
    if(panels[panel].loaded == FALSE) {
        show_modal_error("No database loaded.");
        return 1;
    }

    int y, x;
    int ch;
    struct entry_t* entry = current_entry();
    WINDOW *modal = current_window = newwin(win_props.main_height - 1, win_props.main_width, 0, 0);
    const char* title = "Edit Item Description";
    WINDOW *bar = newwin(1, win_props.main_width, win_props.main_height - 1, 0);
    wbkgd(modal, COLOR_PAIR(3));
    box(modal, 0, 0);
    wattron(modal, WA_STANDOUT);
    mvwprintw(modal, 0, (win_props.main_width - strlen(title))/2, title);
    wattroff(modal, WA_STANDOUT);
    draw_command_bar(bar, editor_actions);
    wrefresh(bar);

    sqlite3_bind_int(description_stmt, 1, entry->id);
    if (sqlite3_step(description_stmt) == SQLITE_ROW) {
        const char* txt = sqlite3_column_text(description_stmt, 0);
        wmove(modal, 1, 1);
        if(txt != NULL) {
            for(int i = 0; i < strlen(txt); i++) {
                if(txt[i] == '\n') {
                    getyx(modal, y, x);
                    if(y <= win_props.view_limit) {
                        y++; x = 1;
                        wmove(modal, y, x);
                    }
                } else {
                    waddch(modal, txt[i]);
                }
            }
        }
    }
    sqlite3_reset(description_stmt);

    wmove(modal, 1, 1);
    wrefresh(modal);
    struct action_source_t source = { .window = modal, .entry = entry };
    while ((ch = getch()) != KEY_F(3)) {
        if(ch != ERR) {
            for(int i = 0; i < ARRLEN(editor_actions); i++) {
                if(ch == editor_actions[i].key && editor_actions[i].function != NULL) {
                    editor_actions[i].function(source);
                }
            }
            if(ch >= 32 && ch <= 126) {
                getyx(modal, y, x);
                if(x < win_props.main_width - 2) {
                    winsch(modal, ch);
                    mvwaddch(modal, y, win_props.main_width - 1, ACS_VLINE);
                    wmove(modal, y, x + 1);
                } else {
                    if(y < win_props.main_height - 3) {
                        wmove(modal, y + 1, x);
                        winsertln(modal);
                        mvwaddch(modal, y + 1, win_props.main_width - 1, ACS_VLINE);
                        mvwaddch(modal, y + 1, 0, ACS_VLINE);
                        mvwaddch(modal, win_props.main_height - 2, 0, ACS_LLCORNER);
                        mvwaddch(modal, win_props.main_height - 2, win_props.main_width - 1, ACS_LRCORNER);
                        mvwhline(modal, win_props.main_height - 2, 1, ACS_HLINE, win_props.main_width - 2);
                        char* buffer = malloc(win_props.main_width);
                        mvwinnstr(modal, y, x, buffer, win_props.main_width - x - 1);
                        mvwhline(modal, y, x, ' ', win_props.main_width - x - 1);
                        mvwaddstr(modal, y + 1, 1, buffer);
                        free(buffer);
                        wmove(modal, y + 1, 1);
                        winsch(modal, ch);
                        mvwaddch(modal, y + 1, win_props.main_width - 1, ACS_VLINE);
                        wmove(modal, y + 1, 2);
                    }
                }
            }
            if(ch == '\n') {
                getyx(modal, y, x);
                if(y < win_props.main_height - 3) {
                    wmove(modal, y + 1, x);
                    winsertln(modal);
                    mvwaddch(modal, y + 1, win_props.main_width - 1, ACS_VLINE);
                    mvwaddch(modal, y + 1, 0, ACS_VLINE);
                    mvwaddch(modal, win_props.main_height - 2, 0, ACS_LLCORNER);
                    mvwaddch(modal, win_props.main_height - 2, win_props.main_width - 1, ACS_LRCORNER);
                    mvwhline(modal, win_props.main_height - 2, 1, ACS_HLINE, win_props.main_width - 2);
                    char* buffer = malloc(win_props.main_width);
                    mvwinnstr(modal, y, x, buffer, win_props.main_width - x - 1);
                    mvwhline(modal, y, x, ' ', win_props.main_width - x - 1);
                    mvwaddstr(modal, y + 1, 1, buffer);
                    free(buffer);
                    wmove(modal, y + 1, 1);
                }
            }
            if(ch == KEY_UP) {
                getyx(modal, y, x);
                if(y > 1) {
                    y--;
                    wmove(modal, y, x);
                }
            }
            if(ch == KEY_DOWN) {
                getyx(modal, y, x);
                if(y <= win_props.view_limit) {
                    y++;
                    wmove(modal, y, x);
                }
            }
            if(ch == KEY_LEFT) {
                getyx(modal, y, x);
                if(x > 1) {
                    x--;
                    wmove(modal, y, x);
                }
            }
            if(ch == KEY_RIGHT) {
                getyx(modal, y, x);
                if(x < win_props.main_width - 2) {
                    x++;
                    wmove(modal, y, x);
                }
            }
            if(ch == KEY_HOME) {
                x = 1;
                wmove(modal, y, x);
            }
            if(ch == KEY_END) {
                x = win_props.main_width - 2;
                while((mvwinch(modal, y, x) & A_CHARTEXT) == ' ') {
                    x--;
                }
                if(x < win_props.main_width - 2) {
                    x++;
                    wmove(modal, y, x);
                }
            }
            if(ch == KEY_BACKSPACE) {
                getyx(modal, y, x);
                if(x > 1) {
                    mvwdelch(modal, y, x - 1);
                    mvwinsch(modal, y, win_props.main_width - 2, ' ');
                    wmove(modal, y, x - 1);
                } else if(y > 1) {
                    char* buffer1 = malloc(win_props.main_width);
                    char* buffer2 = malloc(win_props.main_width);
                    mvwinnstr(modal, y, x, buffer1, win_props.main_width - x - 1);
                    mvwinnstr(modal, y - 1, x, buffer2, win_props.main_width - x - 1);
                    int len1 = chomp(buffer1);
                    int len2 = chomp(buffer2);
                    if(len1 + len2 < win_props.main_width - 2) {
                        wmove(modal, y - 1, x - 1);
                        wdeleteln(modal);
                        mvwinsstr(modal, y - 1, 1, buffer2);
                        mvwaddch(modal, y - 1, win_props.main_width - 1, ACS_VLINE);
                        mvwaddch(modal, win_props.main_height - 3, 0, ACS_VLINE);
                        mvwaddch(modal, win_props.main_height - 3, win_props.main_width - 1, ACS_VLINE);
                        mvwhline(modal, win_props.main_height - 3, 1, ' ', win_props.main_width - 2);
                        mvwaddch(modal, win_props.main_height - 2, 0, ACS_LLCORNER);
                        mvwaddch(modal, win_props.main_height - 2, win_props.main_width - 1, ACS_LRCORNER);
                        mvwhline(modal, win_props.main_height - 2, 1, ACS_HLINE, win_props.main_width - 2);
                        wmove(modal, y - 1, strlen(buffer2) + 1);
                    }
                    free(buffer1);
                    free(buffer2);
                }
            }
            if(ch == KEY_DC) {
                getyx(modal, y, x);
                if(x < win_props.main_width - 1) {
                    mvwdelch(modal, y, x);
                    mvwinsch(modal, y, win_props.main_width - 2, ' ');
                    wmove(modal, y, x);
                }
            }
            wrefresh(modal);
        }
    }
    delwin(modal);
    delwin(bar);
    redraw();
}

int show_modal_add() {
    if(panels[panel].loaded == FALSE) {
        show_modal_error("No database loaded.");
        return 1;
    }

    int ch;
    int width = win_props.main_width - 6;
    WINDOW *modal = newwin(win_props.main_height - 16, width, 8, 3);
    const char* title = "Add New Item";
    char buf[BUFF_SIZE];
    box(modal, 0, 0);
    wattron(modal, WA_STANDOUT);
    mvwprintw(modal, 0, (width - strlen(title))/2, title);
    wattroff(modal, WA_STANDOUT);
    mvwaddstr(modal, 1, 1, "ITEM NAME: ");
    mvwaddstr(modal, 7, 1, "NOTE: Any changes made will be committed immediately.");
    wrefresh(modal);
    echo();
    mvwgetnstr(modal, 1, 12, buf, BUFF_SIZE);
    noecho();
    if(panels[panel].path == NULL) {
        sqlite3_bind_null(insert_stmt, 1);
    } else {
        sqlite3_bind_int(insert_stmt, 1, panels[panel].parent);
    }
    sqlite3_bind_text(insert_stmt, 2, buf, strlen(buf), SQLITE_STATIC);
    sqlite3_bind_null(insert_stmt, 3);
    sqlite3_bind_int(insert_stmt, 4, 1);
    if (sqlite3_step(insert_stmt) != SQLITE_DONE) {
        show_modal_error("Could not add item to database.");
        return 1;
    }
    sqlite3_reset(insert_stmt);
    delwin(modal);
    redraw();
}

int show_modal_rename() {
    if(panels[panel].loaded == FALSE) {
        show_modal_error("No database loaded.");
        return 1;
    }

    struct entry_t* entry = current_entry();
    int ch;
    int width = win_props.main_width - 6;
    WINDOW *modal = newwin(win_props.main_height - 16, width, 8, 3);
    const char* title = "Rename Existing Item";
    char* buf = malloc(BUFF_SIZE);
    box(modal, 0, 0);
    wattron(modal, WA_STANDOUT);
    mvwprintw(modal, 0, (width - strlen(title))/2, title);
    wattroff(modal, WA_STANDOUT);
    mvwaddstr(modal, 1, 1, "ITEM NAME: ");
    mvwaddstr(modal, 7, 1, "NOTE: Any changes made will be committed immediately.");
    wrefresh(modal);
    echo();
    mvwgetnstr(modal, 1, 12, buf, BUFF_SIZE);
    noecho();
    sqlite3_bind_text(rename_stmt, 1, buf, strlen(buf), SQLITE_STATIC);
    sqlite3_bind_int(rename_stmt, 2, entry->id);
    if (sqlite3_step(rename_stmt) != SQLITE_DONE) {
        show_modal_error("Could not rename item.");
        return 1;
    }
    sqlite3_reset(rename_stmt);
    entry->name = buf;
    delwin(modal);
    redraw();
}

int show_modal_count() {
    if(panels[panel].loaded == FALSE) {
        show_modal_error("No database loaded.");
        return 1;
    }

    int ch;
    int width = win_props.main_width - 6;
    WINDOW *modal = newwin(win_props.main_height - 16, width, 8, 3);
    const char* title = "Update Item Count";
    box(modal, 0, 0);
    wattron(modal, WA_STANDOUT);
    mvwprintw(modal, 0, (width - strlen(title))/2, title);
    wattroff(modal, WA_STANDOUT);
    mvwaddstr(modal, 6, 1, "NOTE: Any changes made will be committed immediately.");
    mvwaddstr(modal, 7, 1, "Hit 'Enter' to close this window");
    wrefresh(modal);
    int s, cnt = 0;
    if(item_count_stmt != NULL) {
        struct entry_t *entry = current_entry();
        sqlite3_bind_int(item_count_stmt, 1, entry->id);
        while ((s = sqlite3_step(item_count_stmt)) != SQLITE_DONE) {
            if(s == SQLITE_ROW) {
                cnt = sqlite3_column_int(item_count_stmt, 0);
            }
        }
        sqlite3_reset(item_count_stmt);
        mvwprintw(modal, 1, 1, "ITEM COUNT: %d", cnt);
        mvwprintw(modal, 3, 1, "Hit '+' to increment, '-' to decrement");
        mvwprintw(modal, 4, 1, "Hit 'Tab' to enter a new value");
        wrefresh(modal);
        int newvalue = cnt;
        char buf[256];
        while ((ch = getch()) != '\n') {
            if(ch == '+' || ch == '-' || ch == '\t') {
                if(ch == '+') newvalue = newvalue + 1;
                if(ch == '-' && newvalue > 0) newvalue = newvalue - 1;
                if(ch == '\t') {
                    mvwhline(modal, 1, 13, ' ', 10);
                    echo();
                    mvwgetnstr(modal, 1, 13, buf, sizeof(buf));
                    noecho();
                    newvalue = atoi(buf);
                }
                entry->count = newvalue;
                mvwhline(modal, 1, 13, ' ', 10);
                mvwprintw(modal, 1, 13, "%d", newvalue);
                wrefresh(modal);
            }
        }
        sqlite3_bind_int(update_count_stmt, 1, newvalue);
        sqlite3_bind_int(update_count_stmt, 2, entry->id);
        while ((s = sqlite3_step(update_count_stmt)) != SQLITE_DONE) {
        }
        sqlite3_reset(update_count_stmt);
    } else {
        mvwprintw(modal, 1, 1, "NO DATABASE LOADED", cnt);
        wrefresh(modal);
        while ((ch = getch()) != '\n') { }
    }
    delwin(modal);
    redraw();
}

int draw_button_bar(WINDOW* win, int y, int x, struct button_t* buttons, int selected) {
    wmove(win, y, x);
    for(int i = 0; buttons[i].name != NULL; i++) {
        if(i == selected) {
            wattron(win, WA_STANDOUT);
        } else {
            wattroff(win, WA_STANDOUT);
        }
        waddch(win, '[');
        wprintw(win, buttons[i].name);
        waddch(win, ']');
        wattroff(win, WA_STANDOUT);
        waddch(win, ' ');
    }
    wrefresh(win);
}

int show_modal_open() {
    int ch, button = 0;
    int width_diff = 6;
    int height_diff = 20;
    int width = win_props.main_width - width_diff;
    int height = win_props.main_height - height_diff;
    WINDOW *modal = newwin(height, width, height_diff / 2, width_diff / 2);
    const char* title = "Open Database File";
    box(modal, 0, 0);
    wattron(modal, WA_STANDOUT);
    mvwprintw(modal, 0, (width - strlen(title))/2, title);
    wattroff(modal, WA_STANDOUT);
    mvwaddch(modal, 1, 3, '[');
    mvwaddch(modal, 1, width - 4, ']');
    wrefresh(modal);
    char buf[256];
    draw_button_bar(modal, 3, 3, file_open_buttons, button);
    echo();
    mvwgetnstr(modal, 1, 4, buf, sizeof(buf));
    noecho();
    wmove(modal, 3, 3);
    wrefresh(modal);
    while ((ch = getch()) != '\n') {
        if(ch == '\t') {
            button = (button + 1) % (ARRLEN(file_open_buttons) - 1);
            draw_button_bar(modal, 3, 3, file_open_buttons, button);
        }
    }
    if(file_open_buttons[button].function != NULL) {
        file_open_buttons[button].function(buf);
    }
    delwin(modal);
    redraw();
}

int update_searchview() {
    sqlite3_stmt* stmt;
    int i = 0;
    int reload = (search_panel.offset % win_props.view_limit) == 0 || (search_panel.offset % win_props.view_limit) == win_props.view_limit - 1;
    if(reload == TRUE) {
        if(search_panel.type == BY_NAME) {
            stmt = by_name_stmt;
        } else if(search_panel.type == BY_ABOUT) {
            stmt = by_about_stmt;
        }
        sqlite3_bind_text(stmt, 1, search_panel.query, strlen(search_panel.query), SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, win_props.view_limit);
        sqlite3_bind_int(stmt, 3, (search_panel.offset / win_props.view_limit) * win_props.view_limit);
        // main width, minus border, minus 3 int fields, minus 3 field separators
        int s;
        while((s = sqlite3_step(stmt)) != SQLITE_DONE) {
            search_panel.entries[i].id = sqlite3_column_int(stmt, 0);
            search_panel.entries[i].parent = sqlite3_column_int(stmt, 1);
            if(search_panel.entries[i].name != NULL) free((void*)(search_panel.entries[i].name));
            if(search_panel.entries[i].about != NULL) free((void*)(search_panel.entries[i].about));
            int bytes;
            char* buf;
            bytes = sqlite3_column_bytes(stmt, 2);
            if(bytes > 0) {
                buf = malloc(bytes + 1);
                strcpy(buf, sqlite3_column_text(stmt, 2));
                search_panel.entries[i].name = buf;
            } else {
                search_panel.entries[i].name = NULL;
            }
            bytes = sqlite3_column_bytes(stmt, 3);
            if(bytes > 0) {
                buf = malloc(bytes + 1);
                strcpy(buf, sqlite3_column_text(stmt, 3));
                search_panel.entries[i].about = buf;
            } else {
                search_panel.entries[i].about = NULL;
            }
            search_panel.entries[i].count = sqlite3_column_int(stmt, 4);
            i++;
        }
        while ( i < win_props.view_limit ) {
            search_panel.entries[i].id = 0;
            search_panel.entries[i].name = NULL;
            i++;
        }
        sqlite3_reset(stmt);
    }

    i = 0;
    int name_width = win_props.main_width - 2 - (win_props.int_length * 3) - 3;
    mvwaddch(search_panel.win, 0, 1 + win_props.int_length, ACS_TTEE);
    mvwaddch(search_panel.win, 0, 2 + win_props.int_length * 2, ACS_TTEE);
    mvwaddch(search_panel.win, 0, 3 + win_props.int_length * 2 + name_width, ACS_TTEE);
    mvwaddch(search_panel.win, win_props.main_height - 2, 1 + win_props.int_length, ACS_BTEE);
    mvwaddch(search_panel.win, win_props.main_height - 2, 2 + win_props.int_length * 2, ACS_BTEE);
    mvwaddch(search_panel.win, win_props.main_height - 2, 3 + win_props.int_length * 2 + name_width, ACS_BTEE);

    wattron(search_panel.win, WA_BOLD);
    wattron(search_panel.win, COLOR_PAIR(6));
    mvwprintw(search_panel.win, 1, 1, "%s", "ID");
    wattroff(search_panel.win, COLOR_PAIR(6));
    mvwaddch(search_panel.win, 1, 1 + win_props.int_length, ACS_VLINE);
    wattron(search_panel.win, COLOR_PAIR(6));
    mvwprintw(search_panel.win, 1, 2 + win_props.int_length, "%s", "PID");
    wattroff(search_panel.win, COLOR_PAIR(6));
    mvwaddch(search_panel.win, 1, 2 + win_props.int_length * 2, ACS_VLINE);
    wattron(search_panel.win, COLOR_PAIR(6));
    mvwprintw(search_panel.win, 1, 3 + win_props.int_length * 2, "%s", "Item Name");
    wattroff(search_panel.win, COLOR_PAIR(6));
    mvwaddch(search_panel.win, 1, 3 + win_props.int_length * 2 + name_width, ACS_VLINE);
    wattron(search_panel.win, COLOR_PAIR(6));
    mvwprintw(search_panel.win, 1, 4 + win_props.int_length * 2 + name_width, "%s", "Qty");
    wattroff(search_panel.win, COLOR_PAIR(6));
    wattroff(search_panel.win, WA_BOLD);
    while ( search_panel.entries[i].id > 0 ) {
        if(i == ((search_panel.offset)% win_props.view_limit)) {
            search_panel.current = &(search_panel.entries[i]);
            wattron(search_panel.win, WA_STANDOUT);
        }
        //mvwprintw(search_panel.win, i + 2, 1, "%d %d %s %d", id, parent, name, count);
        mvwhline(search_panel.win, i + 2, 1, ' ', win_props.main_width - 2);
        mvwprintw(search_panel.win, i + 2, 1, "%d", search_panel.entries[i].id);
        mvwaddch(search_panel.win, i + 2, 1 + win_props.int_length, ACS_VLINE);
        mvwprintw(search_panel.win, i + 2, 2 + win_props.int_length, "%d", search_panel.entries[i].parent);
        mvwaddch(search_panel.win, i + 2, 2 + win_props.int_length * 2, ACS_VLINE);
        if(search_panel.entries[i].name != NULL) {
            mvwprintw(search_panel.win, i + 2, 3 + win_props.int_length * 2, "%s", search_panel.entries[i].name);
        }
        mvwaddch(search_panel.win, i + 2, 3 + win_props.int_length * 2 + name_width, ACS_VLINE);
        mvwprintw(search_panel.win, i + 2, 4 + win_props.int_length * 2 + name_width, "%d", search_panel.entries[i].count);
        wattroff(search_panel.win, WA_STANDOUT);
        i++;
    }
    while ( i < win_props.view_limit ) {
        mvwhline(search_panel.win, i + 2, 1, ' ', win_props.main_width - 2);
        mvwaddch(search_panel.win, i + 2, 1 + win_props.int_length, ACS_VLINE);
        mvwaddch(search_panel.win, i + 2, 2 + win_props.int_length * 2, ACS_VLINE);
        mvwaddch(search_panel.win, i + 2, 3 + win_props.int_length * 2 + name_width, ACS_VLINE);
        i++;
    }
    //mvwprintw(search_panel.win, 0, 0, "%d", search_panel.offset);
    wrefresh(search_panel.win);
}

int search_offset_dec() {
    if(search_panel.offset > 0) {
        search_panel.offset--;
        update_searchview();
    }
}

int search_offset_inc() {
    if(search_panel.offset < search_panel.count - 1) {
        search_panel.offset++;
        update_searchview();
    }
}

int search_offset_pgdn() {
    if((search_panel.offset / win_props.view_limit) * win_props.view_limit + win_props.view_limit < search_panel.count) {
        search_panel.offset = (search_panel.offset / win_props.view_limit) * win_props.view_limit + win_props.view_limit;
        update_searchview();
    }
}

int search_offset_pgup() {
    if(search_panel.offset >= win_props.view_limit) {
        search_panel.offset = (search_panel.offset / win_props.view_limit) * win_props.view_limit - win_props.view_limit;
        update_searchview();
    }
}

struct path_t* search_build_path(int item) {
    struct path_t* path = malloc(sizeof(struct path_t));
    struct path_t* p = path;
    if(item == 0) return NULL; // item == 0 => root directory
    path->id = item;
    path->name = NULL;
    path->next = NULL;
    path->offset = 0;
    int s;
    while(item != 0) {
        sqlite3_bind_int(item_parent_stmt, 1, item);
        while ((s = sqlite3_step(item_parent_stmt)) != SQLITE_DONE) {
            if(s == SQLITE_ROW) {
                item = sqlite3_column_int(item_parent_stmt, 0);
            }
        }
        sqlite3_reset(item_parent_stmt);
        if(item != 0) {
            p = malloc(sizeof(struct path_t));
            p->next = path;
            p->id = item;
            p->name = NULL;
            p->offset = 0;
            path = p;
        }
    }
    return p;
}

int search_goto_parent() {
    panels[panel].parent = search_panel.current->parent;
    panels[panel].offset = 0;
    panels[panel].path = search_build_path(search_panel.current->parent);
    //fprintf(stderr, "%d\n", panels[panel].parent);
    update_dataview(&panels[panel], TRUE);
}

int search_goto() {
    if(search_panel.current != NULL) {
        panels[panel].parent = search_panel.current->id;
        panels[panel].offset = 0;
        panels[panel].path = search_build_path(search_panel.current->id);
        //fprintf(stderr, "%d\n", panels[panel].parent);
        update_dataview(&panels[panel], TRUE);
    }
}

int item_search(int type, char* name) {
    int ch;
    sqlite3_stmt* stmt;
    struct entry_t* entries = malloc(sizeof(struct entry_t) * win_props.view_limit + 1);
    memset(entries, 0, sizeof(struct entry_t) * win_props.view_limit + 1);

    if(type == BY_NAME) {
        stmt = count_by_name_stmt;
    } else if(type == BY_ABOUT) {
        stmt = count_by_about_stmt;
    }

    search_panel.win = newwin(win_props.main_height - 1, win_props.main_width, 0, 0);
    search_panel.offset = 0;
    sqlite3_bind_text(stmt, 1, name, strlen(name), SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        search_panel.count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_reset(stmt);
    search_panel.type = type;
    search_panel.query = name;
    search_panel.entries = entries;

    const char* title = "Item Search Results";
    WINDOW *bar = newwin(1, win_props.main_width, win_props.main_height - 1, 0);
    wbkgd(search_panel.win, COLOR_PAIR(3));
    box(search_panel.win, 0, 0);
    wattron(search_panel.win, WA_STANDOUT);
    mvwprintw(search_panel.win, 0, (win_props.main_width - strlen(title))/2, title);
    wattroff(search_panel.win, WA_STANDOUT);
    draw_command_bar(bar, search_actions);
    wrefresh(search_panel.win);
    wrefresh(bar);

    update_searchview();
    struct action_source_t source = { .window = search_panel.win };
    while ((ch = getch()) != KEY_F(3) && ch != KEY_F(1) && ch != KEY_F(2)) {
        if(ch != ERR) {
            for(int i = 0; i < ARRLEN(search_actions); i++) {
                if(ch == search_actions[i].key && search_actions[i].function != NULL) {
                    search_actions[i].function(source);
                }
            }
        }
    }
    if(ch != ERR) {
        for(int i = 0; i < ARRLEN(search_actions); i++) {
            if(ch == search_actions[i].key && search_actions[i].function != NULL) {
                search_actions[i].function(source);
            }
        }
    }


    delwin(search_panel.win);
    delwin(bar);
    //redraw();
}

int item_search_by_name(char* name) {
    item_search(BY_NAME, name);
}

int item_search_by_about(char* about) {
    item_search(BY_ABOUT, about);
}

int show_modal_search() {
    if(panels[panel].loaded == FALSE) {
        show_modal_error("No database loaded.");
        return 1;
    }

    int ch, button = 0;
    int width_diff = 6;
    int height_diff = 20;
    int width = win_props.main_width - width_diff;
    int height = win_props.main_height - height_diff;
    WINDOW *modal = newwin(height, width, height_diff / 2, width_diff / 2);
    const char* title = "Search For Item";
    box(modal, 0, 0);
    wattron(modal, WA_STANDOUT);
    mvwprintw(modal, 0, (width - strlen(title))/2, title);
    wattroff(modal, WA_STANDOUT);
    mvwaddch(modal, 1, 3, '[');
    mvwaddch(modal, 1, width - 4, ']');
    wrefresh(modal);
    char buf[256];
    draw_button_bar(modal, 3, 3, item_search_buttons, button);
    echo();
    mvwgetnstr(modal, 1, 4, buf, sizeof(buf));
    noecho();
    wmove(modal, 3, 3);
    wrefresh(modal);
    while ((ch = getch()) != '\n') {
        if(ch == '\t') {
            button = (button + 1) % (ARRLEN(item_search_buttons) - 1);
            draw_button_bar(modal, 3, 3, item_search_buttons, button);
        }
    }
    if(item_search_buttons[button].function != NULL) {
        item_search_buttons[button].function(buf);
    }
    delwin(modal);
    redraw();
}

int open_database(char* filename) {
   char *zErrMsg = 0;
   int rc;
   char *sql;
   if(db != NULL) {
       sqlite3_close(db);
   }
   rc = sqlite3_open(filename, &db);
   if( rc ) {
      show_modal_error("Can't open database.");
      exit(0);
   } else {
      //fprintf(stderr, "Opened database successfully\n");
   }
   /* Create SQL statement */
   sql = "CREATE TABLE item("  \
         "id     INTEGER PRIMARY KEY NOT NULL," \
         "parent INT," \
         "name   TEXT NOT NULL," \
         "about  TEXT," \
         "count  INT NOT NULL );";

   /* Execute SQL statement */
   rc = sqlite3_exec(db, sql, 0, 0, &zErrMsg);
   if( rc != SQLITE_OK ){
      fprintf(stderr, "SQL error: %s\n", zErrMsg);
      sqlite3_free(zErrMsg);
   } else {
      // fprintf(stderr, "Table created successfully\n");
   }

   if ( sqlite3_prepare(
         db,
         "insert into item(parent, name, about, count) values (?,?,?,?)",  // stmt
         -1, // If less than zero, then stmt is read up to the first nul terminator
         &insert_stmt,
         0  // Pointer to unused portion of stmt
       )
       != SQLITE_OK) {
     show_modal_error("Could not prepare insert statement.");
     return 1;
   }

   if ( sqlite3_prepare(
         db,
         "select id,name,about,count from item where parent is ? limit ? offset ?",  // stmt
         -1, // If less than zero, then stmt is read up to the first nul terminator
         &select_stmt,
         0  // Pointer to unused portion of stmt
       )
       != SQLITE_OK) {
     show_modal_error("Could not prepare select statement.");
     return 1;
   }

   if ( sqlite3_prepare(
         db,
         "select about from item where id=?",  // stmt
         -1, // If less than zero, then stmt is read up to the first nul terminator
         &description_stmt,
         0  // Pointer to unused portion of stmt
       )
       != SQLITE_OK) {
     show_modal_error("Could not prepare select description statement.");
     return 1;
   }

   if ( sqlite3_prepare(
         db,
         "update item set parent=? where id=?",  // stmt
         -1, // If less than zero, then stmt is read up to the first nul terminator
         &move_stmt,
         0  // Pointer to unused portion of stmt
       )
       != SQLITE_OK) {
     show_modal_error("Could not prepare get parent statement.");
     return 1;
   }

   if ( sqlite3_prepare(
         db,
         "select count(*) from item where parent is ?",  // stmt
         -1, // If less than zero, then stmt is read up to the first nul terminator
         &count_stmt,
         0  // Pointer to unused portion of stmt
       )
       != SQLITE_OK) {
     show_modal_error("Could not prepare count statement.");
     return 1;
   }

   if ( sqlite3_prepare(
         db,
         "select count from item where id=?",  // stmt
         -1, // If less than zero, then stmt is read up to the first nul terminator
         &item_count_stmt,
         0  // Pointer to unused portion of stmt
       )
       != SQLITE_OK) {
     show_modal_error("Could not prepare item count statement.");
     return 1;
   }

   if ( sqlite3_prepare(
         db,
         "select parent from item where id=?",  // stmt
         -1, // If less than zero, then stmt is read up to the first nul terminator
         &item_parent_stmt,
         0  // Pointer to unused portion of stmt
       )
       != SQLITE_OK) {
     show_modal_error("Could not prepare item parent statement.");
     return 1;
   }

   if ( sqlite3_prepare(
         db,
         "update item set count=? where id=?",  // stmt
         -1, // If less than zero, then stmt is read up to the first nul terminator
         &update_count_stmt,
         0  // Pointer to unused portion of stmt
       )
       != SQLITE_OK) {
     show_modal_error("Could not prepare update count statement.");
     return 1;
   }

   if ( sqlite3_prepare(
         db,
         "update item set name=? where id=?",  // stmt
         -1, // If less than zero, then stmt is read up to the first nul terminator
         &rename_stmt,
         0  // Pointer to unused portion of stmt
       )
       != SQLITE_OK) {
     show_modal_error("Could not prepare rename statement.");
     return 1;
   }

   if ( sqlite3_prepare(
         db,
         "update item set about=? where id=?",  // stmt
         -1, // If less than zero, then stmt is read up to the first nul terminator
         &redescribe_stmt,
         0  // Pointer to unused portion of stmt
       )
       != SQLITE_OK) {
     show_modal_error("Could not prepare redescribe statement.");
     return 1;
   }

   if ( sqlite3_prepare(
         db,
         "delete from item where id=?",  // stmt
         -1, // If less than zero, then stmt is read up to the first nul terminator
         &delete_stmt,
         0  // Pointer to unused portion of stmt
       )
       != SQLITE_OK) {
     show_modal_error("Could not prepare delete statement.");
     return 1;
   }

   if ( sqlite3_prepare(
         db,
         "select count(*) from item where name like ?",  // stmt
         -1, // If less than zero, then stmt is read up to the first nul terminator
         &count_by_name_stmt,
         0  // Pointer to unused portion of stmt
       )
       != SQLITE_OK) {
     show_modal_error("Could not prepare count by name statement.");
     return 1;
   }

   if ( sqlite3_prepare(
         db,
         "select count(*) from item where about like ?",  // stmt
         -1, // If less than zero, then stmt is read up to the first nul terminator
         &count_by_about_stmt,
         0  // Pointer to unused portion of stmt
       )
       != SQLITE_OK) {
     show_modal_error("Could not prepare count by about statement.");
     return 1;
   }

   if ( sqlite3_prepare(
         db,
         "select id,parent,name,about,count from item where name like ? limit ? offset ?",  // stmt
         -1, // If less than zero, then stmt is read up to the first nul terminator
         &by_name_stmt,
         0  // Pointer to unused portion of stmt
       )
       != SQLITE_OK) {
     show_modal_error("Could not prepare search by name statement.");
     return 1;
   }

   if ( sqlite3_prepare(
         db,
         "select id,parent,name,about,count from item where about like ? limit ? offset ?",  // stmt
         -1, // If less than zero, then stmt is read up to the first nul terminator
         &by_about_stmt,
         0  // Pointer to unused portion of stmt
       )
       != SQLITE_OK) {
     show_modal_error("Could not prepare search by about statement.");
     return 1;
   }

   for(int i = 0; i < ARRLEN(panels); i++) {
       panels[i].loaded = TRUE;
   }
}

int main(int argc, char *argv[]) {
    int ch;

    // Activate the screen and enable the keypad
    initscr();
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    refresh();

    setup_window_properties();

    // Left panel, right panel, and main menu bar locations and sizes
    panels[win_props.panel_left].win = newwin(win_props.main_height - 1, win_props.main_width / 2, 0, 0);
    panels[win_props.panel_left].entries = malloc(sizeof(struct entry_t) * win_props.view_limit);
    panels[win_props.panel_right].win = newwin(win_props.main_height - 1, win_props.main_width / 2, 0, win_props.main_width / 2);
    panels[win_props.panel_right].entries = malloc(sizeof(struct entry_t) * win_props.view_limit);
    bar = newwin(1, win_props.main_width, win_props.main_height - 1, 0);

    init_colors_midnight();
    redraw();

    select_window(panels[panel].win);

    struct action_source_t source = { .window = NULL };
    while ((ch = getch()) != KEY_F(10)) {
        if(ch != ERR) {
            for(int i = 0; i < ARRLEN(actions); i++) {
                if(ch == actions[i].key && actions[i].function != NULL) {
                    actions[i].function(source);
                }
            }
        }
    }

    for(int i = 0; i < ARRLEN(panels); i++) {
      delwin(panels[i].win);
    }

    delwin(bar);
    endwin();
    sqlite3_close(db);
    return 0;
}
