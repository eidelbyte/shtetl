# Configuration roots for the command-line tests

tests/run-cli.sh runs `./shtetl -C` over these. Each is a zconfig
(planning.md 4.4) cut down to what its cases need.

- `site`: shtetl.conf as planning.md 5.2 writes it, holding the
  default recipe too (5.4: the shipped ver and kernconf, and
  what 5.4's worked example puts in the default); common/web.conf and
  containers/nginx.conf after the worked example (ver constraints and
  jailconf stacked from several files); a foundation with patches,
  kernconf lines and a heredoc; and two recipes that are refused
  (`bad`; `drop`, which drops a value).
- `bare`: no shtetl.conf, so `config show` gives the built-in
  settings and recipes have no default.
