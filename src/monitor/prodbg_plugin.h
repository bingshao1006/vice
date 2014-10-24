#ifndef VICE_PRODBG_PLUGIN_H
#define VICE_PRODBG_PLUGIN_H 

#define PRODBG_STATE_RUNNING 0
#define PRODBG_STATE_TRACE 1
#define PRODBG_STATE_BREAK 2

int prodbg_plugin_state();
int prodbg_plugin_init();
void prodbg_plugin_update();
void prodbg_plugin_set_state(int pc, int a, int x, int y, int sp); 

#endif

