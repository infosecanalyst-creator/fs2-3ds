// New 3DS: no standalone server here, so the standalone GUI (3.7.2 uses the mongoose
// web server for it) is replaced by no-ops.
#include "globalincs/pstypes.h"
#include "network/stand_gui.h"

void std_create_gen_dialog(char *title) {}
void std_destroy_gen_dialog() {}
void std_gen_set_text(char *str, int field_num) {}
int std_gen_is_active() { return 0; }
int std_connect_set_connect_count() { return 0; }
void std_connect_set_host_connect_status() {}
void std_connect_add_ip_string(char *string) {}
void std_connect_remove_ip_string(char *string) {}
void std_connect_set_ip_string(char *lookup,char *string) {}
void std_connect_kick_player() {}
void std_connect_update_ping(net_player *p) {}
void std_connect_clear_controls() {}
void std_connect_set_gamename(char *name) {}
void std_connect_handle_name_change() {}
void std_connect_handle_passwd_change() {}
void std_multi_set_standalone_missiontime(float mission_time) {}
void std_multi_set_standalone_mission_name(char *mission_name) {}
void std_multi_setup_goal_tree() {}
void std_multi_add_goals() {}
void std_multi_update_goals() {}
void std_multi_set_framerate(float f) {}
void std_multi_clear_controls() {}
void std_multi_update_netgame_info_controls() {}
void std_pinfo_display_player_info(net_player *p) {}
int std_pinfo_maybe_update_player_info(net_player *p) { return 0; }
void std_pinfo_add_player_list_item(net_player *p) {}
void std_pinfo_remove_player_list_item(net_player *p) {}
void std_pinfo_update_ping(net_player *p) {}
void std_pinfo_clear_controls() {}
void std_gs_add_player(net_player *p) {}
void std_gs_remove_player(net_player *p) {}
void std_gs_send_godstuff_message() {}
void std_gs_set_framerate(float f) {}
void std_gs_clear_controls() {}
void std_debug_set_standalone_state_string(char *str) {}
void std_debug_clear_controls() {}
void std_add_player(net_player *p) {}
int std_remove_player(net_player *p) { return 0; }
void std_set_standalone_fps(float fps) {}
void std_update_player_ping(net_player *p) {}
void std_reset_standalone_gui() {}
void std_multi_standalone_reset_all() {}
void std_init_os() {}
void std_deinit_standalone() {}
void std_init_standalone() {}
void std_do_gui_frame() {}
void std_tracker_notify_login_fail() {}
void std_tracker_login() {}
void std_reset_timestamps() {}
void std_add_chat_text(const char *text,int player_index,int add_id) {}
int std_is_host_passwd() { return 0; }
void std_mutate_sheet() {}
int std_player_is_banned(const char *name) { return 0; }
void std_add_ban(const char *name) {}
