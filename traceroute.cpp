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

bool isValidIpAddress(const char *ipAddress) {
	struct sockaddr_in sa;
	int result = inet_pton(AF_INET, ipAddress, &(sa.sin_addr));
	return result != 0;
}

void fill_in_IP_header(char *sendBuffer, int current_ttl, std::string destIP) {
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
	ip->ttl = current_ttl;
	ip->check = 0;
	ip->saddr = 0;
}

void fill_in_ICMP_header(char *sendBuffer) {
// 4. Fill in all the fields of the ICMP header right behind the IP header.
	struct icmphdr *icmp = (struct icmphdr *)(sendBuffer + sizeof(struct iphdr));
	icmp->type = ICMP_ECHO;
	icmp->code = 0;
	icmp->un.echo.id = htons(getpid() & 0xFFFF);
	icmp->un.echo.sequence = htons(1);
	icmp->checksum = checksum((unsigned short *)icmp, sizeof(struct icmphdr));
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

	if (isValidIpAddress(destIP.c_str()) < 0) {
		std::cout << "Invalid" << std::endl;
	}

// 5. Create the send and receive sockets.
	int sendSock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (sendSock < 0) {
		DEBUG << "problem creating send socket" << ENDL;
		return -1;
	}

	// int one = 1;
	// setsockopt(sendSock, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one));

	int recvSock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (recvSock < 0) {
		DEBUG << "problem creating recv socket" << ENDL;
		return -1;
	}

	int current_ttl = 2;
	bool no_reply = true;
// 6. while (CURRENT_TTL <= 31) and (reply-not-received)
	while (current_ttl <= 31 && no_reply) {
	// 1. Allocate two 64 byte buffers. One for sending and one for receiving.
		char sendBuffer[64];
		char recvBuffer[64];

	// 2. Fill the whole buffer with a pattern of characters of your choice.
		std::memset(sendBuffer, 'a', sizeof(sendBuffer));
		std::memset(recvBuffer, 'a', sizeof(recvBuffer));
	// a. Set the TTL in the IP header in the buffer to CURRENT_TTL
		fill_in_IP_header(sendBuffer, current_ttl, destIP);
	// b. Set the checksum in the ICMP header
		fill_in_ICMP_header(sendBuffer);
	// c. Send the buffer using sendfrom()
		struct sockaddr_in destAddr{};
		destAddr.sin_family = AF_INET;
		destAddr.sin_addr.s_addr = inet_addr(destIP.c_str());

		ssize_t sent_bytes = sendto(sendSock, sendBuffer, sizeof(sendBuffer), 0, (struct sockaddr *)&destAddr, sizeof(destAddr));
		if (sent_bytes < 0) {
			DEBUG << "send from socket failed" << ENDL;
			return -1;
		}
	// d. While (now < START_TIME + 15) and (not-done-reading)
		struct timeval start, now;
		gettimeofday(&start, NULL);

		DEBUG << "Trying packet with a TTL of: " << current_ttl << ENDL;

		bool not_done_reading = true;
		while (not_done_reading) {
			// calculate elapsed time
			gettimeofday(&now, NULL);
			double elapsed = (now.tv_sec - start.tv_sec) + (now.tv_usec - start.tv_usec) / 1e6;

			if (elapsed >= 15.0) {
				DEBUG << "Time exceeded 15 seconds. Increasing TTL." << ENDL;
				break;
			}

		// i. Use select() to sleep for up to 5 seconds, wake up if data arrives.
			fd_set readfds;
			FD_ZERO(&readfds);
			FD_SET(recvSock, &readfds);

			struct timeval tv;
			tv.tv_sec = 5;
			tv.tv_usec = 0;

			int ready = select(recvSock + 1, &readfds, NULL, NULL, &tv);

			if (!(ready > 0)) {
					continue;
			}

		// ii. If data has arrived, read it with recvfrom()
			DEBUG << "Data is arriving" << ENDL;
			struct sockaddr_in recvAddr;
			socklen_t addrlen = sizeof(recvAddr);
			ssize_t recv_bytes = recvfrom(recvSock, recvBuffer, sizeof(recvBuffer), 0, (struct sockaddr *)&recvAddr, &addrlen);

			DEBUG << "Data received " << recv_bytes << " bytes" << ENDL;
			if (recv_bytes > 0) {
				struct iphdr *recv_ip = (struct iphdr *)recvBuffer;
				struct icmphdr *recv_icmp = (struct icmphdr *)(recvBuffer + recv_ip->ihl * 4);

				DEBUG << "Buffer: " << recvBuffer << ENDL;
				DEBUG << "ICMP code: " << recv_icmp->code << ENDL;
				DEBUG << "ICMP check: " << recv_icmp->checksum << ENDL;
				DEBUG << "ICMP buffer: " << recvBuffer[20] << ENDL;
				DEBUG << "Reply type: " << recv_icmp->type << ENDL;
				// problem with the reply type being empty 
				char ip_str[INET_ADDRSTRLEN];
				inet_ntop(AF_INET, &(recv_ip->saddr), ip_str, INET_ADDRSTRLEN);
				DEBUG << "IP: " << ip_str << ENDL;
				if (recv_icmp->type == ICMP_ECHOREPLY) {
					std::cout << "Reply received from " << ip_str << "\n";
					not_done_reading = false;
				} else if (recv_icmp->type == ICMP_TIME_EXCEEDED) {
					std::cout << "No response with a TTL of " << current_ttl << "\n";
					not_done_reading = false;
				}
			} else {
				DEBUG << "No data read." << ENDL;
			}
		}

		current_ttl++;
	}

	if (no_reply) {
		std::cout << "No reply from destination.\n";
	}

	return 0;
}