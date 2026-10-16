/* Link-time import metadata only; no function implementation is embedded.
 * Matches ps5-payload-dev/sdk sce_stubs/genstub.py and sce_stubs/Makefile.
 * The SONAME is libSceMouse.sprx. SDK crt/rtld_sprx.c loads the real system
 * module and resolves these symbol names by NID before compositor entry.
 * https://github.com/ps5-payload-dev/sdk/tree/master/sce_stubs
 * https://github.com/ps5-payload-dev/sdk/blob/master/crt/rtld_sprx.c
 */
__asm__(".global sceMouseInit\n"
        ".type sceMouseInit @function\n"
        "sceMouseInit:\n");
__asm__(".global sceMouseOpen\n"
        ".type sceMouseOpen @function\n"
        "sceMouseOpen:\n");
__asm__(".global sceMouseRead\n"
        ".type sceMouseRead @function\n"
        "sceMouseRead:\n");
__asm__(".global sceMouseClose\n"
        ".type sceMouseClose @function\n"
        "sceMouseClose:\n");
