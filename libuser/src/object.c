/*-
 * Copyright (c) 2016 Hadrien Barral
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

#include <sockets.h>
#include <sys/deduplicate.h>
#include "mips.h"
#include "object.h"
#include "cheric.h"
#include "assert.h"
#include "namespace.h"
#include "queue.h"
#include "syscalls.h"
#include "string.h"
#include "stdio.h"
#include "nano/nanokernel.h"
#include "mman.h"
#include "capmalloc.h"
#include "thread.h"
#include "exceptions.h"
#include "exception_cause.h"
#include "temporal.h"
#include "unistd.h"
#include "sys/deduplicate.h"
#include "cprogram.h"

capability int_cap;

__thread act_control_kt act_self_ctrl = NULL;
__thread act_kt act_self_ref  = NULL;
__thread act_notify_kt act_self_notify_ref = NULL;
__thread queue_t * act_self_queue = NULL;

#if !(LIGHTWEIGHT_OBJECT)
__thread user_stats_t* own_stats;
#endif

int    was_secure_loaded;
auth_t own_auth;
found_id_t* own_found_id;
startup_flags_e default_flags;


#if !(LIGHTWEIGHT_OBJECT)
#ifndef USE_SYSCALL_PUTS

#define STD_BUF_SIZE 0x100

typedef struct {
    unix_like_socket sock;
    struct requester_32 reqs;
    char buf[STD_BUF_SIZE];
} std_sock;

__thread std_sock std_out_sock;
__thread std_sock std_err_sock;

#endif // !USE_SYSCALL_PUTS
#endif // !LIGHTWEIGHT

static void setup_temporal_handle(startup_flags) {
    if(!(startup_flags & STARTUP_NO_EXCEPTIONS)) {
        // WARN these will by dangling after compact. Call again to fix.
        register_vectored_exception(&temporal_exception_handle, MIPS_CP0_EXCODE_TRAP);
    }
}

// Manually link with other libraries (just the socket lib currently)

#if !(LIGHTWEIGHT_OBJECT)
void dylink_sockets(act_control_kt self_ctrl, queue_t * queue, startup_flags_e startup_flags, int first_thread) {
    act_kt dylink_server = namespace_get_ref(namespace_num_lib_socket);

    assert(dylink_server != NULL);

    cap_pair pair;
    lib_socket_if_t* socket_if;
    found_id_t* id;

    if(first_thread) {
        cert_t if_cert = message_send_c(0, 0, 0, 0, NULL, NULL, NULL, NULL, dylink_server, SYNC_CALL, 1);
        id = rescap_check_cert(if_cert, &pair);
        assert(id != NULL);
        socket_if = (lib_socket_if_t*)pair.data;
    }

    size_t size = message_send(0, 0, 0, 0, NULL, NULL, NULL, NULL, dylink_server, SYNC_CALL, 0);

    assert(size != 0);

    // Allocate space / stacks for new thread in other library
    res_t locals_res, stack_res, ustack_res, sign_res;
    locals_res = cap_malloc(size);
    stack_res = mem_request(0, DEFAULT_STACK_SIZE, 0, own_mop).val;
    ustack_res = mem_request(0, Overrequest + MinStackSize, 0, own_mop).val;
    sign_res = cap_malloc(RES_CERT_META_SIZE);

    // Create a new thread in target library
    single_use_cert thread_cert = message_send_c(0, 0, 0, 0, locals_res, stack_res, ustack_res, sign_res, dylink_server, SYNC_CALL, 2);

    // TODO. If we would only accept a certain set of libraries, we would check the sig here
    found_id_t* id2 = rescap_check_single_cert(thread_cert, &pair);

    capability dataarg = pair.data;

    if(first_thread) {
        // Needs same interface
        if(id != id2) {
            CHERI_PRINT_CAP(id);
            CHERI_PRINT_CAP(id2);
            CHERI_PRINT_CAP(thread_cert);
        }
        assert(id == id2);
        init_lib_socket_if_t(socket_if, dataarg, was_secure_loaded ? plt_common_untrusting: plt_common_complete_trusting);
    } else {
        init_lib_socket_if_t_new_thread(dataarg);
    }

    // Then call init in other library

    init_external_thread(self_ctrl, own_mop, queue, startup_flags);
}
#endif

void object_init(act_control_kt self_ctrl, queue_t * queue,
                 kernel_if_t* kernel_if_c, tres_t cds_res,
                 startup_flags_e startup_flags, int first_thread) {

    act_self_ctrl = self_ctrl;
    default_flags = startup_flags;

	if(first_thread) {
        was_secure_loaded = (own_auth != NULL);
        init_nano_if_sys(); // <- this allows us to use non sys versions by calling syscall in advance for each function
        if(cds_res) {
            get_ctl()->cds = tres_take(cds_res);
        }
        // WARN: These are non de-dupped versions. We will have to do this _again_ after dedup
        init_kernel_if_t(kernel_if_c, self_ctrl, was_secure_loaded ? plt_common_untrusting: &plt_common_complete_trusting);
    } else {
        init_kernel_if_t_new_thread(self_ctrl);
    }

	// For secure loaded things this is needed before any calls into the kernel
	// However this is pre-dedup / compact. Will have to call a couple more times
    setup_temporal_handle(startup_flags);

    int_cap = get_integer_space_cap();

#if !(LIGHTWEIGHT_OBJECT)
    own_stats = syscall_act_user_info_ref(self_ctrl);

    if(cheri_getreg(10) != NULL) {
        // temporal will have failed to bump stats correctly
        own_stats->temporal_reqs = 2;
        own_stats->temporal_depth = 2;
    } else {
        own_stats->temporal_depth = 1;
    }
#endif

	act_self_ref  = syscall_act_ctrl_get_ref(self_ctrl);
    act_self_notify_ref = syscall_act_ctrl_get_notify_ref(self_ctrl);
	act_self_queue = queue;

    sync_state = (sync_state_t){.sync_caller = NULL};

#if !(LIGHTWEIGHT_OBJECT)
#if (AUTO_DEDUP_ALL_FUNCTIONS)
    dedup_stats stats;
    int did_dedup = 0;
    if(first_thread) {
        if(!(startup_flags & STARTUP_NO_DEDUP) &&
                namespace_ref &&
                act_self_ref != namespace_ref &&
                get_dedup() != NULL) {
                stats = deduplicate_all_functions(0);
                did_dedup = 1;
                // Re-do these. Will need to do again after compact.
                init_kernel_if_t_change_mode(was_secure_loaded ? plt_common_untrusting: &plt_common_complete_trusting);
                setup_temporal_handle(startup_flags);
        }
    }
#endif
#endif

    if(first_thread) {
        if(was_secure_loaded) own_found_id = foundation_get_id(own_auth);
    }

#if !(LIGHTWEIGHT_OBJECT)
    if(!(startup_flags & STARTUP_NO_MALLOC)) {
        init_cap_malloc();
    }

    if(!(startup_flags & STARTUP_NO_THREADS) && first_thread) {
        thread_init();
    }

    // This creates two sockets with the UART driver and sets stdout/stderr to them
#ifndef USE_SYSCALL_PUTS

    // Dynamic link now.
    dylink_sockets(self_ctrl, queue, startup_flags, first_thread);

    act_kt uart;

    while((uart = namespace_get_ref(namespace_num_uart)) == NULL) {
        sleep(0);
    }

    int res;

    int flags = MSG_NO_CAPS | SOCKF_DRB_INLINE | SOCKF_SOCK_INLINE;

#define MAKE_STD_SOCK(S,IPC_NO)                                                                                 \
        S.sock.write.push_writer = socket_malloc_requester_32(SOCK_TYPE_PUSH, &S.sock.write_copy_buffer);          \
            res = message_send(0,0,0,0,                                                                         \
                               socket_make_ref_for_fulfill(S.sock.write.push_writer), NULL, NULL, NULL,         \
                               uart, SYNC_CALL, IPC_NO);                                                        \
        assert(res == 0);                                                                                       \
        socket_requester_connect(S.sock.write.push_writer);                                            \
        socket_init(&S.sock, flags, S.buf, STD_BUF_SIZE, CONNECT_PUSH_WRITE);

    MAKE_STD_SOCK(std_out_sock, 2);
    MAKE_STD_SOCK(std_err_sock, 3);

    stderr = &std_err_sock.sock;
    stdout = &std_out_sock.sock;

#else
    stderr = NULL;
    stdout = NULL;
#endif

#if (AUTO_DEDUP_ALL_FUNCTIONS && AUTO_DEDUP_STATS)
    if(did_dedup) {
        const char* name = syscall_get_name(act_self_ref);
        printf("%s Ran deduplication on all. Processed %ld. %ld RO. Probably funcs %ld of %ld (%ld of %ld bytes). Other RO %ld of %ld (%ld of %ld bytes). %ld too large\n",
               name,
               stats.processed,
               stats.tried,
               stats.of_which_func_replaced,
               stats.of_which_func,
               stats.of_which_func_bytes_replaced,
               stats.of_which_func_bytes,
               stats.of_which_data_replaced,
               stats.of_which_data,
               stats.of_which_data_bytes_replaced,
               stats.of_which_data_bytes,
               stats.too_large);
    }
#endif

#endif // !LIGHTWEIGHT
}

void object_init_post_compact(startup_flags_e startup_flags, int first_thread) {
#if !(LIGHTWEIGHT_OBJECT)
    setup_temporal_handle(startup_flags);
    init_kernel_if_t_change_mode(was_secure_loaded ? plt_common_untrusting: &plt_common_complete_trusting);
#endif // !LIGHTWEIGHT
}

// Called when any thread exits
__attribute__((noreturn))
void object_destroy() {
#if !(LIGHTWEIGHT_OBJECT)
    #ifndef USE_SYSCALL_PUTS
        flush(stdout);
        flush(stderr);
    #endif
    process_async_closes(1);
#endif // !LIGHTWEIGHT
    syscall_act_terminate(act_self_ctrl);
}

void ctor_null(void) {
	return;
}

void dtor_null(void) {
	return;
}



#define DH(a) weak