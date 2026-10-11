# Configuration roots for the command-line tests

tests/run-cli.sh runs `./shtetl -C` over these. Each is a zconfig
(planning.md 4.4) cut down to what its cases need.

- `site`: shtetl.conf as planning.md 5.2 writes it; default.conf,
  common/web.conf and containers/nginx.conf as 5.4's worked example
  writes them; a foundation with patches and a heredoc; and two
  recipes that are refused (`bad`, `drop`).
- `bare`: no shtetl.conf and no default.conf, so `config show` gives
  the built-in settings.
