/* ---- configurable keymaps (v0.2) -------------------------------------- *
 * A flat table of (key, mods, action) bindings. The app binds keys to its own
 * action enum values, then checks timui_keymap_hit each frame. Bindings with
 * non-zero mods use timui_key_pressed_mods (all requested mods must be
 * present); bindings with mods==0 use plain timui_key_pressed. */
TIMUI_API void timui_keymap_bind(TimuiKeymap *km, TimuiKey key, uint32_t mods, int action){
    if(!km || km->count >= (int)(sizeof(km->bindings) / sizeof(km->bindings[0]))) return;
    km->bindings[km->count].key = key;
    km->bindings[km->count].mods = mods;
    km->bindings[km->count].action = action;
    km->count++;
}
TIMUI_API int timui_keymap_hit(TimuiFrame *f, const TimuiKeymap *km, int action){
    int i;
    if(!f || !f->ui || !km) return 0;
    for(i = 0; i < km->count; i++){
        if(km->bindings[i].action == action){
            /* Y4: an action may be bound to several keys; test them all rather
             * than short-circuiting on the first binding (which made the 2nd
             * unreachable). */
            int hit = km->bindings[i].mods
                ? timui_key_pressed_mods(f, km->bindings[i].key, km->bindings[i].mods)
                : timui_key_pressed(f, km->bindings[i].key);
            if(hit) return 1;
        }
    }
    return 0;
}
