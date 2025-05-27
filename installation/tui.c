/*
 * Copyright (c) 2025 Musa/S42
 * FreSH Installation Wizard - TUI Interface
 * MIT License - See LICENSE file for details
 */

#include "installer.h"
#include "tui.h"
#include <conio.h>

#define COLOR_RESET   7
#define COLOR_HEADER  11
#define COLOR_SUCCESS 10
#define COLOR_ERROR   12
#define COLOR_WARNING 14
#define COLOR_ACCENT  13
#define COLOR_INFO    9


static int console_width = 80;
static int console_height = 25;

void update_console_size() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    
    if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
        console_width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        console_height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        
        
        if (console_width < 80) console_width = 80;
        if (console_height < 25) console_height = 25;
    }
}

void clear_screen() {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    DWORD count;
    COORD homeCoords = {0, 0};

    if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
        DWORD cellCount = csbi.dwSize.X * csbi.dwSize.Y;
        
        FillConsoleOutputCharacterA(hConsole, ' ', cellCount, homeCoords, &count);
        FillConsoleOutputAttribute(hConsole, csbi.wAttributes, cellCount, homeCoords, &count);
        SetConsoleCursorPosition(hConsole, homeCoords);
    } else {
        
        system("cls");
    }
    
    update_console_size();
}

void set_console_color(int color) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
}

void gotoxy(int x, int y) {
    COORD coord;
    coord.X = x;
    coord.Y = y;
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);
}

void draw_box(int x, int y, int width, int height) {
    
    if (x + width > console_width) width = console_width - x - 1;
    if (y + height > console_height) height = console_height - y - 1;
    if (width < 3 || height < 3) return; 
    
    
    gotoxy(x, y);
    printf("╔");
    for (int i = 1; i < width - 1; i++) printf("═");
    printf("╗");
    
    
    for (int i = 1; i < height - 1; i++) {
        gotoxy(x, y + i);
        printf("║");
        gotoxy(x + width - 1, y + i);
        printf("║");
    }
    
    
    gotoxy(x, y + height - 1);
    printf("╚");
    for (int i = 1; i < width - 1; i++) printf("═");
    printf("╝");
}

void print_centered_line(const char *text, int y, int start_x, int width) {
    int text_len = strlen(text);
    int padding = (width - text_len) / 2;
    
    gotoxy(start_x, y);
    for (int i = 0; i < width; i++) printf(" "); 
    
    gotoxy(start_x + padding, y);
    
    if (text_len > width - 2) {
        char truncated[256];
        strncpy(truncated, text, width - 5);
        truncated[width - 5] = '\0';
        strcat(truncated, "...");
        printf("%s", truncated);
    } else {
        printf("%s", text);
    }
}

void print_text_wrapped(const char *text, int start_x, int start_y, int width, int max_lines) {
    char buffer[1024];
    strncpy(buffer, text, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    
    char *word = strtok(buffer, " ");
    int current_line = 0;
    int current_pos = 0;
    
    gotoxy(start_x, start_y + current_line);
    
    while (word && current_line < max_lines) {
        int word_len = strlen(word);
        
        if (current_pos + word_len + 1 > width) {
            current_line++;
            current_pos = 0;
            if (current_line < max_lines) {
                gotoxy(start_x, start_y + current_line);
            }
        }
        
        if (current_line < max_lines) {
            if (current_pos > 0) {
                printf(" ");
                current_pos++;
            }
            printf("%s", word);
            current_pos += word_len;
        }
        
        word = strtok(NULL, " ");
    }
}

void show_progress_bar(int progress, int total) {
    const int bar_width = (console_width > 80) ? 50 : 30;
    int filled = (progress * bar_width) / total;
    
    printf("    Progress: [");
    set_console_color(COLOR_SUCCESS);
    for (int i = 0; i < filled; i++) printf("█");
    set_console_color(COLOR_RESET);
    for (int i = filled; i < bar_width; i++) printf("░");
    printf("] %d/%d (%d%%)", progress, total, (progress * 100) / total);
    set_console_color(COLOR_RESET);
}

int get_user_choice(const char *options[], int count) {
    int choice = 0;
    int key;
    int start_y = console_height - count - 4; 
    
    while (1) {
        
        for (int i = 0; i < count + 2; i++) {
            gotoxy(4, start_y + i);
            for (int j = 0; j < console_width - 8; j++) printf(" ");
        }
        
        
        for (int i = 0; i < count; i++) {
            gotoxy(6, start_y + i);
            if (i == choice) {
                set_console_color(COLOR_ACCENT);
                printf("► ");
                set_console_color(COLOR_RESET);
            } else {
                printf("  ");
            }
            
            
            if (strlen(options[i]) > console_width - 12) {
                char truncated[256];
                strncpy(truncated, options[i], console_width - 15);
                truncated[console_width - 15] = '\0';
                strcat(truncated, "...");
                printf("%s", truncated);
            } else {
                printf("%s", options[i]);
            }
        }
        
        
        gotoxy(6, start_y + count + 1);
        set_console_color(COLOR_INFO);
        printf("Use ↑↓ arrows, Enter to select, Esc to exit");
        set_console_color(COLOR_RESET);
        
        key = _getch();
        
        if (key == 224) { 
            key = _getch();
            if (key == 72) { 
                choice = (choice - 1 + count) % count;
            } else if (key == 80) { 
                choice = (choice + 1) % count;
            }
        } else if (key == 13) { 
            
            gotoxy(6, start_y + count + 1);
            for (int i = 0; i < console_width - 12; i++) printf(" ");
            return choice;
        } else if (key == 27) { 
            return -1;
        }
    }
}

void wait_for_keypress() {
    gotoxy(6, console_height - 2);
    set_console_color(COLOR_INFO);
    printf("Press any key to continue...");
    set_console_color(COLOR_RESET);
    _getch();
}

void show_ascii_logo() {
    int logo_width = 63;
    int start_x = (console_width - logo_width) / 2;
    
    set_console_color(COLOR_HEADER);
    gotoxy(start_x, 3);
    printf("╔═══════════════════════════════════════════════════════════╗");
    gotoxy(start_x, 4);
    printf("║  ███████╗██████╗ ███████╗███████╗██╗  ██╗                ║");
    gotoxy(start_x, 5);
    printf("║  ██╔════╝██╔══██╗██╔════╝██╔════╝██║  ██║                ║");
    gotoxy(start_x, 6);
    printf("║  █████╗  ██████╔╝█████╗  ███████╗███████║                ║");
    gotoxy(start_x, 7);
    printf("║  ██╔══╝  ██╔══██╗██╔══╝  ╚════██║██╔══██║                ║");
    gotoxy(start_x, 8);
    printf("║  ██║     ██║  ██║███████╗███████║██║  ██║                ║");
    gotoxy(start_x, 9);
    printf("║  ╚═╝     ╚═╝  ╚═╝╚══════╝╚══════╝╚═╝  ╚═╝                ║");
    gotoxy(start_x, 10);
    printf("╚═══════════════════════════════════════════════════════════╝");
    set_console_color(COLOR_RESET);
    
    set_console_color(COLOR_WARNING);
    print_centered_line("First-Run Experience Shell - Installation Wizard", 12, 0, console_width);
    set_console_color(COLOR_RESET);
    
    set_console_color(COLOR_INFO);
    print_centered_line("Version " FRESH_VERSION, 13, 0, console_width);
    set_console_color(COLOR_RESET);
}

void show_welcome_screen() {
    clear_screen();
    
    
    int box_width = (console_width > 100) ? 90 : console_width - 4;
    int box_height = console_height - 2;
    int box_x = (console_width - box_width) / 2;
    
    draw_box(box_x, 1, box_width, box_height);
    
    show_ascii_logo();
    
    
    int text_start = 15;
    int text_width = box_width - 8;
    
    gotoxy(box_x + 4, text_start);
    printf("Welcome to the FreSH Installation Wizard!");
    
    gotoxy(box_x + 4, text_start + 2);
    print_text_wrapped("This wizard will guide you through the installation of FreSH, "
                      "a fast bash-like shell for Windows with modern features.", 
                      box_x + 4, text_start + 2, text_width, 3);
    
    gotoxy(box_x + 4, text_start + 5);
    set_console_color(COLOR_SUCCESS);
    printf("✓ Key Features:");
    set_console_color(COLOR_RESET);
    
    
    const char *features[] = {
        "• Fast and lightweight bash-like shell",
        "• Git integration with branch/user display", 
        "• Command history with arrow key navigation",
        "• Custom bin directory support",
        "• Multi-format script execution (.sh, .ps1, .bat)",
        "• Windows shell registration"
    };
    
    for (int i = 0; i < 6; i++) {
        gotoxy(box_x + 6, text_start + 7 + i);
        printf("%s", features[i]);
    }
    
    wait_for_keypress();
}

int show_license_agreement() {
    clear_screen();
    
    int box_width = (console_width > 100) ? 90 : console_width - 4;
    int box_height = console_height - 2;
    int box_x = (console_width - box_width) / 2;
    
    draw_box(box_x, 1, box_width, box_height);
    
    gotoxy(box_x + 4, 3);
    set_console_color(COLOR_HEADER);
    printf("LICENSE AGREEMENT");
    set_console_color(COLOR_RESET);
    
    gotoxy(box_x + 4, 4);
    for (int i = 0; i < box_width - 8; i++) printf("═");
    
    gotoxy(box_x + 4, 6);
    set_console_color(COLOR_SUCCESS);
    printf("MIT License");
    set_console_color(COLOR_RESET);
    
    gotoxy(box_x + 4, 8);
    printf("Copyright (c) 2025 Musa/S42");
    
    
    const char *license_text = "Permission is hereby granted, free of charge, to any person "
                              "obtaining a copy of this software and associated documentation "
                              "files (the \"Software\"), to deal in the Software without "
                              "restriction, including without limitation the rights to use, "
                              "copy, modify, merge, publish, distribute, sublicense, and/or "
                              "sell copies of the Software...";
    
    print_text_wrapped(license_text, box_x + 4, 10, box_width - 8, 5);
    
    gotoxy(box_x + 4, 16);
    set_console_color(COLOR_WARNING);
    printf("THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND.");
    set_console_color(COLOR_RESET);
    
    gotoxy(box_x + 4, 18);
    printf("Do you accept the license agreement?");
    
    const char *options[] = {
        "✓ I accept the license agreement",
        "✗ I do not accept (Exit installer)"
    };
    
    int choice = get_user_choice(options, 2);
    return choice == 0 ? 1 : 0;
}

install_type_t choose_installation_type() {
    clear_screen();
    
    int box_width = (console_width > 100) ? 90 : console_width - 4;
    int box_height = console_height - 2;
    int box_x = (console_width - box_width) / 2;
    
    draw_box(box_x, 1, box_width, box_height);
    
    gotoxy(box_x + 4, 3);
    set_console_color(COLOR_HEADER);
    printf("INSTALLATION TYPE");
    set_console_color(COLOR_RESET);
    
    gotoxy(box_x + 4, 4);
    for (int i = 0; i < box_width - 8; i++) printf("═");
    
    gotoxy(box_x + 4, 6);
    printf("Choose how you want to install FreSH:");
    
    
    gotoxy(box_x + 4, 8);
    set_console_color(COLOR_SUCCESS);
    printf("🏠 Current User Installation (Recommended)");
    set_console_color(COLOR_RESET);
    
    const char *user_features[] = {
        "• Installs FreSH for the current user only",
        "• No administrator privileges required",
        "• Location: %LOCALAPPDATA%\\FreSH",
        "• Quick and simple setup"
    };
    
    for (int i = 0; i < 4; i++) {
        gotoxy(box_x + 7, 9 + i);
        printf("%s", user_features[i]);
    }
    
    
    gotoxy(box_x + 4, 14);
    set_console_color(COLOR_WARNING);
    printf("🌐 System-Wide Installation");
    set_console_color(COLOR_RESET);
    
    const char *system_features[] = {
        "• Installs FreSH for all users on this computer",
        "• Requires administrator privileges",
        "• Location: C:\\Program Files\\FreSH",
        "• Choose this for shared computers"
    };
    
    for (int i = 0; i < 4; i++) {
        gotoxy(box_x + 7, 15 + i);
        printf("%s", system_features[i]);
    }
    
    const char *options[] = {
        "🏠 Install for Current User (Recommended)",
        "🌐 Install System-Wide (Requires Admin)"
    };
    
    int choice = get_user_choice(options, 2);
    return choice == 0 ? INSTALL_USER : INSTALL_SYSTEM;
}

void show_installation_progress() {
    clear_screen();
    
    int box_width = (console_width > 100) ? 90 : console_width - 4;
    int box_height = console_height - 2;
    int box_x = (console_width - box_width) / 2;
    
    draw_box(box_x, 1, box_width, box_height);
    
    gotoxy(box_x + 4, 3);
    set_console_color(COLOR_HEADER);
    printf("INSTALLING FreSH");
    set_console_color(COLOR_RESET);
    
    gotoxy(box_x + 4, 4);
    for (int i = 0; i < box_width - 8; i++) printf("═");
    
    gotoxy(box_x + 4, 6);
    set_console_color(COLOR_INFO);
    printf("Please wait while FreSH is being installed...");
    set_console_color(COLOR_RESET);
    
    gotoxy(box_x + 4, 8);
    printf("Installation steps:");
    
    const char *steps[] = {
        "• Copying FreSH executable",
        "• Registering Windows shell",
        "• Adding to system PATH", 
        "• Creating shortcuts",
        "• Setting up uninstaller"
    };
    
    for (int i = 0; i < 5; i++) {
        gotoxy(box_x + 6, 9 + i);
        printf("%s", steps[i]);
    }
}

void show_completion_screen(int success) {
    clear_screen();
    
    int box_width = (console_width > 100) ? 90 : console_width - 4;
    int box_height = console_height - 2;
    int box_x = (console_width - box_width) / 2;
    
    draw_box(box_x, 1, box_width, box_height);
    
    gotoxy(box_x + 4, 3);
    if (success) {
        set_console_color(COLOR_SUCCESS);
        printf("✓ INSTALLATION COMPLETE!");
        set_console_color(COLOR_RESET);
        
        gotoxy(box_x + 4, 4);
        for (int i = 0; i < box_width - 8; i++) printf("═");
        
        gotoxy(box_x + 4, 6);
        printf("🎉 FreSH has been successfully installed!");
        
        gotoxy(box_x + 4, 8);
        set_console_color(COLOR_SUCCESS);
        printf("🚀 Getting Started:");
        set_console_color(COLOR_RESET);
        
        const char *start_steps[] = {
            "• Open any terminal and type 'FreSH' to start",
            "• FreSH is now registered as a Windows shell option",
            "• Check your desktop or Start Menu for shortcuts",
            "• Type 'help' in FreSH to see available commands"
        };
        
        for (int i = 0; i < 4; i++) {
            gotoxy(box_x + 7, 9 + i);
            printf("%s", start_steps[i]);
        }
        
        gotoxy(box_x + 4, 14);
        set_console_color(COLOR_INFO);
        printf("📝 Quick Tips:");
        set_console_color(COLOR_RESET);
        
        const char *tips[] = {
            "• Use arrow keys for command history",
            "• Git info displays automatically in repositories",
            "• Place custom scripts in %USERPROFILE%\\bin"
        };
        
        for (int i = 0; i < 3; i++) {
            gotoxy(box_x + 7, 15 + i);
            printf("%s", tips[i]);
        }
        
        gotoxy(box_x + 4, 19);
        set_console_color(COLOR_SUCCESS);
        printf("Thank you for choosing FreSH!");
        set_console_color(COLOR_RESET);
        
    } else {
        set_console_color(COLOR_ERROR);
        printf("✗ INSTALLATION FAILED");
        set_console_color(COLOR_RESET);
        
        gotoxy(box_x + 4, 4);
        for (int i = 0; i < box_width - 8; i++) printf("═");
        
        gotoxy(box_x + 4, 6);
        set_console_color(COLOR_ERROR);
        printf("❌ The installation could not be completed.");
        set_console_color(COLOR_RESET);
        
        gotoxy(box_x + 4, 8);
        printf("Possible solutions:");
        
        const char *solutions[] = {
            "• Run the installer as Administrator",
            "• Check available disk space",
            "• Temporarily disable antivirus software",
            "• Close other running applications"
        };
        
        for (int i = 0; i < 4; i++) {
            gotoxy(box_x + 6, 9 + i);
            printf("%s", solutions[i]);
        }
        
        gotoxy(box_x + 4, 14);
        printf("If the problem persists, please contact support.");
    }
    
    wait_for_keypress();
}
