
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

XXX

## TODO

- Add configuration file (format TOML)
- Add init scripts for BSD
- Add SystemD service for Linux

## Related links

- [ICB on Wikipedia](https://en.wikipedia.org/wiki/Internet_Citizen%27s_Band)
- [Internet CB NETwork](http://www.icb.net/) web site about ICB and list of current ICB servers
- [The History of ICB](http://www.icb.net/history.html) by John Rudd and Jon Luini
- [General guide to Netiquette on ICB](http://www.icb.net/_jrudd/icb/netiquette.html)
- [irssi-icb](https://github.com/jperkin/irssi-icb) ICB plugin for irssi (IRC client)
- [ICB Protocol](http://www.icb.net/_jrudd/icb/protocol.html) describes the format of ICB commands and messages
- [RFC 1459](http://www.faqs.org/rfcs/rfc1459.html) Internet Relay Chat Protocol
- [RFC 2812](http://www.faqs.org/rfcs/rfc2812.html) Internet Relay Chat: Client Protocol
- [RFC 2811](http://www.faqs.org/rfcs/rfc2811.html) Internet Relay Chat: Channel Management
