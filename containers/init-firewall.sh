#!/bin/bash
# The egress wall of one implementer container, after the reference firewall of Claude Code's dev container:
# default deny, an ipset allowlist of the addresses the allowed hosts resolve to now, DNS through Docker's resolver.
#
# It runs as root in the container's network sidecar (impl-N-net), the only container that holds NET_ADMIN and
# NET_RAW; the implementer container joins the sidecar's network namespace with no capabilities at all, so it can
# neither change these rules nor leave them. The rules are checked before the sidecar reports healthy, and the
# implementer container starts only once it does (compose.yml).
set -euo pipefail
IFS=$'\n\t'

ALLOWED=/etc/coney/allowed-domains
DENIED_PROBE=https://example.com # any host not on the list
READY=/run/coney/firewall-ok

fail() {
    echo "coney-firewall: $*" >&2
    exit 1
}

[ "$(id -u)" = 0 ] || fail "must run as root"
rm -f "$READY"

# Start clean: only the filter tables are flushed. The nat table holds Docker's rules for its embedded DNS
# (127.0.0.11), which stay as they are.
for t in iptables ip6tables; do
    "$t" -P INPUT DROP
    "$t" -P FORWARD DROP
    "$t" -P OUTPUT DROP
    "$t" -F
    "$t" -X
    # Loopback carries the DNS questions to Docker's resolver; replies to what this side opened come back.
    "$t" -A INPUT -i lo -j ACCEPT
    "$t" -A OUTPUT -o lo -j ACCEPT
    "$t" -A INPUT -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT
    "$t" -A OUTPUT -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT
done

ipset destroy allowed 2>/dev/null || true
ipset create allowed hash:net family inet
mapfile -t hosts < <(grep -Ev '^[[:space:]]*(#|$)' "$ALLOWED")
[ "${#hosts[@]}" -gt 0 ] || fail "no allowed host in $ALLOWED"
for host in "${hosts[@]}"; do
    mapfile -t addrs < <(getent ahostsv4 "$host" | awk '{print $1}' | sort -u)
    [ "${#addrs[@]}" -gt 0 ] || fail "cannot resolve $host"
    for a in "${addrs[@]}"; do
        [[ "$a" =~ ^[0-9]{1,3}(\.[0-9]{1,3}){3}$ ]] || fail "$host resolved to something that is not an IPv4 address"
        ipset add allowed "$a" -exist
        echo "coney-firewall: allow $host $a"
    done
done

iptables -A OUTPUT -p tcp --dport 443 -m set --match-set allowed dst -j ACCEPT
# Refuse the rest at once rather than letting it time out, so a tool fails fast and says why.
for t in iptables ip6tables; do
    "$t" -A OUTPUT -p tcp -j REJECT --reject-with tcp-reset
    "$t" -A OUTPUT -j REJECT
done

# Check the wall before reporting healthy: a host off the list must fail, every listed host must connect.
if curl --silent --max-time 5 --output /dev/null "$DENIED_PROBE"; then
    fail "$DENIED_PROBE is reachable: the wall does not stand"
fi
for host in "${hosts[@]}"; do
    curl --silent --show-error --max-time 15 --output /dev/null "https://$host/" || fail "$host does not connect"
done

mkdir -p "$(dirname "$READY")"
date -u +%Y-%m-%dT%H:%M:%SZ >"$READY"
echo "coney-firewall: ready"
