#include "ncurses_menu.h"
#include <menu.h>
#include <curses.h>
#include <libconfig.h>
#include <string.h>
#include "ncurses_form.h"
#include "ncurses_shared.h"

void set_keyboard_menu(){
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
}

void create_config_item(config_item_t **config_items, int index, char *name, char *path, int config_type, FIELDTYPE *field_type) {
    config_items[index] = calloc(1, sizeof(config_item_t));
    config_items[index]->name = name;
    config_items[index]->path = path;
    config_items[index]->config_type = config_type;
    config_items[index]->field_type = field_type;
}

void set_item_userptrs(ITEM **items, config_item_t **config_items) {
    size_t i;
    for (i = 0; config_items[i] != NULL; ++i) {
        set_item_userptr(items[i], config_items[i]);
    }
    set_item_userptr(items[i], config_items[i]);
}

void create_main_menu(MENU **menu, config_t *lib_config, config_item_t **config_items, ITEM **items) {
    config_init(lib_config);
    if (!config_read_file(lib_config, CONFIG_PATH)) {
        fprintf(stderr, "%s:%d - %s\n", config_error_file(lib_config), config_error_line(lib_config), config_error_text(lib_config));
        return;
    }
    int port;
    const char *root_dir = NULL;
    const char *index_page = NULL;
    const char *not_found_page = NULL;
    const char *mode = NULL;
    char *port_s = NULL;

    int port_lookup_status = config_lookup_int(lib_config, "port", &port);
    if (port_lookup_status != CONFIG_FALSE) {
        convert_int_to_string(port, &port_s);
    }
    config_lookup_string(lib_config, "mode", &mode);
    config_lookup_string(lib_config, "root_dir", &root_dir);
    config_lookup_string(lib_config, "index_page", &index_page);
    config_lookup_string(lib_config, "not_found_page", &not_found_page);

    create_config_item(config_items, 0, "Mode:", "mode", CONFIG_TYPE_STRING, TYPE_ENUM);
    create_config_item(config_items, 1, "Port:", "port", CONFIG_TYPE_INT, TYPE_INTEGER);
    create_config_item(config_items, 2, "Root Directory:", "root_dir", CONFIG_TYPE_STRING, NULL);
    create_config_item(config_items, 3, "Index Page:", "index_page", CONFIG_TYPE_STRING, NULL);
    create_config_item(config_items, 4, "Not Found Page:", "not_found_page", CONFIG_TYPE_STRING, NULL);
    config_items[5] = NULL;
    items[0] = new_item(config_items[0]->name, strdup(mode != NULL && mode[0] != '\0' ? mode : EMPTY_DESCRIPTION));
    items[1] = new_item(config_items[1]->name, port_s != NULL ? port_s : strdup(EMPTY_DESCRIPTION));
    items[2] = new_item(config_items[2]->name, strdup(root_dir != NULL && root_dir[0] != '\0' ? root_dir : EMPTY_DESCRIPTION));
    items[3] = new_item(config_items[3]->name, strdup(index_page != NULL  && index_page[0] != '\0' ? index_page : EMPTY_DESCRIPTION));
    items[4] = new_item(config_items[4]->name, strdup(not_found_page != NULL  && not_found_page[0] != '\0' ? not_found_page : EMPTY_DESCRIPTION));
    items[5] = NULL;

    set_item_userptrs(items, config_items);
    *menu = new_menu(items);
}

void display_main_menu(MENU *menu, WINDOW *sub) {
    set_keyboard_menu();
    set_menu_mark(menu, " > ");
    set_menu_win(menu, stdscr);
    set_menu_sub(menu, sub);
    post_menu(menu);
}

void update_main_menu(MENU *menu, ITEM *item, char *value) {
    int index = item_index(item);
    ITEM **items = menu_items(menu);
    config_item_t *config_item = item_userptr(item);
    free((char*)item_description(item));
    items[index] = new_item(item_name(item), strdup(value));
    set_item_userptr(items[index], config_item);
    free(item);
    unpost_menu(menu);
    set_menu_items(menu, items);
}

void delete_main_menu(MENU *menu, ITEM **items, config_item_t **config_items) {
    free_menu(menu);
    for (size_t i = 0; items[i] != NULL; ++i) {
        free((char*)item_description(items[i]));
        free_item(items[i]);
    }
    for (size_t i = 0; config_items[i] != NULL; ++i) {
        free(config_items[i]);
    }
}

void process_menu_input(MENU *menu, config_t *lib_config, WINDOW *window) {
    int c;
    while ((c = getch()) != KEY_F(1)) {
        switch (c) {
            case KEY_UP:
                menu_driver(menu, REQ_UP_ITEM);
                break;
            case KEY_DOWN:
                menu_driver(menu, REQ_DOWN_ITEM);
                break;
            case 10: {   // ENTER KEY
                ITEM *current = current_item(menu);
                init_item_form(menu, current, lib_config);
                display_main_menu(menu, window);
                break;
            }
            default:
                break;
        }
    }
}
