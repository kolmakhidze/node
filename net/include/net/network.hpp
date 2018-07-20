#ifndef __NETWORK_HPP__
#define __NETWORK_HPP__
#include <boost/asio.hpp>

#include <client/config.hpp>

#include "pacmans.hpp"
#include "transport.hpp"

class Network {
public:
  static Network& init(const Config&);
  static Network& get() { return *networkPtr_; }

  bool isGood() const { return good_; }
  ip::udp::endpoint resolve(const EndpointData&);

  void sendDirect(const Packet, const ip::udp::endpoint&);

  ~Network();

  Network(const Network&) = delete;
  Network(Network&&) = delete;
  Network& operator=(const Network&) = delete;
  Network& operator=(Network&&) = delete;

private:
  Network(const Config&);

  void readerRoutine(const Config&);
  void writerRoutine(const Config&);
  void processorRoutine();

  enum ThreadStatus {
    NonInit,
    Failed,
    Success
  };
  ip::udp::socket* getSocketInThread(const bool,
                                     const EndpointData&,
                                     std::atomic<ThreadStatus>&,
                                     const bool useIPv6);

  static Network* networkPtr_;

  bool good_;

  io_context context_;
  ip::udp::resolver resolver_;

  IPacMan iPacMan_;
  OPacMan oPacMan_;

  Transport* transport_;

  std::thread readerThread_;
  std::thread writerThread_;
  std::thread processorThread_;

  // Only needed in a one-socket configuration
  std::atomic<bool> singleSockOpened_ = { false };
  std::atomic<ip::udp::socket*> singleSock_ = { nullptr };

  std::atomic<ThreadStatus> readerStatus_ = { NonInit };
  std::atomic<ThreadStatus> writerStatus_ = { NonInit };
};

#endif // __NETWORK_HPP__
