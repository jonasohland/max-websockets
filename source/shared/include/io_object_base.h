#include <memory>
#include <thread>

#include <boost/asio/dispatch.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>

#include "connection.h"
#include "types.h"

namespace ohlano {

    namespace io_object {

        template < typename ThreadOption >
        class thread_base {};

        template <>
        class thread_base< threads::single > {
          public:
            virtual ~thread_base() {}

            size_t thread_count() { return static_cast< bool >( worker_thread_ ); }

            void await_threads_end() {
                if ( worker_thread_ )
                    if ( worker_thread_->joinable() )
                        worker_thread_->join();
            }

          protected:
            void create_threads() {
                worker_thread_ = std::make_unique< std::thread >(
                    std::bind( &thread_base::do_work_impl, this ) );
            }

            virtual void do_work_impl() = 0;

          private:
            std::unique_ptr< std::thread > worker_thread_;
        };

        template <>
        class thread_base< threads::multi > {
          public:
            virtual ~thread_base() {}

            size_t thread_count() { return worker_threads_.size(); }

          protected:
            void await_threads_end() {
                for ( auto& thread : worker_threads_ ) {
                    if ( thread )
                        if ( thread->joinable() )
                            thread->join();
                }
            }

            void create_threads( int num_threads = 1 ) {
                for ( int i = 0; i < num_threads; ++i ) {
                    worker_threads_.push_back( std::make_unique< std::thread >(
                        std::bind( &thread_base::do_work_impl, this ) ) );
                }
            }

            virtual void do_work_impl() = 0;

            std::mutex& base_mtx() { return thread_base_mutex; }

          private:
            std::mutex thread_base_mutex;
            std::vector< std::unique_ptr< std::thread > > worker_threads_;
        };

        template < typename ThreadOption >
        class base : thread_base< ThreadOption > {

          public:
            virtual ~base() {}

            using thread_option = ThreadOption;
            using context_type = boost::asio::io_context;

          protected:
            template < typename Opt = ThreadOption >
            static constexpr const bool multithreaded =
                std::is_same< Opt, threads::multi >::value;

            template < typename Opt = ThreadOption >
            typename threads::opt_enable_if_multi_thread< Opt >::type
            call_work_end_notification() {
                std::lock_guard< std::mutex > call_lock{ this->base_mtx() };
                on_work_finished();
            }

            template < typename Opt = ThreadOption >
            typename threads::opt_enable_if_multi_thread< Opt >::type
            call_work_start_notification() {
                std::lock_guard< std::mutex > call_lock{ this->base_mtx() };
                on_work_started();
            }

            template < typename Opt = ThreadOption >
            typename threads::opt_enable_if_single_thread< Opt >::type
            call_work_end_notification() {
                on_work_finished();
            }

            template < typename Opt = ThreadOption >
            typename threads::opt_enable_if_single_thread< Opt >::type
            call_work_start_notification() {
                on_work_started();
            }

            virtual void on_work_started(){};
            virtual void on_work_finished(){};

            template < typename Opt = ThreadOption >
            typename threads::opt_enable_if_single_thread< Opt >::type begin_work() {
                this->create_threads();
            }

            template < typename Opt = ThreadOption >
            typename threads::opt_enable_if_multi_thread< Opt >::type
            begin_work( int threads = 1 ) {
                this->create_threads( threads );
            }

            template < typename... Ts >
            void post( Ts&&... args ) {
                boost::asio::post( ctx_, std::forward< Ts >( args )... );
            }

            template < typename... Ts >
            void dispatch( Ts&&... args ) {
                boost::asio::dispatch( ctx_, std::forward< Ts >( args )... );
            }

            virtual void do_work_impl() override {
                call_work_start_notification();
                ctx_.run();
                call_work_end_notification();
            }

            /// allow the executor to end its work
            bool end_work() {

                if ( object_work_guard_.owns_work() ) {
                    object_work_guard_.reset();
                    return true;
                }

                return false;
            }

            /// wait for any work to end on the executor
            void await_work_end() { this->await_threads_end(); }

            boost::asio::io_context& context() { return ctx_; }

          private:
            boost::asio::io_context ctx_;
            boost::asio::executor_work_guard< boost::asio::io_context::executor_type >
                object_work_guard_{ ctx_.get_executor() };
        };
    } // namespace io_object
} // namespace ohlano