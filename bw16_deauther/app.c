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

// Our application menu has 4 items now.
typedef enum {
    DeautherSubmenuIndexSetup,
    DeautherSubmenuIndexDeauth,
    DeautherSubmenuIndexBeacon,  // Add Beacon index
    DeautherSubmenuIndexPortal,
    DeautherSubmenuIndexAbout,
} DeautherSubmenuIndex;



typedef enum {
    DeautherSubmenuDeauthScan,
    DeautherSubmenuDeauthSelect,
    DeautherSubmenuDeauthAttack,
} DeautherSubmenuDeauth;

typedef enum {
    DeautherSubmenuBeaconSetup,
    DeautherSubmenuBeaconModeMenu,
    DeautherSubmenuBeaconAttack,
} DeautherSubmenuBeacon;


typedef enum {
    DeautherSubmenuBeaconModeRandom,
    DeautherSubmenuBeaconModeRickRoll,
    DeautherSubmenuBeaconModeCustom,
} DeautherSubmenuBeaconModeType;


// Each view is a screen we show the user.
typedef enum {
    DeautherViewSubmenu, // The menu when the app starts
    DeautherViewTextInput, // Input for configuring text settings
    DeautherViewSetup, // The configuration screen
    DeautherViewDeauth, // The deauth screen
    DeautherViewBeacon, // The beacon screen
    DeautherViewAbout, // The about screen with directions, link to social channel, etc.
    DeautherViewScan,
    DeautherViewSelect,
    DeautherViewAttack,
    DeautherViewNetwork, // Grouped network submenu
    DeautherViewPortal,  // Add Evil Portal submenu view
    DeautherViewBeaconMode, // Beacon mode submenu
    DeautherViewBeaconSetup, // The configuration screen
} DeautherView;


#define MAX_LABEL_LEN 64
#define MAX_SELECTED 5
#define MAX_MAC_LEN 20

#define SCAN_DURATION_MIN 1000
#define SCAN_DURATION_MAX 20000
#define SCAN_DURATION_STEP 1000
#define SCAN_DURATION_DEFAULT 5000

typedef struct {
    ViewDispatcher* view_dispatcher; // Switches between our views
    NotificationApp* notifications; // Used for controlling the backlight
    Submenu* submenu; // The application menu
    TextInput* text_input; // The text input screen
    VariableItemList* deauther_setup; // The configuration screen

    Submenu* deauth_submenu; // The deauth screen

    Submenu* beacon_submenu; // The beacon screen

    VariableItemList* beacon_setup; // The beacon configuration screen
    Submenu* beacon_mode_submenu; // The beacon mode screen
    Widget* widget_beacon_attack; // The attack item



    Widget* widget_about; // The about screen

    Widget* widget_scan;
    Submenu* submenu_select;
    Widget* widget_attack;

    Submenu* submenu_network; // Grouped network submenu;
    Submenu* submenu_portal;  // Evil Portal submenu

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
    uint32_t scan_duration_ms; // Custom scan duration in ms

    uint8_t portal_index; // Store selected portal index (1=Default, 2=Amazon, 3=Apple)
    uint32_t portal_submenu_index; // Index for portal submenu items
    
    // Beacon mode selection tracking
    uint8_t selected_beacon_mode; // 0=none, 1=random, 2=rickroll, 3=custom
    char custom_beacon_text[64]; // Store custom beacon text
} DeautherApp;

typedef struct {
    uint32_t wifi_status_index; // The wifi setting index
} DeautherDeauthModel;

typedef struct {
    char name[MAX_LABEL_LEN];
    size_t count;
    int* indexes; // dynamically allocated
    char** macs;  // dynamically allocated
} NetworkGroup;

// Global/static variable to hold the current group for submenu_network
static NetworkGroup* current_network_group = NULL;

// Global variable to track attack state
static bool g_attack_active = false;
static bool g_beacon_attack_active = false;

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


static uint32_t deauther_navigation_beacon_callback(void* context) {
    UNUSED(context);
    return DeautherViewBeacon;
}

// Helper to update the Beacon Attack label with the selected mode
static void deauther_update_beacon_attack_label(DeautherApp* app) {
    if(!app->beacon_submenu) return;
    
    char label[32];
    if(g_beacon_attack_active) {
        snprintf(label, sizeof(label), "Stop Attack");
    } else if(app->selected_beacon_mode == 1) {
        snprintf(label, sizeof(label), "Attack - Random");
    } else if(app->selected_beacon_mode == 2) {
        snprintf(label, sizeof(label), "Attack - Rick Roll");
    } else if(app->selected_beacon_mode == 3) {
        snprintf(label, sizeof(label), "Attack - Custom");
    } else {
        snprintf(label, sizeof(label), "Attack");
    }
    submenu_change_item_label(app->beacon_submenu, DeautherSubmenuBeaconAttack, label);
}

/**
 * @brief      Handle text input result for custom beacon.
 * @details    This function is called when user submits text input.
 * @param      context  The context - DeautherApp object.
*/
static void deauther_text_input_callback(void* context) {
    DeautherApp* app = (DeautherApp*)context;
    
    // Store the custom text and mark as selected
    strncpy(app->custom_beacon_text, app->temp_buffer, sizeof(app->custom_beacon_text) - 1);
    app->custom_beacon_text[sizeof(app->custom_beacon_text) - 1] = '\0';
    app->selected_beacon_mode = 3;
    
    // Clear previous selection markers
    submenu_change_item_label(app->beacon_mode_submenu, DeautherSubmenuBeaconModeRandom, "Random");
    submenu_change_item_label(app->beacon_mode_submenu, DeautherSubmenuBeaconModeRickRoll, "Rick Roll");
    submenu_change_item_label(app->beacon_mode_submenu, DeautherSubmenuBeaconModeCustom, "*Custom");
    
    // Update beacon attack label
    deauther_update_beacon_attack_label(app);
    
    FURI_LOG_I(TAG, "custom beacon text stored: %s", app->custom_beacon_text);
    
    // Return to beacon mode submenu
    view_dispatcher_switch_to_view(app->view_dispatcher, DeautherViewBeaconMode);
}

// Helper: find group index by name
static int find_group(NetworkGroup* groups, size_t group_count, const char* name) {
    for(size_t i = 0; i < group_count; ++i) {
        if(strcmp(groups[i].name, name) == 0) return (int)i;
    }
    return -1;
}

// Helper: add index and mac to group, reallocating as needed
static void add_to_group(NetworkGroup* group, int idx, const char* mac) {
    size_t new_count = group->count + 1;
    group->indexes = (int*)realloc(group->indexes, new_count * sizeof(int));
    group->macs = (char**)realloc(group->macs, new_count * sizeof(char*));
    group->indexes[group->count] = idx;
    group->macs[group->count] = strdup(mac ? mac : "XX:XX:XX:XX:XX:XX");
    group->count = new_count;
}

// Helper: free group memory
static void free_group(NetworkGroup* group) {
    if(group->macs) {
        for(size_t i = 0; i < group->count; ++i) {
            free(group->macs[i]);
        }
        free(group->macs);
        group->macs = NULL;
    }
    if(group->indexes) {
        free(group->indexes);
        group->indexes = NULL;
    }
    group->count = 0;
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
    case DeautherSubmenuIndexBeacon:
        view_dispatcher_switch_to_view(app->view_dispatcher, DeautherViewBeacon);
        break;
    case DeautherSubmenuIndexPortal:
        // Clear portal submenu and request current portal list
        submenu_reset(app->submenu_portal);
        app->portal_submenu_index = 0;
        
        view_dispatcher_switch_to_view(app->view_dispatcher, DeautherViewPortal);
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
        // Use dynamic group array
        static NetworkGroup groups[32];
        static size_t group_count = 0;
        // Free previous group allocations
        for(size_t i = 0; i < group_count; ++i) free_group(&groups[i]);
        group_count = 0;
        // Build groups
        for(size_t i = 0; i < app->select_capacity; ++i) {
            if(app->select_labels[i][0] == '\0') continue;
            const char* name = app->select_labels[i];
            int gidx = find_group(groups, group_count, name);
            if(gidx == -1) {
                strncpy(groups[group_count].name, name, MAX_LABEL_LEN);
                groups[group_count].count = 0;
                groups[group_count].indexes = NULL;
                groups[group_count].macs = NULL;
                gidx = (int)group_count++;
            }
            add_to_group(&groups[gidx], (int)i, app->select_macs && app->select_macs[i] ? app->select_macs[i] : NULL);
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
        // Send a message via UART when scan is selected using hex delimiters
        char uart_cmd[16];
        if(app->scan_duration_ms > 0) {
            snprintf(uart_cmd, sizeof(uart_cmd), "\x02s%lu\x03", (unsigned long)app->scan_duration_ms);
        } else {
            uart_helper_send(app->uart_helper, "\x02", 1);
            uart_helper_send(app->uart_helper, "s", 1);
            uart_helper_send(app->uart_helper, "\x03", 1);
            FURI_LOG_I(TAG, "scan");
            break;
        }
        size_t uart_cmd_len = strlen(uart_cmd);
        uart_helper_send(app->uart_helper, uart_cmd, uart_cmd_len);
        FURI_LOG_I(TAG, "scan");
        break;
    }
    case DeautherSubmenuDeauthSelect: {
        // Clear previous entries in select submenu
        submenu_reset(app->submenu_select);
        app->select_index = 0;
        // Send a message via UART when select is selected using hex delimiters
        uart_helper_send(app->uart_helper, "\x02", 1);
        uart_helper_send(app->uart_helper, "g", 1);
        uart_helper_send(app->uart_helper, "\x03", 1);
        // Do not switch to view or build submenu yet; wait for all networks to arrive
        app->select_ready = false;
        FURI_LOG_I(TAG, "select (waiting for networks)");
        break;
    }
    case DeautherSubmenuDeauthAttack: {
        if(!g_attack_active) {
            g_attack_active = true;
            deauther_update_attack_label(app);
            // Start attack
            if(app->select_selected && app->select_labels) {
                for(size_t i = 0; i < app->select_capacity; ++i) {
                    if(app->select_selected[i]) {
                        uart_helper_send(app->uart_helper, "\x02", 1);
                        uart_helper_send(app->uart_helper, "d", 1);
                        char index_str[16];
                        snprintf(index_str, sizeof(index_str), "%02zu-00", i);
                        uart_helper_send(app->uart_helper, index_str, strlen(index_str));
                        uart_helper_send(app->uart_helper, "\x03", 1);
                        furi_delay_ms(1000);
                    }
                }
            }
            FURI_LOG_I(TAG, "attack started");
        } else if (g_attack_active) {
            // Stop attack
            uart_helper_send(app->uart_helper, "\x02", 1);
            uart_helper_send(app->uart_helper, "d", 1);
            uart_helper_send(app->uart_helper, "s", 1);
            uart_helper_send(app->uart_helper, "\x03", 1);
            g_attack_active = false;
            deauther_update_attack_label(app);
            FURI_LOG_I(TAG, "attack stopped");
        } else {
            FURI_LOG_I(TAG, "attack already stopped");
        }
        break;
    }
    default:
        break;
    }
}


static void beacon_mode_submenu_callback(void* context, uint32_t index){
    DeautherApp* app = (DeautherApp*)context;
    
    switch(index) {
        case DeautherSubmenuBeaconModeRandom: {
            if(app->selected_beacon_mode == 1) {
                // Deselect if already selected
                app->selected_beacon_mode = 0;
                submenu_change_item_label(app->beacon_mode_submenu, DeautherSubmenuBeaconModeRandom, "Random");
                FURI_LOG_I(TAG, "beacon mode: random deselected");
            } else {
                // Clear previous selection markers
                submenu_change_item_label(app->beacon_mode_submenu, DeautherSubmenuBeaconModeRandom, "Random");
                submenu_change_item_label(app->beacon_mode_submenu, DeautherSubmenuBeaconModeRickRoll, "Rick Roll");
                submenu_change_item_label(app->beacon_mode_submenu, DeautherSubmenuBeaconModeCustom, "Custom");
                
                // Select random mode
                submenu_change_item_label(app->beacon_mode_submenu, DeautherSubmenuBeaconModeRandom, "*Random");
                app->selected_beacon_mode = 1;
                FURI_LOG_I(TAG, "beacon mode: random selected");
            }
            deauther_update_beacon_attack_label(app);
            break;
        }
        case DeautherSubmenuBeaconModeRickRoll: {
            if(app->selected_beacon_mode == 2) {
                // Deselect if already selected
                app->selected_beacon_mode = 0;
                submenu_change_item_label(app->beacon_mode_submenu, DeautherSubmenuBeaconModeRickRoll, "Rick Roll");
                FURI_LOG_I(TAG, "beacon mode: rick roll deselected");
            } else {
                // Clear previous selection markers
                submenu_change_item_label(app->beacon_mode_submenu, DeautherSubmenuBeaconModeRandom, "Random");
                submenu_change_item_label(app->beacon_mode_submenu, DeautherSubmenuBeaconModeRickRoll, "Rick Roll");
                submenu_change_item_label(app->beacon_mode_submenu, DeautherSubmenuBeaconModeCustom, "Custom");
                
                // Select rick roll mode
                submenu_change_item_label(app->beacon_mode_submenu, DeautherSubmenuBeaconModeRickRoll, "*Rick Roll");
                app->selected_beacon_mode = 2;
                FURI_LOG_I(TAG, "beacon mode: rick roll selected");
            }
            deauther_update_beacon_attack_label(app);
            break;
        }
        case DeautherSubmenuBeaconModeCustom: {
            if(app->selected_beacon_mode == 3) {
                // Deselect if already selected
                app->selected_beacon_mode = 0;
                submenu_change_item_label(app->beacon_mode_submenu, DeautherSubmenuBeaconModeCustom, "Custom");
                deauther_update_beacon_attack_label(app);
                FURI_LOG_I(TAG, "beacon mode: custom deselected");
            } else {
                // Set up text input for custom beacon
                text_input_reset(app->text_input);
                text_input_set_header_text(app->text_input, "Enter custom beacon text");
                text_input_set_result_callback(
                    app->text_input,
                    deauther_text_input_callback,
                    app,
                    app->temp_buffer,
                    app->temp_buffer_size,
                    true); // Clear default text
                
                // Switch to text input view
                view_dispatcher_switch_to_view(app->view_dispatcher, DeautherViewTextInput);
                FURI_LOG_I(TAG, "beacon custom - text input");
            }
            break;
        }
    }
}

static void beacon_submenu_callback(void* context, uint32_t index){
    DeautherApp* app = (DeautherApp*)context;
    switch(index){
        case DeautherSubmenuBeaconSetup: {
            view_dispatcher_switch_to_view(app->view_dispatcher, DeautherViewBeaconSetup);
            break;
        }
        case DeautherSubmenuBeaconModeMenu: {
            view_dispatcher_switch_to_view(app->view_dispatcher, DeautherViewBeaconMode);
            break;
        }
        case DeautherSubmenuBeaconAttack: {
            if(!g_beacon_attack_active) {
                // Start beacon attack
                if(app->selected_beacon_mode == 1) {
                    // Random mode
                    uart_helper_send(app->uart_helper, "\x02", 1);
                    uart_helper_send(app->uart_helper, "b", 1);
                    uart_helper_send(app->uart_helper, "r", 1);
                    uart_helper_send(app->uart_helper, "\x03", 1);
                    FURI_LOG_I(TAG, "beacon attack: random started");
                } else if(app->selected_beacon_mode == 2) {
                    // Rick Roll mode
                    uart_helper_send(app->uart_helper, "\x02", 1);
                    uart_helper_send(app->uart_helper, "b", 1);
                    uart_helper_send(app->uart_helper, "k", 1);
                    uart_helper_send(app->uart_helper, "\x03", 1);
                    FURI_LOG_I(TAG, "beacon attack: rick roll started");
                } else if(app->selected_beacon_mode == 3) {
                    // Custom mode
                    uart_helper_send(app->uart_helper, "\x02", 1);
                    uart_helper_send(app->uart_helper, "b", 1);
                    uart_helper_send(app->uart_helper, "c", 1);
                    uart_helper_send(app->uart_helper, app->custom_beacon_text, strlen(app->custom_beacon_text));
                    uart_helper_send(app->uart_helper, "\x03", 1);
                    FURI_LOG_I(TAG, "beacon attack: custom (%s) started", app->custom_beacon_text);
                } else {
                    // No mode selected, do nothing
                    FURI_LOG_W(TAG, "beacon attack: no mode selected");
                    break;
                }
                g_beacon_attack_active = true;
                deauther_update_beacon_attack_label(app);
            } else {
                // Stop beacon attack
                uart_helper_send(app->uart_helper, "\x02", 1);
                uart_helper_send(app->uart_helper, "b", 1);
                uart_helper_send(app->uart_helper, "s", 1);
                uart_helper_send(app->uart_helper, "\x03", 1);
                g_beacon_attack_active = false;
                deauther_update_beacon_attack_label(app);
                FURI_LOG_I(TAG, "beacon attack: stopped");
            }
            break;
        }
    }
}

// Helper to build and show the select submenu after all networks are received
static void deauther_build_select_submenu(DeautherApp* app) {
    submenu_reset(app->submenu_select);
    app->select_index = 0;
    if(app->select_labels && app->select_macs && app->select_bands && app->select_capacity > 0) {
        // Use dynamic group array
        static NetworkGroup groups[32];
        size_t group_count = 0;
        // Free previous group allocations
        for(size_t i = 0; i < group_count; ++i) free_group(&groups[i]);
        group_count = 0;
        // Build groups
        for(size_t i = 0; i < app->select_capacity; ++i) {
            if(app->select_labels[i][0] == '\0') continue;
            if(!app->show_hidden_networks && strcmp(app->select_labels[i], "Hidden") == 0) continue;
            const char* name = app->select_labels[i];
            int gidx = find_group(groups, group_count, name);
            if(gidx == -1) {
                strncpy(groups[group_count].name, name, MAX_LABEL_LEN);
                groups[group_count].count = 0;
                groups[group_count].indexes = NULL;
                groups[group_count].macs = NULL;
                gidx = (int)group_count++;
            }
            add_to_group(&groups[gidx], (int)i, app->select_macs && app->select_macs[i] ? app->select_macs[i] : NULL);
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
                    // snprintf(label, sizeof(label), "* (5) %s", name);
                    snprintf(label, sizeof(label), "* (5) %.*s", (int)(sizeof(label) - 7), name);
                } else {
                    // snprintf(label, sizeof(label), "*%s", name);
                    snprintf(label, sizeof(label), "*%.*s", (int)(sizeof(label) - 2), name);
                }
            } else {
                if(band == 1 && groups[g].count == 1) {
                    // snprintf(label, sizeof(label), "(5) %s", name);
                    snprintf(label, sizeof(label), "(5) %.*s", (int)(sizeof(label) - 5), name);
                } else {
                    // snprintf(label, sizeof(label), "%s", name);
                    snprintf(label, sizeof(label), "%.*s", (int)(sizeof(label) - 1), name);
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
    
    // Debug: Log all received UART data
    FURI_LOG_I(TAG, "UART received: '%s'", str);
    
    // Handle portal credentials: \x02c\x1DUSERNAME\x1DPASSWORD\x03
    // Search for the pattern anywhere in the string, not just at the beginning
    char* start_tag = strchr(str, '\x02');
    if(start_tag && start_tag[1] == 'c') {
        char* end_tag = strchr(start_tag, '\x03');
        if(end_tag) {
            FURI_LOG_I(TAG, "Portal credential line detected");
            // Find the separators (hex 1D)
            char* sep1 = strchr(start_tag + 2, '\x1D'); // First separator after 'c'
            char* sep2 = sep1 ? strchr(sep1 + 1, '\x1D') : NULL; // Second separator
            
            if(sep1 && sep2 && sep2 < end_tag) {
                // Extract username between first and second separator
                size_t username_len = sep2 - (sep1 + 1);
                char username[MAX_LABEL_LEN];
                if(username_len > MAX_LABEL_LEN - 1) username_len = MAX_LABEL_LEN - 1;
                memcpy(username, sep1 + 1, username_len);
                username[username_len] = '\0';
                
                // Extract password between second separator and end
                size_t password_len = end_tag - (sep2 + 1);
                char password[MAX_LABEL_LEN];
                if(password_len > MAX_LABEL_LEN - 1) password_len = MAX_LABEL_LEN - 1;
                memcpy(password, sep2 + 1, password_len);
                password[password_len] = '\0';
                
                // Create formatted labels
                char username_label[MAX_LABEL_LEN];
                char password_label[MAX_LABEL_LEN];
                snprintf(username_label, sizeof(username_label), "User: %.*s", (int)(sizeof(username_label) - 7), username);
                snprintf(password_label, sizeof(password_label), "Pass: %.*s", (int)(sizeof(password_label) - 7), password);
                
                // Add username item to portal submenu
                submenu_add_item(
                    app->submenu_portal,
                    username_label,
                    app->portal_submenu_index++,
                    NULL, // No callback for username
                    app);
                
                // Add password item to portal submenu
                submenu_add_item(
                    app->submenu_portal,
                    password_label,
                    app->portal_submenu_index++,
                    NULL, // No callback for password
                    app);
                
                FURI_LOG_I(TAG, "Added portal credentials - User: %s, Pass: %s", username, password);
            } else {
                FURI_LOG_W(TAG, "Portal credential format invalid - missing separators");
            }
        } else {
            FURI_LOG_W(TAG, "Portal credential format invalid - no end tag");
        }
        return;
    }
    
    // Handle network count: \x02iX\x03
    if(str[0] == '\x02' && str[1] == 'i' && strchr(str, '\x03')) {
        char* end_tag = strchr(str, '\x03');
        if(end_tag) {
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
        }
    } else if(str[0] == '\x02' && str[1] == 'n') {
        // Parse network data: \x02nX\x1D....\x03
        char* sep1 = strchr(str, '\x1D');
        char* end = strchr(str, '\x03');
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
                // Only add to submenu if not hidden or if show_hidden_networks is true
                static NetworkGroup groups[32];
                static size_t group_count = 0;
                // Free previous group allocations
                for(size_t i = 0; i < group_count; ++i) free_group(&groups[i]);
                group_count = 0;
                int gidx = find_group(groups, group_count, name);
                if(gidx == -1) {
                    strncpy(groups[group_count].name, name, MAX_LABEL_LEN);
                    groups[group_count].count = 0;
                    groups[group_count].indexes = NULL;
                    groups[group_count].macs = NULL;
                    gidx = (int)group_count++;
                }
                add_to_group(&groups[gidx], idx, mac);
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


static const char* setting_1_config_label = "Wifi Portal";
static uint8_t setting_1_values[] = {0, 1};
static char* setting_1_names[] = {"Off", "On"};
static void deauther_setting_1_change(VariableItem* item) {
    DeautherApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, setting_1_names[index]);
    // Only send UART message if value changes
    if(app->last_wifi_status != index) {
        if(index == 1) {
            // On: send \x02wX\x03 where X is portal_index
            char uart_cmd[8];
            snprintf(uart_cmd, sizeof(uart_cmd), "\x02w%u\x03", app->portal_index);
            uart_helper_send(app->uart_helper, uart_cmd, strlen(uart_cmd));
        } else {
            // Off: send \x02w0\x03
            uart_helper_send(app->uart_helper, "\x02w0\x03", 4);
        }
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

// --- Scan Duration Setup ---
static const char* setting_scan_duration_label = "Scan Duration";
static char* setting_scan_duration_names[] = {
    "1s", "2s", "3s", "4s", "5s", "6s", "7s", "8s", "9s", "10s",
    "11s", "12s", "13s", "14s", "15s", "16s", "17s", "18s", "19s", "20s"
};
#define SCAN_DURATION_COUNT ((SCAN_DURATION_MAX - SCAN_DURATION_MIN) / SCAN_DURATION_STEP + 1)
static uint32_t setting_scan_duration_values[SCAN_DURATION_COUNT] = {
    1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000,
    11000, 12000, 13000, 14000, 15000, 16000, 17000, 18000, 19000, 20000
};

static void deauther_setting_scan_duration_change(VariableItem* item) {
    DeautherApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, setting_scan_duration_names[index]);
    app->scan_duration_ms = setting_scan_duration_values[index];
}


// --- Portal Setup ---
static const char* setting_portal_label = "Portal";
static char* setting_portal_names[] = {"Default", "Amazon", "Apple"};
#define PORTAL_COUNT 3
static void deauther_setting_portal_change(VariableItem* item) {
    DeautherApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, setting_portal_names[index]);
    
    uint8_t new_portal_index = index + 1; // 1=Default, 2=Amazon, 3=Apple
    
    // If wifi portal is on and portal index is changing, send portal change command
    if(app->last_wifi_status == 1 && app->portal_index != new_portal_index) {
        char uart_cmd[8];
        snprintf(uart_cmd, sizeof(uart_cmd), "\x02p%u\x03", new_portal_index);
        uart_helper_send(app->uart_helper, uart_cmd, strlen(uart_cmd));
    }
    
    app->portal_index = new_portal_index;
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
        app->submenu, "Beacon", DeautherSubmenuIndexBeacon, deauther_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Evil Portal Output", DeautherSubmenuIndexPortal, deauther_submenu_callback, app);
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

    /////////////////// beacon screen
    app->beacon_submenu = submenu_alloc();
    submenu_add_item(
        app->beacon_submenu, "Setup", DeautherSubmenuBeaconSetup, beacon_submenu_callback, app);
    submenu_add_item(
        app->beacon_submenu, "Mode", DeautherSubmenuBeaconModeMenu, beacon_submenu_callback, app);
    submenu_add_item(
        app->beacon_submenu, "Attack", DeautherSubmenuBeaconAttack, beacon_submenu_callback, app);

    view_set_previous_callback(submenu_get_view(app->beacon_submenu), deauther_navigation_submenu_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, DeautherViewBeacon, submenu_get_view(app->beacon_submenu));
    /////////////////


    /////////////////// beacon mode screen
    app->beacon_mode_submenu = submenu_alloc();

    submenu_add_item(
        app->beacon_mode_submenu, "Random", DeautherSubmenuBeaconModeRandom, beacon_mode_submenu_callback, app);
    submenu_add_item(
        app->beacon_mode_submenu, "Rick Roll", DeautherSubmenuBeaconModeRickRoll, beacon_mode_submenu_callback, app);
    /*
    submenu_add_item(
        app->beacon_mode_submenu, "Custom", DeautherSubmenuBeaconModeCustom, beacon_mode_submenu_callback, app);
    */
    view_set_previous_callback(submenu_get_view(app->beacon_mode_submenu), deauther_navigation_beacon_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, DeautherViewBeaconMode, submenu_get_view(app->beacon_mode_submenu));
    /////////////////



    // Beacon setup screen
    app->beacon_setup = variable_item_list_alloc();
    variable_item_list_reset(app->beacon_setup);
    

    view_set_previous_callback(
        variable_item_list_get_view(app->beacon_setup),
        deauther_navigation_beacon_callback);
    view_dispatcher_add_view(
        app->view_dispatcher,
        DeautherViewBeaconSetup,
        variable_item_list_get_view(app->beacon_setup));

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

    // --- Evil Portal submenu ---
    app->submenu_portal = submenu_alloc();
    // Start with empty submenu - items will be added via UART
    app->portal_submenu_index = 0;
    view_set_previous_callback(
        submenu_get_view(app->submenu_portal), deauther_navigation_submenu_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, DeautherViewPortal, submenu_get_view(app->submenu_portal));
    //Text input screen
    app->text_input = text_input_alloc();
    view_set_previous_callback(text_input_get_view(app->text_input), deauther_navigation_beacon_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, DeautherViewTextInput, text_input_get_view(app->text_input));
    app->temp_buffer_size = 32;
    app->temp_buffer = (char*)malloc(app->temp_buffer_size);

    // Setup screen
    app->deauther_setup = variable_item_list_alloc();
    variable_item_list_reset(app->deauther_setup);
    VariableItem* item = variable_item_list_add(
        app->deauther_setup,
        setting_1_config_label,
        COUNT_OF(setting_1_values),
        deauther_setting_1_change,
        app);
    uint8_t wifi_status_index = 0;
    variable_item_set_current_value_index(item, wifi_status_index);
    variable_item_set_current_value_text(item, setting_1_names[wifi_status_index]);
    // Add Portal variable item
    VariableItem* portal_item = variable_item_list_add(
        app->deauther_setup,
        setting_portal_label,
        PORTAL_COUNT,
        deauther_setting_portal_change,
        app);
    variable_item_set_current_value_index(portal_item, 0); // Default to "Default"
    variable_item_set_current_value_text(portal_item, setting_portal_names[0]);
    app->portal_index = 1; // Default

    // Add hidden network toggle
    VariableItem* hidden_item = variable_item_list_add(
        app->deauther_setup,
        setting_show_hidden_label,
        2,
        deauther_setting_show_hidden_change,
        app);
    variable_item_set_current_value_index(hidden_item, 0); // Default to hide hidden
    variable_item_set_current_value_text(hidden_item, setting_show_hidden_names[0]);
    app->show_hidden_networks = false;
    // Add scan duration setting
    VariableItem* scan_duration_item = variable_item_list_add(
        app->deauther_setup,
        setting_scan_duration_label,
        SCAN_DURATION_COUNT,
        deauther_setting_scan_duration_change,
        app);
    // Set default scan duration
    uint8_t scan_duration_index = (SCAN_DURATION_DEFAULT - SCAN_DURATION_MIN) / SCAN_DURATION_STEP;
    variable_item_set_current_value_index(scan_duration_item, scan_duration_index);
    variable_item_set_current_value_text(scan_duration_item, setting_scan_duration_names[scan_duration_index]);
    app->scan_duration_ms = SCAN_DURATION_DEFAULT;


    view_set_previous_callback(
        variable_item_list_get_view(app->deauther_setup),
        deauther_navigation_submenu_callback);
    view_dispatcher_add_view(
        app->view_dispatcher,
        DeautherViewSetup,
        variable_item_list_get_view(app->deauther_setup));

    // About screen
    app->widget_about = widget_alloc();
    widget_add_text_scroll_element(
        app->widget_about,
        0,
        0,
        128,
        64,
        "This is my 5GHz deauther app for the BW16.\nThe (5) means that its a 5Ghz network\nTo use Evil Portal, choose a portal in the Setup menu, turn on Wifi Portal and go to the Evil Portal Output menu to collect login info!\n\n");
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
    app->portal_submenu_index = 0; // Initialize portal submenu index
    app->selected_beacon_mode = 0; // No beacon mode selected initially
    memset(app->custom_beacon_text, 0, sizeof(app->custom_beacon_text)); // Clear custom text
    
    // UART buffer for select screen
    app->uart_buffer = (char*)malloc(UART_BUFFER_SIZE);
    app->uart_buffer_len = 0;
    // Set up UART line processing for select screen
    uart_helper_set_delimiter(app->uart_helper, '\x03', true); // Use hex 03 as delimiter, include it
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
    if(app->notifications) notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
#endif
    if(app->notifications) furi_record_close(RECORD_NOTIFICATION);

    if(app->view_dispatcher && app->text_input) {
        view_dispatcher_remove_view(app->view_dispatcher, DeautherViewTextInput);
        text_input_free(app->text_input);
    }
    if(app->temp_buffer) free(app->temp_buffer);

    if(app->view_dispatcher && app->widget_about) {
        view_dispatcher_remove_view(app->view_dispatcher, DeautherViewAbout);
        widget_free(app->widget_about);
    }

    if(app->view_dispatcher && app->widget_scan) {
        view_dispatcher_remove_view(app->view_dispatcher, DeautherViewScan);
        widget_free(app->widget_scan);
    }

    if(app->view_dispatcher && app->submenu_select) {
        view_dispatcher_remove_view(app->view_dispatcher, DeautherViewSelect);
        submenu_free(app->submenu_select);
    }

    if(app->view_dispatcher && app->submenu_network) {
        view_dispatcher_remove_view(app->view_dispatcher, DeautherViewNetwork);
        submenu_free(app->submenu_network);
    }

    if(app->view_dispatcher && app->submenu_portal) {
        view_dispatcher_remove_view(app->view_dispatcher, DeautherViewPortal);
        submenu_free(app->submenu_portal);
    }

    if(app->view_dispatcher && app->widget_attack) {
        view_dispatcher_remove_view(app->view_dispatcher, DeautherViewAttack);
        widget_free(app->widget_attack);
    }

    if(app->view_dispatcher && app->beacon_mode_submenu) {
        view_dispatcher_remove_view(app->view_dispatcher, DeautherViewBeaconMode);
        submenu_free(app->beacon_mode_submenu);
    }

    if(app->view_dispatcher && app->beacon_setup) {
        view_dispatcher_remove_view(app->view_dispatcher, DeautherViewBeaconSetup);
        variable_item_list_free(app->beacon_setup);
    }

    if(app->view_dispatcher && app->beacon_submenu) {
        view_dispatcher_remove_view(app->view_dispatcher, DeautherViewBeacon);
        submenu_free(app->beacon_submenu);
    }

    if(app->view_dispatcher && app->deauth_submenu) {
        view_dispatcher_remove_view(app->view_dispatcher, DeautherViewDeauth);
        submenu_free(app->deauth_submenu);
    }

    if(app->view_dispatcher && app->deauther_setup) {
        view_dispatcher_remove_view(app->view_dispatcher, DeautherViewSetup);
        variable_item_list_free(app->deauther_setup);
    }

    if(app->view_dispatcher && app->submenu) {
        view_dispatcher_remove_view(app->view_dispatcher, DeautherViewSubmenu);
        submenu_free(app->submenu);
    }

    if(app->view_dispatcher) view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);

    if(app->uart_helper) uart_helper_free(app->uart_helper);
    if(app->uart_message) furi_string_free(app->uart_message);
    if(app->uart_buffer) {
        free(app->uart_buffer);
        app->uart_buffer = NULL;
    }
    // Free select_labels
    if(app->select_labels) {
        for(size_t i = 0; i < app->select_capacity; ++i) {
            if(app->select_labels[i]) {
                free(app->select_labels[i]);
                app->select_labels[i] = NULL;
            }
        }
        free(app->select_labels);
        app->select_labels = NULL;
    }
    // Free select_selected
    if(app->select_selected) {
        free(app->select_selected);
        app->select_selected = NULL;
    }
    // Free select_macs
    if(app->select_macs) {
        for(size_t i = 0; i < app->select_capacity; ++i) {
            if(app->select_macs[i]) {
                free(app->select_macs[i]);
                app->select_macs[i] = NULL;
            }
        }
        free(app->select_macs);
        app->select_macs = NULL;
    }
    // Free select_bands
    if(app->select_bands) {
        free(app->select_bands);
        app->select_bands = NULL;
    }
    app->select_capacity = 0;
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




