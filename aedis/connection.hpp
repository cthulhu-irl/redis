/* Copyright (c) 2018-2022 Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * Distributed under the Boost Software License, Version 1.0. (See
 * accompanying file LICENSE.txt)
 */

#ifndef AEDIS_CONNECTION_HPP
#define AEDIS_CONNECTION_HPP

#include <vector>
#include <queue>
#include <limits>
#include <chrono>
#include <memory>
#include <type_traits>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/experimental/channel.hpp>

#include <aedis/adapt.hpp>
#include <aedis/resp3/request.hpp>
#include <aedis/detail/connection_ops.hpp>

namespace aedis {

/** \brief A high level Redis connection.
 *  \ingroup any
 *
 *  This class keeps a connection open to the Redis server where
 *  commands can be sent at any time. For more details, please see the
 *  documentation of each individual function.
 *
 *  https://redis.io/docs/reference/sentinel-clients
 */
template <class AsyncReadWriteStream = boost::asio::ip::tcp::socket>
class connection {
public:
   /// Executor type.
   using executor_type = typename AsyncReadWriteStream::executor_type;

   /// Type of the last layer
   using next_layer_type = AsyncReadWriteStream;

   using default_completion_token_type = boost::asio::default_completion_token_t<executor_type>;

   /** @brief Configuration parameters.
    */
   struct config {
      /// Timeout of the resolve operation.
      std::chrono::milliseconds resolve_timeout = std::chrono::seconds{5};

      /// Timeout of the connect operation.
      std::chrono::milliseconds connect_timeout = std::chrono::seconds{5};

      /// Timeout of the read operation.
      std::chrono::milliseconds read_timeout = std::chrono::seconds{5};

      /// Time interval ping operations.
      std::chrono::milliseconds ping_interval = std::chrono::seconds{5};

      /// The maximum size allowed of read operations.
      std::size_t max_read_size = (std::numeric_limits<std::size_t>::max)();

      /// Whether to coalesce requests or not.
      bool coalesce_requests = true;
   };

   /** \brief Constructor.
    *
    *  \param ex The executor.
    *  \param cfg Configuration parameters.
    */
   connection(boost::asio::any_io_executor ex, config cfg = config{})
   : resv_{ex}
   , ping_timer_{ex}
   , check_idle_timer_{ex}
   , writer_timer_{ex}
   , read_channel_{ex}
   , push_channel_{ex}
   , cfg_{cfg}
   , last_data_{std::chrono::time_point<std::chrono::steady_clock>::min()}
   {
      writer_timer_.expires_at(std::chrono::steady_clock::time_point::max());
   }

   connection(boost::asio::io_context& ioc, config cfg = config{})
   : connection(ioc.get_executor(), cfg)
   { }

   /// Returns the executor.
   auto get_executor() {return resv_.get_executor();}

   /** @brief Starts communication with the Redis server asynchronously.
    *
    *  This function performs the following steps
    *
    *  \li Resolves the Redis host as of \c async_resolve with the
    *  timeout passed in connection::config::resolve_timeout.
    *
    *  \li Connects to one of the endpoints returned by the resolve
    *  operation with the timeout passed in connection::config::connect_timeout.
    *
    *  \li Starts the idle check operation with the timeout of twice
    *  the value of connection::config::ping_interval. If no data is
    *  received during that time interval \c async_run completes with
    *  error::idle_timeout.
    *
    *  \li Starts the healthy check operation that sends command::ping
    *  to Redis with a frequency equal to
    *  connection::config::ping_interval.
    *
    *  \li Start reading from the socket and deliver events to the
    *  request started with \c async_exec or \c async_read_push.
    *
    *  It is safe to call \c async_run again after it has returned.  In this
    *  case, any outstanding commands will be sent after the
    *  connection is restablished.
    *
    * For an example see echo_server.cpp.
    *
    *  \param host The Redis address.
    *  \param port The Redis port.
    *  \param token The completion token.
    *
    *  The completion token must have the following signature
    *
    *  @code
    *  void f(boost::system::error_code);
    *  @endcode
    *
    *  \return This function returns only when there is an error.
    */
   template <class CompletionToken = default_completion_token_type>
   auto
   async_run(
      boost::string_view host = "127.0.0.1",
      boost::string_view port = "6379",
      CompletionToken token = CompletionToken{})
   {
      return boost::asio::async_compose
         < CompletionToken
         , void(boost::system::error_code)
         >(detail::run_op<connection>{this, host, port}, token, resv_);
   }

   /** @brief Executes a request on the redis server.
    *
    *  \param req The request object.
    *  \param adapter The response adapter.
    *  \param token The Asio completion token.
    *
    *  For an example see containers.cpp.
    *
    *  The completion token must have the following signature
    *
    *  @code
    *  void f(boost::system::error_code, std::size_t);
    *  @endcode
    *
    *  Where the second parameter is the size of the response that has
    *  just been read.
    */
   template <
      class Adapter = detail::response_traits<void>::adapter_type,
      class CompletionToken = default_completion_token_type>
   auto async_exec(
      resp3::request const& req,
      Adapter adapter = adapt(),
      CompletionToken token = CompletionToken{})
   {
      return boost::asio::async_compose
         < CompletionToken
         , void(boost::system::error_code, std::size_t)
         >(detail::exec_op<connection, Adapter>{this, &req, adapter}, token, resv_);
   }

   /** @brief Connects and executes a single request.
    *
    *  Combines \c async_run and the other \c async_exec overload in a
    *  single function. This function is useful for users that want to
    *  send a single request to the server.
    *
    *  \param host The address of the Redis server.
    *  \param port The port of the Redis server.
    *  \param req The request object.
    *  \param adapter The response adapter.
    *  \param token The Asio completion token.
    *
    *  For an example see intro.cpp.
    *
    *  The completion token must have the following signature
    *
    *  @code
    *  void f(boost::system::error_code, std::size_t);
    *  @endcode
    *
    *  Where the second parameter is the size of the response that has
    *  just been read.
    */
   template <
      class Adapter = detail::response_traits<void>::adapter_type,
      class CompletionToken = default_completion_token_type>
   auto async_exec(
      boost::string_view host,
      boost::string_view port,
      resp3::request const& req,
      Adapter adapter = adapt(),
      CompletionToken token = CompletionToken{})
   {
      return boost::asio::async_compose
         < CompletionToken
         , void(boost::system::error_code, std::size_t)
         >(detail::runexec_op<connection, Adapter>
            {this, host, port, &req, adapter}, token, resv_);
   }

   /** @brief Receives Redis unsolicited events like pushes.
    *
    *  Users that expect unsolicited events should call this function
    *  in a loop. If an unsolicited events comes in and there is no
    *  reader, the connection will hang and eventually timeout.
    *
    *  \param adapter The response adapter.
    *  \param token The Asio completion token.
    *
    *  For an example see subscriber.cpp.
    *
    *  The completion token must have the following signature
    *
    *  @code
    *  void f(boost::system::error_code, std::size_t);
    *  @endcode
    *
    *  Where the second parameter is the size of the response that has
    *  just been read.
    */
   template <
      class Adapter = detail::response_traits<void>::adapter_type,
      class CompletionToken = default_completion_token_type>
   auto
   async_read_push(
      Adapter adapter = adapt(),
      CompletionToken token = CompletionToken{})
   {
      auto f =
         [adapter]
         (resp3::node<boost::string_view> const& node, boost::system::error_code& ec) mutable
      {
         adapter(std::size_t(-1), node, ec);
      };

      return boost::asio::async_compose
         < CompletionToken
         , void(boost::system::error_code, std::size_t)
         >(detail::read_push_op<connection, decltype(f)>{this, f}, token, resv_);
   }

   /** @brief Closes the connection with the database.
    *
    *  The prefered way to close a connection is to send a \c quit
    *  command.
    */
   void close()
   {
      socket_->close();
      cmds_ = 0;
      payload_ = {};
      read_channel_.cancel();
      push_channel_.cancel();
      check_idle_timer_.expires_at(std::chrono::steady_clock::now());
      writer_timer_.cancel();
      ping_timer_.cancel();
      for (auto& e: reqs_) {
         e->stop = true;
         e->timer.cancel_one();
      }

      reqs_ = {};
   }

private:
   struct req_info {
      boost::asio::steady_timer timer;
      resp3::request const* req = nullptr;
      std::size_t n_cmds = 0;
      bool stop = false;

      bool expects_response() const noexcept { return n_cmds != 0;}
      void pop() noexcept
      {
         BOOST_ASSERT(n_cmds != 0);
         --n_cmds;
      }
   };

   using time_point_type = std::chrono::time_point<std::chrono::steady_clock>;
   using reqs_type = std::deque<std::shared_ptr<req_info>>;

   template <class T, class U> friend struct detail::read_push_op;
   template <class T> friend struct detail::reader_op;
   template <class T> friend struct detail::writer_op;
   template <class T> friend struct detail::ping_op;
   template <class T> friend struct detail::run_op;
   template <class T, class U> friend struct detail::exec_op;
   template <class T, class U> friend struct detail::exec_read_op;
   template <class T, class U> friend struct detail::runexec_op;
   template <class T> friend struct detail::hello_op;
   template <class T> friend struct detail::connect_with_timeout_op;
   template <class T> friend struct detail::resolve_with_timeout_op;
   template <class T> friend struct detail::check_idle_op;
   template <class T> friend struct detail::start_op;

   void on_write(typename reqs_type::iterator end)
   {
      // We have to clear the payload right after the read op in
      // order to to use it as a flag that informs there is no
      // ongoing write.
      payload_.clear();

      auto point = std::stable_partition(std::begin(reqs_), end, [](auto const& ptr) {
         return ptr->req->commands() != 0;
      });

      std::for_each(point, end, [](auto const& ptr) {
            ptr->timer.cancel();
      });

      reqs_.erase(point, end);
   }
   void add_request(resp3::request const& req)
   {
      reqs_.push_back(make_req_info());
      reqs_.back()->timer.expires_at(std::chrono::steady_clock::time_point::max());
      reqs_.back()->req = &req;
      reqs_.back()->n_cmds = req.commands();
      reqs_.back()->stop = false;

      if (socket_ != nullptr && cmds_ == 0 && payload_.empty())
         writer_timer_.cancel();
   }

   auto make_dynamic_buffer()
      { return boost::asio::dynamic_buffer(read_buffer_, cfg_.max_read_size); }

   template <class CompletionToken = default_completion_token_type>
   auto
   async_resolve_with_timeout(
      boost::string_view host,
      boost::string_view port,
      CompletionToken&& token = default_completion_token_type{})
   {
      return boost::asio::async_compose
         < CompletionToken
         , void(boost::system::error_code)
         >(detail::resolve_with_timeout_op<connection>{this, host, port},
            token, resv_);
   }

   // Calls connection::async_connect with a timeout.
   template <class CompletionToken = default_completion_token_type>
   auto
   async_connect_with_timeout(
         CompletionToken&& token = default_completion_token_type{})
   {
      return boost::asio::async_compose
         < CompletionToken
         , void(boost::system::error_code)
         >(detail::connect_with_timeout_op<connection>{this}, token, resv_);
   }

   // Loops on async_read_with_timeout described above.
   template <class CompletionToken = default_completion_token_type>
   auto
   reader(CompletionToken&& token = default_completion_token_type{})
   {
      return boost::asio::async_compose
         < CompletionToken
         , void(boost::system::error_code)
         >(detail::reader_op<connection>{this}, token, resv_.get_executor());
   }

   template <class CompletionToken = default_completion_token_type>
   auto
   writer(CompletionToken&& token = default_completion_token_type{})
   {
      return boost::asio::async_compose
         < CompletionToken
         , void(boost::system::error_code)
         >(detail::writer_op<connection>{this}, token, resv_.get_executor());
   }

   template <class CompletionToken = default_completion_token_type>
   auto
   async_start(CompletionToken&& token = default_completion_token_type{})
   {
      return boost::asio::async_compose
         < CompletionToken
         , void(boost::system::error_code)
         >(detail::start_op<connection>{this}, token, resv_);
   }

   template <class CompletionToken = default_completion_token_type>
   auto
   async_ping(CompletionToken&& token = default_completion_token_type{})
   {
      return boost::asio::async_compose
         < CompletionToken
         , void(boost::system::error_code)
         >(detail::ping_op<connection>{this}, token, resv_);
   }

   template <class CompletionToken = default_completion_token_type>
   auto
   async_check_idle(CompletionToken&& token = default_completion_token_type{})
   {
      return boost::asio::async_compose
         < CompletionToken
         , void(boost::system::error_code)
         >(detail::check_idle_op<connection>{this}, token, check_idle_timer_);
   }

   template <class CompletionToken = default_completion_token_type>
   auto async_hello(CompletionToken token = CompletionToken{})
   {
      return boost::asio::async_compose
         < CompletionToken
         , void(boost::system::error_code)
         >(detail::hello_op<connection>{this}, token, resv_);
   }

   template <
      class Adapter = detail::response_traits<void>::adapter_type,
      class CompletionToken = default_completion_token_type>
   auto
   async_exec_read(
      Adapter adapter = adapt(),
      CompletionToken token = CompletionToken{})
   {
      return boost::asio::async_compose
         < CompletionToken
         , void(boost::system::error_code, std::size_t)
         >(detail::exec_read_op<connection, Adapter>{this, adapter}, token, resv_);
   }

   std::shared_ptr<req_info> make_req_info()
   {
      if (pool_.empty())
         return std::make_shared<req_info>(boost::asio::steady_timer{resv_.get_executor()});

      auto ret = pool_.back();
      pool_.pop_back();
      return ret;
   }

   void release_req_info(std::shared_ptr<req_info> info)
   {
      pool_.push_back(info);
   }

   void coalesce_requests()
   {
      // Coaleces all requests: Copies the request to the variables
      // that won't be touched while async_write is suspended.
      BOOST_ASSERT(payload_.empty());
      BOOST_ASSERT(!reqs_.empty());

      auto const size = cfg_.coalesce_requests ? reqs_.size() : 1;
      for (auto i = 0UL; i < size; ++i) {
         payload_ += reqs_.at(i)->req->payload();
         cmds_ += reqs_.at(i)->req->commands();
      }
   }

   using channel_type = boost::asio::experimental::channel<void(boost::system::error_code, std::size_t)>;

   // IO objects
   boost::asio::ip::tcp::resolver resv_;
   std::shared_ptr<AsyncReadWriteStream> socket_;
   boost::asio::steady_timer ping_timer_;
   boost::asio::steady_timer check_idle_timer_;
   boost::asio::steady_timer writer_timer_;
   channel_type read_channel_;
   channel_type push_channel_;

   // Configuration parameters.
   config cfg_;

   // Buffer used by the read operations.
   std::string read_buffer_;

   std::string payload_;
   std::size_t cmds_ = 0;
   reqs_type reqs_;
   std::vector<std::shared_ptr<req_info>> pool_;

   // Last time we received data.
   time_point_type last_data_;

   // The result of async_resolve.
   boost::asio::ip::tcp::resolver::results_type endpoints_;

   resp3::request req_;
};

} // aedis

#endif // AEDIS_CONNECTION_HPP
