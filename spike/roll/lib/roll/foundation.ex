defmodule Roll.Foundation do
  @moduledoc """
  A toy foundation as an actor. Its one message is roll, which writes
  into the foundation's head; every step after that is chosen by
  reading ZFS, never by remembering, so a crash anywhere and a first
  start are one code path:

    snapshot  f@r<k>, when the head was written since f@r<k-1>
    clone     the house version h<k> from f@r<k>, when it is missing
    beam      f@r<k> to the lake, when the lake lacks its guid

  roll(step) kills the actor right after the step named, once, as a
  crash would; the supervisor starts it again and it carries on from
  what ZFS shows. One roll at a time takes no code: each step queues
  the next as an internal event, which runs before any message
  waiting, so a roll is only ever seen when the actor is idle.

  The snapshot names r0, r1, ... are a toy's: the spike measures the
  runtime, not shtetl's naming. The probe sets up r0, h0 and the lake's
  r0 before the release starts.
  """
  @behaviour :gen_statem
  alias Roll.Zfs

  def child_spec(_), do: %{id: __MODULE__, start: {__MODULE__, :start_link, []}}

  def start_link, do: :gen_statem.start_link({:local, __MODULE__}, __MODULE__, nil, [])

  def roll(crash \\ nil), do: :gen_statem.cast(__MODULE__, {:roll, crash})

  def state, do: :gen_statem.call(__MODULE__, :state, 60_000)

  @impl true
  def callback_mode, do: :handle_event_function

  @impl true
  def init(nil), do: {:ok, where(), nil, [{:next_event, :internal, :go}]}

  @impl true
  def handle_event(:internal, :go, :idle, _crash), do: :keep_state_and_data

  def handle_event(:internal, :go, {step, k}, crash) do
    :ok = run(step, k)
    if step == crash, do: Process.exit(self(), :kill)
    {:next_state, where(), crash, [{:next_event, :internal, :go}]}
  end

  def handle_event(:cast, {:roll, crash}, :idle, _) do
    {:next_state, {:write, newest() + 1}, crash, [{:next_event, :internal, :go}]}
  end

  def handle_event({:call, from}, :state, state, _crash) do
    {:keep_state_and_data, [{:reply, from, state}]}
  end

  # Where the foundation stands, from ZFS alone. written@ counts a write
  # only once its txg has committed, so the pool is synced first.
  defp where do
    :ok = Zfs.call({:sync, pool()})
    k = newest()
    {:ok, written} = Zfs.call({:written, fs(), "r#{k}"})

    cond do
      written > 0 -> {:snapshot, k + 1}
      not Zfs.call({:exists, house(k)}) -> {:clone, k}
      guid(lake_snap(k)) != guid(snap(k)) -> {:beam, k}
      true -> :idle
    end
  end

  defp run(:write, k), do: File.write(Path.join(dir(), "r#{k}"), :rand.bytes(1_048_576))
  defp run(:snapshot, k), do: Zfs.call({:snapshot, snap(k)})
  defp run(:clone, k), do: Zfs.call({:clone, house(k), snap(k)})
  defp run(:beam, k), do: Zfs.call({:beam, snap(k - 1), snap(k), lake_snap(k)})

  defp newest do
    {:ok, snaps} = Zfs.call({:snapshots, fs()})
    length(snaps) - 1
  end

  defp guid(name) do
    case Zfs.call({:guid, name}) do
      {:ok, guid} -> guid
      {:error, _} -> nil
    end
  end

  defp pool, do: System.fetch_env!("S1_RUN")
  defp lake, do: System.fetch_env!("S1_LAKE")
  defp dir, do: System.fetch_env!("S1_DIR")
  defp fs, do: "#{pool()}/f"
  defp snap(k), do: "#{fs()}@r#{k}"
  defp house(k), do: "#{pool()}/h#{k}"
  defp lake_snap(k), do: "#{lake()}/f@r#{k}"
end
