/* Build-host generator. The output embeds the pinned evdev/pc105/us keymap. */
#include <stdio.h>
#include <stdlib.h>
#include <xkbcommon/xkbcommon.h>
int main(int argc, char **argv) {
 if (argc != 3) return 2;
 struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_DEFAULT_INCLUDES);
 if (!context || !xkb_context_include_path_append(context, argv[1])) return 1;
 struct xkb_rule_names names = { .rules = "evdev", .model = "pc105", .layout = "us" };
 struct xkb_keymap *map = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
 if (!map) return 1;
 char *text = xkb_keymap_get_as_string(map, XKB_KEYMAP_FORMAT_TEXT_V1);
 FILE *out = fopen(argv[2], "w"); if (!text || !out) return 1;
 fputs("static const char ps5wl_keymap[] =\n\"", out);
 for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
  if (*p == '\n') fputs("\\n\"\n\"", out);
  else if (*p == '"' || *p == '\\') { fputc('\\', out); fputc(*p, out); }
  else fputc(*p, out);
 }
 fputs("\";\n", out);
 int result = fclose(out) != 0;
 free(text); xkb_keymap_unref(map); xkb_context_unref(context);
 return result;
}
