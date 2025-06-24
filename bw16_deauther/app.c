#include <furi.h>
#include <furi_hal.h>
#include <expansion/expansion.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>
#include <gui/modules/widget.h>
#include <gui/modules/variable_item_list.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>


#include <gui/canvas.h>
#include <input/input.h>

#include "bw16_deauther_app_icons.h"
#include "uart_helper.h"

#define DEVICE_BAUDRATE 115200
#define UART_BUFFER_SIZE 256


#define TAG "Deauther"
// Change this to BACKLIGHT_AUTO if you don't want the backlight to be continuously on.
// If you set it to BACKLIGHT_ON, the backlight will be always on.
#define BACKLIGHT_AUTO 1

// Our application menu has 3 items.
typedef enum {
    DeautherSubmenuIndexSetup,
    DeautherSubmenuIndexDeauth,
    DeautherSubmenuIndexAbout,
} DeautherSubmenuIndex;



typedef enum {
    DeautherSubmenuDeauthScan,
    DeautherSubmenuDeauthSelect,
    DeautherSubmenuDeauthAttack,
} DeautherSubmenuDeauth;


// Each view is a screen we show the user.
typedef enum {
    DeautherViewSubmenu, // The menu when the app starts
    DeautherViewTextInput, // Input for configuring text settings
    DeautherViewSetup, // The configuration screen
    DeautherViewDeauth, // The deauth screen
    DeautherViewAbout, // The about screen with directions, link to social channel, etc.
    DeautherViewScan,
    DeautherViewSelect,
    DeautherViewAttack,
    DeautherViewNetwork, // Grouped network submenu
} DeautherView;


#define MAX_LABEL_LEN 64
#define MAX_SELECTED 5
#define MAX_GROUPED 8
#define MAX_MAC_LEN 20

typedef struct {
    ViewDispatcher* view_dispatcher; // Switches between our views
    NotificationApp* notifications; // Used for controlling the backlight
    Submenu* submenu; // The application menu
    TextInput* text_input; // The text input screen
    VariableItemList* wifi_server_status; // The configuration screen

    Submenu* deauth_submenu; // The deauth screen

    Widget* widget_about; // The about screen

    Widget* widget_scan;
    Submenu* submenu_select;
    Widget* widget_attack;

    Submenu* submenu_network; // Grouped network submenu;

    VariableItem* setting_2_item; // The name setting item (so we can update the text)
    char* temp_buffer; // Temporary buffer for text input
    uint32_t temp_buffer_size; // Size of temporary buffer

    UartHelper* uart_helper; // UART helper for communication
    FuriString* uart_message; // Buffer for UART messages

    uint8_t last_wifi_status; // Track last WiFi status value
    uint32_t select_index; // Index for select submenu items

    char* uart_buffer; // Buffer for UART stream processing
    size_t uart_buffer_len; // Length of valid data in uart_buffer
    char last_select_label[MAX_LABEL_LEN]; // Last label for select submenu item
    char** select_labels; // Dynamic array for select submenu labels
    uint8_t* select_selected; // Dynamic array for selection state
    size_t select_capacity; // Number of items allocated
    bool show_hidden_networks; // Toggle for showing hidden networks
    char** select_macs; // Dynamic array for MAC addresses
    uint8_t* select_bands; // Dynamic array for band (0=2.4, 1=5)
    bool select_ready; // Set to true when <iX> is received and all <n...> are processed
} DeautherApp;

typedef struct {
    uint32_t wifi_status_index; // The wifi setting index
} DeautherDeauthModel;

typedef struct {
    char name[MAX_LABEL_LEN];
    size_t count;
    int indexes[MAX_GROUPED];
    char macs[MAX_GROUPED][20];
} NetworkGroup;

// Global/static variable to hold the current group for submenu_network
static NetworkGroup* current_network_group = NULL;

// Global variable to track attack state
static bool g_attack_active = false;

/**
 * @brief      Callback for exiting the application.
 * @details    This function is called when user press back button.  We return VIEW_NONE to
 *            indicate that we want to exit the application.
 * @param      _context  The context - unused
 * @return     next view id
*/
static uint32_t deauther_navigation_exit_callback(void* _context) {
    UNUSED(_context);
    return VIEW_NONE;
}

/**
 * @brief      Callback for returning to submenu.
 * @details    This function is called when user press back button.  We return VIEW_NONE to
 *            indicate that we want to navigate to the submenu.
 * @param      _context  The context - unused
 * @return     next view id
*/
static uint32_t deauther_navigation_submenu_callback(void* _context) {
    UNUSED(_context);
    return DeautherViewSubmenu;
}

static uint32_t deauther_navigation_deauth_callback(void* _context) {
    UNUSED(_context);
    return DeautherViewDeauth;
}

static uint32_t deauther_navigation_select_callback(void* _context) {
    UNUSED(_context);
    return DeautherViewSelect;
}

// Helper: find group index by name
static int find_group(NetworkGroup* groups, size_t group_count, const char* name) {
    for(size_t i = 0; i < group_count; ++i) {
        if(strcmp(groups[i].name, name) == 0) return (int)i;
    }
    return -1;
}

/**
 * @brief      Handle submenu item selection.
 * @details    This function is called when user selects an item from the submenu.
 * @param      context  The context - DeautherApp object.
 * @param      index     The DeautherSubmenuIndex item that was clicked.
*/
static void deauther_submenu_callback(void* context, uint32_t index) {
    DeautherApp* app = (DeautherApp*)context;
    switch(index) {
    case DeautherSubmenuIndexSetup:
        view_dispatcher_switch_to_view(app->view_dispatcher, DeautherViewSetup);
        break;
    case DeautherSubmenuIndexDeauth:
        view_dispatcher_switch_to_view(app->view_dispatcher, DeautherViewDeauth);
        break;
    case DeautherSubmenuIndexAbout:
        view_dispatcher_switch_to_view(app->view_dispatcher, DeautherViewAbout);
        break;
    default:
        break;
    }
}

// Helper to update the Attack label with the selected count or "Stop Attack"
static void deauther_update_attack_label(DeautherApp* app) {
    if(!app->deauth_submenu) return;
    int selected_count = 0;
    if(app->select_selected && app->select_capacity > 0) {
        for(size_t i = 0; i < app->select_capacity; ++i) {
            if(app->select_selected[i]) selected_count++;
        }
    }
    char label[24];
    if(g_attack_active) {
        snprintf(label, sizeof(label), "Stop Attack");
    } else {
        snprintf(label, sizeof(label), "Attack %d/%d", selected_count, MAX_SELECTED);
    }
    submenu_change_item_label(app->deauth_submenu, DeautherSubmenuDeauthAttack, label);
}

// Callback for when a select submenu item is clicked
static void deauther_network_mac_callback(void* context, uint32_t index) {
    DeautherApp* app = (DeautherApp*)context;
    NetworkGroup* group = current_network_group;
    if(group && index < group->count) {
        int real_idx = group->indexes[index];
        if(app->select_labels && app->select_selected && app->select_macs && app->select_bands && (size_t)real_idx < app->select_capacity) {
            char label[MAX_MAC_LEN + 8];
            const char* mac_label = app->select_macs[real_idx];
            uint8_t band = app->select_bands[real_idx];
            // Toggle selection
            if(app->select_selected[real_idx]) {
                if(band == 1) {
                    snprintf(label, sizeof(label), "%s (5)", mac_label);
                    submenu_change_item_label(app->submenu_network, index, label);
                } else {
                    snprintf(label, sizeof(label), "%s", mac_label);
                    submenu_change_item_label(app->submenu_network, index, label);
                }
                app->select_selected[real_idx] = 0;
            } else {
                int selected_count = 0;
                for(size_t i = 0; i < app->select_capacity; ++i) {
                    if(app->select_selected[i]) selected_count++;
                }
                if(selected_count >= MAX_SELECTED) return;
                if(band == 1) {
                    snprintf(label, sizeof(label), "*%s (5)", mac_label);
                } else {
                    snprintf(label, sizeof(label), "*%s", mac_label);
                }
                submenu_change_item_label(app->submenu_network, index, label);
                app->select_selected[real_idx] = 1;
            }

            // --- Update the label in the select submenu as well ---
            // Show * if any MAC in the group is selected
            if(group->count > 0) {
                int group_select_idx = group->indexes[0];
                char group_label[MAX_LABEL_LEN + 8];
                const char* name = group->name;
                // Check if any MAC in the group is selected
                bool any_selected = false;
                for(size_t i = 0; i < group->count; ++i) {
                    int idx = group->indexes[i];
                    if(app->select_selected[idx]) {
                        any_selected = true;
                        break;
                    }
                }
                if(any_selected) {
                    snprintf(group_label, sizeof(group_label), "*%s", name);
                } else {
                    snprintf(group_label, sizeof(group_label), "%s", name);
                }
                submenu_change_item_label(app->submenu_select, group_select_idx, group_label);
            }
            // Update Attack label after any MAC selection change
            deauther_update_attack_label(app);
        }
    }
}




static void deauther_select_item_callback(void* context, uint32_t index) {
    DeautherApp* app = (DeautherApp*)context;
    if(app->select_labels && app->select_selected && index < app->select_capacity) {
        static NetworkGroup groups[32];
        static size_t group_count = 0;
        group_count = 0;
        // Build groups
        for(size_t i = 0; i < app->select_capacity; ++i) {
            if(app->select_labels[i][0] == '\0') continue;
            const char* name = app->select_labels[i];
            int gidx = find_group(groups, group_count, name);
            if(gidx == -1) {
                strncpy(groups[group_count].name, name, MAX_LABEL_LEN);
                groups[group_count].count = 0;
                gidx = (int)group_count++;
            }
            if(groups[gidx].count < MAX_GROUPED) {
                groups[gidx].indexes[groups[gidx].count] = (int)i;
                if(app->select_macs && app->select_macs[i]) {
                    strncpy(groups[gidx].macs[groups[gidx].count], app->select_macs[i], MAX_MAC_LEN);
                } else {
                    strncpy(groups[gidx].macs[groups[gidx].count], "XX:XX:XX:XX:XX:XX", MAX_MAC_LEN);
                }
                groups[gidx].count++;
            }
        }
        const char* sel_name = app->select_labels[index];
        int sel_group = find_group(groups, group_count, sel_name);
        if(sel_group != -1 && groups[sel_group].count > 1) {
            submenu_reset(app->submenu_network);
            submenu_set_header(app->submenu_network, sel_name);
            current_network_group = &groups[sel_group];
            for(size_t i = 0; i < groups[sel_group].count; ++i) {
                int real_idx = groups[sel_group].indexes[i];
                const char* mac_label = groups[sel_group].macs[i];
                char label[MAX_MAC_LEN + 8] = {0};
                uint8_t band = app->select_bands ? app->select_bands[real_idx] : 0;
                if(app->select_selected && app->select_selected[real_idx]) {
                    if(band == 1) {
                        snprintf(label, sizeof(label), "*%s (5)", mac_label);
                    } else {
                        snprintf(label, sizeof(label), "*%s", mac_label);
                    }
                    submenu_add_item(
                        app->submenu_network,
                        label,
                        i,
                        deauther_network_mac_callback,
                        app);
                } else {
                    if(band == 1) {
                        snprintf(label, sizeof(label), "%s (5)", mac_label);
                        submenu_add_item(
                            app->submenu_network,
                            label,
                            i,
                            deauther_network_mac_callback,
                            app);
                    } else {
                        submenu_add_item(
                            app->submenu_network,
                            mac_label,
                            i,
                            deauther_network_mac_callback,
                            app);
                    }
                }
            }
            view_dispatcher_switch_to_view(app->view_dispatcher, DeautherViewNetwork);
            return;
        }
        // Single selection logic
        char label[MAX_LABEL_LEN + 8] = {0};
        const char* current_label = app->select_labels[index];
        uint8_t band = app->select_bands ? app->select_bands[index] : 0;
        if(app->select_selected[index]) {
            if(band == 1) {
                snprintf(label, sizeof(label), "(5) %s", current_label);
                submenu_change_item_label(app->submenu_select, index, label);
            } else {
                submenu_change_item_label(app->submenu_select, index, current_label);
            }
            app->select_selected[index] = 0;
        } else {
            int selected_count = 0;
            for(size_t i = 0; i < app->select_capacity; ++i) {
                if(app->select_selected[i]) selected_count++;
            }
            if(selected_count >= MAX_SELECTED) return;
            if(band == 1) {
                snprintf(label, sizeof(label), "* (5) %s", current_label);
            } else {
                snprintf(label, sizeof(label), "*%s", current_label);
            }
            submenu_change_item_label(app->submenu_select, index, label);
            app->select_selected[index] = 1;
        }
        deauther_update_attack_label(app); // Update Attack label
    }
}


/**
 * @brief      Handle submenu item selection.
 * @details    This function is called when user selects an item from the submenu.
 * @param      context  The context - DeautherApp object.
 * @param      index     The DeautherSubmenuIndex item that was clicked.
*/
static void deauth_submenu_callback(void* context, uint32_t index) {
    DeautherApp* app = (DeautherApp*)context;
    switch(index) {
    case DeautherSubmenuDeauthScan: {
        // Reset select submenu selection state
        if(app->select_labels) {
            for(size_t i = 0; i < app->select_capacity; ++i) free(app->select_labels[i]);
            free(app->select_labels);
            app->select_labels = NULL;
        }
        if(app->select_selected) {
            free(app->select_selected);
            app->select_selected = NULL;
        }
        app->select_capacity = 0;
        // Send a message via UART when scan is selected
        const char* uart_cmd = "<s>";
        size_t uart_cmd_len = strlen(uart_cmd);
        uart_helper_send(app->uart_helper, uart_cmd, uart_cmd_len);
        // Switch to the scan view
        //view_dispatcher_switch_to_view(app->view_dispatcher, DeautherViewScan);
        FURI_LOG_I(TAG, "scan");
        break;
    }
    case DeautherSubmenuDeauthSelect: {
        // Clear previous entries in select submenu
        submenu_reset(app->submenu_select);
        app->select_index = 0;
        // Send a message via UART when select is selected
        const char* uart_cmd = "<g>";
        size_t uart_cmd_len = strlen(uart_cmd);
        uart_helper_send(app->uart_helper, uart_cmd, uart_cmd_len);
        // Do not switch to view or build submenu yet; wait for all <n...> to arrive
        app->select_ready = false;
        // The submenu will be built and view switched in deauther_select_process_uart when all <n...> are received
        FURI_LOG_I(TAG, "select (waiting for networks)");
        break;
    }
    case DeautherSubmenuDeauthAttack: {
        if(!g_attack_active) {
            g_attack_active = true;
            deauther_update_attack_label(app); // Show "Stop Attack" immediately
            // Start attack
            if(app->select_selected && app->select_labels) {
                for(size_t i = 0; i < app->select_capacity; ++i) {
                    if(app->select_selected[i]) {
                        char uart_cmd[16];
                        snprintf(uart_cmd, sizeof(uart_cmd), "<d%zu>", i);
                        uart_helper_send(app->uart_helper, uart_cmd, strlen(uart_cmd));
                        furi_delay_ms(1000); // Add 1 second delay between each send
                    }
                }
            }
            FURI_LOG_I(TAG, "attack started");
        } else if (g_attack_active) {
            // Stop attack
            uart_helper_send(app->uart_helper, "<ds>", 4);
            g_attack_active = false;
            deauther_update_attack_label(app);
            FURI_LOG_I(TAG, "attack stopped");
        } else {
            // If attack is not active, do nothing
            FURI_LOG_I(TAG, "attack already stopped");
        }
        break;
    }
    default:
        break;
    }
}

// Helper to build and show the select submenu after all networks are received
static void deauther_build_select_submenu(DeautherApp* app) {
    submenu_reset(app->submenu_select);
    app->select_index = 0;
    if(app->select_labels && app->select_macs && app->select_bands && app->select_capacity > 0) {
        static NetworkGroup groups[32];
        size_t group_count = 0;
        // Build groups
        for(size_t i = 0; i < app->select_capacity; ++i) {
            if(app->select_labels[i][0] == '\0') continue;
            if(!app->show_hidden_networks && strcmp(app->select_labels[i], "Hidden") == 0) continue;
            const char* name = app->select_labels[i];
            int gidx = find_group(groups, group_count, name);
            if(gidx == -1) {
                strncpy(groups[group_count].name, name, MAX_LABEL_LEN);
                groups[group_count].count = 0;
                gidx = (int)group_count++;
            }
            if(groups[gidx].count < MAX_GROUPED) {
                groups[gidx].indexes[groups[gidx].count] = (int)i;
                if(app->select_macs && app->select_macs[i]) {
                    strncpy(groups[gidx].macs[groups[gidx].count], app->select_macs[i], MAX_MAC_LEN);
                } else {
                    strncpy(groups[gidx].macs[groups[gidx].count], "XX:XX:XX:XX:XX:XX", MAX_MAC_LEN);
                }
                groups[gidx].count++;
            }
        }
        // Add group items
        for(size_t g = 0; g < group_count; ++g) {
            int idx = groups[g].indexes[0];
            char* name = groups[g].name;
            char label[MAX_LABEL_LEN + 8];
            // Show * if any MAC in the group is selected
            bool any_selected = false;
            for(size_t i = 0; i < groups[g].count; ++i) {
                int mac_idx = groups[g].indexes[i];
                if(app->select_selected[mac_idx]) {
                    any_selected = true;
                    break;
                }
            }
            // Determine if this group is a single 5GHz network
            uint8_t band = 0;
            if(app->select_bands && groups[g].count == 1) {
                band = app->select_bands[groups[g].indexes[0]];
            }
            if(any_selected) {
                if(band == 1 && groups[g].count == 1) {
                    snprintf(label, sizeof(label), "* (5) %s", name);
                } else {
                    snprintf(label, sizeof(label), "*%s", name);
                }
            } else {
                if(band == 1 && groups[g].count == 1) {
                    snprintf(label, sizeof(label), "(5) %s", name);
                } else {
                    snprintf(label, sizeof(label), "%s", name);
                }
            }
            submenu_add_item(
                app->submenu_select,
                label,
                idx,
                deauther_select_item_callback,
                app);
        }
    }
    // Switch to the select view
    view_dispatcher_switch_to_view(app->view_dispatcher, DeautherViewSelect);
    FURI_LOG_I(TAG, "select");
}

static void deauther_select_process_uart(FuriString* line, void* context) {
    DeautherApp* app = (DeautherApp*)context;
    const char* str = furi_string_get_cstr(line);
    if(str[0] == '<' && str[2] == '>' && str[1] == 'i') {
        int num = atoi(str + 2);
        submenu_reset(app->submenu_select);
        app->select_index = 0;
        // Free previous arrays
        if(app->select_labels) {
            for(size_t i = 0; i < app->select_capacity; ++i) free(app->select_labels[i]);
            free(app->select_labels);
            app->select_labels = NULL;
        }
        if(app->select_selected) {
            free(app->select_selected);
            app->select_selected = NULL;
        }
        if(app->select_macs) {
            for(size_t i = 0; i < app->select_capacity; ++i) free(app->select_macs[i]);
            free(app->select_macs);
            app->select_macs = NULL;
        }
        if(app->select_bands) {
            free(app->select_bands);
            app->select_bands = NULL;
        }
        app->select_capacity = (num > 0) ? num : 0;
        if(app->select_capacity > 0) {
            app->select_labels = (char**)malloc(app->select_capacity * sizeof(char*));
            app->select_selected = (uint8_t*)calloc(app->select_capacity, sizeof(uint8_t));
            app->select_macs = (char**)malloc(app->select_capacity * sizeof(char*));
            app->select_bands = (uint8_t*)calloc(app->select_capacity, sizeof(uint8_t));
            for(size_t i = 0; i < app->select_capacity; ++i) {
                app->select_labels[i] = (char*)calloc(MAX_LABEL_LEN, sizeof(char));
                app->select_macs[i] = (char*)calloc(MAX_MAC_LEN, sizeof(char));
                app->select_bands[i] = 0;
            }
        }
        // Mark as not ready, and reset received count
        app->select_ready = false;
        app->select_index = 0;
    } else if(str[0] == '<' && str[1] == 'n') {
        char* sep1 = strchr(str, '\x1D');
        char* end = strchr(str, '>');
        if(sep1 && end && sep1 < end) {
            int idx = atoi(str + 2);
            char* sep2 = strchr(sep1 + 1, '\x1D');
            char* sep3 = sep2 ? strchr(sep2 + 1, '\x1D') : NULL;
            char name[MAX_LABEL_LEN];
            char mac[20] = {0};
            uint8_t band = 0;
            if(sep2 && sep3 && sep3 < end) {
                size_t name_len = sep2 - (sep1 + 1);
                if(name_len > MAX_LABEL_LEN - 1) name_len = MAX_LABEL_LEN - 1;
                memcpy(name, sep1 + 1, name_len);
                name[name_len] = '\0';
                size_t mac_len = sep3 - (sep2 + 1);
                if(mac_len > 19) mac_len = 19;
                memcpy(mac, sep2 + 1, mac_len);
                mac[mac_len] = '\0';
                band = (uint8_t)atoi(sep3 + 1);
            } else if(sep2 && sep2 < end) {
                size_t name_len = sep2 - (sep1 + 1);
                if(name_len > MAX_LABEL_LEN - 1) name_len = MAX_LABEL_LEN - 1;
                memcpy(name, sep1 + 1, name_len);
                name[name_len] = '\0';
                size_t mac_len = end - sep2 - 1;
                if(mac_len > 19) mac_len = 19;
                memcpy(mac, sep2 + 1, mac_len);
                mac[mac_len] = '\0';
            } else {
                size_t name_len = end - sep1 - 1;
                if(name_len > MAX_LABEL_LEN - 1) name_len = MAX_LABEL_LEN - 1;
                memcpy(name, sep1 + 1, name_len);
                name[name_len] = '\0';
            }
            // Dynamically grow arrays if needed
            if(idx >= 0 && (size_t)(idx + 1) > app->select_capacity) {
                size_t new_capacity = idx + 1;
                char** new_labels = (char**)realloc(app->select_labels, new_capacity * sizeof(char*));
                uint8_t* new_selected = (uint8_t*)realloc(app->select_selected, new_capacity * sizeof(uint8_t));
                char** new_macs = (char**)realloc(app->select_macs, new_capacity * sizeof(char*));
                uint8_t* new_bands = (uint8_t*)realloc(app->select_bands, new_capacity * sizeof(uint8_t));
                if(new_labels && new_selected && new_macs && new_bands) {
                    for(size_t i = app->select_capacity; i < new_capacity; ++i) {
                        new_labels[i] = (char*)calloc(MAX_LABEL_LEN, sizeof(char));
                        new_selected[i] = 0;
                        new_macs[i] = (char*)calloc(MAX_MAC_LEN, sizeof(char));
                        new_bands[i] = 0;
                    }
                    app->select_labels = new_labels;
                    app->select_selected = new_selected;
                    app->select_macs = new_macs;
                    app->select_bands = new_bands;
                    app->select_capacity = new_capacity;
                } else {
                    // Allocation failed, skip
                    return;
                }
            }
            if(app->select_labels && app->select_selected && app->select_macs && app->select_bands && idx >= 0 && (size_t)idx < app->select_capacity) {
                strncpy(app->select_labels[idx], name, MAX_LABEL_LEN);
                app->select_labels[idx][MAX_LABEL_LEN - 1] = '\0';
                strncpy(app->select_macs[idx], mac, MAX_MAC_LEN);
                app->select_macs[idx][MAX_MAC_LEN - 1] = '\0';
                app->select_bands[idx] = band;
                // Store MAC for grouping (not shown in select, but used in group submenu)
                // Only add to submenu if not hidden or if show_hidden_networks is true
                static NetworkGroup groups[32];
                static size_t group_count = 0;
                int gidx = find_group(groups, group_count, name);
                if(gidx == -1) {
                    strncpy(groups[group_count].name, name, MAX_LABEL_LEN);
                    groups[group_count].count = 0;
                    gidx = (int)group_count++;
                }
                if(groups[gidx].count < MAX_GROUPED) {
                    groups[gidx].indexes[groups[gidx].count] = idx;
                    strncpy(groups[gidx].macs[groups[gidx].count], mac, 20);
                    groups[gidx].count++;
                }
                if(app->show_hidden_networks || strcmp(name, "Hidden") != 0) {
                    // Only add group item if first occurrence
                    if(groups[gidx].count == 1) {
                        char label[MAX_LABEL_LEN + 8];
                        uint8_t band = app->select_bands[idx];
                        if(app->select_selected[idx]) {
                            if(band == 1) {
                                snprintf(label, sizeof(label), "* (5) %s", name);
                            } else {
                                snprintf(label, sizeof(label), "*%s", name);
                            }
                            submenu_add_item(
                                app->submenu_select,
                                label,
                                idx,
                                deauther_select_item_callback,
                                app);
                        } else {
                            if(band == 1) {
                                snprintf(label, sizeof(label), "(5) %s", name);
                            } else {
                                snprintf(label, sizeof(label), "%s", name);
                            }
                            submenu_add_item(
                                app->submenu_select,
                                label,
                                idx,
                                deauther_select_item_callback,
                                app);
                        }
                    }
                }
                // Mark this index as received
                if((uint32_t)idx >= app->select_index) app->select_index = idx + 1;
            }
            // If all expected networks have been received, mark ready and build submenu
            if(app->select_index == app->select_capacity) {
                app->select_ready = true;
                deauther_build_select_submenu(app);
            }
        }
    }
}


/**
 * Wifi Configuration
*/


static const char* setting_1_config_label = "Wifi Status";
static uint8_t setting_1_values[] = {0, 1};
static char* setting_1_names[] = {"Off", "On"};
static void deauther_setting_1_change(VariableItem* item) {
    DeautherApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, setting_1_names[index]);
    // Only send UART message if value changes
    if(app->last_wifi_status != index) {
        const char* uart_cmd = (index == 1) ? "<w1>" : "<w0>";
        size_t uart_cmd_len = strlen(uart_cmd);
        uart_helper_send(app->uart_helper, uart_cmd, uart_cmd_len);
        app->last_wifi_status = index;
    }
}

// Add a variable item for hidden network toggle
static const char* setting_show_hidden_label = "Show Hidden";
static char* setting_show_hidden_names[] = {"No", "Yes"};
static void deauther_setting_show_hidden_change(VariableItem* item) {
    DeautherApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, setting_show_hidden_names[index]);
    app->show_hidden_networks = (index == 1);
}


/**
 * @brief      Allocate the skeleton application.
 * @details    This function allocates the skeleton application resources.
 * @return     DeautherApp object.
*/
static DeautherApp* deauther_app_alloc() {
    DeautherApp* app = (DeautherApp*)malloc(sizeof(DeautherApp));

    Gui* gui = furi_record_open(RECORD_GUI);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);

    FURI_LOG_I(TAG, "init");


    //main screen
    app->submenu = submenu_alloc();
    submenu_add_item(
        app->submenu, "Setup", DeautherSubmenuIndexSetup, deauther_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Deauth", DeautherSubmenuIndexDeauth, deauther_submenu_callback, app);
    submenu_add_item(
        app->submenu, "About", DeautherSubmenuIndexAbout, deauther_submenu_callback, app);
    view_set_previous_callback(submenu_get_view(app->submenu), deauther_navigation_exit_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, DeautherViewSubmenu, submenu_get_view(app->submenu));
    view_dispatcher_switch_to_view(app->view_dispatcher, DeautherViewSubmenu);

    /////////////////// deauth screen
    app->deauth_submenu = submenu_alloc();
    submenu_add_item(
        app->deauth_submenu, "Scan", DeautherSubmenuDeauthScan, deauth_submenu_callback, app);
    submenu_add_item(
        app->deauth_submenu, "Select", DeautherSubmenuDeauthSelect, deauth_submenu_callback, app);
    // Add Attack label with initial counter 0/MAX_SELECTED
    char attack_label[16];
    snprintf(attack_label, sizeof(attack_label), "Attack 0/%d", MAX_SELECTED);
    submenu_add_item(
        app->deauth_submenu, attack_label, DeautherSubmenuDeauthAttack, deauth_submenu_callback, app);
    view_set_previous_callback(submenu_get_view(app->deauth_submenu), deauther_navigation_submenu_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, DeautherViewDeauth, submenu_get_view(app->deauth_submenu));
    /////////////////


    // Scan screen
    app->widget_scan = widget_alloc();
    widget_add_text_scroll_element(
        app->widget_scan,
        0,
        0,
        128,
        64,
        "This is a samp application.\n---\nReplace code and message\nwith your content!\n\nauthor: @codeallnight\nhttps://discord.com/invite/NsjCvqwPAd\nhttps://youtube.com/@MrDerekJamison");
    view_set_previous_callback(
        widget_get_view(app->widget_scan), deauther_navigation_deauth_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, DeautherViewScan, widget_get_view(app->widget_scan));


    // Select screen
    app->submenu_select = submenu_alloc();
    view_set_previous_callback(
        submenu_get_view(app->submenu_select), deauther_navigation_deauth_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, DeautherViewSelect, submenu_get_view(app->submenu_select));
    

    // Grouped network screen
    app->submenu_network = submenu_alloc();
    view_set_previous_callback(
        submenu_get_view(app->submenu_network), deauther_navigation_select_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, DeautherViewNetwork, submenu_get_view(app->submenu_network));

    // Attack screen
    app->widget_attack = widget_alloc();
    widget_add_text_scroll_element(
        app->widget_attack,
        0,
        0,
        128,
        64,
        "This is a sampl application.\n---\nReplace code and message\nwith your content!\n\nauthor: @codeallnight\nhttps://discord.com/invite/NsjCvqwPAd\nhttps://youtube.com/@MrDerekJamison");
    view_set_previous_callback(
        widget_get_view(app->widget_attack), deauther_navigation_deauth_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, DeautherViewAttack, widget_get_view(app->widget_attack));




    //Text input screen
    app->text_input = text_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, DeautherViewTextInput, text_input_get_view(app->text_input));
    app->temp_buffer_size = 32;
    app->temp_buffer = (char*)malloc(app->temp_buffer_size);

    // Setup screen
    
    app->wifi_server_status = variable_item_list_alloc();
    variable_item_list_reset(app->wifi_server_status);
    VariableItem* item = variable_item_list_add(
        app->wifi_server_status,
        setting_1_config_label,
        COUNT_OF(setting_1_values),
        deauther_setting_1_change,
        app);
    uint8_t wifi_status_index = 0;
    variable_item_set_current_value_index(item, wifi_status_index);
    variable_item_set_current_value_text(item, setting_1_names[wifi_status_index]);
    // Add hidden network toggle
    VariableItem* hidden_item = variable_item_list_add(
        app->wifi_server_status,
        setting_show_hidden_label,
        2,
        deauther_setting_show_hidden_change,
        app);
    variable_item_set_current_value_index(hidden_item, 0); // Default to hide hidden
    variable_item_set_current_value_text(hidden_item, setting_show_hidden_names[0]);
    app->show_hidden_networks = false;


    view_set_previous_callback(
        variable_item_list_get_view(app->wifi_server_status),
        deauther_navigation_submenu_callback);
    view_dispatcher_add_view(
        app->view_dispatcher,
        DeautherViewSetup,
        variable_item_list_get_view(app->wifi_server_status));

    // About screen
    app->widget_about = widget_alloc();
    widget_add_text_scroll_element(
        app->widget_about,
        0,
        0,
        128,
        64,
        "This is my 5GHz deauther app for the BW16.\nThe (5) means that its a 5Ghz network\n---\nIt is still a work in progress.\n\n");
    view_set_previous_callback(
        widget_get_view(app->widget_about), deauther_navigation_submenu_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, DeautherViewAbout, widget_get_view(app->widget_about));

    app->notifications = furi_record_open(RECORD_NOTIFICATION);

#ifdef BACKLIGHT_ON
    notification_message(app->notifications, &sequence_display_backlight_enforce_on);
#endif

    // UART helper initialization
    app->uart_helper = uart_helper_alloc();
    uart_helper_set_baud_rate(app->uart_helper, DEVICE_BAUDRATE);
    app->uart_message = furi_string_alloc();

    app->last_wifi_status = 0; // Default to Off
    app->select_index = 0; // Initialize select submenu index
    // UART buffer for select screen
    app->uart_buffer = (char*)malloc(UART_BUFFER_SIZE);
    app->uart_buffer_len = 0;
    // Set up UART line processing for select screen
    uart_helper_set_delimiter(app->uart_helper, '>', true); // Use '>' as delimiter, include it
    uart_helper_set_callback(app->uart_helper, deauther_select_process_uart, app);

    return app;
}

/**
 * @brief      Free the skeleton application.
 * @details    This function frees the skeleton application resources.
 * @param      app  The skeleton application object.
*/
static void deauther_app_free(DeautherApp* app) {
#ifdef BACKLIGHT_ON
    notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
#endif
    furi_record_close(RECORD_NOTIFICATION);

    view_dispatcher_remove_view(app->view_dispatcher, DeautherViewTextInput);
    text_input_free(app->text_input);
    free(app->temp_buffer);
    view_dispatcher_remove_view(app->view_dispatcher, DeautherViewAbout);
    widget_free(app->widget_about);

    view_dispatcher_remove_view(app->view_dispatcher, DeautherViewScan);
    widget_free(app->widget_scan);

    view_dispatcher_remove_view(app->view_dispatcher, DeautherViewSelect);
    submenu_free(app->submenu_select);

    // Clear the network submenu
    view_dispatcher_remove_view(app->view_dispatcher, DeautherViewNetwork);
    submenu_free(app->submenu_network);

    view_dispatcher_remove_view(app->view_dispatcher, DeautherViewAttack);
    widget_free(app->widget_attack);

    view_dispatcher_remove_view(app->view_dispatcher, DeautherViewDeauth);
    submenu_free(app->deauth_submenu);

    view_dispatcher_remove_view(app->view_dispatcher, DeautherViewSetup);
    variable_item_list_free(app->wifi_server_status);

    view_dispatcher_remove_view(app->view_dispatcher, DeautherViewSubmenu);
    submenu_free(app->submenu);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);

    uart_helper_free(app->uart_helper);
    furi_string_free(app->uart_message);
    free(app->uart_buffer);
    if(app->select_labels) {
        for(size_t i = 0; i < app->select_capacity; ++i) free(app->select_labels[i]);
        free(app->select_labels);
    }
    if(app->select_selected) free(app->select_selected);
    if(app->select_macs) {
        for(size_t i = 0; i < app->select_capacity; ++i) free(app->select_macs[i]);
        free(app->select_macs);
    }
    if(app->select_bands) free(app->select_bands);
    free(app);
}



/**
 * @brief      Main function for skeleton application.
 * @details    This function is the entry point for the skeleton application.  It should be defined in
 *           application.fam as the entry_point setting.
 * @param      _p  Input parameter - unused
 * @return     0 - Success
*/
int32_t main_bw16_deauther_app(void* _p) {
    UNUSED(_p);

    DeautherApp* app = deauther_app_alloc();
    view_dispatcher_run(app->view_dispatcher);

    deauther_app_free(app);
    return 0;
}




