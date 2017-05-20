# Proto-ContainerLib
a prototype container library poorly written from scratch for fun and learning containerization technology.

Run build.sh for compilation
Run sudo ./container --rootfs=/path/to/rootfs

Since I had already installed Docker, there was docker0 vritual bridge and all it's related IPTABLE rules. So I used existing bridge and iptables rules since I was too lazy to create my own. If you don't have docker0 bridge and iptable rules then this wont work. If wanna make it work, add code yourself :p


