/* Copyright (c) 2018-2022 Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * Distributed under the Boost Software License, Version 1.0. (See
 * accompanying file LICENSE.txt)
 */

#ifndef BOOST_REDIS_CONFIG_HPP
#define BOOST_REDIS_CONFIG_HPP

#include <string>
#include <chrono>

namespace boost::redis
{

/** @brief Address of a Redis server
 *  @ingroup high-level-api
 */
struct address {
   /// Redis host.
   std::string host = "127.0.0.1";
   /// Redis port.
   std::string port = "6379";
};

/** @brief Configure parameters used by the connection classes
 *  @ingroup high-level-api
 */
struct config {
   /// Address of the Redis server.
   address addr = address{"127.0.0.1", "6379"};

   /** @brief Username passed to the
    * [HELLO](https://redis.io/commands/hello/) command.  If left
    * empty `HELLO` will be sent without authentication parameters.
    */
   std::string username;

   /** @brief Password passed to the
    * [HELLO](https://redis.io/commands/hello/) command.  If left
    * empty `HELLO` will be sent without authentication parameters.
    */
   std::string password;

   /// Client name parameter of the [HELLO](https://redis.io/commands/hello/) command.
   std::string clientname = "Boost.Redis";

   /// Message used by the health-checker in `boost::redis::connection::async_run`.
   std::string health_check_id = "Boost.Redis";

   /// Logger prefix, see `boost::redis::logger`.
   std::string log_prefix = "(Boost.Redis) ";

   /// Time the resolve operation is allowed to last.
   std::chrono::steady_clock::duration resolve_timeout = std::chrono::seconds{10};

   /// Time the connect operation is allowed to last.
   std::chrono::steady_clock::duration connect_timeout = std::chrono::seconds{10};

   /// Time the SSL handshake operation is allowed to last.
   std::chrono::steady_clock::duration ssl_handshake_timeout = std::chrono::seconds{10};

   /// @brief Health checks interval.  
   std::chrono::steady_clock::duration health_check_interval = std::chrono::seconds{2};

   /// Time waited before trying a reconnection.
   std::chrono::steady_clock::duration reconnect_wait_interval = std::chrono::seconds{1};
};

} // boost::redis

#endif // BOOST_REDIS_CONFIG_HPP
