/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 */

#ifndef CORE_FUTURE_UTIL_HH_
#define CORE_FUTURE_UTIL_HH_

#include "future.hh"
#include <tuple>

// parallel_for_each - run tasks in parallel
//
// Given a range [@begin, @end) of objects, run func(*i) for each i in
// the range, and return a future<> that resolves when all the functions
// complete.  @func should return a future<> that indicates when it is
// complete.
template <typename Iterator, typename Func>
inline
future<>
parallel_for_each(Iterator begin, Iterator end, Func&& func) {
    auto ret = make_ready_future<>();
    while (begin != end) {
        auto f = func(*begin++).then([ret = std::move(ret)] () mutable {
            return std::move(ret);
        });
        ret = std::move(f);
    }
    return ret;
}

// The AsyncAction concept represents an action which can complete later than
// the actual function invocation. It is represented by a function which
// returns a future which resolves when the action is done.

template<typename AsyncAction, typename StopCondition>
static inline
void do_until_continued(StopCondition&& stop_cond, AsyncAction&& action, promise<> p) {
    while (!stop_cond()) {
        auto&& f = action();
        if (!f.available()) {
            f.then([action = std::forward<AsyncAction>(action),
                    stop_cond = std::forward<StopCondition>(stop_cond), p = std::move(p)]() mutable {
                do_until_continued(stop_cond, action, std::move(p));
            });
            return;
        }

        if (f.failed()) {
            f.forward_to(std::move(p));
            return;
        }
    }

    p.set_value();
}

// Invokes given action until it fails or given condition evaluates to true.
template<typename AsyncAction, typename StopCondition>
static inline
future<> do_until(StopCondition&& stop_cond, AsyncAction&& action) {
    promise<> p;
    auto f = p.get_future();
    do_until_continued(std::forward<StopCondition>(stop_cond),
        std::forward<AsyncAction>(action), std::move(p));
    return f;
}

// Invoke given action until it fails.
template<typename AsyncAction>
static inline
future<> keep_doing(AsyncAction&& action) {
    try {
        return action().then([action = std::forward<AsyncAction>(action)] () mutable {
            return keep_doing(std::forward<AsyncAction>(action));
        });
    } catch (...) {
        return make_exception_future(std::current_exception());
    }
}

template<typename Iterator, typename AsyncAction>
static inline
future<> do_for_each(Iterator begin, Iterator end, AsyncAction&& action) {
    while (begin != end) {
        auto f = action(*begin++);
        if (!f.available()) {
            return f.then([action = std::forward<AsyncAction>(action),
                    begin = std::move(begin), end = std::move(end)] () mutable {
                return do_for_each(std::move(begin), std::move(end), std::forward<AsyncAction>(action));
            });
        }
    }
    return make_ready_future<>();
}


template <typename... Future>
future<std::tuple<Future...>> when_all(Future&&... fut);

template <>
inline
future<std::tuple<>>
when_all() {
    return make_ready_future<std::tuple<>>();
}

// gcc can't capture a parameter pack, so we need to capture
// a tuple and use apply.  But apply cannot accept an overloaded
// function pointer as its first parameter, so provide this instead.
struct do_when_all {
    template <typename... Future>
    future<std::tuple<Future...>> operator()(Future&&... fut) const {
        return when_all(std::move(fut)...);
    }
};

template <typename Future, typename... Rest>
inline
future<std::tuple<Future, Rest...>>
when_all(Future&& fut, Rest&&... rest) {
    return std::move(fut).then_wrapped(
            [rest = std::make_tuple(std::move(rest)...)] (Future&& fut) mutable {
        return apply(do_when_all(), std::move(rest)).then_wrapped(
                [fut = std::move(fut)] (future<std::tuple<Rest...>>&& rest) mutable {
            return make_ready_future<std::tuple<Future, Rest...>>(
                    std::tuple_cat(std::make_tuple(std::move(fut)), std::get<0>(rest.get())));
        });
    });
}

#endif /* CORE_FUTURE_UTIL_HH_ */