# Remote Proxy

A alternataive option for ssh -R

If you have a server behind the NAT, and you want to expose it to public.
The best method is to use a jump server with static public ip address, and run ssh -R from the local server to the public server. But is some situation, ssh is not always alivaible. For example, GatewayPorts is set to no and have no premission to change it. Then you can use this repository to solve your problem.


To use this repository, first compile the main.cpp can copy the execuable file to both two server.

```
HostA$ ssh -R HostC:PortC:HostB:PortB  user@HostC

# is equivalent to 

HostC$ ./remote_proxy server PortC PortE
HostA$ ./remote_proxy client HostB PortB HostC PortE
# PortE is used for communicate between HostA and HostC.
```