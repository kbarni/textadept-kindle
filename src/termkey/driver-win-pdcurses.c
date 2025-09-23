// Copyright 2014-2022 Mitchell. See LICENSE.

#include "termkey.h"
#include "termkey-internal.h"

#include <ctype.h>
#include <curses.h>

static void *new_driver(TermKey *tk, const char *term) { return tk; }
static void free_driver(void *info) {}

static int initialized;

// Lookup tables for keysyms and characters.
static int keysyms[] = {0,TERMKEY_SYM_DOWN,TERMKEY_SYM_UP,TERMKEY_SYM_LEFT,TERMKEY_SYM_RIGHT,TERMKEY_SYM_HOME,TERMKEY_SYM_BACKSPACE,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,TERMKEY_SYM_DELETE,TERMKEY_SYM_INSERT,0,0,0,0,0,0,TERMKEY_SYM_PAGEDOWN,TERMKEY_SYM_PAGEUP,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,TERMKEY_SYM_END};
static int shift_keysyms[] = {TERMKEY_SYM_TAB,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,TERMKEY_SYM_DELETE,0,0,TERMKEY_SYM_END,0,0,0,TERMKEY_SYM_HOME,TERMKEY_SYM_INSERT,0,TERMKEY_SYM_LEFT,0,0,0,0,0,0,0,0,TERMKEY_SYM_RIGHT,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,TERMKEY_SYM_UP,TERMKEY_SYM_DOWN};
static int ctrl_keysyms[] = {TERMKEY_SYM_LEFT,TERMKEY_SYM_RIGHT,TERMKEY_SYM_PAGEUP,TERMKEY_SYM_PAGEDOWN,TERMKEY_SYM_HOME,TERMKEY_SYM_END,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,TERMKEY_SYM_INSERT,0,0,TERMKEY_SYM_UP,TERMKEY_SYM_DOWN,TERMKEY_SYM_TAB,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,TERMKEY_SYM_BACKSPACE,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,TERMKEY_SYM_DELETE,0,TERMKEY_SYM_ENTER};
static int alt_keysyms[] = {TERMKEY_SYM_DELETE,TERMKEY_SYM_INSERT,0,0,0,TERMKEY_SYM_TAB,0,0,TERMKEY_SYM_HOME,TERMKEY_SYM_PAGEUP,TERMKEY_SYM_PAGEDOWN,TERMKEY_SYM_END,TERMKEY_SYM_UP,TERMKEY_SYM_DOWN,TERMKEY_SYM_RIGHT,TERMKEY_SYM_LEFT,TERMKEY_SYM_ENTER,TERMKEY_SYM_ESCAPE,0,0,0,0,0,0,0,0,TERMKEY_SYM_BACKSPACE}; // TERMKEY_SYM_RETURN does not work for me
static int alt_keychars[] = {'-','=',0,0,0,0,0,0,0,0,0,0,'`','[',']',';','\'',',','.','/',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'\\'}; // -=`;,./ do not work for me
static int mousesyms[] = {TERMKEY_MOUSE_RELEASE,TERMKEY_MOUSE_PRESS,0,TERMKEY_MOUSE_PRESS,0,TERMKEY_MOUSE_DRAG,0};

static TermKeyResult peekkey(TermKey *tk, void *info, TermKeyKey *key, int force, size_t *nbytep) {
  if (!initialized) {
    raw(), noecho();
    PDC_save_key_modifiers(TRUE), mouse_set(ALL_MOUSE_EVENTS), mouseinterval(0);
    initialized = TRUE;
  }

  int c = wgetch((WINDOW *)tk->fd), shift = 0, ctrl = 0, alt = 0;
  if (c == ERR) return TERMKEY_RES_EOF;
  int codepoint = c, number = 0, button = 0;
  TermKeyType type = TERMKEY_TYPE_UNICODE;
  TermKeySym sym = TERMKEY_SYM_UNKNOWN;
  TermKeyMouseEvent event = TERMKEY_MOUSE_UNKNOWN;
  if (c != KEY_MOUSE) {
    if (c < 0x20 && c != 8 && c != 9 && c != 13 && c != 27)
      type = TERMKEY_TYPE_UNICODE, codepoint = tolower(c ^ 0x40);
    else if (c == 27)
      type = TERMKEY_TYPE_KEYSYM, sym = TERMKEY_SYM_ESCAPE;
    else if (c >= KEY_MIN && c <= KEY_END && keysyms[c - KEY_MIN])
      type = TERMKEY_TYPE_KEYSYM, sym = keysyms[c - KEY_MIN];
    else if (c >= KEY_F(1) && c <= KEY_F(48))
      type = TERMKEY_TYPE_FUNCTION, number = (c - KEY_F(1)) % 12 + 1;
    else if (c >= KEY_BTAB && c <= KEY_SDOWN && shift_keysyms[c - KEY_BTAB])
      type = TERMKEY_TYPE_KEYSYM, sym = shift_keysyms[c - KEY_BTAB];
    else if (c >= CTL_LEFT && c <= CTL_ENTER && ctrl_keysyms[c - CTL_LEFT])
      type = TERMKEY_TYPE_KEYSYM, sym = ctrl_keysyms[c - CTL_LEFT];
    else if (c >= ALT_DEL && c <= ALT_BKSP && alt_keysyms[c - ALT_DEL])
      type = TERMKEY_TYPE_KEYSYM, sym = alt_keysyms[c - ALT_DEL];
    else if (c >= ALT_MINUS && c <= ALT_BSLASH && alt_keychars[c - ALT_MINUS])
      type = TERMKEY_TYPE_UNICODE, codepoint = alt_keychars[c - ALT_MINUS];
    shift = PDC_get_key_modifiers() & PDC_KEY_MODIFIER_SHIFT;
    ctrl = PDC_get_key_modifiers() & PDC_KEY_MODIFIER_CONTROL;
    alt = PDC_get_key_modifiers() & PDC_KEY_MODIFIER_ALT;
    // Do not shift printable keys.
    if (shift && codepoint >= 32 && codepoint <= 127) shift = 0;
    // Alt+Gr maps to Ctrl+Alt; assume non-ascii-letter chars are composed.
    if (ctrl && alt && (codepoint < 'a' || codepoint > 'z')) ctrl = 0, alt = 0;
    key->type = type;
    if (type == TERMKEY_TYPE_UNICODE) {
      key->code.codepoint = codepoint;
      key->utf8[0] = key->code.codepoint, key->utf8[1] = '\0'; // unused
    } else if (type == TERMKEY_TYPE_FUNCTION)
      key->code.number = number;
    else if (type == TERMKEY_TYPE_KEYSYM)
      key->code.sym = sym;
  } else {
    request_mouse_pos();
    if (A_BUTTON_CHANGED) {
      for (int i = 1; i <= 3; i++)
        if (BUTTON_CHANGED(i)) {
          event = mousesyms[BUTTON_STATUS(i) & BUTTON_ACTION_MASK], button = i;
          shift = BUTTON_STATUS(i) & PDC_BUTTON_SHIFT;
          ctrl = BUTTON_STATUS(i) & PDC_BUTTON_CONTROL;
          alt = BUTTON_STATUS(i) & PDC_BUTTON_ALT;
          break;
        }
    } else if (MOUSE_WHEEL_UP || MOUSE_WHEEL_DOWN)
      event = TERMKEY_MOUSE_PRESS, button = MOUSE_WHEEL_UP ? 4 : 5;
    key->type = TERMKEY_TYPE_MOUSE;
    key->code.mouse[0] = button | (event << 4);
    termkey_key_set_linecol(key, MOUSE_X_POS + 1, MOUSE_Y_POS + 1);
  }
  key->modifiers = (shift ? TERMKEY_KEYMOD_SHIFT : 0) |
                   (ctrl ? TERMKEY_KEYMOD_CTRL : 0) |
                   (alt ? TERMKEY_KEYMOD_ALT : 0);
  return TERMKEY_RES_KEY;
}

TermKeyResult termkey_interpret_mouse(TermKey *tk, const TermKeyKey *key, TermKeyMouseEvent *event, int *button, int *line, int *col) {
  if (key->type != TERMKEY_TYPE_MOUSE) return TERMKEY_RES_NONE;
  if (event) *event = (key->code.mouse[0] & 0xF0) >> 4;
  if (button) *button = key->code.mouse[0] & 0xF;
  termkey_key_get_linecol(key, line, col);
  return TERMKEY_RES_KEY;
}

// Unimplemented.
TermKeyResult termkey_interpret_modereport(TermKey *tk, const TermKeyKey *key, int *initial, int *mode, int *value) { return TERMKEY_RES_ERROR; }
TermKeyResult termkey_interpret_position(TermKey *tk, const TermKeyKey *key, int *line, int *col) { return TERMKEY_RES_ERROR; }
TermKeyResult termkey_interpret_csi(TermKey *tk, const TermKeyKey *key, long args[], size_t *nargs, unsigned long *cmd) { return TERMKEY_RES_ERROR; }

struct TermKeyDriver termkey_driver_win_pdcurses = {
  .name = "win-pdcurses",
  .new_driver = new_driver,
  .free_driver = free_driver,
  .peekkey = peekkey,
};
