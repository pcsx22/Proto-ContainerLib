#include <iostream>
#include <unistd.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <algorithm>
#include <time.h>
#include <map>
#include <getopt.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <netlink/netlink.h>
#include <netlink/route/link.h>
#include <netlink/route/link/bridge.h>
#include <netlink/route/link/veth.h>
#include <netlink/route/addr.h>
#include <linux/netlink.h>
#include <fts.h>
#include <libcgroup.h>

#define STACK_SIZE 1024 * 1024 * 10
#define HOST_BRIDGE_NAME "docker0"
using namespace std;

map<string,string> get_dir_mount_name(string root_dir){

	srand(time(NULL));
	int r = rand() % 10000;

	string rw_layer = "/tmp/RWLayer" + std::to_string(r);
	string mount_opt = string("br=/tmp/RWLayer=rw:") + root_dir + string(",udba=reval") ;
	mount_opt.insert(15,to_string(r));
	string mount_point = "pseudo_rootfs" + to_string(r);
	map<string,string> data;
	data["rw_layer"] = rw_layer;
	data["mount_point"] = mount_point;
	data["mount_opt"] = mount_opt;
	return data;
}

//setting up IFF_UP flag with libnl didn't work so had to use ip command
int iface_up(string iface_name){
	string cmd_str = "ip link set dev   up";
	cmd_str.insert(17,iface_name);
	return system(cmd_str.c_str());
}

//setting up addr with libnl didn't work so had to use ip commands
int iface_add_addr(string iface_name,string addr){
	string cmd_str = "ip addr add   dev ";
	cmd_str.insert(18,iface_name);
	cmd_str.insert(13,addr);
	return system(cmd_str.c_str());
}



static int childFunc(void * arg){
	char ** a = (char **)arg;
	string veth_container = string(a[0]);
	string addr_str = string(a[1]);
	string root_dir = string(a[2]);
	cout<<"Container init..\n";

	map<string,string> data = get_dir_mount_name(root_dir);
	if(mkdir(data["rw_layer"].c_str(),0777) == -1)
		perror("Error RW LAYER: ");

	if(mkdir(data["mount_point"].c_str(),0777) == -1)
		perror("Error Mount Directory: ");


	if(mount("",data["mount_point"].c_str(),"aufs",NULL,data["mount_opt"].c_str()) == -1)
	{
		perror("Mount func call: ");
		exit(0);
	}

	if (chroot(data["mount_point"].c_str()) < 0) 
		perror("Error chroot: ");


	if (chdir(data["mount_point"].c_str()) < 0)
		perror("Error chdir: ");

	system("mount -t proc proc /proc");
	
	if(iface_up(veth_container) == -1)
		cout<<"Error occured while setting up the interface. Do it manually"<<endl;

	if( iface_add_addr(veth_container,addr_str) == -1)
		cout<<"Error adding address to interface. Do it manually.."<<endl;

	system("ip route add default via 172.17.0.1");
	system("bash");
	cout<<"CHILD IS DEAD..\n";	
	return 0;
}

int main(int argc,char ** argv){
	
	static struct option long_options[] = {
        {"cpu-share",required_argument,0,'c' },
        {"mem-limit",required_argument,0,'m' },
        {"rootfs",required_argument,0,'r'},
        {0,0,0,0}
    };
    
    key_t key = 5678;
    int shmid;
    int * shm;
    float cpu_share;
    string mem_limit,ip_addr,root_dir;
    int m_flag = 0; int c_flag = 0;
    int long_index = 0;
    int opt = 0;

    srand(time(NULL));
    int r = rand() % 10000;
	cout << r <<  endl;
	string veth_host = "veth_host" + to_string(r);
	string veth_container = "veth_container" + to_string(r);
	
    if ((shmid = shmget(key, sizeof(int), IPC_CREAT | IPC_EXCL | 0666)) < 0) {
    	cout << "Shared memory already exists... \n";
    	if((shmid = shmget(key,sizeof(int),0666)) < 0){
            perror("shmget: ");
            exit(0);
        }
        
        if ((shm = (int *) shmat(shmid, NULL, 0)) == (int *) -1) {
                perror("shmat");
                exit(1);
        }

        *shm = *shm + 1;
    }
    else {
    	
    	if ((shm = (int *) shmat(shmid, NULL, 0)) == (int *) -1) {
                perror("shmat");
                exit(1);
            }

        *shm = 2;
    }

   ip_addr = "172.17.0." + to_string(*shm) + "/16";

    while ((opt = getopt_long(argc, argv,"r:c:m:", 
                   long_options, &long_index )) != -1) {
        switch (opt) {
             case 'c' : c_flag = 1; cpu_share = (float)atof(optarg);
                 break;
             case 'm' : m_flag = 1; mem_limit = string(optarg); 
                 break;
             case 'r' : root_dir = string(optarg); break;
             default: cout << "Please use --rootfs to specify rootdir\n"; exit(1);
        }
    }

    //handler cgroup
    if (c_flag || m_flag){
    	struct cgroup * cg = cgroup_new_cgroup(to_string(r).c_str());
 		cgroup_controller * ctrl_mem = cgroup_add_controller(cg, "memory");
 		cgroup_controller * ctrl_cpu = cgroup_add_controller(cg , "cpu");		
		char * buffer = new char[5];
		if (c_flag){
			snprintf(buffer, sizeof(buffer), "%d", int(cpu_share * 1024));
			cgroup_add_value_string(ctrl_cpu,"cpu.shares",buffer);
			
		}
		if (m_flag)
			cgroup_add_value_string(ctrl_mem,"memory.limit_in_bytes",mem_limit.c_str());
		cgroup_create_cgroup(cg,1);
		cgroup_attach_task(cg);
		free(buffer);
    }

	cout << "IP ADDRESS: " << ip_addr << endl;
	cout << "RootFS location: " << root_dir << endl;
	char * stack = new char[STACK_SIZE];
	char * vargs[] = {(char *)veth_container.c_str(), &*ip_addr.begin(), &*root_dir.begin()};
	pid_t pid = clone(childFunc,stack+STACK_SIZE,CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWNET | SIGCHLD,vargs);
	if (pid < 0)
		perror("Error: ");
	
	int err;
	
	struct nl_sock *sk;

	struct rtnl_link *bridge;
	struct rtnl_link * link;
	struct nl_cache *link_cache;
	sk = nl_socket_alloc();
	if ((err = nl_connect(sk, NETLINK_ROUTE)) < 0) {
		nl_perror(err, "Unable to connect socket");
		return err;
	}

	if((err=rtnl_link_veth_add(sk,veth_host.c_str(),veth_container.c_str(),pid)) < 0){
		nl_perror(err,"Unable to allocate veth devices");
		exit(1);
	}

	if ((err = rtnl_link_alloc_cache(sk, AF_UNSPEC, &link_cache)) < 0) {
		nl_perror(err, "Unable to allocate cache");
		return err;
	}

	nl_cache_refill(sk, link_cache);

	link = rtnl_link_get_by_name(link_cache,veth_host.c_str());
	if(!link){
		cout<<"ERROR LINK:"<<endl;
		exit(1);
	}
	bridge = rtnl_link_get_by_name(link_cache, HOST_BRIDGE_NAME);

	struct rtnl_link * peer_link = rtnl_link_veth_get_peer(link);
	if(!peer_link){
		cout<<"PEER LINk ERROR: "<<endl;
		exit(-1);
	}

	rtnl_link_set_flags(link,IFF_UP | IFF_LOWER_UP);
	rtnl_link_set_flags(peer_link,IFF_UP | IFF_LOWER_UP);
	cout<<"Setting up interface.."<<endl;
	
	if(iface_up(veth_host) == -1)
		cout<<"Error occured while setting up the interface. Do it manually"<<endl;

	if ((err = rtnl_link_enslave(sk, bridge, link)) < 0) {
		nl_perror(err, "Unable to enslave interface to his bridge\n");
		return err;
	}

	cout<<"Container Process PID: "<<pid<<endl;
	sleep(2);
	int status;
	if (waitpid(pid,&status,0) == -1)
		perror("Error: ");
	WTERMSIG(status);
	cout<<endl<<status<<endl;
	cout<<"Child Has Ended\n";	
	return 0;
}

