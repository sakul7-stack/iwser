#pragma once

// ---- Application icon ----
#define IDI_APPICON 101

// ---- Application menu command IDs (used with TrackPopupMenu / WM_COMMAND) ----
#define IDM_NEWTAB          1001
#define IDM_CLOSETAB        1002
#define IDM_RELOAD          1003
#define IDM_FOCUS_ADDRESS   1004
#define IDM_BACK            1005
#define IDM_FORWARD         1006
#define IDM_HOME            1007
#define IDM_NEXT_TAB        1008
#define IDM_PREV_TAB        1009

#define IDM_TOGGLE_BOOKMARK 1010
#define IDM_HISTORY         1011
#define IDM_DOWNLOADS       1012
#define IDM_CLEAR_HISTORY   1013
#define IDM_TOGGLE_THEME    1014
#define IDM_EXIT            1015

#define IDM_DUPLICATE_TAB   1016   // duplicate the context-menu / active tab
#define IDM_CLOSE_OTHERS    1017   // close every tab except the context-menu tab
#define IDM_REOPEN_TAB      1018   // reopen the last closed tab (Ctrl+Shift+T)
#define IDM_TOGGLE_RAIL     1019   // collapse/expand the vertical tab rail (Ctrl+B)
#define IDM_LOAD_EXTENSION  1020
#define IDM_ZOOM_IN         1021   // Ctrl++
#define IDM_ZOOM_OUT        1022   // Ctrl+-
#define IDM_ZOOM_RESET      1023   // Ctrl+0
#define IDM_CTX_RELOAD      1024   // reload the context-menu tab
#define IDM_CTX_CLOSETAB    1025   // close the context-menu tab

// Ctrl+1 .. Ctrl+9 -> jump to tab by position (Ctrl+9 = last tab).
#define IDM_TAB_BASE        1100   // 1100 .. 1108
#define IDM_TAB_LAST        1108

// Ranges for dynamically built submenus.
#define IDM_BOOKMARK_BASE   2000   // 2000 .. 2999  -> open bookmark[i]
#define IDM_BOOKMARK_MAX    2999
#define IDM_EXT_TOGGLE_BASE 3000   // 3000 .. 3099  -> enable/disable extension[i]
#define IDM_EXT_TOGGLE_MAX  3099
#define IDM_EXT_REMOVE_BASE 3100   // 3100 .. 3199  -> remove extension[i]
#define IDM_EXT_REMOVE_MAX  3199
