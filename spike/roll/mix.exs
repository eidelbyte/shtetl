defmodule Roll.MixProject do
  use Mix.Project

  def project do
    [
      app: :roll,
      version: "0.1.0",
      elixir: "~> 1.15",
      start_permanent: true,
      deps: [],
      releases: [roll: [include_executables_for: [:unix]]]
    ]
  end

  def application do
    [extra_applications: [:logger], mod: {Roll.Application, []}]
  end
end
