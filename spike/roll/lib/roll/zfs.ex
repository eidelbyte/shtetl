defmodule Roll.Zfs do
  @moduledoc """
  The owner of the ZFS port, priv/zport (spike/zport/zport.c): one
  request at a time, each a term out and a term back, nothing parsed.
  If the port exits, so does its owner, and the supervisor starts both
  again.
  """
  use GenServer

  def start_link(_), do: GenServer.start_link(__MODULE__, nil, name: __MODULE__)

  def call(request), do: GenServer.call(__MODULE__, {:request, request}, :infinity)

  @impl true
  def init(nil) do
    path = Application.app_dir(:roll, "priv/zport")
    {:ok, Port.open({:spawn_executable, path}, [:binary, {:packet, 4}, :exit_status])}
  end

  @impl true
  def handle_call({:request, request}, _from, port) do
    true = Port.command(port, :erlang.term_to_binary(request))

    receive do
      {^port, {:data, reply}} -> {:reply, :erlang.binary_to_term(reply), port}
      {^port, {:exit_status, status}} -> {:stop, {:zport, status}, port}
    end
  end
end
