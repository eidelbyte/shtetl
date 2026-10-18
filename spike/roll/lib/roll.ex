defmodule Roll do
  @moduledoc "The spike's measures, asked for through bin/roll rpc."

  @doc "The port's round trip, in microseconds, averaged over n pings."
  def port_micros(n) do
    {us, _} = :timer.tc(fn -> for _ <- 1..n, do: :pong = Roll.Zfs.call(:ping) end)
    div(us, n)
  end
end
