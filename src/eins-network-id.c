/* Copyright 2017 Endless Mobile, Inc.
 *
 * This file is part of eos-metrics-instrumentation.
 *
 * eos-metrics-instrumentation is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or (at your
 * option) any later version.
 *
 * eos-metrics-instrumentation is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with eos-metrics-instrumentation.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <glib.h>
#include <stdio.h>

#include "eins-network-id.h"

#define PROC_IPV4_ROUTE_FILE "/proc/net/route"
#define IPV4_ROUTE_REGEXP "^(?:\\S+\\s+){2}(?P<Gateway>[0-9A-Fa-f]{8})\\s+(?:\\S+\\s+){4}(?P<Mask>00000000)"

/*
 * greps the /proc/net/route file for the IPv4 default route. note that even
 * on machines with multiple interfaces, the first listed 00000000 route will
 * be the first one selected for outbound traffic, so we don’t need to do any
 * more complex searching/sorting here (verified in net/ipv4/fib_trie.c on
 * kernel 4.10.5 - the trie is ordered by (prefix,tos,priority) so the highest
 * preference default route will appear first)
 */
static gchar *
get_ipv4_default_gateway (void)
{
  g_autofree gchar *route = NULL;
  g_autoptr(GError) err = NULL;

  if (!g_file_get_contents (PROC_IPV4_ROUTE_FILE, &route, NULL, &err))
    {
      g_debug ("could not load " PROC_IPV4_ROUTE_FILE ": %s", err->message);
      return NULL;
    }

  g_autoptr(GRegex) route_rx = g_regex_new (IPV4_ROUTE_REGEXP, G_REGEX_MULTILINE, 0, NULL);
  g_assert (route_rx != NULL);

  g_autoptr(GMatchInfo) route_mi = NULL;
  if (!g_regex_match (route_rx, route, 0, &route_mi))
    {
      g_debug ("couldn’t match IPv4 default gateway in " PROC_IPV4_ROUTE_FILE);
      return NULL;
    }

  const gchar *gateway = g_match_info_fetch_named (route_mi, "Gateway");
  g_assert (gateway != NULL);

  /* the input to this g_ascii_strtoull call should already be
   * rigorously validated by the regexp: failure is not an option */
  gchar *endptr = NULL;
  errno = 0;
  guint32 gateway_ip = (guint32) g_ascii_strtoull (gateway, &endptr, 16);
  g_assert (errno == 0);
  g_assert (gateway + 8 == endptr);
  gateway_ip = htonl (gateway_ip);

  return g_strdup_printf ("%u.%u.%u.%u",
                          gateway_ip >> 24,
                          0xFF & (gateway_ip >> 16),
                          0xFF & (gateway_ip >> 8),
                          0xFF & gateway_ip);
}

#define PROC_IPV4_ARP_FILE "/proc/net/arp"
#define IPV4_ARP_REGEXP "^%s\\s+(\\S+\\s+){2}(?P<HW>[0-9A-Fa-f:]{17})"

/*
 * find the given IPv4 address in the ARP cache to find the HW address.
 * relies on two assumptions:
 *  - the first (obviously) is that the IP has been communicated with recently
 *    enough to be in the cache. for an internet-connected host, this is a
 *    pretty safe bet for the gateway IP.
 *  - the second is that if the host has multiple interfaces which are in the
 *    same IPv4 segment, that these are the same physical network, because the
 *    search is not constrained by interface which would be the strictly
 *    correct thing to do. this configuration seems pretty unlikely (two bonded
 *    load-balanced links with separate gateways?) on a client machine, and for
 *    our purposes it doesn’t matter if we always report one gateway, or report
 *    both over time, so it’s not worth getting excited about.
 */
static gchar *
get_ipv4_hwaddr (const gchar *address)
{
  g_autofree gchar *arp = NULL;
  g_autoptr(GError) err = NULL;

  if (!g_file_get_contents (PROC_IPV4_ARP_FILE, &arp, NULL, &err))
    {
      g_debug ("could not load " PROC_IPV4_ARP_FILE ": %s", err->message);
      return NULL;
    }

  g_autofree gchar *address_esc = g_regex_escape_string (address, -1);
  g_autofree gchar *arp_rx_raw = g_strdup_printf (IPV4_ARP_REGEXP, address_esc);
  g_autoptr(GRegex) arp_rx = g_regex_new (arp_rx_raw, G_REGEX_MULTILINE, 0, NULL);
  g_assert (arp_rx != NULL);

  g_autoptr(GMatchInfo) arp_mi = NULL;
  if (!g_regex_match (arp_rx, arp, 0, &arp_mi))
    {
      g_debug ("couldn’t find HW address in " PROC_IPV4_ARP_FILE);
      return NULL;
    }

  const gchar *hw = g_match_info_fetch_named (arp_mi, "HW");
  g_assert (hw != NULL);
  return g_ascii_strdown (hw, -1);
}

/*
 * the format of each line is:
 * <dest 32> <prefix 2> <src 32> <prefix 2> <gateway 32> \
 *  <metric 8> <refcount 8> <use? 8> <flags 8> <if>
 * we want to match dest/prefix of 0/0, any src/prefix, capture the gateway,
 * any metric/ref/use, and match flags ending 3, meaning RTF_UP + RTF_GATEWAY
 */
#define PROC_IPV6_ROUTE_FILE "/proc/net/ipv6_route"
#define IPV6_ROUTE_REGEXP "^0{32}\\s00\\s[0-9A-Fa-f]{32}\\s[0-9A-Fa-f]{2}\\s(?P<Gateway>[0-9A-Fa-f]{32})\\s([0-9A-Fa-f]{8}\\s){3}[0-9A-Fa-f]{7}3"

/*
 * greps the /proc/net/ipv6_route file for the first IPv6 default route. same
 * assumptions as above apply.
 */
static gchar *
get_ipv6_default_gateway (void)
{
  g_autofree gchar *route = NULL;
  g_autoptr(GError) err = NULL;

  if (!g_file_get_contents (PROC_IPV6_ROUTE_FILE, &route, NULL, &err))
    {
      g_debug ("could not load " PROC_IPV6_ROUTE_FILE ": %s", err->message);
      return NULL;
    }

  g_autoptr(GRegex) route_rx = g_regex_new (IPV6_ROUTE_REGEXP, G_REGEX_MULTILINE, 0, NULL);
  g_assert (route_rx != NULL);

  g_autoptr(GMatchInfo) route_mi = NULL;
  if (!g_regex_match (route_rx, route, 0, &route_mi))
    {
      g_debug ("couldn’t match IPv6 default gateway in " PROC_IPV6_ROUTE_FILE);
      return NULL;
    }

  const gchar *gateway = g_match_info_fetch_named (route_mi, "Gateway");
  g_assert (gateway != NULL);

  /* the input to this sscanf call is validated by the regexp above */
  guint32 addr[8];
  if (sscanf(gateway,"%4x%4x%4x%4x%4x%4x%4x%4x",
             &addr[0], &addr[1], &addr[2], &addr[3],
             &addr[4], &addr[5], &addr[6], &addr[7]) != 8)
    {
      g_assert_not_reached ();
      return NULL;
    }

  return g_strdup_printf ("%x:%x:%x:%x:%x:%x:%x:%x",
                          addr[0], addr[1], addr[2], addr[3],
                          addr[4], addr[5], addr[6], addr[7]);
}

#define IPV6_NDISC_REGEXP "^(\\S+\\s+){3}lladdr\\s(?P<HW>[0-9A-Fa-f:]{17})"

/*
 * no IPv6 equivalent of /proc/net/arp exists to view the neighbour
 * discovery (ndisc) cache, so rather than breaking out netlink, we
 * invoke the "ip" command for sanity’s sake.
 */
static gchar *
get_ipv6_hwaddr (const gchar *address)
{
  const gchar *ndisc_cmd[] = { "ip", "-6", "neigh", "show", address, NULL };
  g_autofree gchar *ndisc = NULL;
  g_autofree gchar *ndisc_err = NULL;
  g_autoptr(GError) err = NULL;
  gint ret = 0;

  if (!g_spawn_sync (NULL, (gchar **) &ndisc_cmd[0], NULL,
                     G_SPAWN_SEARCH_PATH, NULL, NULL,
                     &ndisc, &ndisc_err, &ret, &err) || ret)
    {
      g_autofree gchar *cmd = g_strjoinv (" ", (gchar **) ndisc_cmd);
      g_debug ("could not execute command \"%s\" (ret %i): %s", cmd, ret,
               err ? err->message : ndisc_err);
      return NULL;
    }

  g_autoptr(GRegex) ndisc_rx = g_regex_new (IPV6_NDISC_REGEXP, G_REGEX_MULTILINE, 0, NULL);
  g_assert (ndisc_rx != NULL);

  g_autoptr(GMatchInfo) ndisc_mi = NULL;
  if (!g_regex_match (ndisc_rx, ndisc, 0, &ndisc_mi))
    {
      g_autofree gchar *cmd = g_strjoinv (" ", (gchar **) ndisc_cmd);
      g_debug ("couldn’t find HW address in \"%s\" output: %s", cmd, ndisc);
      return NULL;
    }

  const gchar *hw = g_match_info_fetch_named (ndisc_mi, "HW");
  g_assert (hw != NULL);
  return g_ascii_strdown (hw, -1);
}

/*
 * returns a 32-bit unsigned integer which is a short hash of the ethernet MAC
 * address of the IPv4 (by preference) or IPv6 default gateway of the system.
 * the intention is to provide an opaque and stable identifier which will be
 * the same for all hosts on the same physical network. returns TRUE if *id has
 * been set, FALSE otherwise.
 */
gboolean
eins_network_id_get (guint32 *id)
{
  g_autofree gchar *hwaddr = NULL;

  g_return_val_if_fail (id != NULL, FALSE);

  g_autofree gchar *ipv4_gateway = get_ipv4_default_gateway ();
  if (ipv4_gateway != NULL)
    {
      hwaddr = get_ipv4_hwaddr (ipv4_gateway);
      g_debug ("got IPv4 gateway %s with HW address %s", ipv4_gateway, hwaddr);
    }

  if (hwaddr == NULL)
    {
      g_autofree gchar *ipv6_gateway = get_ipv6_default_gateway ();
      if (ipv6_gateway != NULL)
        {
          hwaddr = get_ipv6_hwaddr (ipv6_gateway);
          g_debug ("got IPv6 gateway %s with HW address %s", ipv6_gateway, hwaddr);
        }
    }

  if (hwaddr == NULL)
    {
      g_debug ("no IPv4 or IPv6 gateway found");
      return FALSE;
    }

  g_autoptr(GChecksum) checksum = g_checksum_new (G_CHECKSUM_SHA512);
  g_checksum_update (checksum, (guint8 *) hwaddr, -1);
  guint32 bytes[16];
  gsize len = sizeof(bytes);
  g_checksum_get_digest (checksum, (guint8 *) &bytes, &len);
  g_assert (len == sizeof(bytes));

  *id = bytes[0];
  return TRUE;
}
