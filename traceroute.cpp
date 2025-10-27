#include "traceroute.h"
#include <sys/time.h>

// ****************************************************************************
// * Compute the Internet Checksum over an arbitrary buffer.
// * (written with the help of ChatGPT 3.5)
// ****************************************************************************
uint16_t checksum(unsigned short *buffer, int size) {
    unsigned long sum = 0;
    while (size > 1) {
        sum += *buffer++;
        size -= 2;
    }
    if (size == 1) {
        sum += *(unsigned char *) buffer;
    }
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return (unsigned short) (~sum);
}


int main (int argc, char *argv[]) {
  std::string destIP;

  // ********************************************************************
  // * Process the command line arguments
  // ********************************************************************
  int opt = 0;
  while ((opt = getopt(argc,argv,"t:d:")) != -1) {

    switch (opt) {
    case 't':
      destIP = optarg;
      break;
    case 'd':
      LOG_LEVEL = atoi(optarg);;
      break;
    case ':':
    case '?':
    default:
      std::cout << "useage: " << argv[0] << " -t [target ip] -d [Debug Level]" << std::endl;
      exit(-1);
    }
  }

	if (destIP.compare("")) {
		DEBUG << "no destination specified" << ENDL;
		return -1;
	}

// 1. Allocate two 64 byte buffers. One for sending and one for receiving.
	char sendBuffer[64];
	char recvBuffer[64];

// 2. Fill the whole buffer with a pattern of characters of your choice.
	std::memset(sendBuffer, 'a', sizeof(sendBuffer));
	std::memset(recvBuffer, 'a', sizeof(recvBuffer));

// 3. Fill in all the fields of the IP header at the front of the buffer.
	// a. You don’t need to fill in source IP or checksum
	struct iphdr *ip = (struct iphdr *)sendBuffer;
	ip->version = 4;
	ip->ihl = 5;
	ip->tos = 0;
	ip->tot_len = htons(sizeof(sendBuffer));
	ip->id = htons(0);
	ip->frag_off = 0;
	ip->protocol = IPPROTO_ICMP;
	ip->daddr = inet_addr(destIP.c_str());

// 4. Fill in all the fields of the ICMP header right behind the IP header.
	struct icmphdr *icmp = (struct icmphdr *)(sendBuffer + sizeof(struct iphdr));
	icmp->type = ICMP_ECHO;
	icmp->code = 0;
	icmp->un.echo.id = htons(getpid() & 0xFFFF);
	icmp->un.echo.sequence = htons(1);
	icmp->checksum = 0; // will adjust later in the actual loop

// 5. Create the send and receive sockets.
	int sendSock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (sendSock < 0) {
		DEBUG << "problem creating send socket" << ENDL;
		perror("sendSock");  // "Operation not permitted" error --> run with sudo
		return -1;
	}

	// tell socket to use my IP header
	int one = 1;
	if (setsockopt(sendSock, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one)) < 0) {
		DEBUG << "couldn't override socket" << ENDL;
		return -1;
	}

	int recvSock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (recvSock < 0) {
		DEBUG << "problem creating recv socket" << ENDL;
		return -1;
	}

	int current_ttl = 2;
	bool no_reply = true;
// 6. while (CURRENT_TTL <= 31) and (reply-not-received)
	while (current_ttl <= 31 && no_reply) {
	// a. Set the TTL in the IP header in the buffer to CURRENT_TTL
		ip->ttl = current_ttl;
	// b. Set the checksum in the ICMP header
		icmp->checksum = checksum((unsigned short *)icmp, sizeof(struct icmphdr));
	// c. Send the buffer using sendfrom()
		struct sockaddr_in destAddr{};
		destAddr.sin_family = AF_INET;
		destAddr.sin_addr.s_addr = ip->daddr;

		ssize_t sent_bytes = sendto(sendSock, sendBuffer, sizeof(sendBuffer), 0, (struct sockaddr *)&destAddr, sizeof(destAddr));
		if (sent_bytes < 0) {
			DEBUG << "send from socket failed" << ENDL;
		}
	// d. While (now < START_TIME + 15) and (not-done-reading)
	struct timeval start, now;
	gettimeofday(&start, NULL);
	gettimeofday(&now, NULL);
	bool not_done_reading = true;
		while (now.tv_sec < start.tv_sec + 15 && not_done_reading) {
		// i. Use select() to sleep for up to 5 seconds, wake up if data arrives.
			fd_set readfds;
			FD_ZERO(&readfds);
			FD_SET(recvSock, &readfds);

			struct timeval tv;
			tv.tv_sec = 5; 
			tv.tv_usec = 0;
			int ready = select(recvSock + 1, &readfds, NULL, NULL, &tv);

			if (ready < 0) {
				DEBUG << "error with select()" << ENDL;
			}

		// ii. If data has arrived, read it with recevfrom()
			struct sockaddr_in recvAddr;
			socklen_t addrlen = sizeof(recvAddr);
			ssize_t recv_bytes = recvfrom(recvSock, recvBuffer, sizeof(recvBuffer), 0, (struct sockaddr *)&recvAddr, &addrlen);
			if (recv_bytes > 0) {
				struct iphdr *recv_ip = (struct iphdr *)recvBuffer;
				struct icmphdr *recv_icmp = (struct icmphdr *)(recvBuffer + recv_ip->ihl*4);
				
				// 1. If received data is Echo Reply from the destination
				if (recv_icmp->type == ICMP_ECHOREPLY) {
					// a. Print message
						char ip_str[INET_ADDRSTRLEN];
						inet_ntop(AF_INET, &(recv_ip->saddr), ip_str, INET_ADDRSTRLEN);
						std::cout << "Reply received from " << ip_str << "\n";
					// b. Set reply-not-received to false
						no_reply = false;
					// c. Set not-done-reading to false
						not_done_reading = false;

				// 2. If received data is TTL Time Exceeded; TTL
				} else if (recv_icmp->type == ICMP_TIME_EXCEEDED) {
					//check if subtype is 0???
					// a. print message
					std::cout << "Time to live of " << ip->ttl << " had been exceeded.\n";
					// b. Set not-done-reading to false
						not_done_reading = false;
				}
			}

			gettimeofday(&now, NULL);
		}

		current_ttl++;
	}

	if (no_reply) {
		std::cout << "No reply from destination.\n";
	}

	return 0;
}