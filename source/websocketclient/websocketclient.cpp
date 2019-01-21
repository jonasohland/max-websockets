#include <mutex>
#include <thread>

#include "../shared/net_url.h"
#include "../shared/connection.h"
#include "../shared/devices/protobuf_decoder_worker.h"

#include "../shared/messages/generic_max_message.h"
#include "../shared/messages/proto_message_wrapper.h"
#include "../shared/messages/proto_message_base.h"


#include "../shared/devices/devices.h"

#include "../shared/ohlano_min.h"

#include "c74_min.h"

using namespace c74::min;
using namespace std::placeholders;


class websocketclient : public object<websocketclient> {
public:

	using websocket_stream = boost::beast::websocket::stream<boost::asio::ip::tcp::socket>;
	using websocket_connection = ohlano::connection<boost::beast::websocket::stream<boost::asio::ip::tcp::socket>, ohlano::max_message>;

	MIN_DESCRIPTION{ "WebSockets for Max! (Client)" };
	MIN_TAGS{ "net" };
	MIN_AUTHOR{ "Jonas Ohland" };
	MIN_RELATED{ "udpsend, udpreceive" };

	inlet<> main_inlet{ this, "(anything) data in" };
	outlet<thread_check::none, thread_action::assert> data_out{ this, "data out" };
	outlet<> status_out{ this, "status out" };

    void changed_port(int val){}

    void changed_host(std::string val){}

    attribute<int> port { this, "port", 80, min_wrap_member(&websocketclient::set_port),
		description{ "remote port to connect to" }, range{ 0, 65535 }};

	attribute<symbol> host { this, "host", "localhost", min_wrap_member(&websocketclient::set_host)};

	explicit websocketclient(const atoms& args = {}) {

		net_url<>::error_code ec;
		net_url<> url;
		net_url<> t_url;

		if (args.size() > 0) {

			for (auto& arg : args) {

				switch (arg.a_type) {
				case c74::max::e_max_atomtypes::A_SYM:

					if (!url) {
						t_url = net_url<>(arg, ec);

						if (ec != net_url<>::error_code::SUCCESS)
							cerr << "symbol argument could not be decoded to an url" << endl;

						if (url.has_port() && t_url.has_port()) {
							cerr << "Found multiple port arguments!" << endl;
						}

						url = t_url;
					}

					break;

				case c74::max::e_max_atomtypes::A_FLOAT:
					cerr << "float not supported as argument" << endl;
					break;
				case c74::max::e_max_atomtypes::A_LONG:
					cout << "long arg: " << std::string(arg) << endl;
					if (!url.has_port()) { url.set_port(arg); }
					else { cerr << "Found multiple port arguments!" << endl; }
					break;
				default:
					cerr << "unsupported argument type" << endl;
					break;
				}

			}

			

			if (url) {

				atoms host_at = { url.host() };
				atoms port_at = { url.port_int() };

				host.set(host_at, false);
				port.set(port_at, false);

				//there is work to do
				cout << "running network io worker thread" << endl;

				client_thread_ptr = std::make_shared<std::thread>([this]() {
					//try {
						io_context_.run();
					//}
					//catch (std::exception const&  ex) {
					//	cerr << "exception in network io worker thread: " << ex.what() << endl;
					//}
					cout << "finished running network io worker thread" << endl;
				});

				make_connection(url);

				dec_worker_.run(4);

			}
			else {
				cout << "no valid websocket address provided" << endl;
			}
		}
	}

	

	void make_connection(net_url<> url) {

		if (!url.is_resolved()) {

			cout << "resolving " << url.host() << endl;

			resolver.resolve(url, [this](boost::system::error_code ec, net_url<> _url) {
				if (!ec) {
					for (auto& endpoint : _url.endpoints()) {
						cout << "result: " << endpoint.address().to_string() << ":" << endpoint.port() << endl;
					}
					cout << "connecting..." << endl;
					connection_ = std::make_shared<websocket_connection>(io_context_, allocator_);
					connection_->wq()->binary(true);
					perform_connect(_url);
				}
				else {
					cerr << "resolving failed: " << ec.message() << endl;
				}
			});
		}
		else {

			cout << "url:" << url.host() << url.port() << endl;

			cout << "connecting..." << endl;
			connection_ = std::make_shared<websocket_connection>(io_context_, allocator_);
			io_context_.post([=]() { perform_connect(url); });
		}
	}

	void perform_connect(net_url<> _url) {
		if (connection_) {
			connection_->connect(_url, [=](boost::system::error_code ec) {
				if (!ec) {

					cout << "connection established" << endl;

					

					begin_read();
				}
				else {
					cerr << "connection error: " << ec.message() << endl;
				}
			});
		}
	}

    
	void begin_read() {
		if (connection_) {
			connection_->begin_read([=](boost::system::error_code ec, ohlano::max_message* mess, size_t bytes_transferred) {
				if (ec) {
					cout << "connection server closed: " << ec.message() << c74::min::endl;
					return;
				}
				mess->deserialize();
				output_.write(mess);
				allocator_.deallocate(mess);
			});
		}
	}


	~websocketclient(){

		if (connection_) {
			connection_->close([=](boost::system::error_code ec) {
				if (!ec)
					cout << "gracefully closed connection" << endl;
			});
		}

		if (work.owns_work()) {
			work.reset();
		}

		dec_worker_.stop();

		if (client_thread_ptr) {
			if (client_thread_ptr->joinable()) {
				client_thread_ptr->join();
			}
		}

	}

	atoms report_status(const atoms& args, int inlet) {
		if (connection_) { status_out.send(connection_->status_string()); }
		else { status_out.send("no_connection"); }
		return args;
	}

	atoms handle_data(const atoms& args, int inlet) {
		if (connection_) {
			if (connection_->status() == websocket_connection::status_codes::ONLINE) {

				auto msg = allocator_.allocate();

				msg->push_atoms(args);

				dec_worker_.async_encode(msg, [=](ohlano::max_message* msg_) {
					connection_->wq()->submit(msg_);
				});
			}
		}

		return args;
	}

	atoms handle_float(const atoms& args, int inlet) {
		if (connection_) {
			if (connection_->status() == websocket_connection::status_codes::ONLINE) {

				auto msg = allocator_.allocate();

				msg->push_atoms(args);

				dec_worker_.async_encode(msg, [=](ohlano::max_message* msg_) {
					connection_->wq()->submit(msg_);
				});
			}
		}
		return args;
	}

	atoms handle_long(const atoms& args, int inlet) {
		if (connection_) {
			if (connection_->status() == websocket_connection::status_codes::ONLINE) {

				auto msg = allocator_.allocate();

				msg->push_atoms(args);

				dec_worker_.async_encode(msg, [=](ohlano::max_message* msg_) {
					connection_->wq()->submit(msg_);
				});
			}
		}
		return args;
	}

	atoms handle_list(const atoms& args, int inlet) {
		if (connection_) {
			if (connection_->status() == websocket_connection::status_codes::ONLINE) {

				auto msg = allocator_.allocate();

				auto tp = static_cast<c74::max::e_max_atomtypes>(args[0].a_type);

				if (args.size() > 2) {

					auto arg_it = args.cbegin();

					while (arg_it->a_type == tp) {

						arg_it++;
						if (arg_it == args.end()) {
							break;
						}
					}

					if (arg_it - args.begin() > 3) {
						msg->push_atomarray(args.begin(), arg_it, tp);
						for (; arg_it != args.end(); arg_it++) {
							msg->push_atom(*arg_it);
						}
					}
					else {
						msg->push_atoms(args);
					}
				}
				else {
					msg->push_atoms(args);
				}

				dec_worker_.async_encode(msg, [=](ohlano::max_message* msg_) {
					connection_->wq()->submit(msg_);
				});
			}
		}
		return args;
	}

	atoms set_port(const atoms& args, int inlet) {
		return args;
	}

	atoms set_host(const atoms& args, int inlet) {
		return args;
	}

	atoms connect(const atoms& args, int inlet) {
		return args;
	}


	message<> status{ this, "status", "report status", min_wrap_member(&websocketclient::report_status) };
	message<> set_port_cmd{ this, "port", "set port", min_wrap_member(&websocketclient::set_port) };
	message<> set_host_cmd{ this, "host", "set host", min_wrap_member(&websocketclient::set_host) };
	message<> connect_cmd{ this, "connect", "connect websocket", min_wrap_member(&websocketclient::connect) };


	message<threadsafe::yes> data_input{ this, "anything", "send data", min_wrap_member(&websocketclient::handle_data) };
	message<threadsafe::yes> list_input{ this, "list", "send list data", min_wrap_member(&websocketclient::handle_list) };
	message<threadsafe::yes> long_input{ this, "int", "send data", min_wrap_member(&websocketclient::handle_long) };
	message<threadsafe::yes> float_input{ this, "float", "send data", min_wrap_member(&websocketclient::handle_float) };

	message<> version{ this, "anything", "print version number", [=](const atoms& args, int inlet) -> atoms { 

#ifdef VERSION_TAG
			cout << "WebSocket Client for Max " << STR(VERSION_TAG) << "-" << STR(CONFIG_TAG) << "-" << STR(OS_TAG) << c74::min::endl; 
#else
			cout << "test build" << c74::min::endl;
#endif
			return args; 
		} 
	};



private:

	/** The executor that will provide io functionality */
	boost::asio::io_context io_context_;

	/** This object will keep the io_context alive as long as the object exists */
	boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work{ io_context_.get_executor() };

	/** This object is responsible for resolving hostnames to ip addresses */
	ohlano::multi_resolver<boost::asio::ip::tcp> resolver{ io_context_ };

	ohlano::max_message::factory allocator_;

	std::shared_ptr<websocket_connection> connection_;

	std::shared_ptr<std::thread> client_thread_ptr;

	ohlano::protobuf_decoder_worker<ohlano::max_message> dec_worker_{};

	ohlano::outlet_output_adapter<ohlano::max_message> output_{ &data_out };

	

	std::mutex post_mtx;

	ohlano::console_stream_adapter console_adapter{ [this](std::string str) { cout << str << endl; }, true};
	ohlano::console_stream_adapter console_error_adapter{ [this](std::string str) { cerr << str << endl; }, true };

};

void ext_main(void* r) {

#ifdef VERSION_TAG
	c74::max::object_post(nullptr, "WebSocket Client for Max // (c) Jonas Ohland 2018 -- %s-%s-%s built: %s", STR(VERSION_TAG), STR(CONFIG_TAG), STR(OS_TAG), __DATE__);
#else
	c74::max::object_post(nullptr, "WebSocket Client for Max // (c) Jonas Ohland 2018 -- built %s - test build", __DATE__);
#endif

	c74::min::wrap_as_max_external<websocketclient>("websocketclient", __FILE__, r);
}
