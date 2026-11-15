{ config, lib, pkgs, ... }:

let
  inherit (lib) mkOption mkEnableOption mkIf types;
  cfg = config.services.schcvx;

  assocModule = { name, ... }: {
    options = {
      local_ip = mkOption { type = types.str; description = "Local IPv4 underlay address."; };
      peer_ip = mkOption { type = types.str; description = "Peer IPv4 underlay address."; };
      udp_port = mkOption { type = types.ints.between 1 65535; default = 60789; description = "UDP destination port, both directions."; };
      src_port = mkOption { type = types.ints.between 1 65535; default = 60789; description = "Fixed UDP source port of the transmit socket."; };
      variant = mkOption { type = types.enum [ "compact" "extended" ]; default = "compact"; description = "Wire variant."; };
      fr_rule_id = mkOption { type = types.ints.between 0 255; default = 1; description = "Fragmentation RuleID."; };
      cd_rule_id = mkOption { type = types.ints.between 0 255; default = 0; description = "Compression/decompression RuleID."; };
      vnis = mkOption { type = types.nonEmptyListOf (types.ints.between 0 16777215); description = "Admitted VNIs; the first is used for transmission."; };
      tap = mkOption { type = types.str; description = "TAP device name (must be unique across associations)."; };
      pmtu = mkOption { type = types.ints.unsigned; default = 1280; description = "Underlay path MTU in bytes."; };
      h_ip = mkOption { type = types.ints.unsigned; default = 20; description = "IP header length in bytes."; };
      max_frame_len = mkOption { type = types.ints.unsigned; default = 1518; description = "Maximum Ethernet frame length in bytes."; };
      max_fragments = mkOption { type = types.ints.between 2 8; default = 8; description = "Maximum fragments per packet."; };
      receiver = mkOption { type = types.enum [ "ordered" "tolerant" ]; default = "ordered"; description = "Receiver type."; };
      S = mkOption { type = types.ints.unsigned; default = 1000000; description = "Timer S in nanoseconds."; };
      B = mkOption { type = types.ints.unsigned; default = 10000000; description = "Timer B in nanoseconds."; };
      I = mkOption { type = types.ints.unsigned; default = 10000000; description = "Timer I in nanoseconds."; };
      Delta = mkOption { type = types.ints.unsigned; default = 20000000; description = "Timer Delta in nanoseconds."; };
      R = mkOption { type = types.ints.unsigned; default = 100000000; description = "Retirement time R in nanoseconds."; };
      Q = mkOption { type = types.ints.unsigned; default = 200000000; description = "Startup quarantine Q in nanoseconds."; };
      max_entries = mkOption { type = types.ints.positive; default = 16384; description = "Per-association receiver entries."; };
      max_bytes = mkOption { type = types.ints.positive; default = 33554432; description = "Per-association retained bytes."; };
      orphan_budget = mkOption { type = types.ints.unsigned; default = 2048; description = "Per-association orphan budget."; };
      dtag_start = mkOption { type = types.ints.unsigned; default = 0; description = "First DTag value."; };
      dtag_rate = mkOption { type = types.ints.positive; default = 1000000; description = "DTag allocations per second; must exceed the expected fragmented frame rate."; };
      dtag_burst = mkOption { type = types.ints.positive; default = 10000; description = "DTag allocation burst."; };
      counters_file = mkOption { type = types.str; default = "/run/schcvx/${name}.counters"; description = "Counters file; rewritten periodically and on SIGUSR1."; };
      counters_interval = mkOption { type = types.ints.positive; default = 1000000000; description = "Counters write interval in nanoseconds."; };

      bridge = mkOption {
        type = types.nullOr types.str;
        default = null;
        description = ''
          Bridge name. When null (default) no network interface is touched.
          When set, the bridge is created if missing and the TAP device (and
          accessInterface, if given) is enslaved to it. IP addresses are never
          changed: an enslaved interface loses its address until the operator
          moves it to the bridge.
        '';
      };
      accessInterface = mkOption {
        type = types.nullOr types.str;
        default = null;
        description = "Access port to enslave to the bridge (requires bridge).";
      };
    };
  };

  keyOrder = [
    "local_ip" "peer_ip" "udp_port" "src_port" "variant" "fr_rule_id" "cd_rule_id"
    "vnis" "tap" "pmtu" "h_ip" "max_frame_len" "max_fragments" "receiver"
    "S" "B" "I" "Delta" "R" "Q" "max_entries" "max_bytes" "orphan_budget"
    "dtag_start" "dtag_rate" "dtag_burst" "counters_file" "counters_interval"
  ];

  render = v:
    if builtins.isList v then lib.concatMapStringsSep "," toString v
    else toString v;

  assocText = name: a:
    "[association ${name}]\n"
    + lib.concatMapStrings (k: "${k} = ${render a.${k}}\n") keyOrder;

  configFile = pkgs.writeText "schcvx.conf" (
    "[global]\n"
    + "max_entries = ${toString cfg.max_entries}\n"
    + "max_bytes = ${toString cfg.max_bytes}\n"
    + "orphan_budget = ${toString cfg.orphan_budget}\n"
    + lib.concatStrings (lib.mapAttrsToList assocText cfg.associations)
  );

  bridged = lib.filterAttrs (_: a: a.bridge != null) cfg.associations;

  bridgeScript = pkgs.writeShellScript "schcvx-bridge" ''
    set -eu
    ip=${pkgs.iproute2}/bin/ip
    wait_for() {
      i=0
      while ! $ip link show dev "$1" >/dev/null 2>&1; do
        i=$((i + 1))
        if [ "$i" -gt 100 ]; then echo "timeout waiting for $1" >&2; exit 1; fi
        sleep 0.1
      done
    }
    attach() { # bridge tap access
      wait_for "$2"
      if ! $ip link show dev "$1" >/dev/null 2>&1; then $ip link add name "$1" type bridge; fi
      $ip link set dev "$1" up
      $ip link set dev "$2" master "$1"
      $ip link set dev "$2" up
      if [ -n "$3" ]; then
        $ip link set dev "$3" master "$1"
        $ip link set dev "$3" up
      fi
    }
    ${lib.concatStrings (lib.mapAttrsToList (_: a:
      "attach ${lib.escapeShellArg a.bridge} ${lib.escapeShellArg a.tap} ${lib.escapeShellArg (if a.accessInterface == null then "" else a.accessInterface)}\n"
    ) bridged)}
  '';

  taps = lib.mapAttrsToList (_: a: a.tap) cfg.associations;
in
{
  options.services.schcvx = {
    enable = mkEnableOption "the SCHC VXLAN fragmentation daemon";
    package = mkOption {
      type = types.package;
      default = pkgs.callPackage ./package.nix { };
      defaultText = "the schcvxd package of this flake";
      description = "The schcvxd package.";
    };
    max_entries = mkOption { type = types.ints.positive; default = 32768; description = "Global receiver entry limit."; };
    max_bytes = mkOption { type = types.ints.positive; default = 67108864; description = "Global retained byte limit."; };
    orphan_budget = mkOption { type = types.ints.positive; default = 4096; description = "Global orphan budget."; };
    associations = mkOption {
      type = types.attrsOf (types.submodule assocModule);
      default = { };
      description = "Peer associations, keyed by name.";
    };
  };

  config = mkIf cfg.enable {
    assertions = [
      {
        assertion = cfg.associations != { };
        message = "services.schcvx: at least one association is required.";
      }
      {
        assertion = lib.length taps == lib.length (lib.unique taps);
        message = "services.schcvx: two associations use the same tap name.";
      }
      {
        assertion = lib.all (a: a.accessInterface == null || a.bridge != null) (lib.attrValues cfg.associations);
        message = "services.schcvx: accessInterface requires bridge.";
      }
    ];

    systemd.services.schcvx = {
      description = "SCHC VXLAN fragmentation daemon";
      wantedBy = [ "multi-user.target" ];
      wants = [ "network-online.target" ];
      after = [ "network-online.target" ];
      serviceConfig = {
        ExecStart = "${cfg.package}/bin/schcvxd -c ${configFile}";
        # "+" runs this one command with full privileges (it manages bridges).
        ExecStartPost = mkIf (bridged != { }) "+${bridgeScript}";
        Restart = "on-failure";
        RestartSec = 2;
        RuntimeDirectory = "schcvx";
        DynamicUser = true;
        AmbientCapabilities = [ "CAP_NET_ADMIN" "CAP_NET_RAW" ];
        CapabilityBoundingSet = [ "CAP_NET_ADMIN" "CAP_NET_RAW" ];
        DevicePolicy = "closed";
        DeviceAllow = [ "/dev/net/tun rw" ];
        NoNewPrivileges = true;
      };
    };
  };
}
