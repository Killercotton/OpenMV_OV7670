/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Paul Sokolovsky
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <errno.h>
#include <string.h>
#include "py/mpconfig.h"

#include "py/nlr.h"
#include "py/runtime.h"
#include "py/objtuple.h"

#if MICROPY_PY_OS_DUPTERM

void mp_uos_dupterm_tx_strn(const char *str, size_t len) {
    if (MP_STATE_PORT(term_obj) != MP_OBJ_NULL) {
        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            mp_obj_t write_m[3];
            mp_load_method(MP_STATE_PORT(term_obj), MP_QSTR_write, write_m);
            write_m[2] = mp_obj_new_bytearray_by_ref(len, (char*)str);
            mp_call_method_n_kw(1, 0, write_m);
            nlr_pop();
        } else {
            MP_STATE_PORT(term_obj) = NULL;
            mp_printf(&mp_plat_print, "dupterm: Exception in write() method, deactivating: ");
            mp_obj_print_exception(&mp_plat_print, nlr.ret_val);
        }
    }
}

STATIC mp_obj_t mp_uos_dupterm(mp_uint_t n_args, const mp_obj_t *args) {
    if (n_args == 0) {
        if (MP_STATE_PORT(term_obj) == MP_OBJ_NULL) {
            return mp_const_none;
        } else {
            return MP_STATE_PORT(term_obj);
        }
    } else {
        if (args[0] == mp_const_none) {
            MP_STATE_PORT(term_obj) = NULL;
        } else {
            MP_STATE_PORT(term_obj) = args[0];
        }
        return mp_const_none;
    }
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_uos_dupterm_obj, 0, 1, mp_uos_dupterm);

#endif
