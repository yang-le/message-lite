#include <iostream>
#include <sstream>
#include <asio.hpp>
#include <chrono>
#include <regex>

using asio::ip::tcp;
using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using asio::use_awaitable;
namespace this_coro = asio::this_coro;

std::unordered_map<std::string, tcp::socket*> users;

awaitable<void> chat(tcp::socket socket)
{
	std::string name;

	try {
		char data[1024];

		std::size_t n = co_await socket.async_read_some(asio::buffer(data), use_awaitable);
		name = std::string(data, n);

		// try to registration, and return the result
		auto [_, success] = users.try_emplace(name, &socket);
		co_await async_write(socket, asio::buffer(success ? "ok" : "ng"), use_awaitable);
		if (!success)
			co_return;

		for (;;) {
			std::size_t n = co_await socket.async_read_some(asio::buffer(data), use_awaitable);
			std::string msg = std::string(data, n);

			std::smatch m;
			std::regex_match(msg, m, std::regex("@(.*) (.*)"));

			std::string target = m[1].str();
			std::string text = m[2].str();

			std::stringstream ss;
			auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

			if (target == name) {
				ss << std::put_time(std::localtime(&now), "%Y/%m/%d %T ") << name << ": " << text;
				co_await async_write(socket, asio::buffer(ss.str()), use_awaitable);
			}
			else if (target == "all") {
				ss << std::put_time(std::localtime(&now), "%Y/%m/%d %T broadcase from ") << name << ": " << text;
				for (auto& [_, s] : users)
					co_await async_write(*s, asio::buffer(ss.str()), use_awaitable);
			}
			else {
				if (users.contains(target)) {
					ss << std::put_time(std::localtime(&now), "%Y/%m/%d %T ") << name << ": " << text;
					co_await async_write(*users[target], asio::buffer(ss.str()), use_awaitable);
				}
				else
					ss << target << " is not online";

				// echo of the send message
				co_await async_write(socket, asio::buffer(ss.str()), use_awaitable);
			}
		}
	}
	catch (std::system_error& e) {
		// eof / 10054: remote close
		if (e.code() != asio::stream_errc::eof && e.code().value() != 10054)
			std::cerr << e.what() << std::endl;
		else
			users.erase(name);
	}
	catch (std::exception& e) {
		std::cerr << e.what() << std::endl;
	}
}

awaitable<void> listener(unsigned short port)
{
	auto executor = co_await this_coro::executor;
	tcp::acceptor acceptor(executor, { tcp::v4(), port });

	for (;;) {
		tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
		co_spawn(executor, chat(std::move(socket)), detached);
	}
}

int main(int argc, const char** argv)
{
	int port = 12345;

	if (argc >= 2 && !strcmp(argv[1], "--port")) {
		port = atoi(argv[2]);
		if (port < 1 || port > 65535) {
			std::cerr << "invalid port " << port << std::endl;
			return 1;
		}
	}

	try {
		asio::io_context io_context(1);

		asio::signal_set signals(io_context, SIGINT, SIGTERM);
		signals.async_wait([&](auto, auto) { io_context.stop(); });

		co_spawn(io_context, std::bind(listener, port), detached);

		io_context.run();
	}
	catch (std::exception& e) {
		std::cerr << e.what() << std::endl;
	}

	return 0;
}
