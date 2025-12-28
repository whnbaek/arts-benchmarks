/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_distributed__socket_communicator_H_GUARD
#define OCR_TBB_distributed__socket_communicator_H_GUARD

#if (!OCR_USE_SOCK)
inline int gethostname(char* name, int namelen)
{
	::strncpy(name, "localhost", ::strlen("localhost"));
	return 0;
}
#endif

#if (OCR_USE_SOCK)
#ifdef WIN32
#include <WinSock2.h>
typedef int socklen_t;
inline int poll(pollfd* fds, unsigned long fds_count, int timeout) { return WSAPoll(fds, fds_count, timeout); }
inline void init_sockets()
{
	static bool done = false;
	if (!done)
	{
		WSADATA wsaData;
		int err = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (err) throw text_exception("WSAStartup failed");
		done = true;
	}
}
#else
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>
#define INVALID_SOCKET -1
#define SOCKET int
inline void closesocket(SOCKET s) { close(s); }
inline void init_sockets() {}
#endif

typedef SOCKET socket_t;

namespace ocr_tbb
{
	namespace distributed
	{
		struct socket_communicator : public communicator_base
		{
			struct init_data
			{
				bool is_master;
				bool file_initiated;
				int port;
				std::string master_addr;
				std::size_t number_of_slaves;
				std::string boot_file;
				init_data() : is_master(true), file_initiated(false), port(25316), master_addr("127.0.0.1"), number_of_slaves(0) {}
			};

			socket_communicator(int& argc, char** &argv)
			{
				init_data id;
				for (int i = 1; i < argc;)
				{
					if (std::string(argv[i]) == "--ocr:master-of")
					{
						id.is_master = true;
						assert(i+1 < argc);
						id.number_of_slaves = (std::size_t)atoi(argv[i+1]);
						for (int j = i + 2; j <= argc; ++j)
						{
							argv[j - 2] = argv[j];
						}
						argc -= 2;
					}
					else if (std::string(argv[i]) == "--ocr:client")
					{
						id.is_master = false;
						for (int j = i + 1; j <= argc; ++j)
						{
							argv[j - 1] = argv[j];
						}
						argc -= 1;
					}
					else if (std::string(argv[i]) == "--ocr:boot-file")
					{
						id.file_initiated = true;
						assert(i + 1 < argc);
						id.boot_file = argv[i + 1];
						for (int j = i + 2; j <= argc; ++j)
						{
							argv[j - 2] = argv[j];
						}
						argc -= 2;
					}
					else if (std::string(argv[i]) == "--ocr:port")
					{
						assert(i+1 < argc);
						id.port = atoi(argv[i+1]);
						for (int j = i + 2; j <= argc; ++j)
						{
							argv[j - 2] = argv[j];
						}
						argc -= 2;
					}
					else if (std::string(argv[i]) == "--ocr:master-addr")
					{
						assert(i+1 < argc);
						id.master_addr = argv[i+1];
						for (int j = i + 2; j <= argc; ++j)
						{
							argv[j - 2] = argv[j];
						}
						argc -= 2;
					}
					else
					{
						++i;
					}
				}
				if (id.file_initiated && id.is_master)
				{
					assert(id.boot_file.size() > 0);
					std::ofstream str(id.boot_file);
					str << get_my_ip() << " " << id.port << std::endl;
				}
				if (id.file_initiated && !id.is_master)
				{
					std::ifstream str(id.boot_file);
					assert(!str.fail());
					str >> id.master_addr;
					assert(!str.fail());
					str >> id.port;
					assert(!str.fail());
				}
				if (id.is_master)
				{
					internal_initialize_master(id.number_of_slaves, id.port);
				}
				else
				{
					internal_initialize_slave(id.master_addr, id.port);
				}
			}
			~socket_communicator()
			{
				thread_context* ctx = thread_context::get_local();
				for (std::size_t i = 0; i < ports_in_.size(); ++i)
				{
					message m(ctx, command_code::CMD_exit, compute_node::get_my_id(ctx), (node_id)i);
					if (i == compute_node::get_my_id(ctx))
					{
						send_exit_to_local_queue(ctx);
					}
					else
					{
						internal_send_message(ctx, m);
					}
					internal_send_fetch_message(ctx, m);
				}
				for (std::size_t i = 0; i < ports_in_.size(); ++i)
				{
					ports_in_[i].thread.join();
					ports_in_[i].thread_fetch.join();
				}
			}

			static std::string get_my_ip()
			{
				char ac[80];
				if (::gethostname(ac, sizeof(ac))) {
					throw text_exception("::gethostname failed");
				}
				struct hostent *phe = gethostbyname(ac);
				if (phe == 0) {
					throw text_exception("::gethostbyname failed");
				}

				for (int i = 0; phe->h_addr_list[i] != 0; ++i) {
					struct in_addr addr;
					memcpy(&addr, phe->h_addr_list[i], sizeof(struct in_addr));
					std::string saddr = inet_ntoa(addr);
					if (saddr != "127.0.0.1") return saddr;
				}
				throw text_exception("local IP address could not be determined");
			}

			static void nolinger(socket_t sock)
			{
				struct linger l;
				l.l_onoff = 1;
				l.l_linger = 0;
				int err = ::setsockopt(sock, SOL_SOCKET, SO_LINGER, (const char*)&l, sizeof(l));
				if (err) throw text_exception("::setsockopt SO_LINGER failed");
			}

			static void disable_nagle(socket_t sock)
			{
				int flag = 1;
				int err = setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag, sizeof(int));
				if (err) throw text_exception("::setsockopt TCP_NODELAY failed");
			}

			static socket_t create_listener(int port)
			{
				int err;
				socket_t sock = ::socket(AF_INET, SOCK_STREAM, 0);
				if (sock == INVALID_SOCKET) throw text_exception("::socket(AF_INET, SOCK_STREAM, 0) failed");
				logging::log::sock_name(sock, "listener");
				nolinger(sock);
				disable_nagle(sock);
				sockaddr_in service;
				service.sin_family = AF_INET;
				service.sin_addr.s_addr = INADDR_ANY;// ::inet_addr("127.0.0.1");
				service.sin_port = htons(port);
				err = ::bind(sock, (sockaddr*)&service, sizeof(service));
				if (err) throw text_exception("::bind failed");
				return sock;
			}
			static socket_t create_listener()
			{
				return create_listener(0);
			}
			static int get_port(socket_t sock)
			{
				int err;
				sockaddr_in sa;
				socklen_t sa_len = sizeof(sa);
				err = ::getsockname(sock, (sockaddr*)&sa, (socklen_t*)&sa_len);
				if (err) throw text_exception("::getsockname failed");
				return ntohs(sa.sin_port);
			}
			static sockaddr_in get_address(socket_t sock)
			{
				int err;
				sockaddr_in sa;
				socklen_t sa_len = sizeof(sa);
				err = ::getsockname(sock, (sockaddr*)&sa, (socklen_t*)&sa_len);
				if (err) throw text_exception("::getsockname failed");
				return sa;
			}
			static sockaddr_in get_remote_address(socket_t sock)
			{
				int err;
				sockaddr_in addr;
				socklen_t addr_len = sizeof(addr);
				err = ::getpeername(sock, (sockaddr*)&addr, &addr_len);
				if (err) throw text_exception("::getpeername failed");
				return addr;
			}
			static socket_t connect_to(sockaddr_in service)
			{
				int err;
				socket_t sock = ::socket(AF_INET, SOCK_STREAM, 0);
				if (sock == INVALID_SOCKET) throw text_exception("::socket(AF_INET, SOCK_STREAM, 0) failed");
				disable_nagle(sock);
			retry:
				err = ::connect(sock, (sockaddr *)&service, sizeof(service));
#ifdef WIN32
				if (err) { DEBUG_COUT(WSAGetLastError()); }
				if (err && WSAGetLastError() == WSAECONNREFUSED) { DEBUG_COUT("refused"); }
#endif
				if (err) { DEBUG_COUT(strerror(errno)); }
				if (err && errno == 0) goto retry;
				if (err) throw text_exception("::connect failed");
				return sock;
			}
			static socket_t connect_to(const std::string& master_addr, int port)
			{
				int err;
				socket_t sock = ::socket(AF_INET, SOCK_STREAM, 0);
				if (sock == INVALID_SOCKET) throw text_exception("::socket(AF_INET, SOCK_STREAM, 0) failed");
				disable_nagle(sock);
				//int my_port = get_port(sock);
				sockaddr_in service;
				service.sin_family = AF_INET;
				service.sin_addr.s_addr = ::inet_addr(master_addr.c_str());
				service.sin_port = htons(port);
				err = ::connect(sock, (sockaddr *)&service, sizeof(service));
				if (err) { DEBUG_COUT(strerror(errno)); }
				if (err) throw text_exception("::connect failed");
				return sock;
			}
			template<typename T>
			static void send(socket_t s, T value)
			{
				int remaining = sizeof(T);
				const char* buf = (const char*)&value;
				logging::log::send(s, remaining);
				while (remaining)
				{
					//LOCKED_COUT("send(" << (u64)s << ",buf,"<<remaining<<",0)");
					logging::log::socket_start();
					int sent = ::send(s, buf, remaining, 0);
					if (sent < 0) throw text_exception("::send failed");
					remaining -= sent;
					logging::log::socket_stop();
					logging::log::sent(sent);
					//LOCKED_COUT("send on " << (u64)s << " done, remaining=" << remaining << "");
				}
			}
			static void send_buf(socket_t s, const void* data, std::size_t len)
			{
				int remaining = len;
				const char* buf = (const char*)data;
				logging::log::send(s, len);
				while (remaining)
				{
					//LOCKED_COUT("send(" << (u64)s << ",buf," << remaining << ",0)");
					logging::log::socket_start();
					int sent = ::send(s, buf, remaining, 0);
					if (sent < 0) throw text_exception("::send failed");
					logging::log::socket_stop();
					logging::log::sent(sent);
					buf += sent;
					remaining -= sent;
					//LOCKED_COUT("send on " << (u64)s << " done, remaining=" << remaining << "");
				}
			}
			template<typename T>
			static T recv(socket_t s)
			{
				T res;
				int remaining = sizeof(T);
				char* buf = (char*)&res;
				logging::log::recv(s, remaining);
				while (remaining)
				{
					//LOCKED_COUT("recv(" << (u64)s << ",buf," << remaining << ",MSG_WAITALL)");
					logging::log::socket_start();
					int received = ::recv(s, buf, remaining, 0/*MSG_WAITALL*/);
					if (received < 0) throw text_exception("::recv failed");
					logging::log::socket_stop();
					logging::log::recvd(received);
					buf += received;
					remaining -= received;
					//LOCKED_COUT("recv on " << (u64)s << " done, remaining=" << remaining << "");
				}
				return res;
			}
			static void recv_buf(socket_t s, void* data, std::size_t len)
			{
				int remaining = len;
				char* buf = (char*)data;
				logging::log::recv(s, len);
				while (remaining)
				{
					//LOCKED_COUT("recv(" << (u64)s << ",buf," << remaining << ",MSG_WAITALL)");
					logging::log::socket_start();
					int received = ::recv(s, buf, remaining, 0/*MSG_WAITALL*/);
					if (received < 0) throw text_exception("::recv failed");
					logging::log::socket_stop();
					logging::log::recvd(received);
					buf += received;
					remaining -= received;
					//LOCKED_COUT("recv on " << (u64)s << " done, remaining=" << remaining << "");
				}
			}
			static void poll(socket_t sock)
			{
				pollfd pfd;
				pfd.fd = sock;
				pfd.events = POLLIN;
				pfd.revents = 0;
				logging::log::poll(sock);
				::poll(&pfd, 1, -1);
				logging::log::polled();
			}
			static std::string ip_to_string(in_addr addr)
			{
				std::ostringstream str;
#ifdef WIN32
				str << (int)addr.S_un.S_un_b.s_b1 << ".";
				str << (int)addr.S_un.S_un_b.s_b2 << ".";
				str << (int)addr.S_un.S_un_b.s_b3 << ".";
				str << (int)addr.S_un.S_un_b.s_b4;
#else
				str << (int)((addr.s_addr >> 0) & 0xff) << ".";
				str << (int)((addr.s_addr >> 8) & 0xff) << ".";
				str << (int)((addr.s_addr >> 16) & 0xff) << ".";
				str << (int)((addr.s_addr >> 24) & 0xff);
#endif
				return str.str();
			}

			static void accept_all(const std::vector<sockaddr_in>& remote_addresses, socket_communicator* parent)
			{
				std::size_t remaining = 2 * (remote_addresses.size());
				while (remaining)
				{
					socket_t s_ = ::accept(parent->listener_, 0, 0);
					sockaddr_in remote = get_remote_address(s_);
					DEBUG_COUT("Connected via " << remote.sin_family << " at " << ip_to_string(remote.sin_addr) << ":" << ntohs(remote.sin_port));
					if (s_ == INVALID_SOCKET) throw text_exception("::accept faild");
					int type = recv<s32>(s_);
					std::size_t node_id = recv<u64>(s_);
					if (type == 0)
					{
						parent->ports_in_[node_id].socket = s_;
						logging::log::sock_name(s_, "in[" + std::to_string((unsigned long long)node_id) + "]");
					}
					if (type == 1)
					{
						parent->ports_in_[node_id].socket_fetch = s_;
						logging::log::sock_name(s_, "in_fetch[" + std::to_string((unsigned long long)node_id) + "]");
					}
					--remaining;
				}
			}

			bool internal_filter_message(thread_context* ctx, command cmd, message& m) OVERRIDE
			{
				return false;
			}

			void connect_all(std::size_t my_id, const std::vector<sockaddr_in>& remote_addresses);

		private:
			void internal_initialize_master(std::size_t number_of_slaves, int port)
			{
				std::size_t nodes_count = number_of_slaves + 1;
				int err;
				init_sockets();
				listener_ = create_listener(port);
				std::size_t connected = 0;
				err = ::listen(listener_, 2 * (1 + number_of_slaves));
				if (err) throw text_exception("::listen failed");
				std::vector<socket_t> remote_sockets;
				std::vector<sockaddr_in> remote_addresses;
				remote_sockets.reserve(nodes_count);
				remote_addresses.reserve(nodes_count);
				remote_sockets.push_back(listener_);
				remote_addresses.push_back(get_address(listener_));
				remote_addresses.back().sin_addr.s_addr = ::inet_addr("127.0.0.1");
				while (connected < number_of_slaves)
				{
					socket_t s_ = ::accept(listener_, 0, 0);
					if (s_ == INVALID_SOCKET) throw text_exception("::accept failed");

					sockaddr_in addr = get_remote_address(s_);
					std::string remote_addr = ::inet_ntoa(addr.sin_addr);
					DEBUG_COUT("New client at " << remote_addr << ":" << ntohs(addr.sin_port));
					send<u64>(s_, connected + 1);
					int remote_port = recv<s32>(s_);
					DEBUG_COUT("Client listens on " << remote_addr << ":" << remote_port);
					addr.sin_port = htons(remote_port);
					remote_addresses.push_back(addr);
					send<u64>(s_, nodes_count);
					recv<s32>(s_);//the client is listening
					double remote_time = recv<double>(s_);
					logging::log::event("remote_time", true)(connected + 1)(remote_time);
					remote_sockets.push_back(s_);
					++connected;
				}
				DEBUG_COUT("All clients are present");
				for (std::size_t i = 1; i < nodes_count; ++i)
				{
					DEBUG_COUT("Sending address book to client "<<i);
					for (std::size_t j = 1; j < nodes_count; ++j)
					{
						std::string remote_addr = ::inet_ntoa(remote_addresses[j].sin_addr);
						DEBUG_COUT("Sending address book item " << j << ": " << remote_addr << ":" << ntohs(remote_addresses[j].sin_port));
						send<s32>(remote_sockets[i],remote_addr.size());
						send_buf(remote_sockets[i], remote_addr.c_str(), remote_addr.size());
						send<s32>(remote_sockets[i], ntohs(remote_addresses[j].sin_port));
					}
					//send_buf(remote_sockets[i], &remote_addresses.front(), sizeof(sockaddr_in)*remote_addresses.size());
				}
				DEBUG_COUT("Address book was sent to all clients");
				ports_in_.resize(nodes_count);
				ports_out_.resize(nodes_count);
				connect_all(0, remote_addresses);
				closesocket(listener_);
				DEBUG_COUT("All nodes are connected");
				for (std::size_t i = 1; i < nodes_count; ++i)
				{
					closesocket(remote_sockets[i]);
				}
			}
			void internal_initialize_slave(const std::string& master_addr, int master_port)
			{
				int err;
				init_sockets();
				listener_ = create_listener();
				ports_in_.push_back(port_in());
				ports_out_.push_back(port_out());
				int my_port = get_port(listener_);
				DEBUG_COUT("Will listen at port " << my_port);

				socket_t to_master = connect_to(master_addr, master_port);
				std::size_t my_id = recv<u64>(to_master);
				DEBUG_COUT("My ID sent by the master is " << my_id);
				send<s32>(to_master, my_port);
				std::size_t nodes_count = recv<u64>(to_master);
				DEBUG_COUT("There are " << nodes_count << " nodes");
				err = ::listen(listener_, 2 * nodes_count);
				if (err) throw text_exception("::listen failed");
				send<s32>(to_master, 0);//I am listening
				send<double>(to_master, logging::log::now());
				std::vector<sockaddr_in> remote_addresses;
				remote_addresses.resize(nodes_count);
				for (std::size_t remote = 1; remote < nodes_count; ++remote)
				{
					std::size_t addr_len = recv<s32>(to_master);
					assert(addr_len > 0);
					std::vector<char> addr(addr_len, ' ');
					recv_buf(to_master, &addr.front(), addr_len);
					s32 port = recv<s32>(to_master);
					remote_addresses[remote].sin_family = AF_INET;
					remote_addresses[remote].sin_addr.s_addr = ::inet_addr(std::string(addr.begin(),addr.end()).c_str());
					remote_addresses[remote].sin_port = htons(port);
				}
				//recv_buf(to_master, &remote_addresses.front(), sizeof(sockaddr_in)*nodes_count);
				//the address of the master may not be valid on the master, replace it with the local data
				remote_addresses[0].sin_family = AF_INET;
				remote_addresses[0].sin_addr.s_addr = ::inet_addr(master_addr.c_str());
				remote_addresses[0].sin_port = htons(master_port);
				DEBUG_COUT("Address book was received");
				ports_in_.resize(nodes_count);
				ports_out_.resize(nodes_count);
				/*ports_out_.back().socket = connect_to(master_addr, port);
				ports_out_.back().socket_fetch = connect_to(master_addr, port);
				ports_in_.back().socket = connect_to(master_addr, port);
				ports_in_.back().socket_fetch = connect_to(master_addr, port);*/
				closesocket(to_master);
				connect_all(my_id, remote_addresses);
				closesocket(listener_);
				DEBUG_COUT("All nodes are connected");
			}
		private:
			tbb::spin_mutex& internal_mutex(thread_context* ctx, node_id to)
			{
				return ports_out_[(std::size_t)to].mutex;
			}
			tbb::spin_mutex& internal_mutex_fetch(thread_context* ctx, node_id to)
			{
				return ports_out_[(std::size_t)to].mutex_fetch;
			}
			void internal_send_message(thread_context* ctx, const message& m) OVERRIDE
			{
				tbb::spin_mutex::scoped_lock lock(internal_mutex(ctx, m.main.to));
				socket_t sock = ports_out_[(std::size_t)m.main.to].socket;
				send_buf(sock, m.get_ptr(), m.get_size());
				if (m.followup_size() > 0) send_buf(sock, m.followup_ptr(), m.followup_size());
			}
			void internal_send_message__locked(thread_context* ctx, const message& m)
			{
				socket_t sock = ports_out_[(std::size_t)m.main.to].socket;
				send_buf(sock, m.get_ptr(), m.get_size());
				if (m.followup_size() > 0) send_buf(sock, m.followup_ptr(), m.followup_size());
			}
			void internal_send_fetch_back(thread_context* ctx, const message& m) OVERRIDE
			{
				socket_t sock = ports_in_[(std::size_t)m.main.to].socket_fetch;
				send_buf(sock, m.get_ptr(), m.get_size());
				if (m.followup_size() > 0) send_buf(sock, m.followup_ptr(), m.followup_size());
			}
			command internal_get_reply__locked(thread_context* ctx, node_id from, message& m)
			{
				socket_t sock = ports_out_[(std::size_t)from].socket;
				recv_buf(sock, m.get_ptr(), m.get_size());
				std::size_t followup_size = command_processor::describe(m.main.cmd).followup_size(m);
				if (followup_size > 0)
				{
					m.followup_resize_and_clear(followup_size);
					recv_buf(sock, m.followup_ptr(), followup_size);
				}
				return m.main.cmd;
			}
			command internal_get_command_slow(thread_context* ctx, node_id from, message& m) OVERRIDE
			{
				socket_t sock = ports_in_[(std::size_t)from].socket;
				poll(sock);
				recv_buf(sock, m.get_ptr(), m.get_size());
				std::size_t followup_size = command_processor::describe(m.main.cmd).followup_size(m);
				if (followup_size > 0)
				{
					m.followup_resize_and_clear(followup_size);
					recv_buf(sock, m.followup_ptr(), followup_size);
				}
				return m.main.cmd;
			}
			command internal_get_fetch_command(thread_context* ctx, node_id from, message& m) OVERRIDE
			{
				socket_t sock = ports_in_[(std::size_t)from].socket_fetch;
				poll(sock);
				recv_buf(sock, m.get_ptr(), m.get_size());
				std::size_t followup_size = command_processor::describe(m.main.cmd).followup_size(m);
				if (followup_size > 0)
				{
					m.followup_resize_and_clear(followup_size);
					recv_buf(sock, m.followup_ptr(), followup_size);
				}
				return m.main.cmd;
			}
			void internal_send_fetch_message(thread_context* ctx, const message& m)
			{
				tbb::spin_mutex::scoped_lock lock(internal_mutex_fetch(ctx, m.main.to));
				socket_t sock = ports_out_[(std::size_t)m.main.to].socket_fetch;
				send_buf(sock, m.get_ptr(), m.get_size());
				if (m.followup_size() > 0) send_buf(sock, m.followup_ptr(), m.followup_size());
			}
			command internal_send_fetch_message_and_wait_for_reply(thread_context* ctx, message& m) OVERRIDE
			{
				tbb::spin_mutex::scoped_lock lock(internal_mutex_fetch(ctx, m.main.to));
				socket_t sock = ports_out_[(std::size_t)m.main.to].socket_fetch;
				send_buf(sock, m.get_ptr(), m.get_size());
				if (m.followup_size() > 0) send_buf(sock, m.followup_ptr(), m.followup_size());
				poll(sock);
				recv_buf(sock, m.get_ptr(), m.get_size());
				std::size_t followup_size = command_processor::describe(m.main.cmd).followup_size(m);
				if (followup_size > 0)
				{
					m.followup_resize_and_clear(followup_size);
					recv_buf(sock, m.followup_ptr(), followup_size);
				}
				return m.main.cmd;
			}
			u64 internal_number_of_nodes(thread_context* ctx) OVERRIDE
			{
				return ports_in_.size();
			}

		private:
			struct port_out
			{
				tbb::spin_mutex mutex;
				socket_t socket;
				tbb::spin_mutex mutex_fetch;
				socket_t socket_fetch;
				port_out() : socket(INVALID_SOCKET), socket_fetch(INVALID_SOCKET) {}
				port_out(const port_out& other) : socket(INVALID_SOCKET), socket_fetch(INVALID_SOCKET) {}
			};
			struct port_in
			{
				std::thread thread;
				std::thread thread_fetch;
				socket_t socket;
				socket_t socket_fetch;
				//the following are necessary to deal with the std::thread members
				port_in() : socket(INVALID_SOCKET), socket_fetch(INVALID_SOCKET) {}
				port_in(const port_in& other) : socket(INVALID_SOCKET), socket_fetch(INVALID_SOCKET) {}
				void operator=(const port_in& other) {}
			};
			socket_t listener_;
			std::deque<port_in> ports_in_;
			std::deque<port_out> ports_out_;
		};
	}
}

#endif

#endif
