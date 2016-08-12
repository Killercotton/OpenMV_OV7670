#include <stdio.h>

#include "py/obj.h"
#include "py/runtime.h"
#include "py/repl.h"
#include "py/mpz.h"

#if defined(MICROPY_UNIX_COVERAGE)

// function to run extra tests for things that can't be checked by scripts
STATIC mp_obj_t extra_coverage(void) {
    // mp_printf (used by ports that don't have a native printf)
    {
        mp_printf(&mp_plat_print, "# mp_printf\n");
        mp_printf(&mp_plat_print, "%"); // nothing after percent
        mp_printf(&mp_plat_print, "%d %+d % d\n", -123, 123, 123); // sign
        mp_printf(&mp_plat_print, "%05d\n", -123); // negative number with zero padding
        mp_printf(&mp_plat_print, "%ld\n", 123); // long
        mp_printf(&mp_plat_print, "%X\n", 0x1abcdef); // capital hex
        mp_printf(&mp_plat_print, "%.2s %.3s\n", "abc", "abc"); // fixed string precision
        mp_printf(&mp_plat_print, "%.*s\n", -1, "abc"); // negative string precision
        mp_printf(&mp_plat_print, "%b %b\n", 0, 1); // bools
        mp_printf(&mp_plat_print, "%s\n", NULL); // null string
        mp_printf(&mp_plat_print, "%t\n"); // non-format char
        mp_printf(&mp_plat_print, "%d\n", 0x80000000); // should print signed
        mp_printf(&mp_plat_print, "%u\n", 0x80000000); // should print unsigned
        mp_printf(&mp_plat_print, "%x\n", 0x80000000); // should print unsigned
        mp_printf(&mp_plat_print, "%X\n", 0x80000000); // should print unsigned
    }

    // vstr
    {
        mp_printf(&mp_plat_print, "# vstr\n");
        vstr_t *vstr = vstr_new_size(16);
        vstr_hint_size(vstr, 32);
        vstr_add_str(vstr, "ts");
        vstr_ins_byte(vstr, 1, 'e');
        vstr_ins_char(vstr, 3, 't');
        vstr_ins_char(vstr, 10, 's');
        mp_printf(&mp_plat_print, "%.*s\n", (int)vstr->len, vstr->buf);

        vstr_cut_head_bytes(vstr, 2);
        mp_printf(&mp_plat_print, "%.*s\n", (int)vstr->len, vstr->buf);

        vstr_cut_tail_bytes(vstr, 10);
        mp_printf(&mp_plat_print, "%.*s\n", (int)vstr->len, vstr->buf);

        vstr_printf(vstr, "t%cst", 'e');
        mp_printf(&mp_plat_print, "%.*s\n", (int)vstr->len, vstr->buf);

        vstr_cut_out_bytes(vstr, 3, 10);
        mp_printf(&mp_plat_print, "%.*s\n", (int)vstr->len, vstr->buf);

        VSTR_FIXED(fix, 4);
        vstr_add_str(&fix, "large");
        mp_printf(&mp_plat_print, "%.*s\n", (int)fix.len, fix.buf);
    }

    // repl autocomplete
    {
        mp_printf(&mp_plat_print, "# repl\n");

        const char *str;
        mp_uint_t len = mp_repl_autocomplete("__n", 3, &mp_plat_print, &str);
        mp_printf(&mp_plat_print, "%.*s\n", (int)len, str);

        mp_store_global(MP_QSTR_sys, mp_import_name(MP_QSTR_sys, mp_const_none, MP_OBJ_NEW_SMALL_INT(0)));
        mp_repl_autocomplete("sys.", 4, &mp_plat_print, &str);
        len = mp_repl_autocomplete("sys.impl", 8, &mp_plat_print, &str);
        mp_printf(&mp_plat_print, "%.*s\n", (int)len, str);
    }

    // attrtuple
    {
        mp_printf(&mp_plat_print, "# attrtuple\n");

        static const qstr fields[] = {MP_QSTR_start, MP_QSTR_stop, MP_QSTR_step};
        static const mp_obj_t items[] = {MP_OBJ_NEW_SMALL_INT(1), MP_OBJ_NEW_SMALL_INT(2), MP_OBJ_NEW_SMALL_INT(3)};
        mp_obj_print_helper(&mp_plat_print, mp_obj_new_attrtuple(fields, 3, items), PRINT_REPR);
        mp_printf(&mp_plat_print, "\n");
    }

    // str
    {
        mp_printf(&mp_plat_print, "# str\n");

        // intern string
        mp_printf(&mp_plat_print, "%d\n", MP_OBJ_IS_QSTR(mp_obj_str_intern(mp_obj_new_str("intern me", 9, false))));
    }

    // mpz
    {
        mp_printf(&mp_plat_print, "# mpz\n");

        mp_uint_t value;
        mpz_t mpz;
        mpz_init_zero(&mpz);

        // mpz_as_uint_checked, with success
        mpz_set_from_int(&mpz, 12345678);
        mp_printf(&mp_plat_print, "%d\n", mpz_as_uint_checked(&mpz, &value));
        mp_printf(&mp_plat_print, "%d\n", (int)value);

        // mpz_as_uint_checked, with negative arg
        mpz_set_from_int(&mpz, -1);
        mp_printf(&mp_plat_print, "%d\n", mpz_as_uint_checked(&mpz, &value));

        // mpz_as_uint_checked, with overflowing arg
        mpz_set_from_int(&mpz, 1);
        mpz_shl_inpl(&mpz, &mpz, 70);
        mp_printf(&mp_plat_print, "%d\n", mpz_as_uint_checked(&mpz, &value));
    }

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(extra_coverage_obj, extra_coverage);

#endif
