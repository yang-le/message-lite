#include <iostream>
#include <string>
#include <asio.hpp>

using asio::ip::tcp;

int main(int argc, const char** argv)
{
	std::string name;

	if (argc >= 2 && !strcmp(argv[1], "--name")) {
		name = argv[2];
		if (name == "all") {
			std::cerr << "all is not a valid name" << std::endl;
			return 1;
		}
	}
	else {
		std::cerr << "usage: " << argv[0] << " --name <your-name>" << std::endl;
		return 1;
	}

	asio::io_context io_context;
	tcp::socket socket(io_context);

	std::string ip;
	std::string port;
	std::string prompt = name;
	std::thread reciever;
	bool connected = false;

	std::cout << prompt << " > ";

	std::string input;
	while (std::getline(std::cin, input)) {
		if (input.starts_with("HI ")) {
			ip = input.substr(3, input.find(':') - 3);
			port = input.substr(input.find(':') + 1);

			tcp::resolver resolver(io_context);
			try {
				asio::connect(socket, resolver.resolve(ip, port));

				asio::write(socket, asio::buffer(name));

				char data[4] = { 0 };
				socket.read_some(asio::buffer(data));
				if (strcmp("ok", data)) {
					std::cerr << "user name registed, please try another name." << std::endl;
					socket.close();
				}
				else {
					// only one MS, so no need to consider reconnecting to other MS
					connected = true;
					prompt = ip + ':' + port + '|' + name;
					reciever = std::thread([&]() {
						char data[1024] = { 0 };
						while (connected) {
							try {
								auto n = socket.read_some(asio::buffer(data));
								std::cout << std::endl << std::string(data, n) << std::endl;
								std::cout << prompt << " > ";
							}
							catch (std::system_error& e) {
								// 10054: remote close
								if (e.code().value() == 10054) {
									connected = false;
									prompt = name;
								}

								// 10053: software caused connection abort
								if (e.code().value() != 10053)
									std::cerr << e.what() << std::endl;
							}
							catch (std::exception& e) {
								std::cerr << e.what() << std::endl;
							}
						}
						});
				}
			}
			catch (std::exception& e) {
				std::cerr << e.what() << std::endl;
			}
		}

		if (input[0] == '@') {
			std::stringstream ss;

			while (input.ends_with('\\')) {
				input.pop_back();
				ss << input << std::endl;
				std::getline(std::cin, input);
			}

			ss << input;

			try {
				asio::write(socket, asio::buffer(ss.str()));
			}
			catch (std::exception& e) {
				std::cerr << e.what() << std::endl;
			}
		}

		if (input == "BYE" || input == "QUIT") {
			if (reciever.joinable()) {
				connected = false;
				socket.close();
				reciever.join();
			}
			prompt = name;
		}

		if (input == "QUIT")
			break;

		std::cout << prompt << " > ";
	}

	return 0;
}
