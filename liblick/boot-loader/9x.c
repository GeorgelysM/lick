#include <stdlib.h>
#include <string.h>

#include "9x.h"
#include "utils.h"
#include "../drives.h"
#include "../lickdir.h"
#include "../menu/grub4dos.h"
#include "../utils.h"

#define MENU_ITEM "menuitem=LICK, Start Puppy Linux\r\n"
#define LICK_SECTION "\r\n[LICK]\r\nshell="

char *config_sys_path() {
    drive_t *drive = get_windows_drive();
    if(!drive)
        return NULL;
    char *loc = unix_path(concat_strs(2, drive->path, "/config.sys"));
    free_drive(drive);
    return loc;
}

int supported_loader_9x(sys_info_t *info) {
    return info->family == F_WINDOWS_9X && info->version != V_WINDOWS_ME;
}

int check_loader_9x(sys_info_t *info) {
    if(!supported_loader_9x(info)) {
        return 0;
    }

    // load config.sys into a string
    char *config_sys = config_sys_path();
    if(!config_sys)
        return 0;
    FILE *f = fopen(config_sys, "r");
    if(!f) {
        free(config_sys);
        return 0;
    }
    char *config = file_to_str(f);
    fclose(f);

    char *lick = strstr(config, "[LICK]");
    free(config);
    free(config_sys);
    return lick != NULL;
}

char *install_to_config_sys(char *config, lickdir_t *lick) {
    // find [menu] section
    char *start = strstr(config, "[menu]");
    if(start == NULL) {
        // config.sys doesn't have sections
        char *grub_exe = win_path(concat_strs(2, lick->drive, "/pupl.exe"));
        char *ret = concat_strs(7,
                "[menu]\r\nmenuitem=WINDOWS,Start Windows\r\n",
                MENU_ITEM,
                "menudefault=WINDOWS,10\r\nmenucolor=7,0\r\n",
                LICK_SECTION, grub_exe,
                "\r\n\r\n[WINDOWS]", config);
        free(grub_exe);
        return ret;
    }

    // find end of menu (start of next section, or EOF)
    char *end = strchr(start + 1, '[');
    if(end == NULL) {
        end = start;
        while(end[0] != '\0')
            end++;
    }

    // location of menuitem
    char *menuitem = NULL;

    // attempt to find a "nice" place to put menuitem=...
    // aka. after the other menuitems
    char *next_menu = strstr(start, "menuitem=");
    while(next_menu != NULL && next_menu < end) {
        menuitem = next_menu;
        next_menu = strstr(menuitem + 1, "menuitem=");
    }

    if(menuitem != NULL)
        // at last menuitem, move to end of line where the next will go
        menuitem = advance_to_newline(menuitem);
    else
        // otherwise, put right after [menu]
        menuitem = advance_to_newline(start);

    // TODO: timeout?

    char *before = "\n";
    char *after = menuitem + 1;
    if(menuitem[0] == '\0') {
        before = "\r\n";
        after = menuitem;
    }

    char *grub_exe = win_path(concat_strs(2, lick->drive, "/pupl.exe"));

    menuitem[0] = '\0';
    char *ret = concat_strs(8,
            config, before,
            MENU_ITEM, after,
             "\r\n", LICK_SECTION, grub_exe, "\r\n");

    free(grub_exe);
    return ret;

}

// load config.sys
// set [menu] menuitem="PUPPYLINUX, Start Puppy"
// create [PUPPYLINUX] ~> add shell="/path/to/grub.exe"
// save, with attributes
int install_loader_9x(sys_info_t *info, lickdir_t *lick) {
    if(!supported_loader_9x(info)) {
        return 0;
    }

    // load config.sys into a string
    char *config_sys = config_sys_path();
    FILE *f = fopen(config_sys, "r");
    if(!f) {
        free(config_sys);
        if(!lick->err)
            lick->err = strdup2("Could not load config.sys");
        return 0;
    }
    char *config = file_to_str(f);
    fclose(f);

    char *new_config = install_to_config_sys(config, lick);
    free(config);

    backup_file(config_sys);

    attrib_t *attrib = attrib_open(config_sys);
    f = fopen(config_sys, "w");
    if(!f) {
        attrib_save(config_sys, attrib);
        free(new_config);
        free(config_sys);
        if(!lick->err)
            lick->err = strdup2("Could not open config.sys for writing");
        return 0;
    }

    fprintf(f, "%s", new_config);
    fclose(f);
    attrib_save(config_sys, attrib);
    free(new_config);
    free(config_sys);

    char *grub_exe = concat_strs(2, lick->drive, "/pupl.exe");
    char *res_grub_exe = concat_strs(2, lick->res, "/pupl.exe");
    copy_file(grub_exe, res_grub_exe);
    free(grub_exe);
    free(res_grub_exe);
    return 1;
}

int uninstall_loader_9x(sys_info_t *info, lickdir_t *lick) {
    if(!supported_loader_9x(info)) {
        return 0;
    }

    // load config.sys into a string
    char *config_sys = config_sys_path();
    FILE *f = fopen(config_sys, "r");
    if(!f) {
        free(config_sys);
        return 0;
    }
    char *config = file_to_str(f);
    fclose(f);

    char *menuitem = strstr(config, MENU_ITEM);
    if(menuitem == NULL) {
        free(config);
        free(config_sys);
        return 0;
    }

    char *menuitem_end = advance_to_newline(menuitem);
    if(menuitem_end[0] != '\0')
        menuitem_end++;

    char *lick_section = strstr(config, LICK_SECTION);
    if(lick_section == NULL) {
        free(config);
        free(config_sys);
        return 0;
    }
    char *lick_section_end =
        advance_to_newline(lick_section + strlen(LICK_SECTION) - 1);
    if(lick_section_end[0] != '\0')
        lick_section_end++;

    while(lick_section[-4] == '\r'
            && lick_section[-3] == '\n'
            && lick_section[-2] == '\r'
            && lick_section[-1] == '\n')
        lick_section -= 2;

    //sanity check
    if((menuitem < lick_section && lick_section < menuitem_end)
            || (lick_section < menuitem && menuitem < lick_section_end)) {
        free(config);
        free(config_sys);
        return 0;
    }

    menuitem[0] = '\0';
    lick_section[0] = '\0';

    attrib_t *attrib = attrib_open(config_sys);
    f = fopen(config_sys, "w");
    if(!f) {
        attrib_save(config_sys, attrib);
        free(config);
        free(config_sys);
        return 0;
    }

    fprintf(f, "%s", config);
    if(menuitem < lick_section)
        fprintf(f, "%s%s", menuitem_end, lick_section_end);
    else
        fprintf(f, "%s%s", lick_section_end, menuitem_end);
    fclose(f);
    attrib_save(config_sys, attrib);

    char *grub_exe = concat_strs(2, lick->drive, "/pupl.exe");
    unlink_file(grub_exe);
    free(grub_exe);

    free(config);
    free(config_sys);
    return 1;
}

loader_t get_9x() {
    loader_t loader;
    loader.supported = supported_loader_9x;
    loader.check = check_loader_9x;
    loader.install = install_loader_9x;
    loader.uninstall = uninstall_loader_9x;
    loader.get_menu = get_grub4dos;
    return loader;
}
