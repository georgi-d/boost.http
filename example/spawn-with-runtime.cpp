#include <iostream>
#include <algorithm>

#include <boost/utility/string_ref.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/http/socket.hpp>
#include <boost/http/algorithm.hpp>
#include <boost/http/server_socket_adaptor.hpp>

using namespace std;
using namespace boost;

int main()
{
    asio::io_service ios;

    asio::ip::tcp::acceptor acceptor(ios, asio::ip::tcp
                                     ::endpoint(asio::ip::tcp::v6(), 8080));

    auto work = [&acceptor](asio::yield_context yield) {
        while (true) {
            char buffer[4];
            http::server_socket_adaptor<http::socket>
            underlying_socket(acceptor.get_io_service(), asio::buffer(buffer));
            try {
                http::polymorphic_server_socket &socket = underlying_socket;
                std::string method;
                std::string path;
                http::message message;

                cout << "About to accept a new connection" << endl;
                acceptor.async_accept(underlying_socket.next_layer()
                                      .next_layer(), yield);

                cout << "About to receive a new message" << endl;
                socket.async_read_request(method, path, message, yield);
                //message.body().clear(); // freeing not used resources

                if (http::request_continue_required(message)) {
                    cout << "Continue required. About to send \"100-continue\""
                         << std::endl;
                    socket.async_write_response_continue(yield);
                }

                while (socket.read_state() != http::read_state::empty) {
                    cout << "Message not fully received" << endl;
                    switch (socket.read_state()) {
                    case http::read_state::message_ready:
                        cout << "About to receive some body" << endl;
                        socket.async_read_some(message, yield);
                        break;
                    case http::read_state::body_ready:
                        cout << "About to receive trailers" << endl;
                        socket.async_read_trailers(message, yield);
                        break;
                    default:;
                    }
                }

                //cout << "BODY:==";
                //for (const auto &e: message.body()) {
                //    cout << char(e);
                //}
                //cout << "==" << endl;

                cout << "Message received. State = "
                     << int(socket.read_state()) << endl;
                cout << "Method: " << method << endl;
                cout << "Path: " << path << endl;
                {
                    auto host = message.headers().find("host");
                    if (host != message.headers().end())
                        cout << "Host header: " << host->second << endl;
                }

                std::cout << "Write state = " << int(socket.write_state())
                << std::endl;

                cout << "About to send a reply" << endl;

                http::message reply;
                reply.headers().emplace("connection", "close");
                const char body[] = "Foobar";
                std::copy(body, body + sizeof(body) - 1,
                          std::back_inserter(reply.body()));

                socket.async_write_response(200, string_ref("OK"), reply,
                                            yield);
            } catch (system::system_error &e) {
                if (e.code()
                    != system::error_code{http::http_errc::stream_finished}) {
                    cerr << "Aborting on exception: " << e.what() << endl;
                    std::exit(1);
                }

                underlying_socket.next_layer().next_layer()
                .shutdown(asio::ip::tcp::socket::shutdown_both);
                underlying_socket.next_layer().next_layer().close();
            } catch (std::exception &e) {
                cerr << "Aborting on exception: " << e.what() << endl;
                std::exit(1);
            }
        };
    };

    cout << "About to spawn" << endl;
    spawn(ios, work);

    cout << "About to run" << endl;
    ios.run();

    return 0;
}
