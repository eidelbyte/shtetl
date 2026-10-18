defmodule Roll.Application do
  @moduledoc false
  use Application

  # rest_for_one: the port's owner first, so that losing the port
  # starts the actor again too, and the actor is never left talking
  # to a port that is gone. The restart budget lets the spike kill the
  # actor once a roll, five rolls in a row.
  @impl true
  def start(_type, _args) do
    Supervisor.start_link([Roll.Zfs, Roll.Foundation],
      strategy: :rest_for_one,
      max_restarts: 20,
      max_seconds: 5,
      name: Roll.Supervisor
    )
  end
end
