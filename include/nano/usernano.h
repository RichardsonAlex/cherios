/*-
 * Copyright (c) 2017 Lawrence Esswood
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef CHERIOS_USERNANO_H
#define CHERIOS_USERNANO_H

#include "nanokernel.h"

#define GET_NANO_SYSCALL(c1,c2,n)                   \
capability c1, c2;                                  \
__asm__ (                                           \
"li     $a0, 0          \n"                         \
        "li     $a1, %[i]       \n"                 \
        "syscall                \n"                 \
        "cmove  %[_foo_c1], $c1      \n"            \
        "cmove  %[_foo_c2], $c2      \n"            \
:   [_foo_c1]"=C"(c1), [_foo_c2]"=C"(c2)            \
:   [i]"i"(n)                                       \
: "a0", "a1", "$c1", "$c2");                        \


#define BLAH(a,c,...) c
#define RETURN_CASE_VOID_void  a ,
#define DO_RETURN(...) BLAH(__VA_ARGS__, return)

#define CALL_NANO_DEVIRTUAL(Call, n, ret, raw_sig)       \
do {                                                \
    GET_NANO_SYSCALL(c1, c2, n)           \
DO_RETURN(RETURN_CASE_VOID_ ## ret) Call ## _inst(CONTEXT(c1, c2) MAKE_ARG_LIST_APPEND(raw_sig));       \
}while(0);


MAKE_CTR(NANO_CTR)

#define MAKE_WRAPPED(name, ret, raw_sig, ...) static inline ret name ## _sys MAKE_SIG(raw_sig) {\
CALL_NANO_DEVIRTUAL(name, (CTR(NANO_CTR)), ret, raw_sig) \
}

NANO_KERNEL_IF_RAW_LIST(MAKE_WRAPPED,)

/* Assuming you trust your memory (i.e. are secure loaded) you can call this to populate your nano kernel if and
 * then use the normal interface rather than having to use syscall */

MAKE_CTR(NANO_INIT_CTR)

#define INIT_OBJ_SYSCALL(name, ...) {GET_NANO_SYSCALL(c1,c2,CTR(NANO_INIT_CTR)) \
    PLT_UNIQUE_OBJECT(name).code = c1; \
    PLT_UNIQUE_OBJECT(name).data = c2;}

static inline void init_nano_if_sys(void) {
    NANO_KERNEL_IF_RAW_LIST(INIT_OBJ_SYSCALL,)
}
#endif //CHERIOS_USERNANO_H
