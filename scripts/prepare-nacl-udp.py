#!/usr/bin/env python3
"""Repair the verified Pepper47 UDP implementation in a separate build copy."""
from pathlib import Path
import sys


def patch(source):
    old = 'SetMulticastMembership(&membership, group, multicast_addr);'
    if source.count(old) != 2:
        raise ValueError('Unexpected Pepper47 multicast wrapper; review SDK changes')
    source = source.replace(old, 'SetMulticastMembership(&membership, multicast_addr, group);')
    old = '  switch (optname) {\n    case SO_RCVBUF: {'
    if source.count(old) != 1:
        raise ValueError('Unexpected Pepper47 UDP socket options')
    source = source.replace(old, '''  switch (optname) {
    case SO_REUSEADDR: {
      int reuse = *static_cast<const int*>(optval);
      int32_t error = udp_interface_wrapper_->SetOption(
          socket_resource_, PP_UDPSOCKET_OPTION_ADDRESS_REUSE,
          PP_MakeBool(reuse ? PP_TRUE : PP_FALSE), PP_BlockUntilComplete());
      return PPERROR_TO_ERRNO(error);
    }
    case SO_RCVBUF: {''')
    return source + '\nextern "C" int airtizen_nacl_udp_multicast_v1(void) { return 1; }\n'


if __name__ == '__main__':
    Path(sys.argv[2]).write_text(patch(Path(sys.argv[1]).read_text()))
