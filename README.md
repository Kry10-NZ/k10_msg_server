# Kry10 Msg Server native bindings

This mix pagage contains simble NIF bindings for calling a KOS msg_server native
interface from Elixir.

## Installation

```elixir
def deps do
  [
    {:k10_msg_server, path: ./path/to/package}
  ]
end
```

A simple server example:

```elixir

  import K10.MsgServer

  @protocol kos_c_string("testprotocol")

  # Settings for publishing the protocol
  @request_label 1
  @request_badge 100
  @request_flags K10.MsgServer.kos_msg_flag_send_payload

  # Settings for client connection token
  @client_badge_start 10
  @client_token_flags K10.MsgServer.kos_msg_flag_send_payload
  @client_response_payload <<0::64, 0::32, 1::16, 0::16, "help\0">>

  def start(_type, _args) do

    # Set up the current pid as the one to receive kos_msg_server messages
    :ok = set_controlling_pid(self())

    # Publish the protocol
    :kos_status_ok = kos_dir_publish_str(@protocol, @request_label, @request_badge, @request_flags)

    # Read the protocol publish status.
    :kos_status_ok = kos_dir_query_str(@protocol)

    # Allocate a new empty token slot for creating new connection tokens.
    {:kos_status_ok, new_token_slot} = kos_msg_token_slot_pool_alloc()

    # Start receiving loop.
    receive_loop(new_token_slot, 0)

  end

  defp receive_loop(new_token_slot, num_clients) do
    receive do
      {:kos_msg, :up} ->
        # We are successfully receiving kos_msg_server messages.
        IO.puts("Server receiving messages:")
        receive_loop(new_token_slot, num_clients) 

      {:kos_msg, @request_badge, _caller, _msg} ->
        # Connection request
        IO.puts("Request protocol")

        # create a new token with a custom badge
        new_badge = @client_badge_start + num_clients
        :kos_status_created = kos_msg_token_create(new_badge, @client_token_flags, new_token_slot)
        
        # reply to the connection request with a new token for the client to use.
        client_connect_response = kos_msg_new(K10.MsgServer.kos_status_ok, 0, new_token_slot, @client_response_payload)
        :ok = reply(client_connect_response)
        receive_loop(new_token_slot, num_clients + 1) 

      {:kos_msg, badge, caller, kos_msg} ->
        # Message from a client
        IO.puts("Regular message")

        # Print message and reply with dummy response.
        IO.inspect({kos_msg, badge, caller})
        :ok = reply(kos_msg_new(600, 0, 0, "No funds"))
        receive_loop(new_token_slot, num_clients) 
    end
  end

```

A simple client example:

```elixir
  import K10.MsgServer

  @protocol kos_c_string("testprotocol")

  @connection_request_msg kos_msg_new(0, 43, 0, "hi")
  @regular_msg kos_msg_new(1, 0, 0, "a_message")
  @regular_reply_flags K10.MsgServer.kos_msg_flag_send_payload
  def start(_type, _args) do
    hello()

    # Wait for the protocol to get published
    :kos_status_ok = check_published(@protocol)

    # Allocate an empty token slot and request a connection
    {:kos_status_ok, token} = kos_msg_token_slot_pool_alloc()
    {:kos_status_ok, _response_msg} = IO.inspect(kos_dir_request_str(@protocol, token, @connection_request_msg))

    # Once connection is successful send another message using the new token
    {:kos_status_ok, _response_msg} = IO.inspect(kos_msg_call(token, @regular_reply_flags, @regular_msg))

    {:ok, self()}
  end
```
