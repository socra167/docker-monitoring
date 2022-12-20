#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <time.h>
#include "cJSON.h"
using namespace std;
#define getItem(node, key) (cJSON_GetObjectItemCaseSensitive((node), (key)))

typedef struct Container {
	char id[65]; 				// container ID
	char name[30]; 				// container Names
	char ip[16];				// container IP Addr
	char macAddr[18];			// MAC address
	int pid;					// container PID
	int online_cpus; 			// number of allocated cpu
	int mem_limit;				// size of allocated memory (limit)
	int mem_usage;				// memory usage
	double per_mem;				// memory share percentage

	double system_cpu_usage;
	int usage_in_usermode;
	int usage_in_kernelmode;
	double total_usage;

	double ex_system_cpu_usage;
	double ex_total_usage;

	double per_cpu;				// cpu share percentage

	int rx_bytes;
	int rx_packets;
	int tx_bytes;
	int tx_packets;

	int ex_rx_bytes;
	int ex_rx_packets;
	int ex_tx_bytes;
	int ex_tx_packets;

	struct Container* next; // next con (link)
	Container(struct Container* con){
		memset(this, 0x0, sizeof(Container));
		next = con;
	}
}Con;

typedef struct Host {
	int online_containers;
	int cores;
	double per_cpu;
	double total_memory;
	double free_memory;
}Host;

// add container to list
Con* addCon(Con* head) { // add new container at last of list
	Con* con = new Con(NULL);
	Con* ptr; // index pointer
	for(ptr=head; ptr->next!=NULL; ptr=ptr->next); // last Container
	ptr->next = con;
	return con;
}

// delete all linked container & init head
void delAll(Con* head) {
	Con* ptr; // iter
	Con* tmp;
	if (head != NULL){
		for(ptr=head->next; ptr!=NULL; ptr=tmp) {
			tmp = ptr->next;
			ptr = NULL;
			delete ptr;
		}
		head->next = NULL;
	}
}

// return Container with same name
Con* findByID(Con* head, char* id){
	Con* ptr;
	for(ptr=head->next; ptr!=NULL; ptr=ptr->next){
		if (strcmp(ptr->id, id) == 0)
			return ptr;
	}
	return NULL;
}

// print information for each container
void printConInfo(Con* head) {
	// fix decimal point output
	cout << fixed;
    cout.precision(0);

    double cpu_delta;
    double system_cpu_delta;

	Con* ptr;
	int cnt = 1;
	for (ptr=head->next; ptr!=NULL; ptr=ptr->next) {
			cout << "[" << cnt++ << " th container info]" << endl;
			cout << "PID : " << ptr->pid << endl;
			cout << "ID : " << ptr->id << endl;
			cout << "NAMES : " << (ptr->name)+1 << endl;
			cout << "IPAddress : " << ptr->ip << endl;
			cout << "MacAddr : " << ptr->macAddr << endl;

			if (ptr->ex_rx_bytes == 0){
				cout << "(Initial value) ";
				cout << "rx byte : " << ptr->rx_bytes << "(packets:" << ptr->rx_packets << ")" \
				<< ", tx byes : " << ptr->tx_bytes << "(packets:" << ptr->tx_packets << ")" << endl;
			}
			else{
				cout << "(Diff) ";
				cout << "rx byte : " << ptr->rx_bytes - ptr->ex_rx_bytes << "(packets:" << ptr->rx_packets - ptr->ex_rx_packets << ")" \
				<< ", tx byes : " << ptr->tx_bytes - ptr->ex_tx_bytes << "(packets:" << ptr->tx_packets - ptr->ex_tx_packets << ")" << endl;
			}

			cpu_delta = (ptr->total_usage - ptr->ex_total_usage);
			system_cpu_delta = ptr->system_cpu_usage - ptr->ex_system_cpu_usage;

			cout.precision(1);
			cout << "CPU share : " << ptr->per_cpu << "%" << endl;

			cout.precision(2);
			cout << "Allocated Memory(limit) : " << (double)(ptr->mem_limit) / 1024 / 1024 << "MB" << endl;
			cout << "Memory share : " << ptr->per_mem << "%" << endl;
			cout.precision(0);
			cout << "online cpus : " << ptr->online_cpus << ", system cpu usage : " << ptr->system_cpu_usage \
				<< ", usage in kernelmode : " << ptr->usage_in_kernelmode << ", usage in usermode : " << ptr->usage_in_usermode \
				<< ", total_usage : " << ptr->total_usage << ", mem_usage : " << ptr->mem_usage << endl << endl;
	}
}

// get container list length
int getConLength(Con* head) {
	Con* ptr;
	int cnt = 0;
	for (ptr=head->next; ptr!=NULL; ptr=ptr->next)
		cnt++;
	return cnt;
}

// get json from Docker Engine API 1.4.1 at endpoint
string getDockerAPI(string endpoint) {
	FILE* fp;
	char buff[256];
	string json;
	string cmd = "curl -s --unix-socket /var/run/docker.sock http://v1.41/";
	cmd.append(endpoint);

	fp = popen(cmd.c_str(), "r");
		while(fgets(buff, 256, fp) != NULL) {
			json.append(buff);
		}
    	pclose(fp);
	return json;
}

// get container stats based on resource usage by id
string getDockerStats(char* id){
	string endpoint = "containers/";
	endpoint.append(id);
	endpoint.append("/stats?stream=false");
	return getDockerAPI(endpoint.c_str());
}

// get container json based on resource usage by id
string getDockerJson(char* id){
	string endpoint = "containers/";
	endpoint.append(id);
	endpoint.append("/json");
	return getDockerAPI(endpoint.c_str());
}

void printHost(Host* host, Con* head, int* ex_total_jiffels, int* ex_work_jiffels) {
	int idx = 0;
	double per_cpu; // host %CPU

	// for calculate host %CPU
	int total_jiffels = 0;
	int work_jiffels = 0;

	FILE *fp;
	char buff[256];
	int cpu[6];

	// fix decimal point output
	cout << fixed;
   	cout.precision(1);

   	cout << "\n\n================================HOST STATUS================================"<< endl;

	// =========================== Host Resources ===========================
	// online containers
	host->online_containers = getConLength(head);
	cout << host->online_containers << " containers is running" << endl; // running containers

	// core
	fp = fopen("/proc/cpuinfo", "r");
	while(!fscanf(fp, "cpu cores\t: %u", &host->cores)){
		fscanf(fp, "%*[^c]");
	}
	cout << "CPU CORES : " << host->cores << endl;
	fclose(fp);


	// %CPU
	if ((ex_total_jiffels == 0) || (ex_work_jiffels == 0)){ // if there is no past cpu info
		cout << "\%CPU : calculating ... " << endl;
		cout << "IDLE \%CPU : calculating ... " << endl;
	} else {
		// open /proc/stat
		fp = fopen("/proc/stat", "r");
		fscanf(fp, "cpu\t%d %d %d %d %d %d",&cpu[0], &cpu[1], &cpu[2], &cpu[3], &cpu[4], &cpu[5]);
		fclose(fp);

		// calculate %CPU
		for(idx=0; idx<6; idx++)
			total_jiffels += cpu[idx];
		for(idx=0; idx<3; idx++)
			work_jiffels += cpu[idx];

		host->per_cpu = ((double)((work_jiffels - *ex_work_jiffels)) / (double)((total_jiffels - *ex_total_jiffels))) * 100;
		cout << "CPU Usage : " << (host->per_cpu * host->cores) << "%" << endl;
		cout << "IDLE CPU : " << ((100 - host->per_cpu) * host->cores) << "%" << endl;

		// update old cpu info
		*ex_work_jiffels = work_jiffels;
		*ex_total_jiffels = total_jiffels;
	}

	// MEMORY
	int memTotal;
	int memFree;
	fp = fopen("/proc/meminfo", "r");
	fscanf(fp, "MemTotal:\t\t%d kB\nMemFree:\t\t%d kB", &memTotal, &memFree);
	fclose(fp);

	host->total_memory = ((double)memTotal) / 1024;
	host->free_memory = ((double)memFree) / 1024;
	cout << "Total memory size : " << host->total_memory << "MB" << endl;
	cout << "Free memory size : " << host->free_memory << "MB" << endl;
   	cout << "==========================================================================="<< endl << endl;

	return;
}

// make json log file for all container
void printJsonLog(Host* host_info, Con* head){
	FILE* fp;
	time_t curTime = time(NULL);
	struct tm *pLocal = localtime(&curTime);
	char path[30] = "./logs/";
	char fname[20];
	cJSON* json = cJSON_CreateObject();
	cJSON* host = cJSON_CreateObject();
	cJSON* containers = cJSON_CreateArray();
	cJSON* container;
	cJSON* tmp;
	Con* ptr;

	if (pLocal == NULL){
		cout << "Can't get current time to make Log file" << endl;
		return;
	}

	sprintf(fname, "%02d:%02d:%02d.json", pLocal->tm_hour, pLocal->tm_min, pLocal->tm_sec);

	cJSON_AddItemToObject(json, "host", host);
	cJSON_AddItemToObject(json, "containers", containers);

	// ====== add host info ======

	// add per_cpu
	tmp = cJSON_CreateNumber(host_info->cores);
	cJSON_AddItemToObject(host, "cpu_cores", tmp);
	tmp = cJSON_CreateNumber(host_info->per_cpu * host_info->cores);
	cJSON_AddItemToObject(host, "per_cpu_usage", tmp);
	tmp = cJSON_CreateNumber((100 - host_info->per_cpu) * host_info->cores);
	cJSON_AddItemToObject(host, "per_cpu_idle", tmp);

	// add memory size
	tmp = cJSON_CreateNumber(host_info->total_memory);
	cJSON_AddItemToObject(host, "total_memory", tmp);
	tmp = cJSON_CreateNumber(host_info->free_memory);
	cJSON_AddItemToObject(host, "free_memory", tmp);


	// ====== add containers info ======
	for(ptr=head->next; ptr!=NULL; ptr=ptr->next){
		container = cJSON_CreateObject();
		cJSON_AddItemToArray(containers, container);
		
		// container ID
		tmp = cJSON_CreateString(ptr->id);
		cJSON_AddItemToObject(container, "id", tmp);

		// container Name
		tmp = cJSON_CreateString(ptr->name);
		cJSON_AddItemToObject(container, "name", tmp);

		// container PID
		tmp = cJSON_CreateNumber(ptr->pid);
		cJSON_AddItemToObject(container, "pid", tmp);

		// IP Addr
		tmp = cJSON_CreateString(ptr->ip);
		cJSON_AddItemToObject(container, "ip", tmp);

		// MAC Addr
		tmp = cJSON_CreateString(ptr->macAddr);
		cJSON_AddItemToObject(container, "macAddr", tmp);

		// packets info
		tmp = cJSON_CreateNumber(ptr->rx_bytes - ptr->ex_rx_bytes);
		cJSON_AddItemToObject(container, "rx_bytes", tmp);
		tmp = cJSON_CreateNumber(ptr->rx_packets - ptr->ex_rx_packets);
		cJSON_AddItemToObject(container, "rx_packets", tmp);
		tmp = cJSON_CreateNumber(ptr->tx_bytes - ptr->ex_tx_bytes);
		cJSON_AddItemToObject(container, "tx_bytes", tmp);
		tmp = cJSON_CreateNumber(ptr->tx_packets - ptr->ex_tx_packets);
		cJSON_AddItemToObject(container, "tx_packets", tmp);

		// cpu info
		tmp = cJSON_CreateNumber(ptr->online_cpus);
		cJSON_AddItemToObject(container, "online_cpus", tmp);
		tmp = cJSON_CreateNumber(ptr->per_cpu);
		cJSON_AddItemToObject(container, "per_cpu", tmp);
		tmp = cJSON_CreateNumber(ptr->system_cpu_usage);
		cJSON_AddItemToObject(container, "system_cpu_usage", tmp);
		tmp = cJSON_CreateNumber(ptr->usage_in_kernelmode);
		cJSON_AddItemToObject(container, "usage_in_kernelmode", tmp);
		tmp = cJSON_CreateNumber(ptr->usage_in_usermode);
		cJSON_AddItemToObject(container, "usage_in_usermode", tmp);
		tmp = cJSON_CreateNumber(ptr->total_usage);
		cJSON_AddItemToObject(container, "total_usage", tmp);
		tmp = cJSON_CreateNumber(ptr->mem_limit);
		cJSON_AddItemToObject(container, "mem_limit", tmp);
		tmp = cJSON_CreateNumber(ptr->mem_usage);
		cJSON_AddItemToObject(container, "mem_usage", tmp);
		tmp = cJSON_CreateNumber(ptr->per_mem);
		cJSON_AddItemToObject(container, "per_mem", tmp);
	}
	fp = fopen(strcat(path, fname), "w");
	fputs(cJSON_Print(json), fp);
	fclose(fp);

	cout << endl << ">> Log file \'" << fname << "\' is saved." << endl << endl;

	return;
}

int main() {
	Con* head = new Con(NULL); // head for container list
	Con* current = new Con(NULL); // head for container being added
	Host* host = new Host; // save host info

	Con* ptr; // for iteration
	Con* ex_info; // to load past container info
	string json; // json string from API
	
	// for host cpu usage calculate
	int total_jiffels = 0;
	int work_jiffels = 0;

	// for %MEM calculate
	int used_memory = 0;
	int available_memory = 0;

	// for %CPU calculate
	double cpu_delta = 0.0;
	double system_cpu_delta = 0.0;

	cJSON* root = NULL; // parsed json string
	cJSON* csr = NULL; // current node in json (cursor)
	cJSON* conJson = NULL; // for iteration
	cJSON* tmp = NULL; // for iteration

	main:

	while (1) {
		// =========================== containers/json - Parsing ===========================
		// get from API
    	json = getDockerAPI("containers/json");
    	if (json.empty()){ // API failure
    		cout << "API Loading Failed." << endl;
    		return -1;
    	}

    	// json parse
    	root = cJSON_Parse(json.c_str());
    	if (root == NULL){ // cJSON parsing failure
    		cout << "cJSON_Parse() failed" << endl;
    		return -1;
    	}
		
		cJSON_ArrayForEach(conJson, root){ // for each container
			Con* con = addCon(current); // make new Container at the list
			// save ID
    		strcpy(con->id, getItem(conJson, "Id")->valuestring);

    		// save Names
    		csr = getItem(conJson, "Names");
    		cJSON_ArrayForEach(tmp, csr){
    			strcpy(con->name, tmp->valuestring);
    		}
    		
    		// save IP Addr, Mac Addr
    		csr = getItem(conJson, "NetworkSettings");
    		csr = getItem(csr, "Networks");
    		csr = getItem(csr, "bridge");
    		strcpy(con->ip, getItem(csr, "IPAddress")->valuestring);
    		strcpy(con->macAddr, getItem(csr, "MacAddress")->valuestring);
    	}

		
    	for (ptr=current->next; ptr!=NULL; ptr=ptr->next) { // for each Container

    		// get PID
    		// =========================== containers/{id}/json - Parsing ===========================
    		json = getDockerJson(ptr->id);
    		if (json.empty()){ // API failure
    			cout << "API Loading Failed." << endl;
    			return -1;
    		}
			// json parse
    		root = cJSON_Parse(json.c_str());
    		if (root == NULL){ // cJSON parsing failure
    			cout << "cJSON_Parse() failed" << endl;
    			return -1;
    		}
    		csr = getItem(root, "State");
    		if (cJSON_IsInvalid(csr) || csr == NULL){ // when container is being deleted
    			delAll(current);
    			goto main; // start parsing again
    		}
    		ptr->pid = getItem(csr, "Pid")->valueint;


    		// =========================== containers/{id}/stats - Parsing ===========================
    		// get from API : containers/{id}/stats 
    		json = getDockerStats(ptr->id);
    		if (json.empty()){ // API failure
    			cout << "API Loading Failed." << endl;
    			return -1;
    		}

    		// json parse
    		root = cJSON_Parse(json.c_str());
    		if (root == NULL){ // cJSON parsing failure
    			cout << "cJSON_Parse() failed" << endl;
    			return -1;
    		}

    		// save rx byte&packets, tx byte&packets
    		csr = getItem(root, "networks");
    		if (cJSON_IsInvalid(csr) || csr == NULL){ // when container is being deleted
    			delAll(current);
    			goto main; // start parsing again
    		}
    		csr = getItem(csr, "eth0");

    		// save ex info if it exists
    		ex_info = findByID(head, ptr->id);
    		if (ex_info != NULL){
    			ptr->ex_rx_bytes = ex_info->rx_bytes;
    			ptr->ex_rx_packets = ex_info->rx_packets;
    			ptr->ex_tx_bytes = ex_info->tx_bytes;
    			ptr->ex_tx_packets = ex_info->tx_packets;

    			ptr->ex_system_cpu_usage = ex_info->system_cpu_usage;
    			ptr->ex_total_usage = ex_info->total_usage;
    		}

    		ptr->rx_bytes = (int)(getItem(csr, "rx_bytes")->valueint);
    		ptr->rx_packets = (int)(getItem(csr, "rx_packets")->valueint);
    		ptr->tx_bytes = (int)(getItem(csr, "tx_bytes")->valueint);
    		ptr->tx_packets = (int)(getItem(csr, "tx_packets")->valueint);

    		// save CPU num
    		csr = getItem(root, "cpu_stats");
    		ptr->online_cpus = (int)(getItem(csr, "online_cpus")->valueint);


    		// save MEM size
    		csr = getItem(root, "memory_stats");
    		ptr->mem_usage = (int)(getItem(csr, "usage")->valueint);
    		available_memory = (int)(getItem(csr, "limit")->valueint);
    		ptr->mem_limit = available_memory;
    		csr = getItem(csr, "stats");
    		used_memory = (ptr->mem_usage - (int)(getItem(csr, "cache")->valueint));
    		ptr->per_mem = ((double)used_memory / (double)available_memory) * 100;

    		// save system_cpu_usage
    		csr = getItem(root, "cpu_stats");
    		ptr->system_cpu_usage = (double)(getItem(csr, "system_cpu_usage")->valuedouble);

    		// save usage in usermode, usage in kernelmode, total usage
    		csr = getItem(csr, "cpu_usage");
    		ptr->usage_in_usermode = (int)(getItem(csr, "usage_in_usermode")->valueint);
    		ptr->usage_in_kernelmode = (int)(getItem(csr, "usage_in_kernelmode")->valueint);
    		ptr->total_usage = (double)(getItem(csr, "total_usage")->valuedouble);

    		// calculate %CPU
    		cpu_delta = (ptr->total_usage - ptr->ex_total_usage);
			system_cpu_delta = ptr->system_cpu_usage - ptr->ex_system_cpu_usage;
			ptr->per_cpu = (cpu_delta / system_cpu_delta) * ptr->online_cpus * 100.0;

    	}

    	delAll(head); // delete container list
    	head->next = current->next;
    	current->next = NULL;

    	printHost(host, head, &total_jiffels, &work_jiffels); // print & save Host info
   		printConInfo(head); // print Containers info
   		printJsonLog(host, head); // print json log file

   		// monitoring interval (2sec)
    	sleep(2);
	}

	delete host;
	delete current;
	delete head;

	return 0;
}
