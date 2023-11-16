
# icbirc-portable

A portable version of **[ircicb](https://www.benzedrine.ch/icbirc.html)**, a proxy that allows to connect an IRC client to an ICB server.

Initial `icbirc` version for OpenBSD by Daniel Hartmeier.

The following operating systems are supported:

  - [x] Linux
  - [ ] OpenBSD
  - [ ] FreeBSD

## Dependencies

**Linux:**

- `libbsd` for BSD `strlcpy` function

## Installation

**Linux:**

  - build with GNU `make`

## Usage

```bash
icbirc [-d] -c conffile | [-l address] [-p port] -s server [-P port]
```

The options are as follows:

- `-d` Do not daemonize (detach from controlling terminal) and produce debugging
  output on stdout/stderr.

- `-c` Configuration file (TOML format)

- `-l listen-address` Bind to the specified address when listening for client
  connections.  If not specified, connections to any address are accepted.

- `-p listen-port` Bind to the specified port when listening for client
  connections.  Defaults to 6667 when not specified.

- `-s server-name` Hostname or numerical address of the ICB server to connect to.

- `-P server-port` Port of the ICB server to connect to.  Defaults to 7326 when
  not specified.

Configuration file (set with `-c`) and server-name (set with `-s`) are mutually
exclusive options.

## TODO

- Add configuration file (format TOML)
- Add logs for debug and output with syslog
- Add init scripts for BSD
- Add SystemD service for Linux

## Related links

- [ICB on Wikipedia](https://en.wikipedia.org/wiki/Internet_Citizen%27s_Band)
- [Internet CB NETwork](http://www.icb.net/) web site about ICB and list of current ICB servers
- [The History of ICB](http://www.icb.net/history.html) by John Rudd and Jon Luini
- [General guide to Netiquette on ICB](http://www.icb.net/_jrudd/icb/netiquette.html)
- [irssi-icb](https://github.com/mglocker/irssi-icb) ICB plugin for irssi (IRC client)
- [ICB Protocol](http://www.icb.net/_jrudd/icb/protocol.html) describes the format of ICB commands and messages
- [RFC 1459](http://www.faqs.org/rfcs/rfc1459.html) Internet Relay Chat Protocol
- [RFC 2812](http://www.faqs.org/rfcs/rfc2812.html) Internet Relay Chat: Client Protocol
- [RFC 2811](http://www.faqs.org/rfcs/rfc2811.html) Internet Relay Chat: Channel Management
