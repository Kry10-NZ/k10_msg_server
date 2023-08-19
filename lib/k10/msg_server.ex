# Copyright (c) 2023, Kry10 Limited. All rights reserved.
#
# SPDX-License-Identifier: LicenseRef-Kry10

defmodule K10.MsgServer do
  @compile {:autoload, false}
  @on_load :load_nifs

  import Bitwise

  @kos_status_ok 11
  defmacro kos_status_ok, do: @kos_status_ok

  #define KOS_MSG_TOKENS_PER_APP_BITS 6
  @kos_msg_tokens_per_app_bits 6
  defmacro kos_msg_tokens_per_app_bits, do: @kos_msg_tokens_per_app_bits

  #define KOS_MSG_TOKENS_PER_APP (1 << KOS_MSG_TOKENS_PER_APP_BITS)
  @kos_msg_tokens_per_app 1 <<< @kos_msg_tokens_per_app_bits
  defmacro kos_msg_tokens_per_app, do: @kos_msg_tokens_per_app

  #define KOS_MSG_TOKEN_SLOT_MASK (KOS_MSG_TOKENS_PER_APP - 1)
  @kos_msg_token_slot_mask (@kos_msg_tokens_per_app - 1)
  defmacro kos_msg_token_slot_mask, do: @kos_msg_token_slot_mask

  #define KOS_MSG_FLAG_SEND_PAYLOAD (1 << 0)
  @kos_msg_flag_send_payload (1<<<0)
  defmacro kos_msg_flag_send_payload, do: @kos_msg_flag_send_payload

  #define KOS_MSG_FLAG_SEND_TOKEN (1 << 1)
  @kos_msg_flag_send_token (1<<<1)
  defmacro kos_msg_flag_send_token, do: @kos_msg_flag_send_token

  #define KOS_MSG_TOKEN_FLAG_SINGLE_USE (1 << 5)
  @kos_msg_token_flag_single_use (1<<<5)
  defmacro kos_msg_token_flag_single_use, do: @kos_msg_token_flag_single_use


  #define KOS_MSG_PAYLOAD_MAX_SIZE KOS_MSG_TRANSPORT_PAYLOAD_MAX_SIZE
  @kos_msg_payload_max_size 4096
  defmacro kos_msg_payload_max_size, do: @kos_msg_payload_max_size

  defmacro kos_c_string(binary), do: binary <> "\0"

  def kos_msg_new(label, param, transfer_token, payload), do: {label, param, transfer_token, payload}

  def set_controlling_pid(_pid), do: :erlang.nif_error("Did not find set_controlling_pid")
  def reply(_opts), do: :erlang.nif_error("Did not find reply")
  def kos_msg_ping(), do: :erlang.nif_error("Did not find kos_msg_ping")

  def kos_msg_token_slot_pool_alloc(), do: :erlang.nif_error("Did not find kos_msg_token_slot_pool_alloc")
  def kos_msg_token_create(_badge, _flags, _token_slot), do: :erlang.nif_error("Did not find kos_msg_token_create")
  def kos_msg_token_revoke(_badge), do: :erlang.nif_error("Did not find kos_msg_token_revoke")
  def kos_msg_token_delete(_token_slot), do: :erlang.nif_error("Did not find kos_msg_token_delete")
  def kos_msg_token_move(_dst_token_slot, _src_token_slot), do: :erlang.nif_error("Did not find kos_msg_token_move")
  def kos_msg_token_info(_token_slot), do: :erlang.nif_error("Did not find kos_msg_token_info")

  def kos_msg_call(_token_slot, _reply_flags, _msg), do: :erlang.nif_error("Did not find kos_msg_call")
  def kos_msg_call_dirty(_token_slot, _reply_flags, _msg), do: :erlang.nif_error("Did not find kos_msg_call_dirty")
  def kos_msg_send(_token_slot, _msg), do: :erlang.nif_error("Did not find kos_msg_send")
  def kos_msg_send_dirty(_token_slot, _msg), do: :erlang.nif_error("Did not find kos_msg_send_dirty")

  def kos_dir_publish_str(_protocol_name, _request_label, _request_badge, _request_flags), do: :erlang.nif_error("Did not find kos_dir_publish_str")
  def kos_dir_unpublish_str(_protocol_name), do: :erlang.nif_error("Did not find kos_dir_unpublish_str")
  def kos_dir_query_str(_protocol_name), do: :erlang.nif_error("Did not find kos_dir_query_str")
  def kos_dir_request_str(_protocol_name, _empty_token_slot, _msg), do: :erlang.nif_error("Did not find kos_dir_request_str")



  @doc false
  def load_nifs do
    :ok = case :os.type() do
      {:unix, :kos} ->
        :k10_msg_server
          |> :code.priv_dir()
          |> :filename.join('kos_msg_nif')
          |> :erlang.load_nif(0)
      _ ->
        :ok
    end
  end
end
