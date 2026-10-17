#!/usr/bin/env python3
"""Execute Pepper47's actual UDP methods against a strict PPAPI boundary."""
import importlib.util
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SDK = Path(sys.argv.pop(1)) if len(sys.argv) > 1 else Path(os.environ['NACL_SDK_ROOT'])
spec = importlib.util.spec_from_file_location('repair', ROOT/'scripts/prepare-nacl-udp.py')
repair = importlib.util.module_from_spec(spec)
spec.loader.exec_module(repair)

PREFIX = r'''
#include <cerrno>
#include <cstring>
#include <ppapi/c/pp_errors.h>
#include <ppapi/c/ppb_udp_socket.h>
#include <ppapi/c/samsung/ppb_udp_socket_extension_samsung.h>
#include <sys/socket.h>
#define PPERROR_TO_ERRNO(e) ((e)==PP_OK ? 0 : EACCES)
namespace nacl_io {
typedef int Error;
struct FakeUDP {
  int calls=0, error=0; PP_UDPSocket_Option last; PP_Var value;
  PP_Resource GetBoundAddress(PP_Resource) { return 17; }
  int SetOption(PP_Resource, PP_UDPSocket_Option n, PP_Var v, PP_CompletionCallback) {
    ++calls; last=n; value=v; return error;
  }
};
struct FakeExt {
  PP_MulticastMembership last; int error=0;
  int JoinMulticast(PP_Resource, const PP_MulticastMembership *m, PP_CompletionCallback) {last=*m;return error;}
  int LeaveMulticast(PP_Resource, const PP_MulticastMembership *m, PP_CompletionCallback) {last=*m;return error;}
};
struct UDPInterfaceWrapper10 {
  FakeUDP *udp_interface_; FakeExt *udp_ext_interface;
  int32_t SetOption(PP_Resource, PP_UDPSocket_Option, PP_Var, PP_CompletionCallback);
  int32_t JoinGroup(PP_Resource, PP_Resource, PP_CompletionCallback);
  int32_t LeaveGroup(PP_Resource, PP_Resource, PP_CompletionCallback);
  void SetMulticastMembership(PP_MulticastMembership*, PP_Resource, PP_Resource);
};
struct SocketNode {int SetSockOptSocket(int,const void*,socklen_t){return 0;}};
struct UdpNode:SocketNode {
  FakeUDP *udp_interface_wrapper_; PP_Resource socket_resource_=1;
  Error SetSockOptSocket(int,const void*,socklen_t);
};
'''
SUFFIX = r'''
}
int main(int argc,char **argv) {
  using namespace nacl_io;
  FakeUDP api; FakeExt ext; UDPInterfaceWrapper10 legacy{&api,&ext};
  if(!strcmp(argv[1],"membership")) {
    if(legacy.JoinGroup(1,23,PP_BlockUntilComplete())!=0 || ext.last.interface_addr!=17 || ext.last.multicast_addr!=23 || ext.last.source_addr!=0)return 2;
    if(legacy.LeaveGroup(1,23,PP_BlockUntilComplete())!=0 || ext.last.interface_addr!=17 || ext.last.multicast_addr!=23)return 3;
    ext.error=PP_ERROR_NOACCESS;
    return legacy.JoinGroup(1,23,PP_BlockUntilComplete())==PP_ERROR_NOACCESS?0:4;
  }
  UdpNode node; node.udp_interface_wrapper_=&api;
  int yes=1;
  if(node.SetSockOptSocket(SO_REUSEADDR,&yes,sizeof(yes))!=0 || api.calls!=1 || api.last!=PP_UDPSOCKET_OPTION_ADDRESS_REUSE || api.value.type!=PP_VARTYPE_BOOL || !api.value.value.as_bool)return 5;
  api.error=PP_ERROR_NOACCESS;
  if(node.SetSockOptSocket(SO_REUSEADDR,&yes,sizeof(yes))!=EACCES)return 6;
  return 0;
}
'''


def harness(source):
    start = source.index('int32_t UDPInterfaceWrapper10::SetOption(')
    end = source.index('class UdpWork :', start)
    options = source.index('Error UdpNode::SetSockOptSocket(')
    finish = source.index('Error UdpNode::SetSockOptIP(', options)
    return PREFIX + source[start:end] + source[options:finish] + SUFFIX


class SamsungUDP(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.directory = Path(cls.temp.name)
        original = (SDK/'src/nacl_io/socket/udp_node.cc').read_text()
        for label, source in [('sdk',original),('fixed',repair.patch(original))]:
            path=cls.directory/(label+'.cc');path.write_text(harness(source))
            subprocess.run(['c++','-std=c++11','-I'+str(SDK/'include'),str(path),'-o',str(cls.directory/label)],check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_multicast_interface_and_group_contract(self):
        self.assertEqual(subprocess.run([str(self.directory/'sdk'),'membership']).returncode,2)
        self.assertEqual(subprocess.run([str(self.directory/'fixed'),'membership']).returncode,0)

    def test_reuseaddr_reaches_browser_and_errors_propagate(self):
        self.assertEqual(subprocess.run([str(self.directory/'sdk'),'reuse']).returncode,5)
        self.assertEqual(subprocess.run([str(self.directory/'fixed'),'reuse']).returncode,0)


if __name__=='__main__': unittest.main(verbosity=2)
