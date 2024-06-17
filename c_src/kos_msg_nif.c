// Copyright (c) 2023, Kry10 Limited. All rights reserved.
//
// SPDX-License-Identifier: LicenseRef-Kry10

#include <erl_nif.h>

#include <stdio.h>
#include <kos_utils.h>
#include <kos.h>
#include <muslcsys/pthread_manager.h>

#define ATOMS                                      \
    ATOM_DECL(ok);                                 \
    ATOM_DECL(true);                               \
    ATOM_DECL(false);                              \
    ATOM_DECL(kos_msg);                            \
    ATOM_DECL(up);                                 \
    ATOM_DECL(kos_status_ok);                      \
    ATOM_DECL(kos_status_created);                 \
    ATOM_DECL(kos_status_at_token_limit);          \
    ATOM_DECL(kos_status_bad_request);             \
    ATOM_DECL(kos_status_conflict);                \
    ATOM_DECL(kos_status_internal_error);          \
    ATOM_DECL(kos_status_invalid_argument);        \
    ATOM_DECL(kos_status_invalid_token);           \
    ATOM_DECL(kos_status_no_reply);                \
    ATOM_DECL(kos_status_not_enough_memory);       \
    ATOM_DECL(kos_status_not_found);               \
    ATOM_DECL(kos_status_pong);                    \
    ATOM_DECL(kos_status_unauthorized);            \
    ATOM_DECL(kos_status_unavailable);

static ErlNifMutex * pid_mutex;
static ErlNifPid pid;
static ErlNifTid msg_thread;
static kos_cap_t server_reply_cap;
#define ATOM_DECL(A) static ERL_NIF_TERM atom_##A
ATOMS
#undef ATOM_DECL

static void* thr_main(void* obj);
static int load(ErlNifEnv* env, void** priv_data, ERL_NIF_TERM load_info)
{

#define ATOM_DECL(A) atom_##A = enif_make_atom(env, #A)
ATOMS
#undef ATOM_DECL

    pid_mutex = enif_mutex_create("pid_mutex");
    if (!pid_mutex) {
      return -1;
    }

    ErlNifThreadOpts* opts = enif_thread_opts_create("thread_opts");
    if(enif_thread_create("", &msg_thread, thr_main, NULL, opts) != 0) {
        return -1;
    }

    *priv_data = NULL;

    return 0;
}


static ERL_NIF_TERM kos_status_to_atom(kos_status_t status) {
  switch(status) {
    case seL4_InvalidArgument:
      return atom_kos_status_invalid_argument;
    case STATUS_AT_TOKEN_LIMIT:
      return atom_kos_status_at_token_limit;
    case STATUS_BAD_REQUEST:
      return atom_kos_status_bad_request;
    case STATUS_CREATED:
      return atom_kos_status_created;
    case STATUS_CONFLICT:
      return atom_kos_status_conflict;
    case STATUS_INVALID_TOKEN:
      return atom_kos_status_invalid_token;
    case STATUS_NO_REPLY:
      return atom_kos_status_no_reply;
    case STATUS_NOT_ENOUGH_MEMORY:
      return atom_kos_status_not_enough_memory;
    case STATUS_NOT_FOUND:
      return atom_kos_status_not_found;
    case STATUS_OK:
      return atom_kos_status_ok;
    case STATUS_PONG:
      return atom_kos_status_pong;
    case STATUS_UNAUTHORIZED:
      return atom_kos_status_unauthorized;
    case STATUS_UNAVAILABLE:
      return atom_kos_status_unavailable;
    default:
      return atom_kos_status_internal_error;
  }
}


static kos_msg_server_t _server;

kos_status_t setup_msg_server_transport(kos_thread_environment_t* p_env) {
  kos_status_t status = kos_pthread_get_kos_env(&p_env);
  if (status != STATUS_OK) {
    return status;
  }
  kos_token_t token_slot;
  status = kos_msg_token_slot_pool_alloc(&token_slot);
  if (status != STATUS_OK) {
    return status;
  }


  // prepare to receive caps
  kos_cap_t receive_cap = kos_cnode_cap(p_env->p_cnode, KOS_THREAD_SLOT_RECEIVE);
  kos_cap_set_receive(receive_cap);

  // prepare the reply cap
  kos_cap_t reply_cap = kos_cnode_cap(p_env->p_cnode, KOS_THREAD_SLOT_REPLY);

  // a slot to hold the transport
  kos_cap_t server_cap = kos_cap_reserve();

  // set up the server transport
  status = kos_msg_server_create(server_cap, reply_cap, token_slot, &_server);
  // no longer need to receive caps.
  kos_cap_clear_receive();

  return status;
}

static void*
thr_main(void* obj)
{
    ErlNifEnv* env = enif_alloc_env();
    kos_thread_environment_t* p_env;
    kos_status_t status = kos_pthread_get_kos_env(&p_env);
    kos_assert_ok(status, NULL);
    status = setup_msg_server_transport(p_env);
    kos_assert_created(status, NULL);

    server_reply_cap = kos_cnode_cap(p_env->p_cnode, KOS_THREAD_SLOT_REPLY);

    while(true){
      seL4_MessageInfo_t sel4_msg = seL4_Recv(_server.transport.ep_cptr, NULL, _server.reply_cptr);

      // fill out the message struct
      // the caller badge is in the label
      seL4_Word caller = seL4_MessageInfo_get_label(sel4_msg);
      seL4_Word label = seL4_GetMR(0);
      seL4_Word param = seL4_GetMR(1);
      seL4_Word metadata = seL4_GetMR(2);
      seL4_Word badge = seL4_GetMR(3);
      // sanity check
      kos_assert_eq(seL4_MessageInfo_get_length(sel4_msg), 4, "Invalid reply_receive response");

#ifndef CONFIG_KERNEL_MCS
      seL4_CNode_SaveCaller(
        kos_app_root_cap(),
        kos_cap_index(server_reply_cap),
        kos_cap_depth(server_reply_cap));
#endif

      ERL_NIF_TERM payload_term;
      uint16_t recv_payload_size = kos_msg_payload_size(metadata);
      unsigned char *bin = enif_make_new_binary(env, recv_payload_size, &payload_term);
      memcpy(bin, kos_msg_server_payload(), recv_payload_size);

      kos_token_t xfer_token_slot = 0; // by default no token
      if (kos_msg_transfer_token(metadata) > 0) {
	// sjw: FIXME: maybe let the controller know about the error instead of asserting?
	kos_assert_ok(kos_msg_token_slot_pool_alloc(&xfer_token_slot), NULL);
	kos_assert_ok(kos_msg_token_move(xfer_token_slot, kos_msg_transfer_token(metadata)), NULL);
      }
      
      ERL_NIF_TERM msg_term =  enif_make_tuple4(env,
        enif_make_atom(env, "kos_msg"),
        enif_make_ulong(env, badge),
        enif_make_ulong(env, caller),
        enif_make_tuple4(env,
          enif_make_ulong(env, label),
          enif_make_ulong(env, param),
          enif_make_uint(env, xfer_token_slot),
          payload_term));

      enif_mutex_lock(pid_mutex);
      int ret = enif_send(NULL, &pid, env, msg_term);
      enif_mutex_unlock(pid_mutex);
      enif_clear_env(env);
      kos_assert(ret == 1, "Failed to send msg_term\n");

    }

    return NULL;
}


static ERL_NIF_TERM n_set_controlling_pid(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
    if (argc != 1) {
      return enif_make_badarg(env);
    }

    enif_mutex_lock(pid_mutex);
    int ret = !enif_get_local_pid(env, argv[0], &pid);
    enif_mutex_unlock(pid_mutex);
    if(ret) {
        return enif_make_badarg(env);
    }
    ErlNifEnv* send_env = enif_alloc_env();

    // Send {:kos_msg, :up} to the controlling pid

    ERL_NIF_TERM kos_msg = enif_make_atom(send_env, "kos_msg");
    ERL_NIF_TERM up = enif_make_atom(send_env, "up");
    ERL_NIF_TERM msg_term = enif_make_tuple2(send_env, kos_msg, up);
    enif_mutex_lock(pid_mutex);
    enif_send(NULL, &pid, send_env, msg_term);
    enif_mutex_unlock(pid_mutex);

    enif_clear_env(send_env);

    return atom_ok;

}


static ERL_NIF_TERM n_reply(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
  int arity;
  const ERL_NIF_TERM* in_msg;
  unsigned long label;
  unsigned long param;
  unsigned int transfer_token;
  ErlNifBinary payload_bin;
  if (argc != 1
      || !enif_get_tuple(env, argv[0], &arity, &in_msg)
      ||   arity != 4
      || !enif_get_ulong(env, in_msg[0], &label)
      || !enif_get_ulong(env, in_msg[1], &param)
      || !enif_get_uint(env, in_msg[2], &transfer_token)
      || !enif_inspect_binary(env, in_msg[3], &payload_bin)) {
    return enif_make_badarg(env);
  }

  kos_msg_t msg = kos_msg_new(label, param, payload_bin.size, transfer_token, 0);

  memcpy(_server.transport.p_payload, payload_bin.data, payload_bin.size);
  seL4_SetMR(0, msg.label);
  seL4_SetMR(1, msg.param);
  seL4_SetMR(2, msg.metadata);

  seL4_Send(_server.reply_cptr, seL4_MessageInfo_new(STATUS_OK, 0, 0, 3));
  return atom_ok;
}

static ERL_NIF_TERM n_kos_msg_ping(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {

  return kos_status_to_atom(kos_msg_ping());
}

static ERL_NIF_TERM n_kos_msg_token_slot_pool_alloc(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
  kos_token_t token_slot;
  kos_status_t status = kos_msg_token_slot_pool_alloc(&token_slot);
  if (status == STATUS_OK) {
    return enif_make_tuple2(env, atom_kos_status_ok, enif_make_uint(env, token_slot));
  } else {
    return kos_status_to_atom(status);
  }
}

static ERL_NIF_TERM n_kos_msg_token_create(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
  unsigned long badge;
  unsigned long flags;
  unsigned int token_slot;
  if (argc != 3
      || !enif_get_ulong(env, argv[0], &badge)
      || !enif_get_ulong(env, argv[1], &flags)
      || !enif_get_uint(env, argv[2], &token_slot)
      ||   token_slot >= KOS_MSG_TOKENS_PER_APP) {
    return enif_make_badarg(env);
  }

  kos_status_t status = kos_msg_token_create(badge, flags, token_slot);
  return kos_status_to_atom(status);
}

static ERL_NIF_TERM n_kos_msg_token_revoke(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
  unsigned long badge;
  if (argc != 1
      || !enif_get_ulong(env, argv[0], &badge)) {
    return enif_make_badarg(env);
  }

  kos_status_t status = kos_msg_token_revoke(badge);
  return kos_status_to_atom(status);
}

static ERL_NIF_TERM n_kos_msg_token_delete(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
  unsigned int token_slot;
  if (argc != 1
      || !enif_get_uint(env, argv[0], &token_slot)
      ||   token_slot >= KOS_MSG_TOKENS_PER_APP) {
    return enif_make_badarg(env);
  }

  kos_status_t status = kos_msg_token_delete(token_slot);
  return kos_status_to_atom(status);
}

static ERL_NIF_TERM n_kos_msg_token_move(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
  unsigned int dst_token_slot;
  unsigned int src_token_slot;
  if (argc != 2
      || !enif_get_uint(env, argv[0], &dst_token_slot)
      ||    dst_token_slot >= KOS_MSG_TOKENS_PER_APP
      || !enif_get_uint(env, argv[1], &src_token_slot)
      ||    src_token_slot >= KOS_MSG_TOKENS_PER_APP) {
    return enif_make_badarg(env);
  }

  kos_status_t status = kos_msg_token_move(dst_token_slot, src_token_slot);
  return kos_status_to_atom(status);
}



static ERL_NIF_TERM n_kos_msg_token_info(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
  unsigned int token_slot;
  if (argc != 1
      || !enif_get_uint(env, argv[0], &token_slot)
      ||   token_slot >= KOS_MSG_TOKENS_PER_APP) {
    return enif_make_badarg(env);
  }

  seL4_Word token_flags;
  kos_status_t status = kos_msg_token_info(token_slot, &token_flags);
  if (status == STATUS_OK) {
    return enif_make_tuple2(env, atom_kos_status_ok, enif_make_uint(env, token_flags));
  } else {
    return kos_status_to_atom(status);
  }
}


static ERL_NIF_TERM n_kos_msg_call(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
  unsigned int token_slot;
  unsigned int reply_flags;
  int arity;
  const ERL_NIF_TERM* in_msg;
  ErlNifBinary payload_bin;
  unsigned int transfer_token;
  unsigned long label;
  unsigned long param;

  if (argc != 3
      || !enif_get_uint(env, argv[0], &token_slot)
      ||   token_slot >= KOS_MSG_TOKENS_PER_APP
      || !enif_get_uint(env, argv[1], &reply_flags)
      ||   reply_flags >= 255
      || !enif_get_tuple(env, argv[2], &arity, &in_msg)
      ||   arity != 4
      ||   !enif_get_ulong(env, in_msg[0], &label)
      ||   !enif_get_ulong(env, in_msg[1], &param)
      ||   !enif_get_uint(env, in_msg[2], &transfer_token)
      ||   !enif_inspect_binary(env, in_msg[3], &payload_bin)) {
    return enif_make_badarg(env);
  }

  kos_msg_t msg = kos_msg_new(label, param, payload_bin.size, transfer_token, 0);
  memcpy(kos_msg_client_payload(), payload_bin.data, payload_bin.size);
  kos_status_t status = kos_msg_call(token_slot, reply_flags, &msg);
  if (status != STATUS_OK) {
    return kos_status_to_atom(status);
  }

  ERL_NIF_TERM payload_term;
  uint16_t ret_payload_size = kos_msg_payload_size(msg.metadata);
  unsigned char *bin = enif_make_new_binary(env, ret_payload_size, &payload_term);
  memcpy(bin, kos_msg_client_payload(), ret_payload_size);

  return enif_make_tuple2(env,
    atom_kos_status_ok,
    enif_make_tuple4(env,
      enif_make_ulong(env, msg.label),
      enif_make_ulong(env, msg.param),
      enif_make_uint(env, kos_msg_transfer_token(msg.metadata)),
      payload_term));

}

static ERL_NIF_TERM n_kos_msg_send(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
  unsigned int token_slot;
  int arity;
  const ERL_NIF_TERM* in_msg;
  ErlNifBinary payload_bin;
  unsigned int transfer_token;
  unsigned long label;
  unsigned long param;

  if (argc != 2
      || !enif_get_uint(env, argv[0], &token_slot)
      ||   token_slot >= KOS_MSG_TOKENS_PER_APP
      || !enif_get_tuple(env, argv[1], &arity, &in_msg)
      ||   arity != 4
      ||   !enif_get_ulong(env, in_msg[0], &label)
      ||   !enif_get_ulong(env, in_msg[1], &param)
      ||   !enif_get_uint(env, in_msg[2], &transfer_token)
      ||   !enif_inspect_binary(env, in_msg[3], &payload_bin)) {
    return enif_make_badarg(env);
  }

  kos_msg_t msg = kos_msg_new(label, param, payload_bin.size, transfer_token, 0);
  memcpy(kos_msg_client_payload(), payload_bin.data, payload_bin.size);
  kos_status_t status = kos_msg_send(token_slot, msg);

  return kos_status_to_atom(status);
}

static ERL_NIF_TERM n_kos_dir_publish_str(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
  ErlNifBinary protocol_bin;
  unsigned long request_label;
  unsigned long request_badge;
  unsigned int request_flags;

  if (argc != 4
      || !enif_inspect_binary(env, argv[0], &protocol_bin)
      || !enif_get_ulong(env, argv[1], &request_label)
      || !enif_get_ulong(env, argv[2], &request_badge)
      || !enif_get_uint(env, argv[3], &request_flags)) {
    return enif_make_badarg(env);
  }

  kos_status_t status = kos_dir_publish_str(protocol_bin.data, request_label, request_badge, request_flags);
  return kos_status_to_atom(status);
}

static ERL_NIF_TERM n_kos_dir_unpublish_str(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
  ErlNifBinary protocol_bin;
  if (argc != 1
      || !enif_inspect_binary(env, argv[0], &protocol_bin)) {
    return enif_make_badarg(env);
  }

  kos_status_t status = kos_dir_unpublish_str(protocol_bin.data);
  return kos_status_to_atom(status);
}

static ERL_NIF_TERM n_kos_dir_query_str(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
  ErlNifBinary protocol_bin;
  if (argc != 1
      || !enif_inspect_binary(env, argv[0], &protocol_bin)) {
    return enif_make_badarg(env);
  }

  kos_status_t status = kos_dir_query_str(protocol_bin.data);
  return kos_status_to_atom(status);
}

static ERL_NIF_TERM n_kos_dir_request_str(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
  ErlNifBinary protocol_bin;
  unsigned int empty_token_slot;
  int arity;
  const ERL_NIF_TERM* in_msg;
  ErlNifBinary payload_bin;
  unsigned int transfer_token;
  unsigned long label;
  unsigned long param;

  if (argc != 3
      || !enif_inspect_binary(env, argv[0], &protocol_bin)
      || !enif_get_uint(env, argv[1], &empty_token_slot)
      ||   empty_token_slot >= KOS_MSG_TOKENS_PER_APP
      || !enif_get_tuple(env, argv[2], &arity, &in_msg)
      ||   arity != 4
      ||   !enif_get_ulong(env, in_msg[0], &label)
      ||   !enif_get_ulong(env, in_msg[1], &param)
      ||   !enif_get_uint(env, in_msg[2], &transfer_token)
      ||   !enif_inspect_binary(env, in_msg[3], &payload_bin)) {
    return enif_make_badarg(env);
  }

  kos_msg_t msg = kos_msg_new(label, param, payload_bin.size, transfer_token, 0);

  memcpy(kos_msg_client_payload(), payload_bin.data, payload_bin.size);
  kos_status_t status = kos_dir_request_str(protocol_bin.data, empty_token_slot, &msg);
  if (status != STATUS_OK) {
    return kos_status_to_atom(status);
  }

  ERL_NIF_TERM payload_term;
  uint16_t ret_payload_size = kos_msg_payload_size(msg.metadata);
  unsigned char *bin = enif_make_new_binary(env, ret_payload_size, &payload_term);
  memcpy(bin, kos_msg_client_payload(), ret_payload_size);

  return enif_make_tuple2(env,
    atom_kos_status_ok,
    enif_make_tuple4(env,
      enif_make_ulong(env, msg.label),
      enif_make_ulong(env, msg.param),
      enif_make_uint(env, kos_msg_transfer_token(msg.metadata)),
      payload_term));
}

static ErlNifFunc nif_funcs[] = {
  // {erl_function_name, erl_function_arity, c_function}
  {"kos_msg_ping", 0, n_kos_msg_ping, 0},

  {"kos_msg_token_slot_pool_alloc", 0, n_kos_msg_token_slot_pool_alloc, 0},
  {"kos_msg_token_create", 3, n_kos_msg_token_create, 0},
  {"kos_msg_token_revoke", 1, n_kos_msg_token_revoke, 0},
  {"kos_msg_token_delete", 1, n_kos_msg_token_delete, 0},
  {"kos_msg_token_move", 2, n_kos_msg_token_move, 0},
  {"kos_msg_token_info", 1, n_kos_msg_token_info, 0},

  {"kos_msg_call", 3, n_kos_msg_call, 0},
  {"kos_msg_call_dirty", 3, n_kos_msg_call, ERL_NIF_DIRTY_JOB_IO_BOUND},
  {"kos_msg_send", 2, n_kos_msg_send, 0},
  {"kos_msg_send_dirty", 2, n_kos_msg_send, 0},

  {"kos_dir_publish_str", 4, n_kos_dir_publish_str, 0},
  {"kos_dir_unpublish_str", 1, n_kos_dir_unpublish_str, 0},
  {"kos_dir_query_str", 1, n_kos_dir_query_str, 0},
  {"kos_dir_request_str", 3, n_kos_dir_request_str, 0},
  // Not currently supported
  // {"kos_dir_subscribe_str", 1, n_kos_dir_subscribe_str, 0},
  // {"kos_dir_unsubscribe_str", 1, n_kos_dir_unsubscribe_str, 0},

  {"set_controlling_pid", 1, n_set_controlling_pid, 0},
  {"reply", 1, n_reply, 0}


};

ERL_NIF_INIT(Elixir.K10.MsgServer, nif_funcs, load, NULL, NULL, NULL)
