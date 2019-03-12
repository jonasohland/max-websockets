#pragma once

#include <mutex>
#include <boost/tti/tti.hpp>
#include <boost/mpl/lambda.hpp>
#include <boost/mpl/placeholders.hpp>
#include <boost/type_traits/is_same.hpp>

namespace ohlano {

    namespace messages {

        namespace detail {
            BOOST_TTI_HAS_MEMBER_FUNCTION( set_direction );
            BOOST_TTI_HAS_MEMBER_FUNCTION( notify_send );
            BOOST_TTI_HAS_MEMBER_FUNCTION( notify_send_done );
        } // namespace detail

        template < typename Message >
        using is_direction_supported = detail::has_member_function_set_direction<
            Message, void, boost::mpl::vector< bool >,
            boost::function_types::const_qualified >;

        template < typename Message, typename Ty = void >
        using enable_if_direction_supported =
            std::enable_if< is_direction_supported< Message >::value >;

        template < typename Message >
        using is_notify_send_supported = detail::has_member_function_notify_send<
            Message, void, boost::mpl::vector<>, boost::function_types::const_qualified >;

        template < typename Message >
        using is_notify_send_done_supported =
            detail::has_member_function_notify_send_done<
                Message, void, boost::mpl::vector<>,
                boost::function_types::const_qualified >;

        template < typename Message >
        struct is_send_notification_supported {
            static constexpr const bool value =
                is_notify_send_supported< Message >::value &&
                is_notify_send_done_supported< Message >::value;
        };
    } // namespace messages

    namespace sessions {

        namespace roles {
            struct server {};
            struct client {};
        } // namespace roles

        namespace features {
            struct timeout {};
            struct statistics {};
        } // namespace features

        template < typename Role, typename T = void >
        struct enable_for_client {};

        template < typename Ty >
        struct enable_for_client< roles::client, Ty > {
            using type = Ty;
        };

        template < typename Role, typename T = void >
        struct enable_for_server {};

        template < typename Ty >
        struct enable_for_server< roles::server, Ty > {
            using type = Ty;
        };
    } // namespace sessions

    namespace threads {

        // used to indicate that the object will be used in a single threaded context
        struct single {};

        // obvious
        struct multi {};

        template < typename T >
        struct opt_is_multi : public std::false_type {};

        template <>
        struct opt_is_multi< threads::multi > : public std::true_type {};

        template < class Op, class Ty = void >
        struct opt_enable_if_multi_thread {};

        template < class Ty >
        struct opt_enable_if_multi_thread< threads::multi, Ty > {
            using type = Ty;
        };

        template < class Op, class Ty = void >
        struct opt_enable_if_single_thread {};

        template < class Ty >
        struct opt_enable_if_single_thread< threads::single, Ty > {
            using type = Ty;
        };

        namespace detail {
            BOOST_TTI_HAS_TYPE( thread_option );
        }

        template < typename T >
        struct is_multi_thread_enabled {
            static constexpr const bool value = detail::has_type_thread_option<
                T, boost::is_same< boost::mpl::placeholders::_1, multi > >::value;
        };

        template < typename T, typename Ty = void >
        using enable_if_multi_thread_enabled =
            typename std::enable_if< is_multi_thread_enabled< T >::value, Ty >;

        template < typename T, typename Ty = void >
        using enable_if_multi_thread_enabled_t =
            typename enable_if_multi_thread_enabled< T, Ty >::type;

        namespace detail {

            template < bool HasMutex, typename Mutex = std::mutex >
            class single_mtx_base;

            template < typename Mutex >
            class single_mtx_base< true, Mutex > {

              public:
                Mutex& mutex() { return opt_mtx_; }

              private:
                Mutex opt_mtx_;
            };

            template < typename Mutex >
            class single_mtx_base< false, Mutex > {};
        }

    } // namespace threads

    template < typename Thing, bool DoLock, typename Mutex = std::mutex >
    class safe_visitable
        : public threads::detail::single_mtx_base< DoLock, Mutex > {

        Thing thing;

      public:
        template < typename... Args >
        explicit safe_visitable( Args... args )
            : thing( std::forward< Args >( args )... ) {}

        template < bool Enable = DoLock, typename Visitor >
        typename std::enable_if< Enable >::type apply( Visitor v ) {
            std::lock_guard< Mutex > visit_lock( this->mutex() );
            v( thing );
        }

        template < bool Enable = DoLock, typename Visitor >
        typename std::enable_if< !(Enable) >::type apply( Visitor v ) {
            v( thing );
        }

        template < bool Enable = DoLock, typename Visitor >
        typename std::enable_if< Enable >::type apply_adopt( Visitor v ) {
            v( thing, this->mutex() );
        }

        Thing* operator->() { return thing; }

        const Thing* operator->() const { return thing; }

        Thing& operator*() { return thing; }

        const Thing& operator*() const { return thing; }
    };
    
    namespace threads {
        template < typename Thing, typename ThreadOption, typename Mutex = std::mutex >
        class opt_safe_visitable : public safe_visitable<Thing, threads::opt_is_multi< ThreadOption >::value, Mutex> {};
    }

} // namespace ohlano
